// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptTypes.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace VeroScript
{
	/**
	 * Language keyword table shared by the lexer and tooling completion.
	 * OutCount is the number of entries; returned pointers are null-terminated literals.
	 */
	const char* const* GetLanguageKeywords(std::size_t& OutCount);
	bool IsLanguageKeyword(std::string_view Lexeme);

	// Parser / recovery classifiers (subsets of GetLanguageKeywords).
	bool IsTypeKeyword(std::string_view Lexeme);
	// Canonical storage spelling for fixed-width source aliases.
	std::string_view CanonicalizePrimitiveTypeKeyword(std::string_view Lexeme);
	bool IsContainerTypeKeyword(std::string_view Lexeme);
	bool IsUnsignedBaseTypeKeyword(std::string_view Lexeme);
	/**
	 * Primitive type keywords usable as a bare `T _;` recovery stub
	 * (`void` / `unsigned` excluded - need different tails).
	 */
	bool IsPrimitiveDeclarableTypeKeyword(std::string_view Lexeme);

	/**
	 * Scanner: source text -> FToken stream (single pass, lazy error).
	 */
	class VeroScriptLexer
	{
	public:
		explicit VeroScriptLexer(std::string_view Source, std::string_view FileLabel = "<source>");

		const std::vector<FToken>& Tokenize();
		void SetInitialPosition(std::uint32_t InLine, std::uint32_t InColumn)
		{
			InitialLine = InLine;
			InitialColumn = InColumn;
		}
		bool HasError() const
		{
			return bHasError;
		}
		const std::string& GetErrorMessage() const
		{
			return ErrorMessage;
		}

	private:
		bool IsAtEnd() const;
		char Peek() const;
		char Advance();
		bool Match(char Expected);
		// False when a block comment is unterminated (sets HasError).
		bool SkipWhitespaceAndComments();
		FToken MakeToken(ETokenKind Kind, std::string_view Lexeme) const;
		FToken ErrorToken(std::string_view Message);
		bool ScanIdentifierOrKeyword();
		bool ScanNumber();
		bool ScanString();
		bool ScanRawString();
		bool ScanWString();
		bool ScanCharacter();

		std::string_view Source;
		std::string_view FileLabel;
		std::size_t Current = 0;
		std::uint32_t InitialLine = 1, InitialColumn = 1;
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
		std::vector<FToken> Tokens;
		bool bHasError = false;
		std::string ErrorMessage;
		bool bTokenized = false;
	};
}
