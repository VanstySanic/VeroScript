// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "ApplicationInternal.h"
#include "VeroScriptDarwinPaths.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <cstdio>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace VeroScript::Application::Detail
{
	FFailure::FFailure(std::string Code, std::string Message, std::string Path)
	    : std::runtime_error(Message), Diagnostic{std::move(Code), std::move(Message), std::move(Path), {}}
	{
	}
	[[noreturn]] void Fail(const std::string& Code, const std::string& Message, const std::string& Path)
	{
		throw FFailure(Code, Message, Path);
	}
	void Check(const FLimits& Limits)
	{
		if (Limits.CheckCancelled)
		{
			Limits.CheckCancelled();
		}
	}
	J Parse(std::string_view Text, const FLimits& Limits)
	{
		if (Text.size() > Limits.MaxMetadataBytes)
		{
			Fail("metadata-limit", "JSON exceeds its byte budget");
		}
		std::vector<std::set<std::string>> Objects;
		try
		{
			return J::parse(Text, [&](int Depth, J::parse_event_t Event, J& Value)
			{
				Check(Limits);
				if (Depth < 0 || static_cast<std::size_t>(Depth) > Limits.MaxJsonDepth)
				{
					Fail("json-depth", "JSON exceeds its nesting budget");
				}
				if (Event == J::parse_event_t::object_start)
				{
					Objects.emplace_back();
				}
				else if (Event == J::parse_event_t::key)
				{
					if (Objects.empty() || !Objects.back().insert(Value.get<std::string>()).second)
					{
						Fail("json-key", "duplicate JSON key");
					}
				}
				else if (Event == J::parse_event_t::object_end)
				{
					Objects.pop_back();
				}
				return true;
			});
		}
		catch (const J::exception& E)
		{
			Fail("json", E.what());
		}
	}
	void Keys(const J& Value, std::initializer_list<const char*> Allowed, const std::string& Field)
	{
		if (!Value.is_object())
		{
			Fail("object", "expected a JSON object", Field);
		}
		for (auto I = Value.begin(); I != Value.end(); ++I)
		{
			if (std::none_of(Allowed.begin(), Allowed.end(), [&](const char* K)
			{
				return I.key() == K;
			}))
			{
				Fail("unknown-field", "unknown JSON field", Field + "." + I.key());
			}
		}
	}
	std::uint64_t Count(const J& Value, const std::string& Field)
	{
		if (!Value.is_string())
		{
			Fail("count", "expected a canonical unsigned decimal string", Field);
		}
		const auto S = Value.get<std::string>();
		std::uint64_t Result = 0;
		const auto R = std::from_chars(S.data(), S.data() + S.size(), Result);
		if (S.empty() || (S.size() > 1 && S[0] == '0') ||
		    R.ec != std::errc{} || R.ptr != S.data() + S.size() || S[0] == '+' || S[0] == '-')
		{
			Fail("count", "invalid unsigned decimal string", Field);
		}
		return Result;
	}
	std::string Fold(std::string S)
	{
		for (char& C : S)
		{
			if (C >= 'A' && C <= 'Z')
			{
				C = static_cast<char>(C - 'A' + 'a');
			}
		}
		return S;
	}
	bool Under(const fs::path& Child, const fs::path& Parent)
	{
		const auto C = TreePath(Child);
		const auto P = TreePath(Parent);
		auto I = C.begin();
		for (auto K = P.begin(); K != P.end(); ++K, ++I)
		{
			if (I == C.end()) return false;
			#if defined(_WIN32)
			if (CompareStringOrdinal(I->native().c_str(), -1, K->native().c_str(), -1, TRUE) != CSTR_EQUAL) return false;
			#else
			if (*I != *K) return false;
			#endif
		}
		return true;
	}
	void Path(const std::string& S)
	{
		std::string Error;
		if (!ValidateContentPath(S, Error))
		{
			Fail("content-path", Error, S);
		}
	}
	fs::path TreePath(const fs::path& P)
	{
		auto Result = PathUtil::FromUtf8(PathUtil::ToUtf8(P.lexically_normal()));
		#if defined(_WIN32)
		const auto Native = Result.native();
		if (Result.is_absolute() && Native.rfind(L"\\\\?\\", 0) != 0)
		{
			return fs::path(Native.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + Native.substr(2) : L"\\\\?\\" + Native);
		}
		#endif
		return Result;
	}
	void Physical(const fs::path& P, bool Missing)
	{
		if (PathUtil::ToUtf8(P).find('\0') != std::string::npos)
		{
			Fail("physical-path", "physical paths cannot contain a zero byte");
		}
		if (!P.is_absolute())
		{
			Fail("absolute-path", "an explicit absolute physical path is required", PathUtil::ToUtf8(P));
		}
		#if defined(_WIN32)
		auto NativePath = PathUtil::FromUtf8(PathUtil::ToUtf8(P.lexically_normal()));
		NativePath.make_preferred();
		const auto Native = NativePath.native();
		std::size_t Start = NativePath.root_path().native().size();
		if (Native.rfind(L"\\\\.\\", 0) == 0)
		{
			Fail("physical-device", "device paths are not content paths");
		}
		if (Native.rfind(L"\\\\?\\UNC\\", 0) == 0)
		{
			const auto Server = Native.find(L'\\', 8);
			Start = Server == std::wstring::npos ? Native.size() : Native.find(L'\\', Server + 1);
		}
		else if (Native.rfind(L"\\\\?\\", 0) == 0)
		{
			if (Native.size() < 7 || Native[5] != L':' || Native[6] != L'\\')
			{
				Fail("physical-device", "unsupported extended path namespace");
			}
			Start = 7;
		}
		if (Start == std::wstring::npos)
		{
			Start = Native.size();
		}
		for (auto End = Start; End <= Native.size(); ++End)
		{
			if (End != Native.size() && Native[End] != L'\\')
			{
				continue;
			}
			const auto Prefix = Native.substr(0, End);
			const auto Flags = GetFileAttributesW(Prefix.c_str());
			if (Flags == INVALID_FILE_ATTRIBUTES)
			{
				const auto Code = GetLastError();
				if (Missing && (Code == ERROR_FILE_NOT_FOUND || Code == ERROR_PATH_NOT_FOUND))
				{
					continue;
				}
				Fail("physical-path", "physical path cannot be inspected", PathUtil::ToUtf8(fs::path(Prefix)));
			}
			if (Flags & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))
			{
				Fail("physical-link", "redirected or device path is not allowed", PathUtil::ToUtf8(fs::path(Prefix)));
			}
		}
		#else
		fs::path Current;
		for (const auto& Part : P.lexically_normal())
		{
			Current /= Part;
			std::error_code Ec;
			const auto Status = fs::symlink_status(Current, Ec);
			if (Ec || !fs::exists(Status))
			{
				if (Missing && ((!Ec && !fs::exists(Status)) || Ec == std::errc::no_such_file_or_directory))
				{
					continue;
				}
				Fail("physical-path", "physical path cannot be inspected", PathUtil::ToUtf8(Current));
			}
			if (fs::is_symlink(Status) && !PathDetail::IsTrustedDarwinSystemAlias(Current))
			{
				Fail("physical-link", "symbolic links are not allowed", PathUtil::ToUtf8(Current));
			}
		}
		#endif
	}
	std::vector<std::uint8_t> ReadFile(const fs::path& P, const FLimits& Limits)
	{
		Physical(P);
		FArtifactData Data;
		std::string Error;
		if (!ReadArtifactFile({PathUtil::ToUtf8(P), "generic", {}}, {Limits.MaxFileBytes, Limits.CheckCancelled}, Data, Error))
		{
			Fail("read", Error, PathUtil::ToUtf8(P));
		}
		return std::move(Data.Bytes);
	}
	void WriteFile(const fs::path& InputPath, const std::vector<std::uint8_t>& Data)
	{
		const auto P = PathUtil::FromUtf8(PathUtil::ToUtf8(InputPath));
		Physical(P, true);
		if (fs::exists(P))
		{
			Fail("output-exists", "output already exists", PathUtil::ToUtf8(P));
		}
		fs::create_directories(P.parent_path());
		Physical(P.parent_path());
		#if defined(_WIN32)
		HANDLE Out = CreateFileW(P.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (Out == INVALID_HANDLE_VALUE)
		{
			Fail("output-create", "cannot exclusively create output", PathUtil::ToUtf8(P));
		}
		bool Ok = true;
		std::size_t Offset = 0;
		while (Offset < Data.size())
		{
			const DWORD Size = static_cast<DWORD>(std::min<std::size_t>(Data.size() - Offset, 1u << 20));
			DWORD Written = 0;
			if (!::WriteFile(Out, Data.data() + Offset, Size, &Written, nullptr) || Written != Size)
			{
				Ok = false;
				break;
			}
			Offset += Written;
		}
		Ok = FlushFileBuffers(Out) && Ok;
		CloseHandle(Out);
		#else
		const int Out = ::open(P.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (Out < 0)
		{
			Fail("output-create", "cannot exclusively create output", PathUtil::ToUtf8(P));
		}
		bool Ok = true;
		std::size_t Offset = 0;
		while (Offset < Data.size())
		{
			const auto Written = ::write(Out, Data.data() + Offset, std::min<std::size_t>(Data.size() - Offset, 1u << 20));
			if (Written < 0 && errno == EINTR)
			{
				continue;
			}
			if (Written <= 0)
			{
				Ok = false;
				break;
			}
			Offset += static_cast<std::size_t>(Written);
		}
		Ok = ::fsync(Out) == 0 && Ok;
		::close(Out);
		#endif
		if (!Ok)
		{
			Fail("write", "cannot write output", PathUtil::ToUtf8(P));
		}
	}
	void FContentTreeWriter::Write(const std::string& Relative, const std::vector<std::uint8_t>& Data)
	{
		Path(Relative);
		const auto Name = PathUtil::FromUtf8(Relative);
		Directory(Name.parent_path());
		WriteFile(Root / Name, Data);
	}

	void FContentTreeWriter::Directory(const fs::path& Relative)
	{
		if (Relative.empty()) return;
		const auto Name = PathUtil::ToUtf8(Relative);
		if (Directories.count(Name)) return;
		Directory(Relative.parent_path());
		const auto P = PathUtil::FromUtf8(PathUtil::ToUtf8((Root / Relative)));
		Physical(P, true);
		if (!fs::create_directory(P))
		{
			Fail("content-collision", "logical content directories alias on the output filesystem", Name);
		}
		Directories.insert(Name);
	}
	std::vector<std::uint8_t> Bytes(std::string_view S)
	{
		return {S.begin(), S.end()};
	}
}
