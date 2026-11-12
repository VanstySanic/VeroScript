// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptArchive.h"
#include "ApplicationInternal.h"
#include "miniz/miniz.h"
#include "sha256/picosha2.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <mutex>
#include <ctime>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace VeroScript::Application
{
	using namespace Detail;
	namespace
	{
		class FArchiveOutput
		{
		public:
			explicit FArchiveOutput(const fs::path& InPath) : Path(InPath)
			{
				#if defined(_WIN32)
				int Descriptor = -1;
				if (_wsopen_s(&Descriptor, Path.c_str(), _O_CREAT | _O_EXCL | _O_BINARY | _O_WRONLY,
				        _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0)
				{
					Fail("archive-create", "cannot exclusively create archive", PathUtil::ToUtf8(Path));
				}
				File = _fdopen(Descriptor, "wb");
				if (!File)
				{
					_close(Descriptor);
				}
				#else
				const int Descriptor = ::open(Path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
				if (Descriptor < 0)
				{
					Fail("archive-create", "cannot exclusively create archive", PathUtil::ToUtf8(Path));
				}
				File = ::fdopen(Descriptor, "wb");
				if (!File)
				{
					::close(Descriptor);
				}
				#endif
				if (!File)
				{
					std::error_code E;
					fs::remove(Path, E);
					Fail("archive-create", "cannot open archive stream", PathUtil::ToUtf8(Path));
				}
			}
			~FArchiveOutput()
			{
				if (File)
				{
					std::fclose(File);
				}
				if (!Completed)
				{
					std::error_code E;
					fs::remove(Path, E);
				}
			}
			std::size_t Write(std::uint64_t Offset, const void* Data, std::size_t Size)
			{
				#if defined(_WIN32)
				if (_fseeki64(File, static_cast<__int64>(Offset), SEEK_SET)) return 0;
				#else
				if (::fseeko(File, static_cast<off_t>(Offset), SEEK_SET)) return 0;
				#endif
				return std::fwrite(Data, 1, Size, File);
			}
			bool Complete()
			{
				if (std::fflush(File)) return false;
				#if defined(_WIN32)
				if (_commit(_fileno(File))) return false;
				#else
				if (::fsync(fileno(File))) return false;
				#endif
				const int Status = std::fclose(File);
				File = nullptr;
				Completed = Status == 0;
				return Completed;
			}

		private:
			fs::path Path;
			std::FILE* File = nullptr;
			bool Completed = false;
		};
		std::vector<std::uint8_t> CentralModes(const void* Data, std::size_t Size,
		    const std::map<std::string, bool>& Modes)
		{
			const auto* Begin = static_cast<const std::uint8_t*>(Data);
			std::vector<std::uint8_t> Out(Begin, Begin + Size);
			std::size_t Position = 0;
			while (Position < Size)
			{
				auto* Header = Out.data() + Position;
				if (Size - Position < 46 || std::memcmp(Header, "PK\1\2", 4))
				{
					Fail("archive-directory", "invalid generated ZIP directory");
				}
				const auto Word = [&](unsigned N)
				{
					return static_cast<std::size_t>(Header[N] | (Header[N + 1] << 8));
				};
				const auto NameSize = Word(28), Length = 46 + NameSize + Word(30) + Word(32);
				if (Length > Size - Position)
				{
					Fail("archive-directory", "truncated generated ZIP directory");
				}
				const std::string Name(reinterpret_cast<const char*>(Header + 46), NameSize);
				const std::uint32_t Attributes = (0100000u | (Modes.at(Name) ? 0755u : 0644u)) << 16;
				Header[4] = 20;
				Header[5] = 3;
				for (unsigned I = 0; I < 4; ++I)
				{
					Header[38 + I] = static_cast<std::uint8_t>(Attributes >> (8 * I));
				}
				Position += Length;
			}
			return Out;
		}
		bool ValidUtf8(std::string_view S)
		{
			try
			{
				(void)J(S).dump();
				return true;
			}
			catch (const J::exception&)
			{
				return false;
			}
		}
		void ValidateFiles(const std::vector<FContentFile>& Files, const FLimits& Limits)
		{
			if (Files.size() > Limits.MaxFiles)
			{
				Fail("file-count", "content exceeds its file count budget");
			}
			std::set<std::string> Names;
			std::uint64_t Total = 0;
			for (const auto& File : Files)
			{
				Check(Limits);
				Path(File.Path);
				if (!Names.insert(Fold(File.Path)).second)
				{
					Fail("duplicate-path", "duplicate or case-ambiguous content", File.Path);
				}
				if (File.Bytes.size() > Limits.MaxFileBytes || File.Bytes.size() > Limits.MaxExpandedBytes - Total)
				{
					Fail("content-limit", "content exceeds its expanded byte budget", File.Path);
				}
				Total += File.Bytes.size();
			}
			for (const auto& Name : Names)
			{
				for (auto P = PathUtil::FromUtf8(Name).parent_path(); !P.empty(); P = P.parent_path())
				{
					if (Names.count(PathUtil::ToUtf8(P)))
					{
						Fail("prefix-path", "file and directory paths overlap", Name);
					}
				}
			}
		}
	}

	bool ValidateContentPath(std::string_view S, std::string& Error)
	{
		Error.clear();
		// miniz header validation represents filenames in a fixed 512-byte field.
		if (S.empty() || S.size() > 511 || S.front() == '/' || S.back() == '/' || !ValidUtf8(S))
		{
			Error = "invalid UTF-8 relative content path";
			return false;
		}
		for (const unsigned char C : S)
		{
			if (C < 32 || C == 127 || C == '\\' || C == ':' || C == '*' || C == '?' || C == '"' || C == '<' || C == '>' || C == '|')
			{
				Error = "unsafe character in content path";
				return false;
			}
		}
		std::size_t Begin = 0;
		while (Begin < S.size())
		{
			const auto End = S.find('/', Begin);
			const auto Part = S.substr(Begin, End == std::string_view::npos ? S.size() - Begin : End - Begin);
			const auto Base = Fold(std::string(Part.substr(0, Part.find('.'))));
			if (Part.empty() || Part.size() > 255 || Part == "." || Part == ".." || Part.back() == '.' || Part.back() == ' ' ||
			    Base == "con" || Base == "nul" || Base == "prn" || Base == "aux" ||
			    (Base.size() == 4 && (Base.substr(0, 3) == "com" || Base.substr(0, 3) == "lpt") && Base[3] >= '0' && Base[3] <= '9'))
			{
				Error = "unsafe or ambiguous content path component";
				return false;
			}
			if (End == std::string_view::npos)
			{
				break;
			}
			Begin = End + 1;
		}
		return true;
	}
	std::string ContentSha256(const std::vector<std::uint8_t>& Data)
	{
		return picosha2::hash256_hex_string(Data);
	}
	std::string ContentSha256(std::string_view Data)
	{
		return picosha2::hash256_hex_string(Data.begin(), Data.end());
	}
	bool FileSha256(const std::string& Path, std::string& Result, FDiagnostic& Error, const FLimits& Limits)
	{
		try
		{
			Physical(PathUtil::FromUtf8(Path));
			std::ifstream In(PathUtil::FromUtf8(Path), std::ios::binary);
			if (!In)
			{
				Fail("hash-read", "cannot open content for hashing", Path);
			}
			std::array<char, 65536> Buffer;
			std::uint64_t Total = 0;
			picosha2::hash256_one_by_one Hash;
			while (In)
			{
				Check(Limits);
				In.read(Buffer.data(), Buffer.size());
				const auto Count = static_cast<std::size_t>(In.gcount());
				if (Count > Limits.MaxArchiveBytes - Total)
				{
					Fail("hash-limit", "content exceeds its byte budget", Path);
				}
				Total += Count;
				Hash.process(Buffer.begin(), Buffer.begin() + Count);
			}
			if (!In.eof())
			{
				Fail("hash-read", "content could not be hashed", Path);
			}
			Hash.finish();
			Result = picosha2::get_hash_hex_string(Hash);
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return false;
		}
		catch (const fs::filesystem_error& E)
		{
			Error = {"filesystem", E.what(), Path, {}};
			return false;
		}
	}
	bool CheckPhysicalPath(const std::string& P, bool Missing, FDiagnostic& Error)
	{
		try
		{
			Physical(PathUtil::FromUtf8(P), Missing);
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return false;
		}
	}

	struct FArchive::FImpl
	{
		~FImpl()
		{
			mz_zip_reader_end(&Zip);
		}
		static std::size_t ReadAt(void* Context, mz_uint64 Offset, void* Data, std::size_t Size)
		{
			auto& Self = *static_cast<FImpl*>(Context);
			if (Offset > Self.Size || Size > Self.Size - Offset) return 0;
			if (Size == 0) return 0;
			if (!Self.Memory.empty())
			{
				std::memcpy(Data, Self.Memory.data() + static_cast<std::size_t>(Offset), Size);
				return Size;
			}
			Self.Input.clear();
			Self.Input.seekg(static_cast<std::streamoff>(Offset));
			Self.Input.read(static_cast<char*>(Data), static_cast<std::streamsize>(Size));
			return static_cast<std::size_t>(Self.Input.gcount());
		}
		mz_zip_archive Zip{};
		std::ifstream Input;
		std::vector<std::uint8_t> Memory;
		mutable std::mutex Mutex;
		std::uint64_t Size = 0;
		std::string Identity;
		std::vector<FContentEntry> Entries;
		std::map<std::string, mz_uint> Indices;
		FLimits Limits;
	};
	std::shared_ptr<const FArchive> FArchive::Open(const std::string& Name, FDiagnostic& Error, const FLimits& Limits)
	{
		try
		{
			Physical(PathUtil::FromUtf8(Name));
			auto Impl = std::make_shared<FImpl>();
			Impl->Limits = Limits;
			Impl->Input.open(PathUtil::FromUtf8(Name), std::ios::binary | std::ios::ate);
			const auto End = Impl->Input ? Impl->Input.tellg() : std::streampos{-1};
			if (End < 22 || static_cast<std::uint64_t>(End) > Limits.MaxArchiveBytes)
			{
				Fail("archive-limit", "invalid archive size", Name);
			}
			Impl->Size = static_cast<std::uint64_t>(End);
			return Initialize(std::move(Impl), Name, Error, Limits);
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return {};
		}
		catch (const std::exception& E)
		{
			Error = {"archive", E.what(), Name, {}};
			return {};
		}
	}

	std::shared_ptr<const FArchive> FArchive::Open(std::vector<std::uint8_t> Data, FDiagnostic& Error, const FLimits& Limits)
	{
		auto State = std::make_shared<FImpl>();
		State->Memory = std::move(Data);
		State->Size = State->Memory.size();
		return Initialize(std::move(State), "<memory>", Error, Limits);
	}

	FArchive::~FArchive() = default;

	const std::vector<FContentEntry>& FArchive::GetEntries() const noexcept
	{
		return Impl->Entries;
	}

	const std::string& FArchive::GetIdentity() const noexcept
	{
		return Impl->Identity;
	}

	const std::vector<std::uint8_t>* FArchive::GetMemoryBytes() const noexcept
	{
		return Impl->Memory.empty() ? nullptr : &Impl->Memory;
	}

	bool FArchive::Read(const std::string& Path, std::vector<std::uint8_t>& Bytes,
	    FDiagnostic& Error, const FArtifactWork& Work) const
	{
		Bytes.clear();
		try
		{
			if (Work.CheckCancelled)
			{
				Work.CheckCancelled();
			}
			std::lock_guard<std::mutex> Lock(Impl->Mutex);
			const auto I = Impl->Indices.find(Path);
			if (I == Impl->Indices.end())
			{
				Fail("content-missing", "archive entry is missing", Path);
			}
			mz_zip_archive_file_stat S{};
			if (!mz_zip_reader_file_stat(&Impl->Zip, I->second, &S) || S.m_uncomp_size > Work.MaxBytes)
			{
				Fail("read-limit", "archive entry exceeds the read budget", Path);
			}
			Bytes.resize(static_cast<std::size_t>(S.m_uncomp_size));
			struct FOutput
			{
				std::vector<std::uint8_t>& Bytes;
				const FArtifactWork& Work;
				std::exception_ptr Failure;
			};
			FOutput Output{Bytes, Work, {}};
			auto Write = [](void* Context, mz_uint64 Offset, const void* Buffer, std::size_t Size) -> std::size_t
			{
				auto& Out = *static_cast<FOutput*>(Context);
				try
				{
					if (Out.Work.CheckCancelled)
					{
						Out.Work.CheckCancelled();
					}
					if (Offset > Out.Bytes.size() || Size > Out.Bytes.size() - Offset) return 0;
					if (Size == 0) return 0;
					std::memcpy(Out.Bytes.data() + static_cast<std::size_t>(Offset), Buffer, Size);
					return Size;
				}
				catch (...)
				{
					Out.Failure = std::current_exception();
					return 0;
				}
			};
			const bool Ok = mz_zip_reader_extract_to_callback(&Impl->Zip, I->second, Write, &Output, 0) != 0;
			if (Output.Failure)
			{
				std::rethrow_exception(Output.Failure);
			}
			if (!Ok)
			{
				Fail("archive-integrity", "archive entry is truncated or corrupt", Path);
			}
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Bytes.clear();
			Error = E.Diagnostic;
			return false;
		}
	}

	std::shared_ptr<const FArchive> FArchive::Initialize(std::shared_ptr<FImpl> Impl,
	    const std::string& Name, FDiagnostic& Error, const FLimits& Limits)
	{
		try
		{
			Impl->Limits = Limits;
			if (Impl->Size < 22 || Impl->Size > Limits.MaxArchiveBytes)
			{
				Fail("archive-limit", "invalid archive size", Name);
			}

			std::array<std::uint8_t, 4> Magic{};
			if (FImpl::ReadAt(Impl.get(), 0, Magic.data(), Magic.size()) != Magic.size() ||
			    Magic[0] != 'P' || Magic[1] != 'K')
			{
				Fail("archive-format", "expected a standard ZIP archive", Name);
			}
			Impl->Zip.m_pRead = FImpl::ReadAt;
			Impl->Zip.m_pIO_opaque = Impl.get();
			if (!mz_zip_reader_init(&Impl->Zip, Impl->Size, 0))
			{
				Fail("archive-format", "invalid ZIP directory", Name);
			}
			const auto Count = mz_zip_reader_get_num_files(&Impl->Zip);
			if (Count > Limits.MaxFiles)
			{
				Fail("file-count", "archive exceeds its entry budget", Name);
			}
			std::set<std::string> Names, FileNames;
			std::vector<std::pair<std::uint64_t, std::uint64_t>> Ranges;
			std::uint64_t Total = 0;
			for (mz_uint I = 0; I < Count; ++I)
			{
				Check(Limits);
				mz_zip_archive_file_stat S{};
				if (!mz_zip_reader_file_stat(&Impl->Zip, I, &S))
				{
					Fail("archive-entry", "invalid ZIP entry", Name);
				}
				const auto N = mz_zip_reader_get_filename(&Impl->Zip, I, nullptr, 0);
				if (N < 2 || N > 4097)
				{
					Fail("archive-name", "invalid ZIP entry name", Name);
				}
				std::string Entry(N, '\0');
				mz_zip_reader_get_filename(&Impl->Zip, I, Entry.data(), N);
				Entry.pop_back();
				const bool Directory = mz_zip_reader_is_file_a_directory(&Impl->Zip, I) != 0;
				if (Directory && Entry.back() == '/')
				{
					Entry.pop_back();
				}
				Path(Entry);
				const auto Kind = (S.m_external_attr >> 16) & 0170000u;
				// Validate local/central agreement through miniz, without eagerly inflating payloads.
				if (!mz_zip_validate_file(&Impl->Zip, I, MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY))
				{
					Fail("archive-local-header", "ZIP local header differs from its directory", Entry);
				}
				std::array<std::uint8_t, 30> Local{};
				if (FImpl::ReadAt(Impl.get(), S.m_local_header_ofs, Local.data(), Local.size()) != Local.size())
				{
					Fail("archive-local-header", "ZIP local header is truncated", Entry);
				}
				const auto Length = static_cast<std::uint64_t>(30 + Local[26] + (Local[27] << 8) + Local[28] + (Local[29] << 8));
				if (S.m_local_header_ofs > Impl->Zip.m_central_directory_file_ofs ||
				    Length > Impl->Zip.m_central_directory_file_ofs - S.m_local_header_ofs ||
				    S.m_comp_size > Impl->Zip.m_central_directory_file_ofs - S.m_local_header_ofs - Length)
				{
					Fail("archive-range", "ZIP entry overlaps its central directory", Entry);
				}
				Ranges.emplace_back(S.m_local_header_ofs, S.m_local_header_ofs + Length + S.m_comp_size);
				if (S.m_is_encrypted || (S.m_bit_flag & 1u) || !S.m_is_supported ||
				    (S.m_method != 0 && S.m_method != 8) ||
				    (Kind && Kind != (Directory ? 0040000u : 0100000u)))
				{
					Fail("archive-type", "encrypted, linked or unsupported ZIP entry", Entry);
				}
				if (!Names.insert(Fold(Entry)).second)
				{
					Fail("archive-duplicate", "duplicate or case-ambiguous archive path", Entry);
				}
				if (S.m_uncomp_size > Limits.MaxFileBytes || S.m_uncomp_size > Limits.MaxExpandedBytes - Total ||
				    (S.m_uncomp_size && (!S.m_comp_size || (S.m_uncomp_size / S.m_comp_size > Limits.MaxExpansionRatio || (S.m_uncomp_size / S.m_comp_size == Limits.MaxExpansionRatio && S.m_uncomp_size % S.m_comp_size)))))
				{
					Fail("archive-limit", "archive entry exceeds its expanded budget", Entry);
				}
				Total += S.m_uncomp_size;
				if (!Directory)
				{
					FileNames.insert(Fold(Entry));
					Impl->Entries.push_back({Entry, S.m_uncomp_size, ((S.m_external_attr >> 16) & 0111u) != 0});
					Impl->Indices.emplace(Entry, I);
				}
			}
			for (const auto& File : Names)
			{
				for (auto Parent = PathUtil::FromUtf8(File).parent_path(); !Parent.empty(); Parent = Parent.parent_path())
				{
					if (FileNames.count(PathUtil::ToUtf8(Parent)))
					{
						Fail("archive-prefix", "file and directory paths overlap", File);
					}
				}
			}
			std::sort(Ranges.begin(), Ranges.end());
			for (std::size_t I = 1; I < Ranges.size(); ++I)
			{
				if (Ranges[I].first < Ranges[I - 1].second)
				{
					Fail("archive-range", "ZIP entries overlap");
				}
			}
			std::sort(Impl->Entries.begin(), Impl->Entries.end(), [](const auto& A, const auto& B)
			{
				return A.Path < B.Path;
			});
			picosha2::hash256_one_by_one Hasher;
			std::array<std::uint8_t, 65536> Buffer{};
			for (std::uint64_t Offset = 0; Offset < Impl->Size;)
			{
				Check(Limits);
				const auto Size = static_cast<std::size_t>(std::min<std::uint64_t>(Buffer.size(), Impl->Size - Offset));
				if (FImpl::ReadAt(Impl.get(), Offset, Buffer.data(), Size) != Size)
				{
					Fail("archive-read", "archive changed while hashing", Name);
				}
				Hasher.process(Buffer.begin(), Buffer.begin() + Size);
				Offset += Size;
			}
			Hasher.finish();
			Impl->Identity = picosha2::get_hash_hex_string(Hasher);
			Error = {};
			return std::shared_ptr<const FArchive>(new FArchive(std::move(Impl)));
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return {};
		}
		catch (const std::exception& E)
		{
			Error = {"archive", E.what(), Name, {}};
			return {};
		}
	}

	FArchive::FArchive(std::shared_ptr<FImpl> State) : Impl(std::move(State))
	{
	}

	bool WriteArchive(const std::vector<FContentFile>& Files, const std::string& Name,
	    FDiagnostic& Error, const FLimits& Limits)
	{
		try
		{
			ValidateFiles(Files, Limits);
			Physical(PathUtil::FromUtf8(Name), true);
			if (fs::exists(PathUtil::FromUtf8(Name)))
			{
				Fail("output-exists", "archive output already exists", Name);
			}
			fs::create_directories(PathUtil::FromUtf8(Name).parent_path());
			FArchiveOutput Out{PathUtil::FromUtf8(Name)};
			mz_zip_archive Zip{};
			std::map<std::string, bool> Modes;
			for (const auto& F : Files)
			{
				Modes.emplace(F.Path, F.Executable);
			}
			struct FWriter
			{
				FArchiveOutput& Out;
				const FLimits& Limits;
				mz_zip_archive* Zip;
				const std::map<std::string, bool>* Modes;
				std::exception_ptr Failure;
			};
			FWriter Writer{Out, Limits, &Zip, &Modes, {}};
			Zip.m_pIO_opaque = &Writer;
			Zip.m_pWrite = [](void* Context, mz_uint64 Offset, const void* Data, std::size_t Size) -> std::size_t
			{
				auto& W = *static_cast<FWriter*>(Context);
				try
				{
					Check(W.Limits);
					if (Offset > W.Limits.MaxArchiveBytes || Size > W.Limits.MaxArchiveBytes - Offset) return 0;
					if (Size == 0) return 0;
					if (Offset && Offset == W.Zip->m_central_directory_file_ofs)
					{
						const auto Directory = CentralModes(Data, Size, *W.Modes);
						return W.Out.Write(Offset, Directory.data(), Directory.size());
					}
					return W.Out.Write(Offset, Data, Size);
				}
				catch (...)
				{
					W.Failure = std::current_exception();
					return 0;
				}
			};
			if (!mz_zip_writer_init(&Zip, 0))
			{
				Fail("archive-write", "cannot initialize archive", Name);
			}
			struct FEnd
			{
				~FEnd()
				{
					mz_zip_writer_end(Zip);
				}
				mz_zip_archive* Zip;
			} End{&Zip};
			std::vector<const FContentFile*> Sorted;
			for (const auto& F : Files)
			{
				Sorted.push_back(&F);
			}
			std::sort(Sorted.begin(), Sorted.end(), [](auto A, auto B)
			{
				return A->Path < B->Path;
			});
			std::tm Fixed{};
			Fixed.tm_year = 80;
			Fixed.tm_mon = 0;
			Fixed.tm_mday = 1;
			Fixed.tm_isdst = -1;
			MZ_TIME_T Timestamp = std::mktime(&Fixed);
			for (const auto* F : Sorted)
			{
				Check(Limits);
				// Store entries to keep archive budget guarantees independent of entropy.
				if (!mz_zip_writer_add_mem_ex_v2(&Zip, F->Path.c_str(), F->Bytes.data(), F->Bytes.size(),
				        nullptr, 0, 0, 0, 0, &Timestamp, nullptr, 0, nullptr, 0))
				{
					if (Writer.Failure)
					{
						std::rethrow_exception(Writer.Failure);
					}
					Fail("archive-write", "cannot write archive entry", F->Path);
				}
			}
			if (!mz_zip_writer_finalize_archive(&Zip) || !Out.Complete())
			{
				Fail("archive-write", "cannot finalize archive", Name);
			}
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception& E)
		{
			Error = {"archive-write", E.what(), Name, {}};
			return false;
		}
	}
	bool WriteArchive(const std::vector<FContentFile>& Files, std::vector<std::uint8_t>& Bytes,
	    FDiagnostic& Error, const FLimits& Limits)
	{
		Bytes.clear();
		mz_zip_archive Zip{};
		struct FEnd
		{
			mz_zip_archive* Zip;
			~FEnd()
			{
				mz_zip_writer_end(Zip);
			}
		} End{&Zip};
		try
		{
			ValidateFiles(Files, Limits);
			std::map<std::string, bool> Modes;
			for (const auto& F : Files)
			{
				Modes.emplace(F.Path, F.Executable);
			}
			struct FOutput
			{
				std::vector<std::uint8_t>* Bytes;
				const FLimits* Limits;
				mz_zip_archive* Zip;
				const std::map<std::string, bool>* Modes;
				std::exception_ptr Failure;
			} Output{&Bytes, &Limits, &Zip, &Modes, {}};
			Zip.m_pIO_opaque = &Output;
			Zip.m_pWrite = [](void* Context, mz_uint64 Offset, const void* Data, std::size_t Size) -> std::size_t
			{
				auto& O = *static_cast<FOutput*>(Context);
				try
				{
					Check(*O.Limits);
					if (Offset > O.Limits->MaxArchiveBytes || Size > O.Limits->MaxArchiveBytes - Offset) return 0;
					// miniz permits null buffers for empty I/O; avoid pointer arithmetic and memcpy.
					if (Size == 0) return 0;
					if (Offset + Size > O.Bytes->size())
					{
						O.Bytes->resize(static_cast<std::size_t>(Offset + Size));
					}
					if (Offset && Offset == O.Zip->m_central_directory_file_ofs)
					{
						const auto Directory = CentralModes(Data, Size, *O.Modes);
						std::memcpy(O.Bytes->data() + static_cast<std::size_t>(Offset), Directory.data(), Size);
					}
					else
					{
						std::memcpy(O.Bytes->data() + static_cast<std::size_t>(Offset), Data, Size);
					}
					return Size;
				}
				catch (...)
				{
					O.Failure = std::current_exception();
					return 0;
				}
			};
			if (!mz_zip_writer_init(&Zip, 0))
			{
				Fail("archive-write", "cannot initialize memory archive");
			}
			std::vector<const FContentFile*> Sorted;
			for (const auto& F : Files)
			{
				Sorted.push_back(&F);
			}
			std::sort(Sorted.begin(), Sorted.end(), [](auto A, auto B)
			{
				return A->Path < B->Path;
			});
			std::tm Fixed{};
			Fixed.tm_year = 80;
			Fixed.tm_mday = 1;
			Fixed.tm_isdst = -1;
			MZ_TIME_T Timestamp = std::mktime(&Fixed);
			for (const auto* F : Sorted)
			{
				if (!mz_zip_writer_add_mem_ex_v2(&Zip, F->Path.c_str(), F->Bytes.data(), F->Bytes.size(), nullptr, 0, 0, 0, 0, &Timestamp, nullptr, 0, nullptr, 0))
				{
					if (Output.Failure)
					{
						std::rethrow_exception(Output.Failure);
					}
					Fail("archive-write", "cannot write memory archive", F->Path);
				}
			}
			if (!mz_zip_writer_finalize_archive(&Zip))
			{
				Fail("archive-write", "cannot finalize memory archive");
			}
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Bytes.clear();
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception& E)
		{
			Bytes.clear();
			Error = {"archive-write", E.what(), {}, {}};
			return false;
		}
	}
	bool ReadContentTree(const std::string& Root, std::vector<FContentFile>& Files,
	    FDiagnostic& Error, const FLimits& Limits)
	{
		Files.clear();
		try
		{
			const auto Base = TreePath(PathUtil::FromUtf8(Root));
			Physical(Base);
			std::uint64_t Total = 0;
			for (const auto& Item : fs::recursive_directory_iterator(Base))
			{
				Check(Limits);
				Physical(Item.path());
				if (Item.is_directory())
				{
					continue;
				}
				if (!Item.is_regular_file())
				{
					Fail("content-type", "unsupported content file type", PathUtil::ToUtf8(Item.path()));
				}
				const auto Name = PathUtil::ToUtf8(Item.path().lexically_relative(Base));
				Path(Name);
				if (Files.size() >= Limits.MaxFiles || Item.file_size() > Limits.MaxExpandedBytes - Total)
				{
					Fail("content-limit", "content tree exceeds its budget", Name);
				}
				auto Data = ReadFile(Item.path(), Limits);
				Total += Data.size();
				Files.push_back({Name, std::move(Data),
				    (Item.status().permissions() & fs::perms::owner_exec) != fs::perms::none});
			}
			ValidateFiles(Files, Limits);
			std::sort(Files.begin(), Files.end(), [](const auto& A, const auto& B)
			{
				return A.Path < B.Path;
			});
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Files.clear();
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception& E)
		{
			Files.clear();
			Error = {"content-read", E.what(), Root, {}};
			return false;
		}
	}
	bool WriteContentTree(const std::vector<FContentFile>& Files, const std::string& Root,
	    FDiagnostic& Error, const FLimits& Limits)
	{
		try
		{
			ValidateFiles(Files, Limits);
			const auto Base = PathUtil::FromUtf8(Root);
			Physical(Base, true);
			if (fs::exists(Base))
			{
				Fail("output-exists", "content output already exists", Root);
			}
			fs::create_directories(Base.parent_path());
			Physical(Base.parent_path());
			if (!fs::create_directory(Base))
			{
				Fail("output-exists", "content output was created concurrently", Root);
			}
			FContentTreeWriter Writer(Base);
			for (const auto& File : Files)
			{
				Check(Limits);
				Writer.Write(File.Path, File.Bytes);
				#if !defined(_WIN32)
				const auto Target = Base / PathUtil::FromUtf8(File.Path);
				fs::permissions(Target, File.Executable ? fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec : fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);
				#endif
			}
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception& E)
		{
			Error = {"content-write", E.what(), Root, {}};
			return false;
		}
	}
}
