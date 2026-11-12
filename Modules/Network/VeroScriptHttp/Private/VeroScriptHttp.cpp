// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttp.h"
#include <algorithm>

namespace VeroScript::Http
{
	static bool Equal(std::string_view A, std::string_view B)
	{
		if (A.size() != B.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < A.size(); ++I)
		{
			auto Lower = [](char C)
			{
				return C >= 'A' && C <= 'Z' ? C - 'A' + 'a' : C;
			};
			if (Lower(A[I]) != Lower(B[I]))
			{
				return false;
			}
		}
		return true;
	}
	bool FHeaders::TryAdd(std::string Name, std::string Value, Network::FError& Error)
	{
		Error = {};
		bool bValid = !Name.empty();
		for (unsigned char C : Name)
		{
			bValid =
				bValid && ((C >= '0' && C <= '9') || (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
							  std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(C)) != std::string_view::npos);
		}
		for (unsigned char C : Value)
		{
			bValid = bValid && ((C >= 0x20 && C != 0x7f) || C == '\t');
		}
		if (!bValid)
		{
			Error = {Network::EError::InvalidArgument, "Invalid HTTP header field", 0};
			return false;
		}
		Values.emplace_back(std::move(Name), std::move(Value));
		return true;
	}
	bool FHeaders::TrySet(std::string Name, std::string Value, Network::FError& Error)
	{
		FHeaders Copy = *this;
		Copy.Erase(Name);
		if (!Copy.TryAdd(std::move(Name), std::move(Value), Error))
		{
			return false;
		}
		*this = std::move(Copy);
		return true;
	}
	void FHeaders::Erase(std::string_view Name)
	{
		Values.erase(std::remove_if(Values.begin(),
						 Values.end(),
						 [&](const auto& Pair)
						 {
							 return Equal(Pair.first, Name);
						 }),
			Values.end());
	}
	std::vector<std::string> FHeaders::GetAll(std::string_view Name) const
	{
		std::vector<std::string> Out;
		for (const auto& Pair : Values)
		{
			if (Equal(Pair.first, Name))
			{
				Out.push_back(Pair.second);
			}
		}
		return Out;
	}
	const std::vector<std::pair<std::string, std::string>>& FHeaders::Fields() const noexcept
	{
		return Values;
	}
} // namespace VeroScript::Http
