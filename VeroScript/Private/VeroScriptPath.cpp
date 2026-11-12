// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPath.h"

#include "VeroScriptRuntime.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace VeroScript::PathUtil
{
	namespace
	{
		#if defined(_WIN32)
		bool IsOrdinaryWindowsComponent(const std::string_view Component) noexcept
		{
			if (Component.empty()) return true;
			if (Component.back() == '.' || Component.back() == ' ') return false;
			if (Component.find_first_of(":*?\"<>|") != std::string_view::npos) return false;
			const auto Base = Component.substr(0, Component.find('.'));
			if (!Base.empty() && Base.back() == ' ') return false;
			const auto IsName = [&](const std::string_view Name)
			{
				if (Base.size() != Name.size()) return false;
				for (std::size_t Index = 0; Index < Base.size(); ++Index)
				{
					const auto Character = Base[Index];
					const auto Upper = Character >= 'a' && Character <= 'z'
						? static_cast<char>(Character - 'a' + 'A') : Character;
					if (Upper != Name[Index]) return false;
				}
				return true;
			};
			if (IsName("CON") || IsName("PRN") || IsName("AUX") || IsName("NUL") ||
				IsName("CONIN$") || IsName("CONOUT$"))
			{
				return false;
			}
			if (Base.size() >= 4u)
			{
				const auto Prefix = Base.substr(0, 3u);
				const auto IsPort = [](const std::string_view Prefix, const char* Name)
				{
					for (std::size_t Index = 0; Index < 3u; ++Index)
					{
						const auto Character = Prefix[Index];
						const auto Upper = Character >= 'a' && Character <= 'z'
							? static_cast<char>(Character - 'a' + 'A') : Character;
						if (Upper != Name[Index]) return false;
					}
					return true;
				};
				const auto Number = Base.substr(3u);
				if ((IsPort(Prefix, "COM") || IsPort(Prefix, "LPT")) &&
					((Number.size() == 1u && Number[0] >= '0' && Number[0] <= '9') ||
						Number == "\xc2\xb9" || Number == "\xc2\xb2" || Number == "\xc2\xb3"))
				{
					return false;
				}
			}
			return true;
		}

		void NormalizeWindowsPublicSpelling(std::string& Path)
		{
			if (Path.compare(0, 4u, "//?/") != 0) return;
			const bool Drive = Path.size() >= 7u &&
				((Path[4] >= 'A' && Path[4] <= 'Z') || (Path[4] >= 'a' && Path[4] <= 'z')) &&
				Path[5] == ':' && Path[6] == '/';
			const bool Unc = Path.size() > 8u &&
				(Path[4] == 'U' || Path[4] == 'u') &&
				(Path[5] == 'N' || Path[5] == 'n') &&
				(Path[6] == 'C' || Path[6] == 'c') && Path[7] == '/';
			if (!Drive && !Unc) return;
			std::size_t Begin = Drive ? 7u : 8u;
			if (Unc)
			{
				const auto ServerEnd = Path.find('/', Begin);
				if (ServerEnd == std::string::npos || ServerEnd == Begin ||
					ServerEnd + 1u >= Path.size() || Path[ServerEnd + 1u] == '/')
				{
					return;
				}
				const auto ShareEnd = Path.find('/', ServerEnd + 1u);
				const auto LastShare = ShareEnd == std::string::npos ? Path.size() - 1u : ShareEnd - 1u;
				if (Path[ServerEnd - 1u] == '.' || Path[ServerEnd - 1u] == ' ' ||
					Path[LastShare] == '.' || Path[LastShare] == ' ')
				{
					return;
				}
				Begin = ShareEnd == std::string::npos ? Path.size() : ShareEnd + 1u;
			}
			for (std::size_t Start = Begin; Start < Path.size();)
			{
				const auto End = Path.find('/', Start);
				if (!IsOrdinaryWindowsComponent(std::string_view(Path).substr(Start,
						End == std::string::npos ? End : End - Start)))
				{
					return;
				}
				if (End == std::string::npos)
				{
					break;
				}
				Start = End + 1u;
			}
			// Extended DOS/UNC prefixes belong to the filesystem boundary, not to
			// public module identities. Other namespaces and verbatim-only names
			// keep their spelling because removing the prefix can change the object.
			if (Drive)
			{
				Path.erase(0, 4u);
			}
			else
			{
				Path.replace(0, 8u, "//");
			}
		}
		#endif

		void TrimTrailingSeparators(std::string& Path)
		{
			std::size_t RootLength = 1;
			try
			{
				const std::string Root = ToUtf8(FromUtf8(Path).root_path());
				RootLength = Root.empty() ? 0 : Root.size();
			}
			catch (const std::exception&)
			{
				// Keep at least one leading separator for an unrepresentable root.
			}
			while (Path.size() > RootLength && !Path.empty() && Path.back() == '/')
			{
				Path.pop_back();
			}
		}
	}

	std::filesystem::path FromUtf8(const std::string_view Text)
	{
		#if defined(_WIN32)
		#if defined(__cpp_char8_t) && __cpp_char8_t >= 201811L
		std::u8string Utf8Text;
		Utf8Text.reserve(Text.size());
		for (const unsigned char Byte : Text)
		{
			Utf8Text.push_back(static_cast<char8_t>(Byte));
		}
		std::filesystem::path Result(Utf8Text);
		#else
		auto Result = std::filesystem::u8path(Text.begin(), Text.end());
		#endif
		Result.make_preferred();
		// Win32 extended paths are needed even when the embedding executable
		// has no longPathAware manifest. Keep ordinary public path spelling for
		// short paths; apply the prefix only at the filesystem boundary.
		auto Native = Result.native();
		if (Result.is_absolute() && Native.size() >= 248 && Native.rfind(L"\\\\?\\", 0) != 0)
		{
			if (Native.rfind(L"\\\\", 0) == 0)
			{
				Native = L"\\\\?\\UNC\\" + Native.substr(2);
			}

			else
			{
				Native = L"\\\\?\\" + Native;
			}

			return std::filesystem::path(Native);
		}
		return Result;
		#else
		return std::filesystem::path(std::string(Text));
		#endif
	}

	std::string ToUtf8(const std::filesystem::path& Path)
	{
		#if defined(_WIN32)
		const auto Utf8Bytes = Path.generic_u8string();
		std::string Result;
		Result.reserve(Utf8Bytes.size());
		for (const auto Byte : Utf8Bytes)
		{
			Result.push_back(static_cast<char>(Byte));
		}
		NormalizeWindowsPublicSpelling(Result);
		return Result;
		#else
		return Path.generic_string();
		#endif
	}

	std::string Normalize(std::string Path)
	{
		#if defined(_WIN32)
		for (char& Character : Path)
		{
			if (Character == '\\')
			{
				Character = '/';
			}
		}
		#endif
		if (Path.empty())
		{
			return Path;
		}
		try
		{
			std::string Result = ToUtf8(FromUtf8(Path).lexically_normal());
			TrimTrailingSeparators(Result);
			return Result;
		}
		catch (const std::exception&)
		{
			TrimTrailingSeparators(Path);
			return Path;
		}
	}

	std::string IdentityKey(std::string Path)
	{
		std::string Result = Normalize(std::move(Path));
		try
		{
			if (!Result.empty())
			{
				std::error_code ErrorCode;
				const std::filesystem::path AsPath = FromUtf8(Result);
				if (std::filesystem::exists(AsPath, ErrorCode) && !ErrorCode)
				{
					const std::filesystem::path Canonical =
						std::filesystem::weakly_canonical(AsPath, ErrorCode);
					if (!ErrorCode)
					{
						Result = ToUtf8(Canonical);
						TrimTrailingSeparators(Result);
					}
				}
			}
		}
		catch (const std::exception&)
		{
			// Keep the normalized UTF-8 spelling for invalid/unrepresentable paths.
		}
		#ifdef _WIN32
		bool bCaseFolded = false;
		try
		{
			const std::wstring Native = FromUtf8(Result).native();
			if (!Native.empty()
				&& Native.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()))
			{
				const int Required = LCMapStringEx(
					LOCALE_NAME_INVARIANT,
					LCMAP_LOWERCASE,
					Native.data(),
					static_cast<int>(Native.size()),
					nullptr,
					0,
					nullptr,
					nullptr,
					0);
				if (Required > 0)
				{
					std::wstring Folded(static_cast<std::size_t>(Required), L'\0');
					if (LCMapStringEx(
							LOCALE_NAME_INVARIANT,
							LCMAP_LOWERCASE,
							Native.data(),
							static_cast<int>(Native.size()),
							Folded.data(),
							Required,
							nullptr,
							nullptr,
							0)
						== Required)
					{
						Result = ToUtf8(std::filesystem::path(std::move(Folded)));
						bCaseFolded = true;
					}
				}
			}
		}
		catch (const std::exception&)
		{
		}
		if (!bCaseFolded)
		{
			for (char& Character : Result)
			{
				if (Character >= 'A' && Character <= 'Z')
				{
					Character = static_cast<char>(Character - 'A' + 'a');
				}
			}
		}
		#endif
		return Result;
	}

	bool Equal(const std::string& Left, const std::string& Right)
	{
		return IdentityKey(Left) == IdentityKey(Right);
	}

	std::filesystem::path FindSourceModuleRoot(const std::string_view Directory)
	{
		namespace fs = std::filesystem;
		std::error_code Error;
		const fs::path Root = fs::absolute(FromUtf8(Directory), Error).lexically_normal();
		if (Error)
		{
			return {};
		}
		const auto IsSourceModule = [](const fs::path& Candidate)
		{
			std::error_code CandidateError;
			return fs::is_regular_file(
				Candidate / "VeroScript/Public/VeroScriptVersion.h", CandidateError);
		};
		if (IsSourceModule(Root))
		{
			return Root;
		}
		// Runtime working directories often contain many binaries. Only enumerate
		// a source repository, never unrelated user directories or deployment bins.
		if (!fs::is_regular_file(Root / "CMakeLists.txt", Error))
		{
			return {};
		}
		fs::path Module;
		fs::directory_iterator Entry(Root, Error);
		const fs::directory_iterator End;
		while (!Error && Entry != End)
		{
			if (IsSourceModule(Entry->path()))
			{
				if (!Module.empty())
				{
					return {};
				}
				Module = Entry->path();
			}
			Entry.increment(Error);
		}
		return Error ? fs::path{} : Module;
	}

	std::filesystem::path MakeTemporarySiblingPath(
		const std::filesystem::path& FinalPath)
	{
		static std::atomic<std::uint64_t> Counter{0};
		const std::uint64_t Sequence =
			Counter.fetch_add(1, std::memory_order_relaxed);
		#if defined(_WIN32)
		const std::uint64_t ProcessId = GetCurrentProcessId();
		#else
		const std::uint64_t ProcessId = static_cast<std::uint64_t>(getpid());
		#endif
		std::filesystem::path Result = FinalPath;
		Result += ".tmp."
			+ std::to_string(ProcessId)
			+ "."
			+ std::to_string(Sequence);
		return Result;
	}

	bool ReplaceFileAtomically(
		const std::filesystem::path& TempPath,
		const std::filesystem::path& FinalPath,
		std::error_code& Error)
	{
		Error.clear();
		#if defined(_WIN32)
		if (!MoveFileExW(
				TempPath.c_str(),
				FinalPath.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			Error = std::error_code(
				static_cast<int>(GetLastError()), std::system_category());
			return false;
		}
		return true;
		#else
		std::filesystem::rename(TempPath, FinalPath, Error);
		return !Error;
		#endif
	}

	std::string ExecutableDirectory()
	{
		return Runtime::GetExecutableDirectory();
	}
}
