// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnvironment.h"

#include <cstdlib>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace VeroScript::Environment
{
	namespace
	{
		bool IsValidName(const std::string_view Name)
		{
			return !Name.empty() && Name.find('=') == std::string_view::npos && Name.find('\0') == std::string_view::npos;
		}

		std::mutex& EnvironmentMutex()
		{
			static std::mutex Mutex;
			return Mutex;
		}

		#if defined(_WIN32)
		bool Utf8ToWide(const std::string_view Text, std::wstring& Out)
		{
			Out.clear();
			if (Text.empty())
			{
				return true;
			}
			if (Text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			{
				return false;
			}
			const int Required = MultiByteToWideChar(
			    CP_UTF8,
			    MB_ERR_INVALID_CHARS,
			    Text.data(),
			    static_cast<int>(Text.size()),
			    nullptr,
			    0);
			if (Required <= 0)
			{
				return false;
			}
			Out.resize(static_cast<std::size_t>(Required));
			return MultiByteToWideChar(
			           CP_UTF8,
			           MB_ERR_INVALID_CHARS,
			           Text.data(),
			           static_cast<int>(Text.size()),
			           Out.data(),
			           Required) == Required;
		}

		bool WideToUtf8(const std::wstring_view Text, std::string& Out)
		{
			Out.clear();
			if (Text.empty())
			{
				return true;
			}
			if (Text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			{
				return false;
			}
			const int Required = WideCharToMultiByte(
			    CP_UTF8,
			    WC_ERR_INVALID_CHARS,
			    Text.data(),
			    static_cast<int>(Text.size()),
			    nullptr,
			    0,
			    nullptr,
			    nullptr);
			if (Required <= 0)
			{
				return false;
			}
			Out.resize(static_cast<std::size_t>(Required));
			return WideCharToMultiByte(
			           CP_UTF8,
			           WC_ERR_INVALID_CHARS,
			           Text.data(),
			           static_cast<int>(Text.size()),
			           Out.data(),
			           Required,
			           nullptr,
			           nullptr) == Required;
		}
		#endif
	}

	std::optional<std::string> GetUtf8(const std::string_view Name)
	{
		std::optional<std::string> Result;
		(void)ReadUtf8(Name, Result);
		return Result;
	}

	bool ReadUtf8(const std::string_view Name, std::optional<std::string>& Output)
	{
		if (!IsValidName(Name)) return false;
		const std::lock_guard<std::mutex> Lock(EnvironmentMutex());
		#if defined(_WIN32)
		std::wstring WideName;
		if (!Utf8ToWide(Name, WideName)) return false;
		const auto EmptyOrMissing = [&](DWORD Error)
		{
			if (Error == ERROR_ENVVAR_NOT_FOUND)
			{
				Output.reset();
				return true;
			}
			if (Error == ERROR_SUCCESS)
			{
				Output.emplace();
				return true;
			}
			return false;
		};
		SetLastError(ERROR_SUCCESS);
		DWORD Capacity = GetEnvironmentVariableW(WideName.c_str(), nullptr, 0);
		if (!Capacity) return EmptyOrMissing(GetLastError());
		for (int Attempt = 0; Attempt < 4; ++Attempt)
		{
			std::wstring WideValue(static_cast<std::size_t>(Capacity), L'\0');
			SetLastError(ERROR_SUCCESS);
			const DWORD Written = GetEnvironmentVariableW(WideName.c_str(), WideValue.data(), Capacity);
			if (!Written) return EmptyOrMissing(GetLastError());
			if (Written < Capacity)
			{
				WideValue.resize(static_cast<std::size_t>(Written));
				std::string Value;
				if (!WideToUtf8(WideValue, Value)) return false;
				Output.emplace(std::move(Value));
				return true;
			}
			Capacity = Written;
		}
		return false;
		#else
		const std::string NativeName(Name);
		if (const char* const Value = std::getenv(NativeName.c_str()))
		{
			std::string Copy(Value);
			Output.emplace(std::move(Copy));
		}
		else
		{
			Output.reset();
		}
		return true;
		#endif
	}

	bool SetUtf8(const std::string_view Name, const std::string_view Value)
	{
		if (!IsValidName(Name) || Value.find('\0') != std::string_view::npos)
		{
			return false;
		}
		const std::lock_guard<std::mutex> Lock(EnvironmentMutex());
		#if defined(_WIN32)
		std::wstring WideName;
		std::wstring WideValue;
		return Utf8ToWide(Name, WideName) && Utf8ToWide(Value, WideValue) && SetEnvironmentVariableW(WideName.c_str(), WideValue.c_str()) != FALSE;
		#else
		const std::string NativeName(Name);
		const std::string NativeValue(Value);
		return setenv(NativeName.c_str(), NativeValue.c_str(), 1) == 0;
		#endif
	}

	bool UnsetUtf8(const std::string_view Name)
	{
		if (!IsValidName(Name))
		{
			return false;
		}
		const std::lock_guard<std::mutex> Lock(EnvironmentMutex());
		#if defined(_WIN32)
		std::wstring WideName;
		if (!Utf8ToWide(Name, WideName))
		{
			return false;
		}
		if (SetEnvironmentVariableW(WideName.c_str(), nullptr) != FALSE)
		{
			return true;
		}
		// Removing an already absent variable is idempotent, matching unsetenv().
		return GetLastError() == ERROR_ENVVAR_NOT_FOUND;
		#else
		const std::string NativeName(Name);
		return unsetenv(NativeName.c_str()) == 0;
		#endif
	}
}
