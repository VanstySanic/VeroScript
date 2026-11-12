// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptLexer.h"
#include "VeroScriptAnalysisControl.h"

#include <cctype>
#include <cstring>

namespace VeroScript
{
	namespace Detail
	{
		bool IsAsciiAlpha(const char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z');
		}

		bool IsAsciiDigit(const char C)
		{
			return C >= '0' && C <= '9';
		}

		bool IsIdentifierStart(const char C)
		{
			return IsAsciiAlpha(C) || C == '_';
		}

		bool IsIdentifierPart(const char C)
		{
			return IsAsciiAlpha(C) || IsAsciiDigit(C) || C == '_';
		}

		bool IsKeyword(std::string_view Lexeme)
		{
			return IsLanguageKeyword(Lexeme);
		}

		bool IsBooleanLiteral(std::string_view Lexeme)
		{
			return Lexeme == "true" || Lexeme == "false";
		}
	}

	const char* const* GetLanguageKeywords(std::size_t& OutCount)
	{
		static constexpr const char* Keywords[] = {
			"void",
			"bool",
			"char",
			"short",
			"int",
			"long",
			"int8",
			"uint8",
			"int16",
			"uint16",
			"int32",
			"uint32",
			"int64",
			"uint64",
			"float",
			"double",
			"string",
			"wstring",
			"unsigned",
			"auto",
			"channel",
			"array",
			"map",
			"multimap",
			"set",
			"multiset",
			"queue",
			"priority_queue",
			"stack",
			"ordered_map",
			"ordered_multimap",
			"deque",
			"ordered_multiset",
			"fn",
			"function",
			"if",
			"else",
			"while",
			"do",
			"for",
			"switch",
			"case",
			"default",
			"return",
			"break",
			"continue",
			"fallthrough",
			"goto",
			"try",
			"catch",
			"finally",
			"throw",
			"struct",
			"class",
			"interface",
			"enum",
			"import",
			"export",
			"using",
			"typedef",
			"namespace",
			"friend",
			"new",
			"delete",
			"this",
			"const",
			"constexpr",
			"ref",
			"static",
			"explicit",
			"public",
			"protected",
			"private",
			"virtual",
			"override",
			"final",
			"where",
			"operator",
			"template",
			"in",
			"true",
			"false",
			"nullptr",
		};
		OutCount = sizeof(Keywords) / sizeof(Keywords[0]);
		return Keywords;
	}

	bool IsLanguageKeyword(const std::string_view Lexeme)
	{
		std::size_t Count = 0;
		const char* const* Keywords = GetLanguageKeywords(Count);
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			if (Lexeme == Keywords[Index])
			{
				return true;
			}
		}
		return false;
	}

	bool IsTypeKeyword(const std::string_view Lexeme)
	{
		return Lexeme == "void" || Lexeme == "bool" || Lexeme == "char" || Lexeme == "short"
			|| Lexeme == "int" || Lexeme == "long" || Lexeme == "float" || Lexeme == "double"
			|| Lexeme == "int8" || Lexeme == "uint8"
			|| Lexeme == "int16" || Lexeme == "uint16"
			|| Lexeme == "int32" || Lexeme == "uint32"
			|| Lexeme == "int64" || Lexeme == "uint64"
			|| Lexeme == "string" || Lexeme == "wstring" || Lexeme == "unsigned" || Lexeme == "auto";
	}

	std::string_view CanonicalizePrimitiveTypeKeyword(const std::string_view Lexeme)
	{
		if (Lexeme == "int8")
		{
			return "char";
		}
		if (Lexeme == "uint8")
		{
			return "unsigned char";
		}
		if (Lexeme == "int16")
		{
			return "short";
		}
		if (Lexeme == "uint16")
		{
			return "unsigned short";
		}
		if (Lexeme == "int32")
		{
			return "int";
		}
		if (Lexeme == "uint32")
		{
			return "unsigned int";
		}
		return Lexeme;
	}

	bool IsUnsignedBaseTypeKeyword(const std::string_view Lexeme)
	{
		return Lexeme == "char" || Lexeme == "short" || Lexeme == "int" || Lexeme == "long";
	}

	bool IsContainerTypeKeyword(const std::string_view Lexeme)
	{
		return Lexeme == "array" || Lexeme == "map" || Lexeme == "set"
			|| Lexeme == "multiset" || Lexeme == "ordered_multiset"
			|| Lexeme == "queue" || Lexeme == "priority_queue" || Lexeme == "stack"
			|| Lexeme == "multimap" || Lexeme == "ordered_map"
			|| Lexeme == "ordered_multimap" || Lexeme == "deque" || Lexeme == "channel";
	}

	bool IsPrimitiveDeclarableTypeKeyword(const std::string_view Lexeme)
	{
		return Lexeme == "bool" || Lexeme == "char" || Lexeme == "short"
			|| Lexeme == "int" || Lexeme == "long" || Lexeme == "float"
			|| Lexeme == "int8" || Lexeme == "uint8"
			|| Lexeme == "int16" || Lexeme == "uint16"
			|| Lexeme == "int32" || Lexeme == "uint32"
			|| Lexeme == "int64" || Lexeme == "uint64"
			|| Lexeme == "double" || Lexeme == "string" || Lexeme == "wstring"
			|| Lexeme == "auto";
	}

	VeroScriptLexer::VeroScriptLexer(const std::string_view InSource, const std::string_view InFileLabel)
		: Source(InSource)
		, FileLabel(InFileLabel)
	{
	}

	const std::vector<FToken>& VeroScriptLexer::Tokenize()
	{
		if (bTokenized)
		{
			return Tokens;
		}

		// Mark the cache complete only after EOF (including a lexical error).
		// Cancellation can throw midway through scanning; a retry starts afresh.
		Tokens.clear();
		bHasError = false;
		ErrorMessage.clear();
		Current = 0;
		Line = InitialLine;
		Column = InitialColumn;
		// Skip UTF-8 BOM so editors that save with BOM still lex cleanly.
		if (Source.size() >= 3
			&& static_cast<unsigned char>(Source[0]) == 0xEFu
			&& static_cast<unsigned char>(Source[1]) == 0xBBu
			&& static_cast<unsigned char>(Source[2]) == 0xBFu)
		{
			Current = 3;
		}

		while (!IsAtEnd())
		{
			if (!SkipWhitespaceAndComments())
			{
				return Tokens;
			}
			if (IsAtEnd())
			{
				break;
			}

			const std::size_t TokenStart = Current;
			const std::uint32_t TokenLine = Line;
			const std::uint32_t TokenColumn = Column;
			const char C = Peek();

			if (Detail::IsIdentifierStart(C))
			{
				if (C == 'r'
					&& Current + 1 < Source.size()
					&& Source[Current + 1] == '"')
				{
					if (!ScanRawString())
					{
						return Tokens;
					}
					continue;
				}
				if ((C == 'L' || C == 'l')
					&& Current + 1 < Source.size()
					&& Source[Current + 1] == '"')
				{
					if (!ScanWString())
					{
						return Tokens;
					}
					continue;
				}
				if ((C == 'L' || C == 'l')
					&& Current + 1 < Source.size()
					&& Source[Current + 1] == '\'')
				{
					ErrorToken("wide character literals are not supported");
					return Tokens;
				}
				if (!ScanIdentifierOrKeyword())
				{
					return Tokens;
				}
				continue;
			}

			if (std::isdigit(static_cast<unsigned char>(C))
				|| (C == '.' && Current + 1 < Source.size()
					&& std::isdigit(static_cast<unsigned char>(Source[Current + 1]))))
			{
				if (!ScanNumber())
				{
					return Tokens;
				}
				continue;
			}

			if (C == '"')
			{
				if (!ScanString())
				{
					return Tokens;
				}
				continue;
			}

			if (C == '\'')
			{
				if (!ScanCharacter())
				{
					return Tokens;
				}
				continue;
			}

			if (C == '/' && Current + 1 < Source.size() && Source[Current + 1] == '/')
			{
				if (!SkipWhitespaceAndComments())
				{
					return Tokens;
				}
				continue;
			}

			if (C == '/' && Current + 1 < Source.size() && Source[Current + 1] == '*')
			{
				if (!SkipWhitespaceAndComments())
				{
					return Tokens;
				}
				continue;
			}

			const char First = Advance();
			std::string_view Lexeme = Source.substr(TokenStart, Current - TokenStart);

			if (First == '=' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '=' && Match('>'))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '.' && Match('.') && Match('.'))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '+' && Match('+'))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '+' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '-' && Match('-'))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '-' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '-' && Match('>'))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '*' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '/' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '%' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '!' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '<' && Match('<'))
			{
				Match('=');
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '<' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '>' && Match('>'))
			{
				Match('=');
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '>' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '&' && Match('&'))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '&' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '|' && Match('|'))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '|' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == '^' && Match('='))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}
			else if (First == ':' && Match(':'))
			{
				Lexeme = Source.substr(TokenStart, Current - TokenStart);
			}

			FToken Token = MakeToken(ETokenKind::Symbol, Lexeme);
			Token.Location.Line = TokenLine;
			Token.Location.Column = TokenColumn;
			Tokens.push_back(Token);
		}

		Tokens.push_back(MakeToken(ETokenKind::EndOfFile, {}));
		bTokenized = true;
		return Tokens;
	}

	bool VeroScriptLexer::IsAtEnd() const
	{
		return Current >= Source.size();
	}

	char VeroScriptLexer::Peek() const
	{
		return IsAtEnd() ? '\0' : Source[Current];
	}

	char VeroScriptLexer::Advance()
	{
		if ((Current & 1023u) == 0)
		{
			CheckAnalysisCancellation();
		}
		if (IsAtEnd())
		{
			return '\0';
		}

		const char C = Source[Current++];
		if (C == '\n')
		{
			++Line;
			Column = 1;
		}
		else
		{
			++Column;
		}
		return C;
	}

	bool VeroScriptLexer::Match(const char Expected)
	{
		if (IsAtEnd() || Source[Current] != Expected)
		{
			return false;
		}
		Advance();
		return true;
	}

	bool VeroScriptLexer::SkipWhitespaceAndComments()
	{
		while (!IsAtEnd())
		{
			const char C = Peek();
			if (C == ' ' || C == '\t' || C == '\r' || C == '\n')
			{
				Advance();
				continue;
			}

			if (C == '/' && Current + 1 < Source.size() && Source[Current + 1] == '/')
			{
				Advance();
				Advance();
				while (!IsAtEnd() && Peek() != '\n')
				{
					Advance();
				}
				continue;
			}

			if (C == '/' && Current + 1 < Source.size() && Source[Current + 1] == '*')
			{
				Advance();
				Advance();
				bool bClosed = false;
				while (!IsAtEnd())
				{
					if (Peek() == '*' && Current + 1 < Source.size() && Source[Current + 1] == '/')
					{
						Advance();
						Advance();
						bClosed = true;
						break;
					}
					Advance();
				}
				if (!bClosed)
				{
					ErrorToken("unterminated block comment");
					return false;
				}
				continue;
			}

			break;
		}
		return true;
	}

	FToken VeroScriptLexer::MakeToken(const ETokenKind Kind, const std::string_view Lexeme) const
	{
		FToken Token;
		Token.Kind = Kind;
		Token.Lexeme = Lexeme;
		Token.Location.FileLabel = FileLabel;
		Token.Location.Line = Line;
		Token.Location.Column = Column;
		return Token;
	}

	FToken VeroScriptLexer::ErrorToken(const std::string_view Message)
	{
		bHasError = true;
		ErrorMessage.assign(Message.begin(), Message.end());
		Tokens.push_back(MakeToken(ETokenKind::EndOfFile, {}));
		bTokenized = true;
		return Tokens.back();
	}

	bool VeroScriptLexer::ScanIdentifierOrKeyword()
	{
		const std::size_t TokenStart = Current;
		const std::uint32_t TokenLine = Line;
		const std::uint32_t TokenColumn = Column;

		while (!IsAtEnd() && Detail::IsIdentifierPart(Peek()))
		{
			Advance();
		}

		const std::string_view Lexeme = Source.substr(TokenStart, Current - TokenStart);
		ETokenKind Kind = ETokenKind::Identifier;
		if (Lexeme == "nullptr")
		{
			Kind = ETokenKind::NullptrLiteral;
		}
		else if (Detail::IsBooleanLiteral(Lexeme))
		{
			Kind = ETokenKind::BooleanLiteral;
		}
		else if (Detail::IsKeyword(Lexeme))
		{
			Kind = ETokenKind::Keyword;
		}

		FToken Token = MakeToken(Kind, Lexeme);
		Token.Location.Line = TokenLine;
		Token.Location.Column = TokenColumn;
		Tokens.push_back(Token);
		return true;
	}

	bool VeroScriptLexer::ScanNumber()
	{
		const std::size_t TokenStart = Current;
		const std::uint32_t TokenLine = Line;
		const std::uint32_t TokenColumn = Column;
		bool bIsFloat = false;
		int Base = 10;
		const auto IsDigitForBase = [](const char Character, const int DigitBase)
		{
			if (DigitBase == 2)
			{
				return Character == '0' || Character == '1';
			}
			if (DigitBase == 16)
			{
				return std::isxdigit(static_cast<unsigned char>(Character)) != 0;
			}
			return std::isdigit(static_cast<unsigned char>(Character)) != 0;
		};
		const auto ScanDigits = [&](const int DigitBase, const bool bRequireDigit, const char* Error)
		{
			bool bSawDigit = false;
			while (!IsAtEnd())
			{
				if (IsDigitForBase(Peek(), DigitBase))
				{
					bSawDigit = true;
					Advance();
					continue;
				}
				if (Peek() != '_')
				{
					break;
				}
				if (!bSawDigit || Current + 1 >= Source.size()
					|| !IsDigitForBase(Source[Current + 1], DigitBase))
				{
					ErrorToken(Error);
					return false;
				}
				Advance();
			}
			if (bRequireDigit && !bSawDigit)
			{
				ErrorToken(Error);
				return false;
			}
			return true;
		};

		if (Peek() == '0' && Current + 1 < Source.size()
			&& (Source[Current + 1] == 'x' || Source[Current + 1] == 'X'
				|| Source[Current + 1] == 'b' || Source[Current + 1] == 'B'))
		{
			Base = Source[Current + 1] == 'x' || Source[Current + 1] == 'X' ? 16 : 2;
			Advance();
			Advance();
			if (!ScanDigits(
					Base,
					true,
					Base == 16 ? "invalid hexadecimal literal" : "invalid binary literal"))
			{
				return false;
			}
		}
		else if (!ScanDigits(10, false, "invalid numeric separator"))
		{
			return false;
		}

		if (Base == 10 && !IsAtEnd() && Peek() == '.')
		{
			bIsFloat = true;
			Advance();
			if (!ScanDigits(10, false, "invalid numeric separator"))
			{
				return false;
			}
		}

		if (Base == 10 && !IsAtEnd() && (Peek() == 'e' || Peek() == 'E'))
		{
			bIsFloat = true;
			Advance();
			if (!IsAtEnd() && (Peek() == '+' || Peek() == '-'))
			{
				Advance();
			}
			if (!ScanDigits(10, true, "invalid floating-point exponent"))
			{
				return false;
			}
		}

		if (Base == 10 && !IsAtEnd() && (Peek() == 'f' || Peek() == 'F'))
		{
			bIsFloat = true;
			Advance();
		}

		if (!bIsFloat)
		{
			const std::string_view Remaining = Source.substr(Current);
			if (Remaining.substr(0, 3) == "i64" || Remaining.substr(0, 3) == "I64"
				|| Remaining.substr(0, 3) == "u64" || Remaining.substr(0, 3) == "U64")
			{
				Advance();
				Advance();
				Advance();
			}
			else
			{
				const char Suffix = Peek();
				if (Suffix == 'u' || Suffix == 'U')
				{
					Advance();
					if (!IsAtEnd() && (Peek() == 'l' || Peek() == 'L'))
					{
						Advance();
					}
				}
				else if (Suffix == 'l' || Suffix == 'L')
				{
					Advance();
					if (!IsAtEnd() && (Peek() == 'u' || Peek() == 'U'))
					{
						Advance();
					}
				}
			}
		}

		if (!IsAtEnd() && (Detail::IsIdentifierPart(Peek())
			|| std::isdigit(static_cast<unsigned char>(Peek()))))
		{
			ErrorToken(Base == 2 ? "invalid binary literal" : "invalid numeric literal");
			return false;
		}

		const std::string_view Lexeme = Source.substr(TokenStart, Current - TokenStart);
		FToken Token = MakeToken(bIsFloat ? ETokenKind::FloatLiteral : ETokenKind::IntegerLiteral, Lexeme);
		Token.Location.Line = TokenLine;
		Token.Location.Column = TokenColumn;
		Tokens.push_back(Token);
		return true;
	}

	bool VeroScriptLexer::ScanString()
	{
		const std::uint32_t TokenLine = Line;
		const std::uint32_t TokenColumn = Column;
		const std::size_t TokenStart = Current;
		Advance();

		while (!IsAtEnd() && Peek() != '"')
		{
			if (Peek() == '\\')
			{
				Advance();
				if (IsAtEnd())
				{
					ErrorToken("unterminated string escape");
					return false;
				}
				Advance();
				continue;
			}

			if (Peek() == '\n')
			{
				ErrorToken("unterminated string literal");
				return false;
			}

			Advance();
		}

		if (IsAtEnd())
		{
			ErrorToken("unterminated string literal");
			return false;
		}

		Advance();
		const std::string_view Lexeme = Source.substr(TokenStart, Current - TokenStart);
		FToken Token = MakeToken(ETokenKind::StringLiteral, Lexeme);
		Token.Location.Line = TokenLine;
		Token.Location.Column = TokenColumn;
		Tokens.push_back(Token);
		return true;
	}

	bool VeroScriptLexer::ScanRawString()
	{
		const std::uint32_t TokenLine = Line;
		const std::uint32_t TokenColumn = Column;
		const std::size_t TokenStart = Current;
		Advance(); // r
		Advance(); // opening quote

		// Raw strings deliberately have a fixed delimiter: the first following
		// quote closes the literal. Backslashes and newlines are ordinary text.
		while (!IsAtEnd() && Peek() != '"')
		{
			Advance();
		}

		if (IsAtEnd())
		{
			ErrorToken("unterminated raw string literal");
			return false;
		}

		Advance();
		const std::string_view Lexeme = Source.substr(TokenStart, Current - TokenStart);
		FToken Token = MakeToken(ETokenKind::StringLiteral, Lexeme);
		Token.Location.Line = TokenLine;
		Token.Location.Column = TokenColumn;
		Tokens.push_back(Token);
		return true;
	}

	bool VeroScriptLexer::ScanWString()
	{
		const std::uint32_t TokenLine = Line;
		const std::uint32_t TokenColumn = Column;
		const std::size_t TokenStart = Current;
		Advance();
		if (IsAtEnd() || Peek() != '"')
		{
			ErrorToken("expected '\"' after wide string prefix");
			return false;
		}
		Advance();

		while (!IsAtEnd() && Peek() != '"')
		{
			if (Peek() == '\\')
			{
				Advance();
				if (IsAtEnd())
				{
					ErrorToken("unterminated wide string escape");
					return false;
				}
				Advance();
				continue;
			}

			if (Peek() == '\n')
			{
				ErrorToken("unterminated wide string literal");
				return false;
			}

			Advance();
		}

		if (IsAtEnd())
		{
			ErrorToken("unterminated wide string literal");
			return false;
		}

		Advance();
		const std::string_view Lexeme = Source.substr(TokenStart, Current - TokenStart);
		FToken Token = MakeToken(ETokenKind::WStringLiteral, Lexeme);
		Token.Location.Line = TokenLine;
		Token.Location.Column = TokenColumn;
		Tokens.push_back(Token);
		return true;
	}

	bool VeroScriptLexer::ScanCharacter()
	{
		const std::uint32_t TokenLine = Line;
		const std::uint32_t TokenColumn = Column;
		const std::size_t TokenStart = Current;
		Advance();

		while (!IsAtEnd() && Peek() != '\'')
		{
			if (Peek() == '\\')
			{
				Advance();
				if (IsAtEnd() || Peek() == '\n')
				{
					ErrorToken("unterminated character escape");
					return false;
				}
				Advance();
				continue;
			}
			if (Peek() == '\n')
			{
				ErrorToken("unterminated character literal");
				return false;
			}
			Advance();
		}

		if (IsAtEnd())
		{
			ErrorToken("unterminated character literal");
			return false;
		}

		Advance();
		const std::string_view Lexeme = Source.substr(TokenStart, Current - TokenStart);
		FToken Token = MakeToken(ETokenKind::CharacterLiteral, Lexeme);
		Token.Location.Line = TokenLine;
		Token.Location.Column = TokenColumn;
		Tokens.push_back(Token);
		return true;
	}
}
