// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInternal.h"
#include "VeroScriptDarwinPaths.h"
#include "VeroScriptNativeExtension.h"
#include "VeroScriptVersion.h"
#include "VeroScriptVmModule.h"
#include "sha256/picosha2.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <random>
#include <system_error>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shlobj.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <sys/stdio.h>
#endif
#endif

namespace VeroScript::Packages::Detail
{
	namespace
	{
		fs::path NativePath(fs::path P)
		{
			#if defined(_WIN32)
			P.make_preferred();
			auto S = P.native();
			if (P.is_absolute() && S.rfind(L"\\\\?\\", 0) != 0)
			{
				if (S.rfind(L"\\\\", 0) == 0)
				{
					S = L"\\\\?\\UNC\\" + S.substr(2);
				}
				else
				{
					S = L"\\\\?\\" + S;
				}
				return fs::path(S);
			}
			#endif
			return P;
		}
	} // namespace
	fs::path Absolute(const std::string& S, const std::string& Base)
	{
		if (S.empty() || S.find('\0') != std::string::npos)
		{
			Fail("invalid-path", "empty or invalid external path");
		}
		auto P = U8Path(S);
		if (P.has_root_name() && !P.has_root_directory())
		{
			Fail("invalid-path", "drive-relative paths are not supported");
		}
		if (!P.is_absolute())
		{
			auto B = U8Path(Base);
			if (Base.empty() || !B.is_absolute())
			{
				Fail("path-base", "relative input needs an explicit absolute base");
			}
			P = B / P;
		}
		return NativePath(fs::weakly_canonical(NativePath(P)));
	}
	fs::path Relative(const fs::path& P, const fs::path& Root)
	{
		auto R = Absolute(NativePathText(P), {}).lexically_relative(Absolute(NativePathText(Root), {}));
		if (R.empty())
		{
			Fail("path-volume", "paths do not share a filesystem root");
		}
		return R;
	}
	std::string PathReference(const fs::path& P, const fs::path& Root)
	{
		auto Full = Absolute(NativePathText(P), {});
		auto Local = Full.lexically_relative(Absolute(NativePathText(Root), {}));
		return Local.empty() ? NativePathText(Full) : PathUtil::ToUtf8(Local);
	}
	void CheckPath(const std::string& Input, bool Directory)
	{
		std::string S = Input;
		if (Directory && !S.empty() && S.back() == '/')
		{
			S.pop_back();
		}
		if (S.size() > 4096 || S.empty() || S.front() == '/' || S.find('\\') != std::string::npos ||
			S.find('\0') != std::string::npos)
		{
			Fail("package-path", "invalid package relative path", Input);
		}
		(void)J(S).dump();
		std::size_t Start = 0, Depth = 0;
		for (;;)
		{
			if (++Depth > 128)
			{
				Fail("package-path", "package path exceeds directory nesting budget", Input);
			}
			auto End = S.find('/', Start);
			auto C = S.substr(Start, End == std::string::npos ? End : End - Start);
			if (C.size() > 255 || C.empty() || C == "." || C == ".." || C.back() == '.' || C.back() == ' ' ||
				C.find_first_of(":*?\"<>|") != std::string::npos ||
				std::any_of(C.begin(), C.end(),
					[](unsigned char X)
					{
						return X < 32;
					}))
			{
				Fail("package-path", "unsafe or ambiguous package path", Input);
			}
			auto N = C.substr(0, C.find('.'));
			std::transform(N.begin(), N.end(), N.begin(),
				[](unsigned char X)
				{
					return char(std::tolower(X));
				});
			if (N == "con" || N == "prn" || N == "aux" || N == "nul" ||
				(N.size() == 4 && (N.substr(0, 3) == "com" || N.substr(0, 3) == "lpt") && N[3] >= '1' &&
					N[3] <= '9'))
			{
				Fail("package-path", "reserved package path", Input);
			}
			if (End == std::string::npos)
			{
				break;
			}
			Start = End + 1;
		}
	}
	bool Under(const fs::path& P, const fs::path& Root)
	{
		auto A = Absolute(NativePathText(P), {}), B = Absolute(NativePathText(Root), {});
		auto I = A.begin();
		for (const auto& Part : B)
		{
			if (I == A.end())
			{
				return false;
			}
			#if defined(_WIN32)
			if (CompareStringOrdinal(I->native().c_str(), -1, Part.native().c_str(), -1, TRUE) != CSTR_EQUAL)
			{
				return false;
			}
			#else
			if (*I != Part)
			{
				return false;
			}
			#endif
			++I;
		}
		return true;
	}
	void NoLinks(const fs::path& Input, bool Missing)
	{
		#if defined(_WIN32)
		auto P = Input;
		P.make_preferred();
		auto S = P.native();
		std::size_t Start = 0;
		if (S.rfind(L"\\\\?\\UNC\\", 0) == 0)
		{
			auto A = S.find(L'\\', 8);
			Start = A == std::wstring::npos ? S.size() : S.find(L'\\', A + 1);
		}
		else if (S.rfind(L"\\\\?\\", 0) == 0)
		{
			Start = 7;
		}
		else
		{
			Start = P.root_path().native().size();
		}
		if (Start == std::wstring::npos)
		{
			Start = S.size();
		}
		for (std::size_t End = Start; End <= S.size(); ++End)
		{
			if (End != S.size() && S[End] != L'\\')
			{
				continue;
			}
			if (End == 0)
			{
				continue;
			}
			auto Prefix = S.substr(0, End);
			auto Flags = GetFileAttributesW(Prefix.c_str());
			if (Flags == INVALID_FILE_ATTRIBUTES)
			{
				auto E = GetLastError();
				if (Missing && (E == ERROR_FILE_NOT_FOUND || E == ERROR_PATH_NOT_FOUND))
				{
					continue;
				}
				Fail("file-missing", "required path does not exist", NativePathText(fs::path(Prefix)));
			}
			if (Flags & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))
			{
				Fail("file-link", "links, reparse points and device files are not allowed",
					 NativePathText(fs::path(Prefix)));
			}
		}
		#else
		fs::path Prefix = Input.root_path();
		for (const auto& Part : Input.relative_path())
		{
			Prefix /= Part;
			std::error_code E;
			auto S = fs::symlink_status(Prefix, E);
			if (E || S.type() == fs::file_type::not_found)
			{
				if (Missing && (E == std::errc::no_such_file_or_directory || !E))
				{
					continue;
				}
				Fail("file-missing", "required path does not exist", NativePathText(Prefix));
			}
			if (fs::is_symlink(S) && PathDetail::IsTrustedDarwinSystemAlias(Prefix))
			{
				continue;
			}
			if (fs::is_symlink(S) || (!fs::is_directory(S) && !fs::is_regular_file(S)))
			{
				Fail("file-link", "links and special files are not allowed", NativePathText(Prefix));
			}
		}
		#endif
	}

	std::string FileIdentity(const fs::path& P)
	{
		NoLinks(P);
		#if defined(_WIN32)
		auto H = CreateFileW(P.c_str(), FILE_READ_ATTRIBUTES,
							 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
							 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (H == INVALID_HANDLE_VALUE)
		{
			Fail("file-identity", "cannot query filesystem identity");
		}
		BY_HANDLE_FILE_INFORMATION I{};
		bool Ok = GetFileInformationByHandle(H, &I) != 0;
		CloseHandle(H);
		if (!Ok)
		{
			Fail("file-identity", "cannot query filesystem identity");
		}
		return std::to_string(I.dwVolumeSerialNumber) + ":" + std::to_string(I.nFileIndexHigh) + ":" +
			   std::to_string(I.nFileIndexLow);
		#else
		struct stat I{};
		if (lstat(P.c_str(), &I))
		{
			Fail("file-identity", "cannot query filesystem identity");
		}
		return std::to_string(I.st_dev) + ":" + std::to_string(I.st_ino);
		#endif
	}
	std::string Read(const fs::path& P, std::uint64_t Limit, const FWork* W)
	{
		if (W)
		{
			W->Check();
		}
		NoLinks(P);
		auto Size = fs::file_size(P);
		if (Size > Limit || Size > std::string{}.max_size())
		{
			Fail("file-limit", "file exceeds its read budget", NativePathText(P));
		}
		std::ifstream F(P, std::ios::binary);
		if (!F)
		{
			Fail("file-read", "cannot open file", NativePathText(P));
		}
		std::string S;
		S.resize(static_cast<std::size_t>(Size));
		for (std::size_t I = 0; I < S.size();)
		{
			if (W)
			{
				W->Check();
			}
			auto N = std::min<std::size_t>(65536, S.size() - I);
			if (!F.read(S.data() + I, static_cast<std::streamsize>(N)))
			{
				Fail("file-changed", "file changed during read", NativePathText(P));
			}
			I += N;
		}
		char Extra;
		if (F.get(Extra))
		{
			Fail("file-changed", "file grew during read", NativePathText(P));
		}
		return S;
	}
	std::string HashFile(const fs::path& P, const FWork& W, std::uint64_t Limit)
	{
		NoLinks(P);
		if (fs::file_size(P) > Limit)
		{
			Fail("file-limit", "file exceeds hash budget", NativePathText(P));
		}
		std::ifstream F(P, std::ios::binary);
		if (!F)
		{
			Fail("file-read", "cannot open file", NativePathText(P));
		}
		picosha2::hash256_one_by_one H;
		std::array<char, 65536> B;
		std::uint64_t Total = 0;
		while (F)
		{
			W.Check();
			F.read(B.data(), B.size());
			auto N = static_cast<std::uint64_t>(F.gcount());
			if (N > Limit - Total)
			{
				Fail("file-limit", "file grew beyond hash budget");
			}
			Total += N;
			H.process(B.begin(), B.begin() + N);
		}
		if (!F.eof())
		{
			Fail("file-read", "failed reading file");
		}
		H.finish();
		return picosha2::get_hash_hex_string(H);
	}
	void Write(const fs::path& P, std::string_view S)
	{
		NoLinks(P.parent_path(), true);
		fs::create_directories(P.parent_path());
		NoLinks(P.parent_path());
		NoLinks(P, true);
		std::ofstream F(P, std::ios::binary | std::ios::trunc);
		if (!F || !F.write(S.data(), static_cast<std::streamsize>(S.size())) || !F.flush())
		{
			Fail("file-write", "failed writing file", NativePathText(P));
		}
		F.close();
		#if defined(_WIN32)
		auto H = CreateFileW(P.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
							 FILE_ATTRIBUTE_NORMAL, nullptr);
		if (H == INVALID_HANDLE_VALUE)
		{
			Fail("file-flush", "cannot open committed file");
		}
		bool Ok = FlushFileBuffers(H) != 0;
		CloseHandle(H);
		if (!Ok)
		{
			Fail("file-flush", "failed flushing file");
		}
		#else
		int Fd = open(P.c_str(), O_RDONLY | O_NOFOLLOW);
		if (Fd < 0 || fsync(Fd) != 0)
		{
			if (Fd >= 0)
			{
				close(Fd);
			}
			Fail("file-flush", "failed flushing file");
		}
		close(Fd);
		#endif
	}
	void AtomicWrite(const fs::path& P, std::string_view S)
	{
		FTempDirectory Temp(P.parent_path());
		auto T = Temp.Path / "new";
		Write(T, S);
		NoLinks(P, true);
		#if defined(_WIN32)
		if (!MoveFileExW(T.c_str(), P.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			Fail("atomic-write", "failed atomic file replacement", NativePathText(P));
		}
		#else
		fs::rename(T, P);
		int Fd = open(P.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
		if (Fd < 0 || fsync(Fd) != 0)
		{
			if (Fd >= 0)
			{
				close(Fd);
			}
			Fail("file-flush", "failed flushing directory");
		}
		close(Fd);
		#endif
	}
	void MovePathNoReplace(const fs::path& From, const fs::path& To)
	{
		NoLinks(From);
		NoLinks(To.parent_path());
		const auto SourceStatus = fs::symlink_status(From);
		const bool Regular = fs::is_regular_file(SourceStatus);
		if (!Regular && !fs::is_directory(SourceStatus))
		{
			Fail("file-move", "only staged regular files or directories can be committed", NativePathText(From));
		}
		const auto CommitFailed = [&](const std::error_code& Error)
		{
			std::error_code StatusError;
			if (Error == std::errc::file_exists || fs::exists(fs::symlink_status(To, StatusError)))
			{
				Fail("output-exists", "output path already exists", NativePathText(To));
			}
			Fail("file-move", "cannot commit staged path without replacement: " + Error.message(),
				 NativePathText(To));
		};
		#if defined(_WIN32)
		if (!MoveFileExW(From.c_str(), To.c_str(), MOVEFILE_WRITE_THROUGH))
		{
			const auto Error = GetLastError();
			CommitFailed(std::error_code(static_cast<int>(Error), std::system_category()));
		}
		#else
		// Prefer atomic no-replace rename so filesystems without hard links retain
		// the same publication support as ordinary rename.
		int RenameError = 0;
		#if defined(__linux__) && defined(SYS_renameat2)
		constexpr unsigned RenameNoReplace = 1u; // Linux renameat2 ABI flag.
		if (::syscall(SYS_renameat2, AT_FDCWD, From.c_str(), AT_FDCWD, To.c_str(), RenameNoReplace) == 0)
		{
			return;
		}
		RenameError = errno;
		#elif defined(__APPLE__) && defined(RENAME_EXCL)
		if (__builtin_available(macOS 10.12, iOS 10.0, tvOS 10.0, watchOS 3.0, *))
		{
			if (::renamex_np(From.c_str(), To.c_str(), RENAME_EXCL) == 0)
			{
				return;
			}
			RenameError = errno;
		}
		#endif
		if (RenameError && RenameError != ENOSYS && RenameError != EINVAL &&
			RenameError != ENOTSUP && RenameError != EOPNOTSUPP)
		{
			CommitFailed(std::error_code(RenameError, std::generic_category()));
		}
		if (!Regular)
		{
			CommitFailed(std::make_error_code(std::errc::operation_not_supported));
		}
		// link() atomically claims the final name on the same filesystem and fails
		// if it already exists. Unsupported filesystems fail without a rename retry.
		if (::link(From.c_str(), To.c_str()) != 0)
		{
			CommitFailed(std::error_code(errno, std::generic_category()));
		}
		// The output is committed. Leave cleanup to the staging directory if this
		// unlink fails; a cleanup failure must never remove the published output.
		(void)::unlink(From.c_str());
		#endif
	}
	FTempDirectory::FTempDirectory(const fs::path& Input)
	{
		Parent = Absolute(NativePathText(Input), {});
		NoLinks(Parent, true);
		fs::create_directories(Parent);
		NoLinks(Parent);
		std::random_device Random;
		for (unsigned N = 0; N < 32; ++N)
		{
			Path = Parent / ("stage-" + std::to_string(Random()) + "-" + std::to_string(Random()));
			if (fs::create_directory(Path))
			{
				#if !defined(_WIN32)
				chmod(Path.c_str(), 0700);
				#endif
				return;
			}
		}
		Fail("temporary-directory", "could not create unique staging directory");
	}
	FTempDirectory::~FTempDirectory()
	{
		try
		{
			std::error_code E;
			if (Path.empty() || !fs::exists(Path, E))
			{
				return;
			}
			if (!Under(Path, Parent) || Path == Parent)
			{
				return;
			}
			NoLinks(Path);
			for (const auto& F : fs::recursive_directory_iterator(Path))
			{
				NoLinks(F.path());
			}
			fs::remove_all(Path, E);
		}
		catch (...)
		{
		}
	}
	FFileLock::FFileLock(const fs::path& P, bool Exclusive, bool Create, bool Wait, const FWork* Work)
	{
		NoLinks(P, true);
		if (Create)
		{
			NoLinks(P.parent_path(), true);
			fs::create_directories(P.parent_path());
		}
		#if defined(_WIN32)
		HANDLE H = CreateFileW(P.c_str(), GENERIC_READ | (Create ? GENERIC_WRITE : 0),
							   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
							   Create ? OPEN_ALWAYS : OPEN_EXISTING,
							   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (H == INVALID_HANDLE_VALUE)
		{
			Fail("environment-lock", "cannot open environment or lease lock", NativePathText(P));
		}
		#else
		int H = open(P.c_str(), (Create ? O_RDWR | O_CREAT : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (H < 0)
		{
			Fail("environment-lock", "cannot open environment or lease lock", NativePathText(P));
		}
		#endif
		auto Start = std::chrono::steady_clock::now();
		try
		{
			for (;;)
			{
				if (Work)
				{
					Work->Check();
				}
				#if defined(_WIN32)
				OVERLAPPED O{};
				if (LockFileEx(H, LOCKFILE_FAIL_IMMEDIATELY | (Exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0), 0, 1,
							   0, &O))
				{
					Handle = reinterpret_cast<std::intptr_t>(H);
					return;
				}
				#else
				if (flock(H, (Exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) == 0)
				{
					Handle = H;
					return;
				}
				#endif
				if (!Wait || std::chrono::steady_clock::now() - Start > std::chrono::seconds(30))
				{
					Fail("environment-busy", "environment or package lease is in use", NativePathText(P));
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
		catch (...)
		{
			#if defined(_WIN32)
			CloseHandle(H);
			#else
			close(H);
			#endif
			throw;
		}
	}
	FFileLock::~FFileLock()
	{
		if (Handle == -1)
		{
			return;
		}
		#if defined(_WIN32)
		CloseHandle(reinterpret_cast<HANDLE>(Handle));
		#else
		close(static_cast<int>(Handle));
		#endif
	}
	std::string FindProjectRoot(const fs::path& StartDirectory, const std::string& Boundary)
	{
		auto P = StartDirectory;
		auto Limit = Boundary.empty() ? P.root_path() : Absolute(Boundary, {});
		if (!Under(P, Limit))
		{
			Fail("workspace-boundary", "entry is outside workspace boundary");
		}
		for (;;)
		{
			auto File = P / "veroscript-project.json";
			if (fs::exists(File))
			{
				auto V = Parse(Read(File, 4 << 20), {});
				if (V.value("format", "") == "veroscript-project")
				{
					Project(V);
					return NativePathText(P);
				}
			}
			if (!fs::exists(P / ".veroscript" / "state.json") &&
				fs::exists(P / ".veroscript" / "transaction.json"))
			{
				Fail("recovery-required", "installation transaction needs writer recovery");
			}
			if (P == Limit || P == P.parent_path())
			{
				return {};
			}
			P = P.parent_path();
		}
	}
} // namespace VeroScript::Packages::Detail

namespace VeroScript::Packages
{
	FTargetContext HostTargetContext()
	{
		auto P = GetHostPlatformInfo();
		FTargetContext T;
		T.Core = VeroScript::FormatVersion(VEROSCRIPT_VERSION);
		T.CoreBuild = VEROSCRIPT_CORE_IMPLEMENTATION_ID;
		T.Os = P.OperatingSystem;
		T.Architecture = P.Architecture;
		T.Environment = P.Environment;
		T.Abis = {{"native-extension", BuildNativeExtensionAbi()}, {"jit", BuildJitModuleAbi()}, {"vm", BuildVmModuleAbi()}};
		if (P.bAllowsNativeJit)
		{
			T.Capabilities.push_back("native-jit");
		}
		if (P.bAllowsDynamicJitLibraries)
		{
			T.Capabilities.push_back("dynamic-jit");
		}
		return T;
	}
	std::string DefaultCacheRoot()
	{
		#if defined(_WIN32)
		PWSTR Data = nullptr;
		if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &Data)))
		{
			Detail::Fail("cache-root", "system cache directory is unavailable");
		}
		Detail::fs::path Directory(Data);
		CoTaskMemFree(Data);
		return Detail::NativePathText(Directory / Detail::U8Path("VeroScript/Cache/Packages"));
		#else
		const char* X = std::getenv("XDG_CACHE_HOME");
		if (X && Detail::U8Path(X).is_absolute())
		{
			return Detail::NativePathText(Detail::U8Path(X) / Detail::U8Path("VeroScript/Packages"));
		}
		const char* H = std::getenv("HOME");
		if (!H)
		{
			Detail::Fail("cache-root", "explicit cache root required");
		}
		#if defined(__APPLE__)
		return Detail::NativePathText(Detail::U8Path(H) / Detail::U8Path("Library/Caches/VeroScript/Packages"));
		#else
		return Detail::NativePathText(Detail::U8Path(H) / Detail::U8Path(".cache/VeroScript/Packages"));
		#endif
		#endif
	}
	bool ResolveEnvironment(const FEnvironmentInput& I, FEnvironmentLayout& O, FDiagnostic& E)
	{
		try
		{
			using namespace Detail;
			E = {};
			O = {};
			if (I.Scope != EEnvironmentScope::Project && I.Scope != EEnvironmentScope::Runtime &&
				I.Scope != EEnvironmentScope::Sdk)
			{
				Fail("environment-scope", "unsupported installation scope");
			}
			auto R = Absolute(I.Root, I.WorkingDirectory);
			NoLinks(R);
			if (!fs::is_directory(R))
			{
				Fail("environment-root", "environment root must be an existing directory");
			}
			O.Root = Detail::NativePathText(R);
			O.Scope = I.Scope;
			O.Host = I.Host.Core.empty() ? HostTargetContext() : I.Host;
			O.Target = I.Target.Core.empty() ? O.Host : I.Target;
			auto Actual = HostTargetContext();
			if (O.Host.Core != Actual.Core || O.Host.Os != Actual.Os ||
				O.Host.Architecture != Actual.Architecture || O.Host.Environment != Actual.Environment ||
				O.Host.CoreBuild != Actual.CoreBuild || O.Host.Abis != Actual.Abis)
			{
				Fail("host-identity", "HostContext must match the executing core");
			}
			if (O.Target.Core.empty() || O.Target.CoreBuild.empty() || O.Target.Os.empty() ||
				O.Target.Architecture.empty() || O.Target.Environment.empty())
			{
				Fail("target-identity", "complete target context is required");
			}
			if ((I.Scope == EEnvironmentScope::Runtime || I.Scope == EEnvironmentScope::Sdk) &&
				TargetJson(O.Target) != TargetJson(O.Host))
			{
				Fail("component-target", "cross-target installation cannot modify component baseline");
			}
			O.TargetKey = Id(TargetJson(O.Target));
			auto M = R / ".veroscript";
			O.Management = Detail::NativePathText(M);
			O.Requirements =
				Detail::NativePathText(I.Scope == EEnvironmentScope::Project ? R / "veroscript-project.json" : M / "environment.json");
			O.Lockfile = I.Lockfile.empty() ? Detail::NativePathText(I.Scope == EEnvironmentScope::Project ? R / "veroscript.lock"
																					 : M / "environment.lock")
											: Detail::NativePathText(Absolute(I.Lockfile, I.WorkingDirectory));
			O.State = Detail::NativePathText(M / "state.json");
			auto P = I.Scope == EEnvironmentScope::Runtime ? R / "Packages"
														   : R / Detail::U8Path(".veroscript/packages");
			O.Packages =
				I.PackagesDirectory.empty() ? Detail::NativePathText(P) : Detail::NativePathText(Absolute(I.PackagesDirectory, O.Root));
			NoLinks(U8Path(O.Packages), true);
			O.Cache =
				Detail::NativePathText(Absolute(I.CacheDirectory.empty() ? DefaultCacheRoot() : I.CacheDirectory, I.WorkingDirectory));
			return true;
		}
		catch (const Detail::FFailure& X)
		{
			E = X.Diagnostic;
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "environment-path";
			E.Message = X.what();
			return false;
		}
	}
	namespace
	{
		FEnvironmentInput ReadComponentDescriptor(const Detail::fs::path& Descriptor)
		{
			using namespace Detail;
			auto D = Parse(Read(Descriptor, 4 << 20), {});
			Keys(D, {"format", "formatVersion", "role", "root", "target"}, "component");
			if (D.value("format", "") != "veroscript-component" ||
				D.value("formatVersion", "") != FormatVersion)
			{
				Fail("component-format", "invalid component deployment descriptor");
			}
			auto Role = D.value("role", "");
			if (Role != "runtime" && Role != "core-sdk")
			{
				Fail("component-role", "unsupported component role");
			}
			auto Target = ReadTarget(D.at("target"));
			if (TargetJson(Target) != TargetJson(HostTargetContext()))
			{
				Fail("component-identity", "component descriptor and executing core differ");
			}
			FEnvironmentInput Result;
			Result.Scope = Role == "runtime" ? EEnvironmentScope::Runtime : EEnvironmentScope::Sdk;
			Result.Root = NativePathText(Absolute(D.at("root"), NativePathText(Descriptor.parent_path())));
			Result.Target = Target;
			Result.Host = HostTargetContext();
			NoLinks(U8Path(Result.Root));
			if (!fs::is_directory(U8Path(Result.Root)))
			{
				Fail("component-root", "component root must be an existing directory");
			}
			return Result;
		}
	} // namespace
	bool DiscoverComponent(const std::string& Executable, FEnvironmentInput& O, bool& Found, FDiagnostic& E)
	{
		try
		{
			using namespace Detail;
			auto SelectedExecutable = Executable;
			E = {};
			O = {};
			Found = false;
			auto Descriptor = Absolute(SelectedExecutable, {}).parent_path() / "veroscript-component.json";
			if (!fs::exists(Descriptor))
			{
				return true;
			}
			O = ReadComponentDescriptor(Descriptor);
			Found = true;
			return true;
		}
		catch (const Detail::FFailure& X)
		{
			E = X.Diagnostic;
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "component-descriptor";
			E.Message = X.what();
			return false;
		}
	}
	bool DiscoverComponentRoot(const std::string& Root, FEnvironmentInput& O, bool& Found, FDiagnostic& E)
	{
		try
		{
			using namespace Detail;
			auto SelectedRoot = Root;
			E = {};
			O = {};
			Found = false;
			auto Directory = Absolute(SelectedRoot, {});
			NoLinks(Directory);
			if (!fs::is_directory(Directory))
			{
				Fail("component-root", "component root must be an existing directory");
			}
			for (const auto& Descriptor :
				 {Directory / "veroscript-component.json", Directory / "bin" / "veroscript-component.json"})
			{
				if (!fs::exists(Descriptor))
				{
					continue;
				}
				auto Candidate = ReadComponentDescriptor(Descriptor);
				if (!PathUtil::Equal(Candidate.Root, Detail::NativePathText(Directory)))
				{
					Fail("component-root", "deployment descriptor does not identify the selected root");
				}
				if (Found && Candidate.Scope != O.Scope)
				{
					Fail("component-ambiguous", "component root contains conflicting descriptors");
				}
				O = std::move(Candidate);
				Found = true;
			}
			return true;
		}
		catch (const Detail::FFailure& X)
		{
			E = X.Diagnostic;
			O = {};
			Found = false;
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "component-descriptor";
			E.Message = X.what();
			O = {};
			Found = false;
			return false;
		}
	}
	bool DiscoverProject(const std::string& Entry, const std::string& Boundary, std::string& Root,
						 FDiagnostic& E)
	{
		try
		{
			using namespace Detail;
			E = {};
			Root.clear();
			Root = FindProjectRoot(Absolute(Entry, {}).parent_path(), Boundary);
			return true;
		}
		catch (const Detail::FFailure& X)
		{
			E = X.Diagnostic;
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "project-discovery";
			E.Message = X.what();
			return false;
		}
	}
} // namespace VeroScript::Packages
