// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPath.h"
#include <string>

namespace VeroScript::ModuleSupport
{
	inline bool Text(const std::string& S, bool Empty = false)
	{
		if ((!Empty && S.empty()) || S.size() > 1024 * 1024 || S.find('\0') != std::string::npos) return false;
		for (std::size_t I = 0; I < S.size();)
		{
			const auto C = static_cast<unsigned char>(S[I++]);
			if (C < 0x80)
			{
				continue;
			}

			unsigned N = C >= 0xc2 && C <= 0xdf ? 1
				: C >= 0xe0 && C <= 0xef		? 2
				: C >= 0xf0 && C <= 0xf4		? 3
												: 0;
			if (!N || N > S.size() - I) return false;
			const auto First = static_cast<unsigned char>(S[I]);
			if ((C == 0xe0 && First < 0xa0) || (C == 0xed && First >= 0xa0) || (C == 0xf0 && First < 0x90) ||
				(C == 0xf4 && First >= 0x90))
			{
				return false;
			}

			while (N--)
			{
				if ((static_cast<unsigned char>(S[I++]) & 0xc0) != 0x80) return false;
			}
		}
		return true;
	}
	inline void Append(std::string& To, const std::string& Value)
	{
		To += std::to_string(Value.size());
		To += ':';
		To += Value;
	}
	inline bool Relative(const std::string& S)
	{
		if (!Text(S) || S.front() == '/' || S.back() == '/' || S.find('\\') != std::string::npos ||
			S.find(':') != std::string::npos)
		{
			return false;
		}

		std::size_t Start = 0;
		while (Start < S.size())
		{
			const auto End = S.find('/', Start);
			const auto Part = S.substr(Start, End == std::string::npos ? End : End - Start);
			if (Part.empty() || Part == "." || Part == "..") return false;
			if (End == std::string::npos)
			{
				break;
			}

			Start = End + 1;
		}
		return true;
	}
	inline std::string Physical(const std::string& S)
	{
		if (!Text(S) || !PathUtil::FromUtf8(S).is_absolute()) return {};
		std::error_code E;
		const auto P = std::filesystem::weakly_canonical(PathUtil::FromUtf8(S), E);
		return E ? std::string{} : PathUtil::Normalize(PathUtil::ToUtf8(P));
	}
	inline bool Within(const std::string& Path, const std::string& Root)
	{
		return Path == Root || (Path.size() > Root.size() && Path.compare(0, Root.size(), Root) == 0 &&
			(Root.back() == '/' || Path[Root.size()] == '/'));
	}
}
