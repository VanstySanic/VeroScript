// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCliProject.h"
#include <cerrno>
#include <filesystem>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace VeroScript::Cli
{
	namespace
	{
		namespace fs = std::filesystem;

		fs::path PathFromUtf8(std::string_view Text)
		{
			#if defined(__cpp_char8_t) && __cpp_char8_t >= 201811L
			return fs::path(std::u8string(Text.begin(), Text.end()));
			#else
			return fs::u8path(Text);
			#endif
		}

		std::string Utf8Path(const fs::path& Path)
		{
			// u8string stores char8_t in C++20. Preserve UTF-8 bytes for the
			// narrow strings and streams used at the CLI boundary in both modes.
			const auto Bytes = Path.u8string();
			return {reinterpret_cast<const char*>(Bytes.data()), Bytes.size()};
		}

		void Usage(std::ostream& Output)
		{
			Output << "Usage: veroscript project init [--path <directory>]\n"
			          "Create missing project/build configuration and a default main.vs.\n"
			          "Existing regular files are kept; no dependencies are installed.\n";
		}

		bool ExistingFile(const fs::path& Path)
		{
			std::error_code Error;
			const auto Status = fs::symlink_status(Path, Error);
			if (Error == std::errc::no_such_file_or_directory || Status.type() == fs::file_type::not_found)
			{
				return false;
			}
			if (Error)
			{
				throw std::system_error(Error, "cannot inspect " + Utf8Path(Path));
			}
			if (!fs::is_regular_file(Status))
			{
				throw std::runtime_error("initialization destination must be a regular file: " + Utf8Path(Path));
			}
			return true;
		}

		bool CreateTextFile(const fs::path& Path, std::string_view Text)
		{
			if (ExistingFile(Path))
			{
				return false;
			}
			// C++17 ofstream truncates existing files. Exclusive native creation
			// preserves user files even if another initializer wins the race.
			std::error_code Error;
			#if defined(_WIN32)
			auto NativePath = fs::path(Path).make_preferred().native();
			if (NativePath.rfind(L"\\\\?\\", 0) != 0)
			{
				NativePath = NativePath.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + NativePath.substr(2) : L"\\\\?\\" + NativePath;
			}
			const auto Handle = ::CreateFileW(NativePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			    FILE_ATTRIBUTE_NORMAL, nullptr);
			if (Handle == INVALID_HANDLE_VALUE)
			{
				const auto Code = GetLastError();
				if (Code == ERROR_FILE_EXISTS || Code == ERROR_ALREADY_EXISTS)
				{
					if (!ExistingFile(Path))
					{
						throw std::runtime_error("initialization destination changed; retry: " + Utf8Path(Path));
					}
					return false;
				}
				throw std::system_error(static_cast<int>(Code), std::system_category(), "cannot create " + Utf8Path(Path));
			}
			DWORD Written = 0;
			if (!WriteFile(Handle, Text.data(), static_cast<DWORD>(Text.size()), &Written, nullptr))
			{
				Error = {static_cast<int>(GetLastError()), std::system_category()};
			}
			else if (Written != Text.size())
			{
				Error = std::make_error_code(std::errc::io_error);
			}
			if (!CloseHandle(Handle) && !Error)
			{
				Error = {static_cast<int>(GetLastError()), std::system_category()};
			}
			#else
			const auto Handle = ::open(Path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
			if (Handle < 0)
			{
				if (errno == EEXIST)
				{
					if (!ExistingFile(Path))
					{
						throw std::runtime_error("initialization destination changed; retry: " + Utf8Path(Path));
					}
					return false;
				}
				throw std::system_error(errno, std::generic_category(), "cannot create " + Utf8Path(Path));
			}
			std::size_t Offset = 0;
			while (Offset < Text.size())
			{
				const auto Written = ::write(Handle, Text.data() + Offset, Text.size() - Offset);
				if (Written < 0 && errno == EINTR)
				{
					continue;
				}
				if (Written <= 0)
				{
					Error = Written < 0 ? std::error_code(errno, std::generic_category()) : std::make_error_code(std::errc::io_error);
					break;
				}
				Offset += static_cast<std::size_t>(Written);
			}
			if (::close(Handle) != 0 && !Error)
			{
				Error = {errno, std::generic_category()};
			}
			#endif
			if (Error)
			{
				std::error_code CleanupError;
				fs::remove(Path, CleanupError);
				throw std::system_error(Error, "cannot write " + Utf8Path(Path));
			}
			return true;
		}

		std::string ProjectName(const fs::path& Root)
		{
			auto Directory = Utf8Path(Root.filename());
			if (Directory.empty())
			{
				Directory = Utf8Path(Root.parent_path().filename());
			}
			std::string Name;
			for (unsigned char C : Directory)
			{
				if (C >= 'A' && C <= 'Z')
				{
					C += 'a' - 'A';
				}
				if ((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9'))
				{
					Name += static_cast<char>(C);
				}
				else if (!Name.empty() && Name.back() != '-')
				{
					Name += '-';
				}
				if (Name.size() == 64)
				{
					break;
				}
			}
			if (!Name.empty() && Name.back() == '-')
			{
				Name.pop_back();
			}
			return Name.size() >= 2 ? Name : "my-project";
		}
	}

	int RunProject(const std::vector<std::string>& Arguments, const std::string& StartupDirectory,
	    std::ostream& Output, std::ostream& Errors)
	{
		try
		{
			if (Arguments.empty())
			{
				Usage(Errors);
				return 2;
			}
			if (Arguments.size() == 1 && (Arguments[0] == "--help" || Arguments[0] == "-h" || Arguments[0] == "-?"))
			{
				Usage(Output);
				return 0;
			}
			if (Arguments[0] != "init")
			{
				throw std::runtime_error("unknown project command: " + Arguments[0]);
			}
			std::string SelectedPath;
			bool bHasPath = false, bHelp = false;
			for (std::size_t Index = 1; Index < Arguments.size(); ++Index)
			{
				const auto& Argument = Arguments[Index];
				if (Argument == "--help" || Argument == "-h" || Argument == "-?")
				{
					bHelp = true;
					continue;
				}
				if (Argument != "--path" && Argument.rfind("--path=", 0) != 0)
				{
					throw std::runtime_error("unknown project init option: " + Argument);
				}
				if (bHasPath)
				{
					throw std::runtime_error("duplicate --path selector");
				}
				bHasPath = true;
				if (Argument == "--path")
				{
					if (++Index == Arguments.size() || Arguments[Index].empty() || Arguments[Index].front() == '-')
					{
						throw std::runtime_error("--path requires a directory");
					}
					SelectedPath = Arguments[Index];
				}
				else
				{
					SelectedPath = Argument.substr(7);
				}
				if (SelectedPath.empty())
				{
					throw std::runtime_error("--path requires a directory");
				}
			}
			if (bHelp)
			{
				Usage(Output);
				return 0;
			}
			const auto Startup = PathFromUtf8(StartupDirectory);
			const auto Selected = PathFromUtf8(SelectedPath);
			if (!Startup.is_absolute() || (!Selected.is_absolute() && (Selected.has_root_name() || Selected.has_root_directory())))
			{
				throw std::runtime_error("project paths require an absolute startup directory and an unambiguous --path");
			}
			const auto Root = fs::weakly_canonical(SelectedPath.empty() ? Startup : Startup / Selected);
			const auto Project = Root / "veroscript-project.json", Build = Root / "veroscript-build.json", Entry = Root / "main.vs";
			ExistingFile(Project);
			const bool bHasBuild = ExistingFile(Build);
			if (!bHasBuild)
			{
				ExistingFile(Entry);
			}
			fs::create_directories(Root);
			const auto Name = ProjectName(Root);
			const auto Create = [&](const fs::path& Path, const std::string& Text)
			{
				const bool bCreated = CreateTextFile(Path, Text);
				Output << (bCreated ? "Created " : "Kept ") << Utf8Path(Path) << '\n';
				return bCreated;
			};
			Create(Project, "{\n\t\"format\": \"veroscript-project\",\n\t\"formatVersion\": \"0.1.1\",\n\t\"name\": \"" + Name +
			    "\",\n\t\"version\": \"0.1.1\",\n\t\"dependencies\": {}\n}\n");
			if (!bHasBuild)
			{
				Create(Entry, "int main()\n{\n\treturn 0;\n}\n");
			}
			Create(Build, "{\n\t\"format\": \"veroscript-build\",\n\t\"formatVersion\": \"0.1.1\",\n\t\"platform\": \"portable\",\n"
			    "\t\"defaultTargets\": [\"application\"],\n\t\"targets\": {\n\t\t\"application\": {\n\t\t\t\"kind\": \"application\",\n"
			    "\t\t\t\"entry\": \"main.vs\",\n\t\t\t\"packageName\": \"" + Name + "\",\n\t\t\t\"options\": {\"version\": \"0.1.1\"},\n"
			    "\t\t\t\"output\": \"app.vsapp\"\n\t\t}\n\t}\n}\n");
			return 0;
		}
		catch (const std::exception& Error)
		{
			Errors << "VeroScript: " << Error.what() << '\n';
			return 2;
		}
	}
}
