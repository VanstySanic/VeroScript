// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	inline std::string_view TrimTypeDetail(std::string_view Text)
	{
		while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.front())))
		{
			Text.remove_prefix(1);
		}

		while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.back())))
		{
			Text.remove_suffix(1);
		}

		return Text;
	}

	// Source-facing type details contain function arrows, nested generic arguments
	// and array suffixes. Only separators outside all these constructs are structural.
	struct FTypeDetailNesting
	{
		bool IsTopLevel() const
		{
			return !Angles && !Parentheses && !Brackets && !Braces && !Quote;
		}

		void Consume(std::string_view Text, std::size_t Index)
		{
			const char C = Text[Index];
			if (Quote)
			{
				if (bEscaped)
				{
					bEscaped = false;
				}

				else if (C == '\\')
				{
					bEscaped = true;
				}

				else if (C == Quote)
				{
					Quote = 0;
				}

				return;
			}
			if (C == '"' || C == '\'')
			{
				Quote = C;
			}

			else if (C == '<')
			{
				++Angles;
			}

			else if (C == '>' && Angles && (Index == 0 || Text[Index - 1] != '-'))
			{
				--Angles;
			}

			else if (C == '(')
			{
				++Parentheses;
			}

			else if (C == ')' && Parentheses)
			{
				--Parentheses;
			}

			else if (C == '[')
			{
				++Brackets;
			}

			else if (C == ']' && Brackets)
			{
				--Brackets;
			}

			else if (C == '{')
			{
				++Braces;
			}

			else if (C == '}' && Braces)
			{
				--Braces;
			}
		}

		int Angles = 0, Parentheses = 0, Brackets = 0, Braces = 0;
		char Quote = 0;
		bool bEscaped = false;
	};

	inline std::vector<std::string_view> SplitTypeDetailList(std::string_view Text)
	{
		std::vector<std::string_view> Result;
		FTypeDetailNesting Nesting;
		std::size_t Start = 0;
		for (std::size_t Index = 0; Index <= Text.size(); ++Index)
		{
			if (Index == Text.size() || (Text[Index] == ',' && Nesting.IsTopLevel()))
			{
				const auto Part = TrimTypeDetail(Text.substr(Start, Index - Start));
				if (!Part.empty())
				{
					Result.push_back(Part);
				}

				Start = Index + 1;
			}
			else
			{
				Nesting.Consume(Text, Index);
			}
		}
		return Result;
	}

	inline std::vector<std::string> GenericArgumentsFromDetail(std::string_view Type, bool bStripDefaults = true)
	{
		const auto Open = Type.find('<');
		if (Open == std::string_view::npos) return {};
		FTypeDetailNesting Nesting;
		std::size_t Close = Open;
		for (; Close < Type.size(); ++Close)
		{
			Nesting.Consume(Type, Close);
			if (Nesting.IsTopLevel())
			{
				break;
			}
		}
		if (Close == Type.size()) return {};
		std::vector<std::string> Result;
		for (auto Argument : SplitTypeDetailList(Type.substr(Open + 1, Close - Open - 1)))
		{
			// Declaration-side defaults are not part of the substitution identifier.
			if (const auto Default = Argument.find('='); bStripDefaults && Default != std::string_view::npos)
			{
				Argument = TrimTypeDetail(Argument.substr(0, Default));
			}

			Result.emplace_back(Argument);
		}
		return Result;
	}

	inline std::vector<std::string> BaseTypesFromDetail(std::string_view Text)
	{
		FTypeDetailNesting Nesting;
		std::size_t Start = std::string_view::npos, End = Text.size();
		for (std::size_t Index = 0; Index < Text.size(); ++Index)
		{
			if (Nesting.IsTopLevel())
			{
				if (Start == std::string_view::npos && Text[Index] == ':' &&
					(Index == 0 || Text[Index - 1] != ':') &&
					(Index + 1 == Text.size() || Text[Index + 1] != ':'))
				{
					Start = Index + 1;
				}

				else if (Start != std::string_view::npos &&
					(Text[Index] == '[' ||
						(Text.substr(Index, 6) == "where " &&
							(Index == 0 || std::isspace(static_cast<unsigned char>(Text[Index - 1]))))))
				{
					End = Index;
					break;
				}
			}
			Nesting.Consume(Text, Index);
		}
		if (Start == std::string_view::npos) return {};
		std::vector<std::string> Result;
		for (auto Base : SplitTypeDetailList(Text.substr(Start, End - Start)))
		{
			for (const std::string_view Access : {"public ", "private ", "protected "})
			{
				if (Base.substr(0, Access.size()) == Access)
				{
					Base = TrimTypeDetail(Base.substr(Access.size()));
					break;
				}
			}
			if (!Base.empty())
			{
				Result.emplace_back(Base);
			}
		}
		return Result;
	}
}
