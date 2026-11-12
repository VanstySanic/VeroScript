// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	// `.` and `::` are equivalent member / type / namespace separators in expressions.
	inline bool IsMemberAccessSymbol(const std::string_view Lexeme)
	{
		return Lexeme == "." || Lexeme == "::";
	}

	// Parsed operator names have punctuation or a conversion separator after
	// the keyword. Identifiers such as operatorCount, operator_count and operator2
	// must keep ordinary symbol resolution and rename behavior.
	inline bool IsOperatorMethodName(const std::string_view Name)
	{
		constexpr std::string_view Prefix = "operator";
		if (Name.size() <= Prefix.size() || Name.substr(0, Prefix.size()) != Prefix)
		{
			return false;
		}
		const char Next = Name[Prefix.size()];
		return !((Next >= 'a' && Next <= 'z') || (Next >= 'A' && Next <= 'Z')
			|| (Next >= '0' && Next <= '9') || Next == '_');
	}

	/**
	 * Build outline/AST operator method name from tokens starting at Keyword "operator".
	 * Mirrors VeroScriptParser::MatchOperatorMethodName (read-only; no error reporting).
	 * Returns false when the span is incomplete or unsupported.
	 * OutEndExclusive is the first token index after the operator name.
	 */
	inline bool TryParseOperatorName(
		const std::vector<FToken>& Tokens,
		const std::size_t OperatorIndex,
		std::string& OutName,
		std::size_t& OutEndExclusive)
	{
		OutName.clear();
		OutEndExclusive = OperatorIndex;
		if (OperatorIndex >= Tokens.size()
			|| Tokens[OperatorIndex].Kind != ETokenKind::Keyword
			|| Tokens[OperatorIndex].Lexeme != "operator")
		{
			return false;
		}

		std::size_t Index = OperatorIndex + 1;
		if (Index >= Tokens.size())
		{
			return false;
		}

		const FToken& Next = Tokens[Index];
		if (Next.Kind == ETokenKind::Symbol)
		{
			const std::string_view Symbol = Next.Lexeme;
			if (Symbol == "["
				&& Index + 1 < Tokens.size()
				&& Tokens[Index + 1].Kind == ETokenKind::Symbol
				&& Tokens[Index + 1].Lexeme == "]")
			{
				OutName = "operator[]";
				OutEndExclusive = Index + 2;
				return true;
			}
			if (Symbol == "("
				&& Index + 1 < Tokens.size()
				&& Tokens[Index + 1].Kind == ETokenKind::Symbol
				&& Tokens[Index + 1].Lexeme == ")")
			{
				OutName = "operator()";
				OutEndExclusive = Index + 2;
				return true;
			}

			const bool bAllowed = Symbol == "=" || Symbol == "+"
				|| Symbol == "-"
				|| Symbol == "*"
				|| Symbol == "/"
				|| Symbol == "%"
				|| Symbol == "+="
				|| Symbol == "-="
				|| Symbol == "*="
				|| Symbol == "/="
				|| Symbol == "%="
				|| Symbol == "=="
				|| Symbol == "!="
				|| Symbol == "<"
				|| Symbol == "<="
				|| Symbol == ">"
				|| Symbol == ">="
				|| Symbol == "!"
				|| Symbol == "++"
				|| Symbol == "--"
				|| Symbol == "&"
				|| Symbol == "|"
				|| Symbol == "^"
				|| Symbol == "<<"
				|| Symbol == ">>"
				|| Symbol == "~"
				|| Symbol == "&&"
				|| Symbol == "||";
			if (bAllowed)
			{
				OutName = "operator";
				OutName += Symbol;
				OutEndExclusive = Index + 1;
				return true;
			}
		}

		// Conversion: operator Type — Identifier / keywords (bool, int, …), dotted / generic names.
		if (Next.Kind == ETokenKind::Identifier
			|| (Next.Kind == ETokenKind::Keyword
				&& Next.Lexeme != "operator"))
		{
			OutName = "operator conv ";
			OutName.append(Next.Lexeme.data(), Next.Lexeme.size());
			++Index;
			while (Index + 1 < Tokens.size()
				&& Tokens[Index].Kind == ETokenKind::Symbol
				&& IsMemberAccessSymbol(Tokens[Index].Lexeme)
				&& (Tokens[Index + 1].Kind == ETokenKind::Identifier
					|| Tokens[Index + 1].Kind == ETokenKind::Keyword))
			{
				OutName += '.';
				OutName.append(
					Tokens[Index + 1].Lexeme.data(),
					Tokens[Index + 1].Lexeme.size());
				Index += 2;
			}
			// Optional type arguments: OperatorName leaf matches parser `ParseTypeName` generics.
			if (Index < Tokens.size()
				&& Tokens[Index].Kind == ETokenKind::Symbol
				&& Tokens[Index].Lexeme == "<")
			{
				int AngleDepth = 0;
				const std::size_t Start = Index;
				while (Index < Tokens.size())
				{
					if (Tokens[Index].Kind == ETokenKind::Symbol)
					{
						if (Tokens[Index].Lexeme == "<")
						{
							++AngleDepth;
						}
						else if (Tokens[Index].Lexeme == ">")
						{
							--AngleDepth;
							if (AngleDepth == 0)
							{
								++Index;
								break;
							}
						}
					}
					++Index;
				}
				if (AngleDepth != 0)
				{
					return false;
				}
				for (std::size_t Piece = Start; Piece < Index; ++Piece)
				{
					OutName.append(Tokens[Piece].Lexeme.data(), Tokens[Piece].Lexeme.size());
				}
			}
			OutEndExclusive = Index;
			return !OutName.empty();
		}
		return false;
	}

	inline bool TokenCoversColumn(
		const FToken& Token,
		const std::uint32_t Line,
		const std::uint32_t Column)
	{
		if (Token.Location.Line != Line)
		{
			return false;
		}
		const std::uint32_t Start = Token.Location.Column;
		const std::uint32_t EndExclusive =
			Start + static_cast<std::uint32_t>(Token.Lexeme.size());
		return Column >= Start && Column < EndExclusive;
	}

	/**
	 * Destructor declaration span `~Type` / `virtual ~Type` under the caret.
	 * Requires a following `(` so bitwise-not `~x` is not misclassified.
	 */
	inline bool FindDestructorNameAt(
		const std::vector<FToken>& Tokens,
		const std::uint32_t Line,
		const std::uint32_t Column,
		std::string& OutName,
		std::uint32_t& OutStartColumn,
		std::uint32_t& OutEndColumn)
	{
		for (std::size_t Index = 0; Index + 2 < Tokens.size(); ++Index)
		{
			const FToken& Tilde = Tokens[Index];
			if (Tilde.Kind != ETokenKind::Symbol || Tilde.Lexeme != "~")
			{
				continue;
			}
			const FToken& TypeToken = Tokens[Index + 1];
			if (TypeToken.Kind != ETokenKind::Identifier)
			{
				continue;
			}
			const FToken& Open = Tokens[Index + 2];
			if (Open.Kind != ETokenKind::Symbol || Open.Lexeme != "(")
			{
				continue;
			}
			const bool bOnTilde = TokenCoversColumn(Tilde, Line, Column);
			const bool bOnType = TokenCoversColumn(TypeToken, Line, Column);
			if (!bOnTilde && !bOnType)
			{
				continue;
			}
			OutName = "~";
			OutName.append(TypeToken.Lexeme.data(), TypeToken.Lexeme.size());
			OutStartColumn = Tilde.Location.Column;
			OutEndColumn =
				TypeToken.Location.Column
					+ static_cast<std::uint32_t>(TypeToken.Lexeme.size());
			return true;
		}
		return false;
	}

	// True when TokenText is the destructor spelling of TypeName (`~Foo` for `Foo`).
	inline bool IsDestructorTokenForTypeName(
		const std::string_view TokenText,
		const std::string_view TypeName)
	{
		return !TypeName.empty()
			&& TokenText.size() == TypeName.size() + 1
			&& TokenText.front() == '~'
			&& TokenText.compare(1, TypeName.size(), TypeName) == 0;
	}

	/**
	 * If caret sits on an `operator…` declaration span, OutName is the AST method name
	 * (e.g. operator+, operator[], operator conv bool) and OutStart/OutEnd cover the span.
	 */
	inline bool FindOperatorNameAt(
		const std::vector<FToken>& Tokens,
		const std::uint32_t Line,
		const std::uint32_t Column,
		std::string& OutName,
		std::uint32_t& OutStartColumn,
		std::uint32_t& OutEndColumn)
	{
		for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
		{
			std::string Name;
			std::size_t EndExclusive = Index;
			if (!TryParseOperatorName(Tokens, Index, Name, EndExclusive)
				|| EndExclusive <= Index)
			{
				continue;
			}
			bool bCovers = false;
			std::uint32_t StartColumn = Tokens[Index].Location.Column;
			std::uint32_t EndColumn = StartColumn;
			for (std::size_t Part = Index; Part < EndExclusive; ++Part)
			{
				const FToken& Tok = Tokens[Part];
				if (Tok.Location.Line != Line)
				{
					continue;
				}
				const std::uint32_t PartStart = Tok.Location.Column;
				const std::uint32_t PartEnd =
					PartStart + static_cast<std::uint32_t>(Tok.Lexeme.size());
				if (PartStart < StartColumn)
				{
					StartColumn = PartStart;
				}
				if (PartEnd > EndColumn)
				{
					EndColumn = PartEnd;
				}
				if (TokenCoversColumn(Tok, Line, Column))
				{
					bCovers = true;
				}
			}
			if (bCovers)
			{
				OutName = std::move(Name);
				OutStartColumn = StartColumn;
				OutEndColumn = EndColumn;
				return true;
			}
		}
		return false;
	}

	// True when tokens between OpenExclusive..CloseExclusive look like type arguments.
	inline bool IsTypeArgumentSpan(
		const std::vector<FToken>& Tokens,
		const std::size_t OpenExclusive,
		const std::size_t CloseExclusive)
	{
		for (std::size_t Index = OpenExclusive; Index < CloseExclusive; ++Index)
		{
			const FToken& Token = Tokens[Index];
			if (Token.Kind == ETokenKind::Identifier || Token.Kind == ETokenKind::Keyword)
			{
				continue;
			}
			if (Token.Kind != ETokenKind::Symbol)
			{
				return false;
			}
			const std::string_view Symbol = Token.Lexeme;
			if (Symbol == "," || Symbol == "." || Symbol == "<" || Symbol == ">"
				|| Symbol == "&" || Symbol == "::" || Symbol == "[" || Symbol == "]")
			{
				continue;
			}
			return false;
		}
		return OpenExclusive < CloseExclusive;
	}

	inline bool FindMatchingGenericAngleClose(
		const std::vector<FToken>& Tokens,
		const std::size_t OpenIndex,
		std::size_t& OutCloseIndex)
	{
		if (OpenIndex >= Tokens.size()
			|| Tokens[OpenIndex].Kind != ETokenKind::Symbol
			|| Tokens[OpenIndex].Lexeme != "<")
		{
			return false;
		}
		int Depth = 1;
		for (std::size_t Index = OpenIndex + 1; Index < Tokens.size(); ++Index)
		{
			const FToken& Token = Tokens[Index];
			if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == ";")
			{
				return false;
			}
			if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == "{")
			{
				return false;
			}
			if (Token.Kind != ETokenKind::Symbol)
			{
				continue;
			}
			if (Token.Lexeme == "<")
			{
				++Depth;
			}
			else if (Token.Lexeme == ">")
			{
				--Depth;
				if (Depth == 0)
				{
					if (!IsTypeArgumentSpan(Tokens, OpenIndex + 1, Index))
					{
						return false;
					}
					OutCloseIndex = Index;
					return true;
				}
			}
		}
		return false;
	}

	inline bool FindMatchingGenericAngleOpen(
		const std::vector<FToken>& Tokens,
		const std::size_t CloseIndex,
		std::size_t& OutOpenIndex)
	{
		if (CloseIndex >= Tokens.size()
			|| Tokens[CloseIndex].Kind != ETokenKind::Symbol
			|| Tokens[CloseIndex].Lexeme != ">")
		{
			return false;
		}
		int Depth = 1;
		for (std::size_t Index = CloseIndex; Index > 0;)
		{
			--Index;
			const FToken& Token = Tokens[Index];
			if (Token.Kind == ETokenKind::Symbol
				&& (Token.Lexeme == ";" || Token.Lexeme == "{"))
			{
				return false;
			}
			if (Token.Kind != ETokenKind::Symbol)
			{
				continue;
			}
			if (Token.Lexeme == ">")
			{
				++Depth;
			}
			else if (Token.Lexeme == "<")
			{
				--Depth;
				if (Depth == 0)
				{
					if (!IsTypeArgumentSpan(Tokens, Index + 1, CloseIndex))
					{
						return false;
					}
					OutOpenIndex = Index;
					return true;
				}
			}
		}
		return false;
	}

	// Map a bare use-site symbol (e.g. `+`, `[`) to AST operator name (`operator+`).
	inline bool TryMapUseSymbolToOperatorName(
		const std::string_view Symbol,
		std::string& OutName)
	{
		if (Symbol == "[" || Symbol == "]")
		{
			OutName = "operator[]";
			return true;
		}
		if (Symbol == "(" || Symbol == ")")
		{
			OutName = "operator()";
			return true;
		}
		const bool bAllowed = Symbol == "=" || Symbol == "+"
			|| Symbol == "-"
			|| Symbol == "*"
			|| Symbol == "/"
			|| Symbol == "%"
			|| Symbol == "+="
			|| Symbol == "-="
			|| Symbol == "*="
			|| Symbol == "/="
			|| Symbol == "%="
			|| Symbol == "=="
			|| Symbol == "!="
			|| Symbol == "<"
			|| Symbol == "<="
			|| Symbol == ">"
			|| Symbol == ">="
			|| Symbol == "!"
			|| Symbol == "++"
			|| Symbol == "--"
			|| Symbol == "&"
			|| Symbol == "|"
			|| Symbol == "^"
			|| Symbol == "<<"
			|| Symbol == ">>"
			|| Symbol == "~"
			|| Symbol == "&&"
			|| Symbol == "||";
		if (!bAllowed)
		{
			return false;
		}
		OutName = "operator";
		OutName += Symbol;
		return true;
	}

	/**
	 * Compiler mirrors: != ← ==, > ↔ <, >= ↔ <= (see MakeMirrorBinaryOp / != synthesis).
	 */
	inline bool TryOperatorMirrorDefinitionName(
		const std::string_view OperatorName,
		std::string& OutMirrorName)
	{
		if (OperatorName == "operator!=")
		{
			OutMirrorName = "operator==";
			return true;
		}
		if (OperatorName == "operator>")
		{
			OutMirrorName = "operator<";
			return true;
		}
		if (OperatorName == "operator<")
		{
			OutMirrorName = "operator>";
			return true;
		}
		if (OperatorName == "operator>=")
		{
			OutMirrorName = "operator<=";
			return true;
		}
		if (OperatorName == "operator<=")
		{
			OutMirrorName = "operator>=";
			return true;
		}
		return false;
	}

	// Receiver before `(` / `[` for call-operator and index use-site detection.
	inline bool IsOperatorCallReceiverToken(const FToken& Token)
	{
		return Token.Kind == ETokenKind::Identifier
			|| (Token.Kind == ETokenKind::Keyword && Token.Lexeme == "this");
	}

	inline bool MatchOpeningBracketToken(
		const std::vector<FToken>& Tokens,
		const std::size_t CloseIndex,
		std::size_t& OutOpenIndex)
	{
		int Depth = 0;
		for (std::size_t Index = CloseIndex + 1; Index-- > 0;)
		{
			const FToken& Token = Tokens[Index];
			if (Token.Kind != ETokenKind::Symbol)
			{
				continue;
			}
			if (Token.Lexeme == "]")
			{
				++Depth;
			}
			else if (Token.Lexeme == "[")
			{
				if (Depth == 0)
				{
					OutOpenIndex = Index;
					return true;
				}
				--Depth;
				if (Depth == 0)
				{
					OutOpenIndex = Index;
					return true;
				}
			}
		}
		return false;
	}

	inline bool HasCallOperatorReceiver(
		const std::vector<FToken>& Tokens,
		const std::size_t OpenParenIndex)
	{
		if (OpenParenIndex == 0 || OpenParenIndex >= Tokens.size())
		{
			return false;
		}
		const FToken& Previous = Tokens[OpenParenIndex - 1];
		if (IsOperatorCallReceiverToken(Previous))
		{
			return true;
		}
		if (Previous.Kind == ETokenKind::Symbol && Previous.Lexeme == "]")
		{
			std::size_t OpenIndex = 0;
			if (MatchOpeningBracketToken(Tokens, OpenParenIndex - 1, OpenIndex)
				&& OpenIndex > 0
				&& IsOperatorCallReceiverToken(Tokens[OpenIndex - 1]))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Operator use-site under the caret (`a + b`, `a[i]`), not `operator…` declarations.
	 */
	inline bool FindOperatorUseAt(
		const std::vector<FToken>& Tokens,
		const std::uint32_t Line,
		const std::uint32_t Column,
		std::string& OutName,
		std::uint32_t& OutStartColumn,
		std::uint32_t& OutEndColumn)
	{
		for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
		{
			const FToken& Token = Tokens[Index];
			if (Token.Kind != ETokenKind::Symbol
				|| !TokenCoversColumn(Token, Line, Column))
			{
				continue;
			}
			if (Index > 0
				&& Tokens[Index - 1].Kind == ETokenKind::Keyword
				&& Tokens[Index - 1].Lexeme == "operator")
			{
				continue;
			}
			// Skip generic `Foo<T>` angles (comparison `<`/`>` still resolve).
			if (Token.Lexeme == "<")
			{
				std::size_t CloseIndex = 0;
				if (FindMatchingGenericAngleClose(Tokens, Index, CloseIndex))
				{
					continue;
				}
			}
			else if (Token.Lexeme == ">")
			{
				std::size_t OpenIndex = 0;
				if (FindMatchingGenericAngleOpen(Tokens, Index, OpenIndex))
				{
					continue;
				}
			}
			std::string Name;
			if (!TryMapUseSymbolToOperatorName(Token.Lexeme, Name))
			{
				continue;
			}
			// `operator()` at call sites: empty `()` or `recv(` with arguments.
			// Anchor on '(' — a lone ')' (grouping / ordinary call close) is not a use-site.
			if (Name == "operator()")
			{
				if (Token.Lexeme != "(")
				{
					continue;
				}
				const bool bEmpty = Index + 1 < Tokens.size()
					&& Tokens[Index + 1].Kind == ETokenKind::Symbol
					&& Tokens[Index + 1].Lexeme == ")";
				if (!bEmpty && !HasCallOperatorReceiver(Tokens, Index))
				{
					continue;
				}
			}
			if (Name == "operator[]" && Token.Lexeme == "]")
			{
				// Anchor on '[' when possible.
				continue;
			}
			OutName = std::move(Name);
			OutStartColumn = Token.Location.Column;
			OutEndColumn =
				Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
			if (OutName == "operator[]"
				&& Index + 1 < Tokens.size()
				&& Tokens[Index + 1].Kind == ETokenKind::Symbol
				&& Tokens[Index + 1].Lexeme == "]")
			{
				OutEndColumn =
					Tokens[Index + 1].Location.Column
						+ static_cast<std::uint32_t>(Tokens[Index + 1].Lexeme.size());
			}
			if (OutName == "operator()"
				&& Index + 1 < Tokens.size()
				&& Tokens[Index + 1].Kind == ETokenKind::Symbol
				&& Tokens[Index + 1].Lexeme == ")")
			{
				OutEndColumn =
					Tokens[Index + 1].Location.Column
						+ static_cast<std::uint32_t>(Tokens[Index + 1].Lexeme.size());
			}
			return true;
		}
		return false;
	}

	inline bool OperatorNameLeafMatches(
		const std::string_view DeclName,
		const std::string_view QueryName)
	{
		if (DeclName == QueryName)
		{
			return true;
		}
		const std::size_t Dot = DeclName.rfind(".operator");
		if (Dot != std::string_view::npos
			&& DeclName.substr(Dot + 1) == QueryName)
		{
			return true;
		}
		return false;
	}

	// Querying `operator==` should also find `!=` use-sites (and `<` ↔ `>`).
	inline bool OperatorUseMatchesQuery(
		const std::string_view UseName,
		const std::string_view QueryName)
	{
		if (OperatorNameLeafMatches(UseName, QueryName))
		{
			return true;
		}
		if (QueryName == "operator==" && UseName == "operator!=")
		{
			return true;
		}
		if (QueryName == "operator<" && UseName == "operator>")
		{
			return true;
		}
		if (QueryName == "operator<=" && UseName == "operator>=")
		{
			return true;
		}
		if (QueryName == "operator!=" && UseName == "operator==")
		{
			return true;
		}
		if (QueryName == "operator>" && UseName == "operator<")
		{
			return true;
		}
		if (QueryName == "operator>=" && UseName == "operator<=")
		{
			return true;
		}
		return false;
	}

	// Presentation-only: `operator conv T` → `operator T`. Matching still uses AST names.
	inline std::string FormatOperatorDisplayName(const std::string_view Name)
	{
		constexpr std::string_view Prefix = "operator conv ";
		if (Name.size() > Prefix.size() && Name.compare(0, Prefix.size(), Prefix) == 0)
		{
			std::string Out = "operator ";
			Out.append(Name.substr(Prefix.size()));
			return Out;
		}
		const std::size_t Dot = Name.rfind(".operator conv ");
		if (Dot != std::string_view::npos)
		{
			std::string Out(Name.substr(0, Dot + 1));
			Out += "operator ";
			Out.append(Name.substr(Dot + std::string_view(".operator conv ").size()));
			return Out;
		}
		return std::string(Name);
	}
}
