// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "TypeSupport/VeroScriptTypeMemberBinding.h"

namespace VeroScript::CompilerDetail
{
	// Compare conversions for each actual argument, including packed rest arguments.
	template <typename TScore, typename TArray>
	std::vector<std::string> CallParameterTypes(const std::vector<std::string> &Arguments,
												std::vector<std::string> Parameters, const TScore &Score,
												const TArray &Array)
	{
		if (!Parameters.empty() && Parameters.back().size() >= 3 &&
			Parameters.back().compare(Parameters.back().size() - 3, 3, "...") == 0)
		{
			const auto Fixed = Parameters.size() - 1;
			const auto Element = Parameters.back().substr(0, Parameters.back().size() - 3);
			Parameters.resize(Fixed);
			if (Arguments.size() == Fixed + 1)
			{
				const auto Packed = Array(Element);
				int PackedScore = 0, ElementScore = 0;
				const bool PackedMatch = Score(Arguments.back(), Packed, PackedScore);
				const bool ElementMatch = Score(Arguments.back(), Element, ElementScore);
				Parameters.push_back(PackedMatch && (!ElementMatch || PackedScore >= ElementScore) ? Packed : Element);
			}
			else if (Arguments.size() > Fixed)
			{
				Parameters.resize(Arguments.size(), Element);
			}
		}
		Parameters.resize(Arguments.size());
		return Parameters;
	}

	inline std::vector<std::string> TemplateFunctionPatterns(const FFunctionDecl &Declaration)
	{
		std::vector<std::string> Result;
		for (const auto &Parameter : Declaration.Parameters)
		{
			Result.push_back((Parameter.bIsRef ? (Parameter.bIsConst ? "const ref " : "ref ") : "") +
							 Parameter.TypeName + (Parameter.bIsVariadic ? "..." : ""));
		}
		return Result;
	}

	// Ordering compares declaration patterns; substituted runtime signatures alone
	// cannot distinguish f(T) from f(array<T>) after both close to the same type.
	template <typename TMatch>
	bool TemplatePatternAccepts(const std::vector<std::string> &Parameters, const std::vector<std::string> &Pattern,
								const std::vector<std::string> &Argument, const TMatch &Match)
	{
		if (Pattern.size() != Argument.size())
		{
			return false;
		}
		std::vector<std::string> Bindings(Parameters.size());
		for (std::size_t Index = 0; Index < Pattern.size(); ++Index)
		{
			for (std::size_t Parameter = 0; Parameter < Parameters.size(); ++Parameter)
			{
				std::string Value;
				if (Match(Pattern[Index], Argument[Index], Parameters[Parameter], Value))
				{
					if (!Bindings[Parameter].empty() && Bindings[Parameter] != Value)
					{
						return false;
					}
					Bindings[Parameter] = std::move(Value);
				}
			}
		}
		for (std::size_t Index = 0; Index < Bindings.size(); ++Index)
		{
			if (Bindings[Index].empty())
			{
				Bindings[Index] = Parameters[Index];
			}
		}
		for (std::size_t Index = 0; Index < Pattern.size(); ++Index)
		{
			if (SubstituteScopedTypeParameters(Pattern[Index], Parameters, Bindings) != Argument[Index])
			{
				return false;
			}
		}
		return true;
	}

	template <typename TMatch>
	bool MoreSpecializedTemplatePattern(const std::vector<std::string> &LeftParameters,
										std::vector<std::string> LeftPattern,
										const std::vector<std::string> &RightParameters,
										std::vector<std::string> RightPattern, const TMatch &Match)
	{
		std::vector<std::string> LeftSymbols, RightSymbols;
		for (std::size_t Index = 0; Index < LeftParameters.size(); ++Index)
		{
			LeftSymbols.push_back("__template_left_" + std::to_string(Index));
		}
		for (std::size_t Index = 0; Index < RightParameters.size(); ++Index)
		{
			RightSymbols.push_back("__template_right_" + std::to_string(Index));
		}
		for (auto &Pattern : LeftPattern)
		{
			Pattern = SubstituteScopedTypeParameters(Pattern, LeftParameters, LeftSymbols);
		}
		for (auto &Pattern : RightPattern)
		{
			Pattern = SubstituteScopedTypeParameters(Pattern, RightParameters, RightSymbols);
		}
		return TemplatePatternAccepts(RightSymbols, RightPattern, LeftPattern, Match) &&
			   !TemplatePatternAccepts(LeftSymbols, LeftPattern, RightPattern, Match);
	}
} // namespace VeroScript::CompilerDetail
