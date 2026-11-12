// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptLexer.h"
#include "VeroScriptAnalysisControl.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptLexerTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool ExpectTokenKind(const VeroScript::FToken& Token, const VeroScript::ETokenKind Kind, const char* Message)
	{
		return Expect(Token.Kind == Kind, Message);
	}

	bool ExpectLexeme(const VeroScript::FToken& Token, const std::string_view Lexeme, const char* Message)
	{
		return Expect(Token.Lexeme == Lexeme, Message);
	}
}

int main()
{
	using namespace VeroScript;

	{
		// A cancelled scan must be restartable on the same lexer, including BOM
		// handling and source coordinates. Never cache a partial token stream.
		const std::string Source = std::string("\xEF\xBB\xBFint first;\n")
			+ std::string(4096, ' ') + "int last;\n";
		VeroScriptLexer Lexer(Source);
		int Checks = 0;
		bool bCancelled = false;
		try
		{
			FAnalysisControlScope Control([&Checks]()
			{
				return ++Checks == 2;
			});
			(void)Lexer.Tokenize();
		}
		catch (const FAnalysisCancelled&)
		{
			bCancelled = true;
		}
		if (!Expect(bCancelled, "scan should cancel after producing some tokens"))
		{
			return EXIT_FAILURE;
		}
		VeroScriptLexer Fresh(Source);
		const auto& Expected = Fresh.Tokenize();
		const auto& Retried = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError() && Retried.size() == Expected.size(),
			"cancelled lexer should restart with the complete token stream"))
		{
			return EXIT_FAILURE;
		}
		for (std::size_t Index = 0; Index < Expected.size(); ++Index)
		{
			if (!Expect(Retried[Index].Kind == Expected[Index].Kind
				&& Retried[Index].Lexeme == Expected[Index].Lexeme
				&& Retried[Index].Location.Line == Expected[Index].Location.Line
				&& Retried[Index].Location.Column == Expected[Index].Location.Column,
				"retried tokens should retain their original spelling and coordinates"))
			{
				return EXIT_FAILURE;
			}
		}
		if (!Expect(Lexer.Tokenize().size() == Expected.size(), "completed scan should remain cached"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		if (!Expect(IsLanguageKeyword("template"), "template should be a language keyword")
			|| !Expect(IsLanguageKeyword("explicit"), "explicit should be a language keyword")
			|| !Expect(!IsLanguageKeyword("mutable"), "mutable should remain a contextual keyword")
			|| !Expect(IsTypeKeyword("int64"), "int64 should be a type keyword")
			|| !Expect(IsTypeKeyword("uint64"), "uint64 should be a type keyword")
			|| !Expect(IsTypeKeyword("int8"), "int8 should be a type keyword")
			|| !Expect(IsTypeKeyword("uint8"), "uint8 should be a type keyword")
			|| !Expect(IsTypeKeyword("int16"), "int16 should be a type keyword")
			|| !Expect(IsTypeKeyword("uint16"), "uint16 should be a type keyword")
			|| !Expect(IsTypeKeyword("int32"), "int32 should be a type keyword")
			|| !Expect(IsTypeKeyword("uint32"), "uint32 should be a type keyword")
			|| !Expect(
				CanonicalizePrimitiveTypeKeyword("int8") == "char",
				"int8 should canonicalize to char")
			|| !Expect(
				CanonicalizePrimitiveTypeKeyword("uint8") == "unsigned char",
				"uint8 should canonicalize to unsigned char")
			|| !Expect(
				CanonicalizePrimitiveTypeKeyword("int16") == "short",
				"int16 should canonicalize to short")
			|| !Expect(
				CanonicalizePrimitiveTypeKeyword("uint16") == "unsigned short",
				"uint16 should canonicalize to unsigned short")
			|| !Expect(
				CanonicalizePrimitiveTypeKeyword("int32") == "int",
				"int32 should canonicalize to int")
			|| !Expect(
				CanonicalizePrimitiveTypeKeyword("uint32") == "unsigned int",
				"uint32 should canonicalize to unsigned int")
			|| !Expect(
				!IsTypeKeyword("float32") && !IsTypeKeyword("float64"),
				"float32/float64 should not be language aliases"))
		{
			return EXIT_FAILURE;
		}
		VeroScriptLexer Lexer(
			"int8 a = 1; uint8 b = 2u; int16 c = 3; uint16 d = 4u; "
			"int64 value = 1i64; uint64 bits = 1u64;");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "64-bit integer declarations should lex")
			|| !ExpectTokenKind(Tokens[0], ETokenKind::Keyword, "int8 keyword")
			|| !ExpectTokenKind(Tokens[5], ETokenKind::Keyword, "uint8 keyword")
			|| !ExpectTokenKind(Tokens[10], ETokenKind::Keyword, "int16 keyword")
			|| !ExpectTokenKind(Tokens[15], ETokenKind::Keyword, "uint16 keyword")
			|| !ExpectTokenKind(Tokens[20], ETokenKind::Keyword, "int64 keyword")
			|| !ExpectTokenKind(Tokens[23], ETokenKind::IntegerLiteral, "i64 literal")
			|| !ExpectLexeme(Tokens[23], "1i64", "i64 literal spelling")
			|| !ExpectTokenKind(Tokens[25], ETokenKind::Keyword, "uint64 keyword")
			|| !ExpectTokenKind(Tokens[28], ETokenKind::IntegerLiteral, "u64 literal")
			|| !ExpectLexeme(Tokens[28], "1u64", "u64 literal spelling"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("struct Token { explicit Token(int value); };");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "explicit constructor should lex")
			|| !ExpectTokenKind(Tokens[3], ETokenKind::Keyword, "explicit keyword")
			|| !ExpectLexeme(Tokens[3], "explicit", "explicit keyword spelling"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("int mutable = 1;");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "contextual mutable identifier should lex")
			|| !ExpectTokenKind(Tokens[1], ETokenKind::Identifier, "mutable identifier"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("template<T> class Box { T value; }");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "template declaration should lex")
			|| !ExpectTokenKind(Tokens[0], ETokenKind::Keyword, "template keyword")
			|| !ExpectLexeme(Tokens[0], "template", "template keyword spelling"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("1 + 2");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "simple expression should lex without error"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Tokens.size() == 4, "expected three tokens plus EOF"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[0], ETokenKind::IntegerLiteral, "first token should be integer"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[0], "1", "first token lexeme"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[1], ETokenKind::Symbol, "second token should be +"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[1], "+", "plus lexeme"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[2], ETokenKind::IntegerLiteral, "third token should be integer"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[2], "2", "second integer lexeme"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[3], ETokenKind::EndOfFile, "final token should be EOF"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer(".5f .25 .5e2 .5E-1 object.member ...");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "leading-dot floating literals should lex")
			|| !Expect(Tokens.size() == 9, "leading-dot literals should preserve following symbols")
			|| !ExpectTokenKind(Tokens[0], ETokenKind::FloatLiteral, ".5f token kind")
			|| !ExpectLexeme(Tokens[0], ".5f", ".5f spelling")
			|| !ExpectTokenKind(Tokens[1], ETokenKind::FloatLiteral, ".25 token kind")
			|| !ExpectLexeme(Tokens[1], ".25", ".25 spelling")
			|| !ExpectTokenKind(Tokens[2], ETokenKind::FloatLiteral, ".5e2 token kind")
			|| !ExpectLexeme(Tokens[2], ".5e2", ".5e2 spelling")
			|| !ExpectTokenKind(Tokens[3], ETokenKind::FloatLiteral, ".5E-1 token kind")
			|| !ExpectLexeme(Tokens[3], ".5E-1", ".5E-1 spelling")
			|| !ExpectTokenKind(Tokens[4], ETokenKind::Identifier, "member receiver token kind")
			|| !ExpectLexeme(Tokens[5], ".", "member access dot remains a symbol")
			|| !ExpectTokenKind(Tokens[6], ETokenKind::Identifier, "member name token kind")
			|| !ExpectLexeme(Tokens[7], "...", "ellipsis remains a symbol")
			|| !ExpectTokenKind(Tokens[8], ETokenKind::EndOfFile, "leading-dot final EOF"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("int main = 0xFF; // comment\n\"hi\"");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "statement should lex without error"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[0], ETokenKind::Keyword, "int keyword"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[1], ETokenKind::Identifier, "identifier"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[1], "main", "identifier lexeme"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[3], ETokenKind::IntegerLiteral, "hex literal"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[3], "0xFF", "hex lexeme"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[5], ETokenKind::StringLiteral, "string literal"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("true false nullptr class struct");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "keywords should lex without error"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[0], ETokenKind::BooleanLiteral, "true"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[1], ETokenKind::BooleanLiteral, "false"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[2], ETokenKind::NullptrLiteral, "nullptr"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[3], ETokenKind::Keyword, "class keyword"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[4], ETokenKind::Keyword, "struct keyword"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("n += 1; n -= 2; n *= 3; n /= 4; n %= 5; ++n; --n;");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "compound assignment symbols should lex without error"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[1], "+=", "+="))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[5], "-=", "-="))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[9], "*=", "*="))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[13], "/=", "/="))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[17], "%=", "%="))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[20], "++", "++"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectLexeme(Tokens[23], "--", "--"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("/* unterminated");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		(void)Tokens;
		if (!Expect(Lexer.HasError(), "unterminated block comment should error"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Lexer.GetErrorMessage().find("unterminated") != std::string::npos,
				"unterminated block comment message"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("/* ok */ int x");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "terminated block comment should lex"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectTokenKind(Tokens[0], ETokenKind::Keyword, "int after comment"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("do 'A' '\\n' '\\'' '\\\\' '\\x00' '\\xFF'");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "do keyword and character literals should lex")
			|| !ExpectTokenKind(Tokens[0], ETokenKind::Keyword, "do keyword")
			|| !ExpectLexeme(Tokens[0], "do", "do keyword spelling")
			|| !ExpectTokenKind(Tokens[1], ETokenKind::CharacterLiteral, "plain character")
			|| !ExpectTokenKind(Tokens[2], ETokenKind::CharacterLiteral, "escaped newline character")
			|| !ExpectTokenKind(Tokens[3], ETokenKind::CharacterLiteral, "escaped quote character")
			|| !ExpectTokenKind(Tokens[4], ETokenKind::CharacterLiteral, "escaped slash character")
			|| !ExpectTokenKind(Tokens[5], ETokenKind::CharacterLiteral, "NUL character")
			|| !ExpectTokenKind(Tokens[6], ETokenKind::CharacterLiteral, "high-bit character"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("0b0 0B1010 1_000 0xFF_FF 0b1010_0101 1_2.5_0 1.2_5e1_0 0b1_000u64");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "binary literals and valid separators should lex")
			|| !ExpectLexeme(Tokens[0], "0b0", "binary zero spelling")
			|| !ExpectLexeme(Tokens[1], "0B1010", "uppercase binary spelling")
			|| !ExpectLexeme(Tokens[2], "1_000", "decimal separator spelling")
			|| !ExpectLexeme(Tokens[3], "0xFF_FF", "hex separator spelling")
			|| !ExpectLexeme(Tokens[4], "0b1010_0101", "binary separator spelling")
			|| !ExpectTokenKind(Tokens[5], ETokenKind::FloatLiteral, "fraction separator literal")
			|| !ExpectTokenKind(Tokens[6], ETokenKind::FloatLiteral, "exponent separator literal")
			|| !ExpectLexeme(Tokens[7], "0b1_000u64", "binary width suffix spelling"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("r\"C:\\new\\file\" r\"first\nsecond\" r");
		const std::vector<FToken>& Tokens = Lexer.Tokenize();
		if (!Expect(!Lexer.HasError(), "raw strings should lex")
			|| !ExpectTokenKind(Tokens[0], ETokenKind::StringLiteral, "raw path token kind")
			|| !ExpectLexeme(Tokens[0], "r\"C:\\new\\file\"", "raw path spelling")
			|| !ExpectTokenKind(Tokens[1], ETokenKind::StringLiteral, "multiline raw token kind")
			|| !ExpectLexeme(Tokens[1], "r\"first\nsecond\"", "multiline raw spelling")
			|| !Expect(Tokens[2].Location.Line == 2, "raw strings must update following token locations")
			|| !ExpectTokenKind(Tokens[2], ETokenKind::Identifier, "ordinary r identifier remains available"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScriptLexer Lexer("r\"unterminated");
		(void)Lexer.Tokenize();
		if (!Expect(Lexer.HasError(), "unterminated raw string should fail lexing")
			|| !Expect(
				Lexer.GetErrorMessage().find("unterminated raw string literal") != std::string::npos,
				"unterminated raw string diagnostic"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const char* Invalid : {
			 "0b", "0b_1", "0b102", "0b1_u", "0x_FF", "0xFF_",
			"1_", "1__0", "1_.0", "1._0", "1_0_f", "1_e1",
			"1e_1", "1e+_1", "1e1_"})
	{
		VeroScriptLexer Lexer(Invalid);
		(void)Lexer.Tokenize();
		if (!Expect(Lexer.HasError(), "invalid numeric separator or binary literal should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const char* Invalid : {"L'A'", "'unterminated"})
	{
		VeroScriptLexer Lexer(Invalid);
		(void)Lexer.Tokenize();
		if (!Expect(Lexer.HasError(), "unsupported wide or unterminated character literal should fail lexing"))
		{
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
