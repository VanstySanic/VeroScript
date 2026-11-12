// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "VeroScriptNetworkFiles.h"
#include "NetworkInternal.h"
#include "NetworkTestPaths.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#include <winioctl.h>
#else
#include <sys/stat.h>
#endif

namespace N = VeroScript::Network;
namespace D = N::Detail;
namespace Fs = std::filesystem;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " + #X); \
		} \
	} \
	while (false)

template <class F>
static void Until(N::FContext& C, F Predicate)
{
	const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(8);
	while (!Predicate())
	{
		CHECK(std::chrono::steady_clock::now() < End);
		if (C.Mode() == N::EDriveMode::HostDriven)
		{
			N::FError E;
			C.PollIo(E);
			CHECK(!E);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
static void Retired(N::FContext& C, const N::FFileOperation& O)
{
	Until(C,
	    [&]
	{
		return O.Operation().IsComplete() && O.Progress().bPhysicalComplete;
	});
}
static void Save(const Fs::path& P, const std::string& Bytes)
{
	std::ofstream F(P, std::ios::binary | std::ios::trunc);
	CHECK(F && F.write(Bytes.data(), static_cast<std::streamsize>(Bytes.size())));
}
static std::string Load(const Fs::path& P)
{
	std::ifstream F(P, std::ios::binary);
	CHECK(F);
	return std::string(std::istreambuf_iterator<char>(F), {});
}
static N::FBytePipe Source(const std::string& Bytes, bool Finish = true)
{
	N::FBytePipe P;
	N::FError E;
	CHECK(N::TryCreateBytePipe(std::max<std::size_t>(1, Bytes.size()), P, E));
	if (!Bytes.empty())
	{
		CHECK(P.Writer->WriteAll(Bytes.data(), Bytes.size()).Bytes == Bytes.size());
	}
	if (Finish)
	{
		P.Writer->Finish();
	}
	return P;
}
static std::string Read(N::FContext& C, const N::FFileOperation& O)
{
	std::string Out;
	bool Done = false;
	Until(C,
	    [&]
	{
		char Block[4093];
		auto R = O.Body()->Read(Block, sizeof(Block));
		CHECK(R.State != N::EStreamState::Failed);
		Out.append(Block, R.Bytes);
		Done = R.State == N::EStreamState::End;
		return Done;
	});
	Retired(C, O);
	CHECK(!O.Operation().Error());
	return Out;
}
static void Test(const Fs::path& Root, N::EDriveMode Mode)
{
	N::FContextOptions ContextOptions;
	ContextOptions.Mode = Mode;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), ContextOptions);
	N::FFileExecutor Files(C), Other(C);
	CHECK(Files.BelongsTo(C) && Files.Statistics().WorkerThreads == 2);
	const auto SourcePath = Root / "input.bin", Destination = Root / "output.bin";
	std::string Bytes(64 * 1024 * 17 + 13, ' ');
	for (std::size_t I = 0; I < Bytes.size(); ++I)
	{
		Bytes[I] = static_cast<char>(I % 251);
	}
	Save(SourcePath, Bytes);
	N::FFileReadOptions R;
	for (const auto& Bad : {std::string("bad\0path", 8),
	         std::string("\xc0\xaf", 2),
	         std::string("\xed\xa0\x80", 3),
	         std::string("\xf4\x90\x80\x80", 4)})
	{
		R.Path = Bad;
		R.BaseDirectory = N::Test::PathToUtf8(Root);
		auto Invalid = Files.Read(R);
		CHECK(Invalid.Operation().Error().Code == N::EError::InvalidArgument && Invalid.Progress().bPhysicalComplete);
		N::FFileWriteOptions W;
		W.Path = N::Test::PathToUtf8(Destination);
		W.PrefixPath = Bad;
		W.BaseDirectory = N::Test::PathToUtf8(Root);
		auto P = Source("", false);
		Invalid = Files.Write(W, P.Reader);
		CHECK(Invalid.Operation().Error().Code == N::EError::InvalidArgument);
		CHECK(P.Writer->Write("x", 1).State == N::EStreamState::Progress);
	}
	R.BaseDirectory.clear();
	R.Path = N::Test::PathToUtf8(SourcePath);
	R.Offset = 7;
	R.Length = Bytes.size() - 14;
	auto Op = Files.Read(R);
	CHECK(Read(C, Op) == Bytes.substr(7, Bytes.size() - 14));
	CHECK(Op.Progress().TotalBytes == R.Length && Op.Progress().Bytes == *R.Length);
	CHECK(Op.Operation().Result<N::FFileResult>()->Bytes == *R.Length);
	R.Offset = 0;
	R.Length.reset();
	Op = Files.Read(R);
	N::FFileWriteOptions W;
	W.Path = N::Test::PathToUtf8(Destination);
	W.ExpectedLength = Bytes.size();
	auto Write = Files.Write(W, Op.Body());
	Retired(C, Write);
	CHECK(!Write.Operation().Error() && Write.Operation().Result<N::FFileResult>()->bCommitted);
	CHECK(Write.Progress().Commit == N::EFileCommitState::Committed && Write.Progress().PartialPath.empty());
	CHECK(Load(Destination) == Bytes);
	CHECK(Op.Operation().IsComplete() && !Op.Operation().Error());
	// Atomic no-replace does not modify an existing target on either platform.
	auto P = Source("new");
	W.ExpectedLength = 3;
	Write = Files.Write(W, P.Reader);
	Retired(C, Write);
	CHECK(Write.Operation().Error().Code == N::EError::Write && Write.Operation().Error().SystemCode != 0);
	CHECK(Load(Destination) == Bytes && Write.Progress().PartialPath.empty());
	W.bReplaceExisting = true;
	const auto PrefixPath = Root / "prefix.bin";
	Save(PrefixPath, "prefix-");
	W.PrefixPath = N::Test::PathToUtf8(PrefixPath);
	W.PrefixLength = 7;
	W.ExpectedLength = 11;
	P = Source("tail");
	Write = Files.Write(W, P.Reader);
	Retired(C, Write);
	CHECK(!Write.Operation().Error() && Load(Destination) == "prefix-tail" && Load(PrefixPath) == "prefix-");
	W.PrefixLength = 6;
	P = Source("tail");
	Write = Files.Write(W, P.Reader);
	Retired(C, Write);
	CHECK(Write.Operation().Error().Code == N::EError::State && Load(Destination) == "prefix-tail");
	W.PrefixLength = 0;
	W.PrefixPath.clear();
	W.Path = N::Test::PathToUtf8(Root / "invalid.bin");
	for (unsigned Case = 0; Case < 3; ++Case)
	{
		W.ExpectedLength = Case < 2 ? std::optional<std::uint64_t>(Case == 0 ? 3 : 1) : std::nullopt;
		W.MaxBytes = Case == 2 ? 1 : 100;
		P = Source("ab");
		Write = Files.Write(W, P.Reader);
		Retired(C, Write);
		CHECK(Write.Operation().Error().Code == (Case == 2 ? N::EError::Limit : N::EError::Protocol));
		CHECK(!Fs::exists(Fs::u8path(W.Path)) && Write.Progress().PartialPath.empty());
	}
	W.MaxBytes = 100;
	W.ExpectedLength.reset();
	P = Source("ab", false);
	P.Writer->Fail({N::EError::Read, "source", 731});
	Write = Files.Write(W, P.Reader);
	Retired(C, Write);
	CHECK(Write.Operation().Error().Code == N::EError::Read && Write.Operation().Error().SystemCode == 731);
	CHECK(!Fs::exists(Fs::u8path(W.Path)));
	// Waiting for data does not occupy a file worker, and cancellation wakes the producer immediately.
	W.bKeepPartial = true;
	P = Source("", false);
	Write = Files.Write(W, P.Reader);
	Until(C,
	    [&]
	{
		return !Write.Progress().PartialPath.empty();
	});
	CHECK(Write.Operation().Cancel());
	CHECK(P.Writer->Write("x", 1).State == N::EStreamState::Failed);
	Retired(C, Write);
	CHECK(Write.Operation().Error().Code == N::EError::Cancelled);
	CHECK(!Write.Progress().PartialPath.empty() && Fs::exists(Fs::u8path(Write.Progress().PartialPath)));
	CHECK(!Fs::exists(Fs::u8path(W.Path)));
	Op = Files.Read(R);
	Until(C,
	    [&]
	{
		return Op.Body()->BufferedBytes() != 0;
	});
	Op.Body()->Cancel();
	Retired(C, Op);
	CHECK(Op.Operation().Error().Code == N::EError::Cancelled);
	R.Path = "relative.bin";
	Op = Files.Read(R);
	CHECK(Op.Operation().Error().Code == N::EError::InvalidArgument && Op.Progress().bPhysicalComplete);
	R.BaseDirectory = N::Test::PathToUtf8(Root);
	R.Path = "input.bin";
	R.Offset = Bytes.size();
	Op = Files.Read(R);
	CHECK(Read(C, Op).empty());
	R.Offset = Bytes.size() + 1;
	Op = Files.Read(R);
	Retired(C, Op);
	CHECK(Op.Operation().Error().Code == N::EError::InvalidArgument);
	R.Offset = 0;
	R.MaxBytes = 1;
	Op = Files.Read(R);
	Retired(C, Op);
	CHECK(Op.Operation().Error().Code == N::EError::Limit);
	R.MaxBytes = Bytes.size();
	R.Path = N::Test::PathToUtf8(Root);
	Op = Files.Read(R);
	Retired(C, Op);
	CHECK(Op.Operation().Error().Code == N::EError::Read);
	// The pool is shared; closing one owner does not close a sibling executor.
	R.Path = N::Test::PathToUtf8(SourcePath);
	Files.Close();
	Op = Other.Read(R);
	CHECK(Read(C, Op) == Bytes);
	CHECK(Files.Read(R).Operation().Error().Code == N::EError::Closed);
	// Context shutdown joins every physical file command, including commands whose result was not polled.
	Op = Other.Read(R);
	W.bKeepPartial = false;
	P = Source("", false);
	Write = Other.Write(W, P.Reader);
	if (Mode == N::EDriveMode::HostDriven)
	{
		D::Post(D::FAccess::Context(C),
		    [&]
		{
			C.Shutdown();
		});
		N::FError E;
		C.PollIo(E);
	}
	else
	{
		C.Shutdown();
	}
	CHECK(Op.Operation().IsComplete() && Write.Operation().IsComplete());
	CHECK(Op.Progress().bPhysicalComplete && Write.Progress().bPhysicalComplete);
	CHECK(P.Writer->Write("x", 1).State == N::EStreamState::Failed);
	CHECK(Files.Statistics().PendingFiles == 0 && Files.Statistics().ActiveWorkers == 0);
}
static void CancelReadyTests(const Fs::path& Root)
{
	const auto Path = Root / "ready.bin";
	Save(Path, "opened-but-not-delivered");
	N::FContextOptions O;
	O.Mode = N::EDriveMode::HostDriven;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), O);
	N::FFileExecutor Files(C);
	N::FFileReadOptions R;
	R.Path = N::Test::PathToUtf8(Path);
	auto Op = Files.Read(R);
	N::FError E;
	while (!Files.Statistics().PendingFiles)
	{
		C.PollIo(E, 1);
		CHECK(!E);
	}
	// Let Open finish on the worker, while deliberately not processing its completion.
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!Op.Progress().TotalBytes || Files.Statistics().ActiveWorkers)
	{
		CHECK(std::chrono::steady_clock::now() < Deadline);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(Op.Operation().Cancel());
	Retired(C, Op);
	#if defined(_WIN32)
	auto Handle = CreateFileW(Path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	CHECK(Handle != INVALID_HANDLE_VALUE);
	CHECK(CloseHandle(Handle));
	#elif defined(__linux__)
	for (const auto& Entry : Fs::directory_iterator("/proc/self/fd"))
	{
		std::error_code Error;
		const auto Target = Fs::read_symlink(Entry.path(), Error);
		CHECK(Error || Target != Path);
	}
	#endif
	C.Shutdown();
}
static void QueueTests(const Fs::path& Root)
{
	N::FContextOptions O;
	O.Mode = N::EDriveMode::HostDriven;
	N::FContext C(VeroScript::Boost::CreateNetworkService(), O);
	N::FFileExecutorOptions Limits;
	Limits.MaxPendingFiles = 2;
	Limits.WorkerThreads = 1;
	Limits.BlockBytes = 16;
	N::FFileExecutor Files(C, Limits);
	bool Rejected = false;
	try
	{
		N::FFileExecutor Inconsistent(C);
	}
	catch (const std::invalid_argument&)
	{
		Rejected = true;
	}
	CHECK(Rejected);
	N::FFileWriteOptions W;
	W.Path = N::Test::PathToUtf8(Root / "queue.bin");
	auto A = Source("", false), B = Source("", false), D = Source("", false);
	auto One = Files.Write(W, A.Reader), Two = Files.Write(W, B.Reader), Three = Files.Write(W, D.Reader);
	Until(C,
	    [&]
	{
		return Three.Operation().IsComplete();
	});
	CHECK(Three.Operation().Error().Code == N::EError::QueueFull);
	CHECK(D.Writer->Write("x", 1).State == N::EStreamState::Failed);
	CHECK(Files.Statistics().PendingFiles == 2 && Files.Statistics().WorkerThreads == 1);
	Files.Close();
	Retired(C, One);
	Retired(C, Two);
	CHECK(One.Operation().Error().Code == N::EError::Closed && Two.Operation().Error().Code == N::EError::Closed);
	C.Shutdown();
}
static void LargeRangeTest(const Fs::path& Root)
{
	const auto Path = Root / "sparse-offset.bin";
	const std::uint64_t Offset = (std::uint64_t{1} << 32) + 17;
	const std::string Expected("wide\0offset", 11);
	#if defined(_WIN32)
	const auto File = CreateFileW(Path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	CHECK(File != INVALID_HANDLE_VALUE);
	DWORD Bytes = 0;
	if (!DeviceIoControl(File, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &Bytes, nullptr))
	{
		CHECK(CloseHandle(File));
		CHECK(Fs::remove(Path));
		std::cout << "Sparse offset fixture unavailable on this filesystem; >4 GiB test skipped\n";
		return;
	}
	LARGE_INTEGER Position;
	Position.QuadPart = static_cast<LONGLONG>(Offset);
	const bool Prepared = SetFilePointerEx(File, Position, nullptr, FILE_BEGIN) &&
	    WriteFile(File, Expected.data(), static_cast<DWORD>(Expected.size()), &Bytes, nullptr) &&
	    Bytes == Expected.size();
	const bool Closed = CloseHandle(File) != 0;
	CHECK(Prepared && Closed);
	#else
	// Some mounted filesystems allocate holes instead of creating sparse files.
	// Probe a bounded hole before attempting an offset above 4 GiB.
	constexpr std::uint64_t ProbeOffset = 1024 * 1024;
	{
		std::ofstream File(Path, std::ios::binary);
		CHECK(File && File.seekp(static_cast<std::streamoff>(ProbeOffset)) &&
		    File.write(Expected.data(), static_cast<std::streamsize>(Expected.size())));
		File.close();
		CHECK(File);
	}
	struct stat Allocation{};
	CHECK(::stat(Path.c_str(), &Allocation) == 0);
	CHECK(static_cast<std::uint64_t>(Allocation.st_size) == ProbeOffset + Expected.size());
	if (Allocation.st_blocks < 0 ||
	    static_cast<std::uint64_t>(Allocation.st_blocks) >= (ProbeOffset + Expected.size() + 511) / 512)
	{
		CHECK(Fs::remove(Path));
		std::cout << "Sparse offset fixture unavailable on this filesystem; >4 GiB test skipped\n";
		return;
	}
	{
		std::ofstream File(Path, std::ios::binary);
		CHECK(File && File.seekp(static_cast<std::streamoff>(Offset)) &&
		    File.write(Expected.data(), static_cast<std::streamsize>(Expected.size())));
		File.close();
		CHECK(File);
	}
	#endif
	CHECK(Fs::file_size(Path) == Offset + Expected.size());
	N::FContext C(VeroScript::Boost::CreateNetworkService());
	N::FFileExecutor Files(C);
	N::FFileReadOptions Input;
	Input.Path = N::Test::PathToUtf8(Path);
	Input.Offset = Offset;
	Input.Length = Expected.size();
	Input.MaxBytes = Expected.size();
	auto Task = Files.Read(Input);
	CHECK(Read(C, Task) == Expected && Task.Progress().Bytes == Expected.size());
	C.Shutdown();
	CHECK(Fs::remove(Path));
	std::cout << "Verified a sparse file range above 4 GiB\n";
}
int main(int Argc, char** Argv)
{
	Fs::path Root;
	try
	{
		CHECK(Argc == 2);
		const auto Base = Fs::absolute(Fs::u8path(Argv[1])).lexically_normal();
		Fs::create_directories(Base);
		Root = Base /
		    Fs::u8path("files-文件 " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		CHECK(Root.parent_path() == Base && Fs::create_directory(Root));
		CancelReadyTests(Root);
		LargeRangeTest(Root);
		for (const auto Mode : {N::EDriveMode::NativeBackground, N::EDriveMode::HostDriven})
		{
			const auto Directory = Root / (Mode == N::EDriveMode::HostDriven ? "host" : "worker");
			CHECK(Fs::create_directory(Directory));
			Test(Directory, Mode);
		}
		QueueTests(Root);
		Fs::remove_all(Root);
		std::cout << "Bounded file streams, atomic commit, ranges, owner cancellation and physical shutdown passed\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << " (test files retained in " << N::Test::PathToUtf8(Root) << ")\n";
		return 1;
	}
}
