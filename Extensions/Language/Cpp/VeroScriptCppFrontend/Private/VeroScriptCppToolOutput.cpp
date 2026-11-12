// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppToolOutput.h"
#include "VeroScriptCppPath.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <sys/stat.h>
#endif

namespace VeroScript::CppTools
{
	namespace
	{
		struct FStagedFile
		{
			std::filesystem::path Target;
			std::filesystem::path Temporary;
			std::filesystem::path Backup;
			bool bHadOriginal = false;
			bool bCommitted = false;
		};

		bool MovePath(
		    const std::filesystem::path& Source,
		    const std::filesystem::path& Target,
		    const bool bReplace,
		    std::string& OutError)
		{
			#if defined(_WIN32)
			const DWORD Flags = MOVEFILE_WRITE_THROUGH | (bReplace ? MOVEFILE_REPLACE_EXISTING : 0u);
			if (MoveFileExW(Source.c_str(), Target.c_str(), Flags))
			{
				return true;
			}
			OutError = "failed to move generated file (Win32 error " + std::to_string(GetLastError()) + ')';
			return false;
			#else
			std::error_code Error;
			if (!bReplace && std::filesystem::exists(Target, Error))
			{
				OutError = "generated-file backup path already exists";
				return false;
			}
			std::filesystem::rename(Source, Target, Error);
			if (!Error)
			{
				return true;
			}
			OutError = "failed to move generated file: " + Error.message();
			return false;
			#endif
		}

		void RemovePath(const std::filesystem::path& Path)
		{
			std::error_code Ignored;
			std::filesystem::remove(Path, Ignored);
		}

		std::string MakeNonce()
		{
			static std::atomic<std::uint64_t> Sequence{0};
			return std::to_string(
			           std::chrono::system_clock::now().time_since_epoch().count()) +
			    '.' + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + '.' + std::to_string(Sequence.fetch_add(1, std::memory_order_relaxed));
		}

		std::filesystem::path NormalizePath(const std::string_view Path)
		{
			const auto Raw = CppFrontend::Path::FromUtf8(std::string(Path));
			std::error_code Error;
			auto Absolute = std::filesystem::absolute(Raw, Error);
			if (Error) return Raw;
			const auto Canonical = std::filesystem::weakly_canonical(Absolute, Error);
			return (Error ? Absolute : Canonical).lexically_normal();
		}

		struct FPathLess
		{
			bool operator()(const std::filesystem::path& A, const std::filesystem::path& B) const
			{
				#if defined(_WIN32)
				return CompareStringOrdinal(A.c_str(), -1, B.c_str(), -1, TRUE) == CSTR_LESS_THAN;
				#else
				return A < B;
				#endif
			}
		};

		std::optional<std::string> PhysicalIdentity(const std::filesystem::path& Path)
		{
			#if defined(_WIN32)
			const HANDLE File = CreateFileW(Path.c_str(), 0,
			    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (File == INVALID_HANDLE_VALUE) return {};
			FILE_ID_INFO Info{};
			const bool Read = GetFileInformationByHandleEx(File, FileIdInfo, &Info, sizeof(Info)) != 0;
			CloseHandle(File);
			if (!Read) return {};
			// The volume and full 128-bit ID identify physical aliases, including ReFS.
			std::string Key(reinterpret_cast<const char*>(&Info.VolumeSerialNumber), sizeof(Info.VolumeSerialNumber));
			Key.append(reinterpret_cast<const char*>(Info.FileId.Identifier), sizeof(Info.FileId.Identifier));
			return Key;
			#else
			struct stat Info{};
			if (::stat(Path.c_str(), &Info) != 0) return {};
			return std::to_string(static_cast<std::uintmax_t>(Info.st_dev)) + ':' + std::to_string(static_cast<std::uintmax_t>(Info.st_ino));
			#endif
		}
	}

	bool FOutputFileNameLess::operator()(const std::string_view Left, const std::string_view Right) const
	{
		#if defined(_WIN32)
		return FPathLess{}(CppFrontend::Path::FromUtf8(Left), CppFrontend::Path::FromUtf8(Right));
		#else
		const auto Fold = [](const unsigned char Character)
		{
			return Character >= 'A' && Character <= 'Z' ? static_cast<unsigned char>(Character + ('a' - 'A'))
			                                            : Character;
		};
		return std::lexicographical_compare(Left.begin(), Left.end(), Right.begin(), Right.end(),
		    [&](const unsigned char A, const unsigned char B)
		{
			return Fold(A) < Fold(B);
		});
		#endif
	}

	struct FFilePathSet::FState
	{
		bool Contains(const std::string& Original, const std::filesystem::path& Path,
		    const std::optional<std::string>& Identity, const bool Unknown) const
		{
			if (Paths.find(Path) != Paths.end() || (Identity && Identities.count(*Identity))) return true;
			// Some filesystems do not expose identities. Retain the original alias
			// checks there instead of weakening protection against overwriting inputs.
			const auto& Fallbacks = Unknown ? Originals : Unidentified;
			for (const auto& Previous : Fallbacks)
			{
				if (PathsReferToSameFile(Previous, Original)) return true;
			}

			return false;
		}

		std::map<std::filesystem::path, std::size_t, FPathLess> Paths;
		std::unordered_set<std::string> Identities;
		std::vector<std::string> Originals;
		std::vector<std::string> Unidentified;
	};

	FFilePathSet::FFilePathSet() : State(std::make_unique<FState>())
	{
	}
	FFilePathSet::~FFilePathSet() = default;

	bool FFilePathSet::Insert(const std::string_view Original)
	{
		const auto Path = NormalizePath(Original);
		const auto Identity = PhysicalIdentity(Path);
		std::error_code Error;
		const bool Unknown = !Identity && (std::filesystem::exists(Path, Error) || Error);
		const std::string Name(Original);
		if (State->Contains(Name, Path, Identity, Unknown)) return false;
		State->Paths.emplace(Path, State->Originals.size());
		if (Identity)
		{
			State->Identities.insert(*Identity);
		}

		if (Unknown)
		{
			State->Unidentified.push_back(Name);
		}

		State->Originals.push_back(Name);
		return true;
	}

	bool FFilePathSet::Contains(const std::string_view Original) const
	{
		const auto Path = NormalizePath(Original);
		const auto Identity = PhysicalIdentity(Path);
		std::error_code Error;
		const bool Unknown = !Identity && (std::filesystem::exists(Path, Error) || Error);
		return State->Contains(std::string(Original), Path, Identity, Unknown);
	}

	bool PathsReferToSameFile(const std::string& Left, const std::string& Right)
	{
		const std::filesystem::path RawLeft = CppFrontend::Path::FromUtf8(Left);
		const std::filesystem::path RawRight = CppFrontend::Path::FromUtf8(Right);
		std::error_code EquivalentError;
		if (std::filesystem::equivalent(RawLeft, RawRight, EquivalentError) && !EquivalentError)
		{
			return true;
		}
		std::error_code LeftError;
		std::error_code RightError;
		std::filesystem::path LeftPath = std::filesystem::absolute(RawLeft, LeftError);
		std::filesystem::path RightPath = std::filesystem::absolute(RawRight, RightError);
		if (LeftError || RightError)
		{
			return Left == Right;
		}
		std::error_code WeakError;
		const std::filesystem::path WeakLeft = std::filesystem::weakly_canonical(LeftPath, WeakError);
		if (!WeakError)
		{
			LeftPath = WeakLeft;
		}
		WeakError.clear();
		const std::filesystem::path WeakRight = std::filesystem::weakly_canonical(RightPath, WeakError);
		if (!WeakError)
		{
			RightPath = WeakRight;
		}
		LeftPath = LeftPath.lexically_normal();
		RightPath = RightPath.lexically_normal();
		#if defined(_WIN32)
		return CompareStringOrdinal(LeftPath.c_str(), -1, RightPath.c_str(), -1, TRUE) == CSTR_EQUAL;
		#else
		return LeftPath == RightPath;
		#endif
	}

	bool WriteOutputFilesAtomically(
	    const std::vector<FOutputFile>& Files,
	    std::string& OutError)
	{
		OutError.clear();
		if (Files.empty())
		{
			return true;
		}
		FFilePathSet Targets;
		for (const auto& File : Files)
		{
			if (File.Path.empty())
			{
				OutError = "generated output path must not be empty";
				return false;
			}
			// Native file APIs terminate paths at NUL. Appending our temporary suffix
			// after one would open and truncate the target itself during staging.
			if (File.Path.find('\0') != std::string::npos)
			{
				OutError = "generated output path must not contain NUL characters";
				return false;
			}
			if (!Targets.Insert(File.Path))
			{
				OutError = "generated output paths must be distinct";
				return false;
			}
		}

		const std::string Nonce = MakeNonce();
		std::vector<FStagedFile> Staged;
		Staged.reserve(Files.size());
		for (std::size_t Index = 0; Index < Files.size(); ++Index)
		{
			FStagedFile Entry;
			Entry.Target = CppFrontend::Path::FromUtf8(Files[Index].Path);
			Entry.Temporary = Entry.Target;
			Entry.Temporary += ".tmp." + Nonce + '.' + std::to_string(Index);
			Entry.Backup = Entry.Target;
			Entry.Backup += ".bak." + Nonce + '.' + std::to_string(Index);
			std::error_code Error;
			if (!Entry.Target.parent_path().empty())
			{
				std::filesystem::create_directories(Entry.Target.parent_path(), Error);
				if (Error)
				{
					OutError = "failed to create output directory: " + Error.message();
					for (const FStagedFile& Previous : Staged)
					{
						RemovePath(Previous.Temporary);
					}
					return false;
				}
			}
			std::ofstream Stream(Entry.Temporary, std::ios::binary | std::ios::trunc);
			if (!Stream)
			{
				OutError = "failed to open temporary output file";
				for (const FStagedFile& Previous : Staged)
				{
					RemovePath(Previous.Temporary);
				}
				return false;
			}
			Stream.write(Files[Index].Contents.data(),
			    static_cast<std::streamsize>(Files[Index].Contents.size()));
			Stream.flush();
			Stream.close();
			if (!Stream)
			{
				OutError = "failed to write temporary output file";
				RemovePath(Entry.Temporary);
				for (const FStagedFile& Previous : Staged)
				{
					RemovePath(Previous.Temporary);
				}
				return false;
			}
			Staged.push_back(std::move(Entry));
		}

		for (FStagedFile& Entry : Staged)
		{
			std::error_code ExistsError;
			Entry.bHadOriginal = std::filesystem::exists(Entry.Target, ExistsError);
			if (ExistsError)
			{
				OutError = "failed to inspect existing output file: " + ExistsError.message();
				for (const FStagedFile& Pending : Staged)
				{
					RemovePath(Pending.Temporary);
				}
				return false;
			}
			if (Entry.bHadOriginal && !std::filesystem::is_regular_file(Entry.Target, ExistsError))
			{
				OutError = ExistsError
				    ? "failed to inspect existing output file: " + ExistsError.message()
				    : "existing generated output target is not a regular file";
				for (const FStagedFile& Pending : Staged)
				{
					RemovePath(Pending.Temporary);
				}
				return false;
			}
		}
		for (std::size_t Index = 0; Index < Staged.size(); ++Index)
		{
			if (!Staged[Index].bHadOriginal)
			{
				continue;
			}
			std::string MoveError;
			if (!MovePath(Staged[Index].Target, Staged[Index].Backup, false, MoveError))
			{
				OutError = std::move(MoveError);
				for (std::size_t Restore = 0; Restore < Index; ++Restore)
				{
					if (Staged[Restore].bHadOriginal)
					{
						std::string RestoreError;
						if (!MovePath(Staged[Restore].Backup, Staged[Restore].Target, true,
						        RestoreError) &&
						    OutError.find("rollback") == std::string::npos)
						{
							OutError += "; rollback failed: " + RestoreError;
						}
					}
				}
				for (const FStagedFile& Entry : Staged)
				{
					RemovePath(Entry.Temporary);
				}
				return false;
			}
		}

		for (std::size_t Index = 0; Index < Staged.size(); ++Index)
		{
			std::string MoveError;
			if (MovePath(Staged[Index].Temporary, Staged[Index].Target, true, MoveError))
			{
				Staged[Index].bCommitted = true;
				continue;
			}
			OutError = std::move(MoveError);
			for (FStagedFile& Entry : Staged)
			{
				if (Entry.bCommitted)
				{
					RemovePath(Entry.Target);
				}
				RemovePath(Entry.Temporary);
			}
			for (FStagedFile& Entry : Staged)
			{
				if (!Entry.bHadOriginal)
				{
					continue;
				}
				std::string RestoreError;
				if (!MovePath(Entry.Backup, Entry.Target, true, RestoreError) && OutError.find("rollback") == std::string::npos)
				{
					OutError += "; rollback failed: " + RestoreError;
				}
			}
			return false;
		}

		for (const FStagedFile& Entry : Staged)
		{
			if (Entry.bHadOriginal)
			{
				RemovePath(Entry.Backup);
			}
		}
		return true;
	}
}
