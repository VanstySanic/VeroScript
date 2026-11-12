// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRuntime.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::CompilerDetail
{
	inline bool IsVariantIntrinsicName(std::string_view Name)
	{
		if (Name.substr(0, 4) == "std.")
		{
			Name.remove_prefix(4);
		}
		return Name == "variant" || Name == "variant_is" || Name == "variant_has_value" || Name == "variant_type_name";
	}
	inline bool NeedsVariantBoxing(const std::vector<std::string>& Arguments, const std::vector<std::string>& Parameters)
	{
		const auto Base = [](std::string T)
		{
			for (const auto Prefix : {"const lvalue ", "lvalue ", "rvalue ", "const ref ", "ref ", "const "})
			{
				if (T.rfind(Prefix, 0) == 0)
				{
					T.erase(0, std::char_traits<char>::length(Prefix));
				}
			}
			return T;
		};
		for (std::size_t I = 0; I < Arguments.size() && !Parameters.empty(); ++I)
		{
			const auto& P = Parameters[std::min(I, Parameters.size() - 1)];
			std::string Param = P;
			if (Param.size() >= 3 && Param.compare(Param.size() - 3, 3, "...") == 0)
			{
				Param.resize(Param.size() - 3);
			}
			if (Base(Param) == "variant" && !Arguments[I].empty() && Base(Arguments[I]) != "variant") return true;
		}
		return false;
	}
	template <class TCandidates, class TScore, class TBoxing>
	void DiscardBoxingFallbacks(TCandidates& Candidates, TScore Score, TBoxing Boxing)
	{
		if (std::any_of(Candidates.begin(), Candidates.end(), [&](const auto& C)
		{
			return Score(C) >= 0 && !Boxing(C);
		}))
		{
			Candidates.erase(std::remove_if(Candidates.begin(), Candidates.end(), [&](const auto& C)
			{
				return Score(C) >= 0 && Boxing(C);
			}),
			    Candidates.end());
		}
	}
}
