// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAnnotation.h"
#include "VeroScriptSourceMetadata.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VeroScript
{
	/**
	 * Lexer token category for identifiers, literals, keywords, and symbols.
	 */
	enum class ETokenKind : std::uint8_t
	{
		EndOfFile,
		Identifier,
		IntegerLiteral,
		FloatLiteral,
		StringLiteral,
		WStringLiteral,
		Keyword,
		BooleanLiteral,
		NullptrLiteral,
		Symbol,
		CharacterLiteral,
	};

	/**
	 * C++-style member / base inheritance access.
	 */
	enum class EMemberAccess : std::uint8_t
	{
		Public,
		Protected,
		Private,
	};

	/**
	 * Source file label and 1-based line/column for diagnostics and tokens.
	 */
	struct FSourceLocation
	{
		std::string_view FileLabel = {};
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
	};

	/**
	 * Single lexer token with kind, lexeme slice, and source location.
	 */
	struct FToken
	{
		ETokenKind Kind = ETokenKind::EndOfFile;
		std::string_view Lexeme = {};
		FSourceLocation Location = {};
	};


	/**
	 * Opaque declaration annotations (`Ident(...)` before a decl), flattened to key/value.
	 * Language does not interpret keys; hosts consume via ModuleMeta / Session.
	 * Convention: ("__annot", name) then args; nested `meta=(k=v)` -> key `meta.k`.
	 */
	struct FDeclMeta : std::vector<std::pair<std::string, std::string>>
	{
		using std::vector<std::pair<std::string, std::string>>::vector;
		std::vector<FDeclarationAnnotation> Annotations;
		std::vector<FSourceComment> Comments;
		std::string Documentation, StableId;
		FSourceRange SourceRange;
		ECommentRetention CommentRetention = ECommentRetention::Full;
		friend bool operator==(const FDeclMeta& A, const FDeclMeta& B)
		{
			using FFlat = std::vector<std::pair<std::string, std::string>>;
			return static_cast<const FFlat&>(A) == static_cast<const FFlat&>(B) && A.Annotations == B.Annotations;
		}
		friend bool operator!=(const FDeclMeta& A, const FDeclMeta& B)
		{
			return !(A == B);
		}
	};

	inline constexpr std::string_view DeclaredReturnTypeMetaKey =
		"__veroscript.declared_return_type";
	inline constexpr std::string_view DeclaredParamTypeMetaPrefix =
		"__veroscript.declared_param_type.";

	inline bool HasDeclaredSignatureTypeMetadata(const FDeclMeta& Meta)
	{
		for (const auto& Entry : Meta)
		{
			if (Entry.first == DeclaredReturnTypeMetaKey
				|| Entry.first.rfind(DeclaredParamTypeMetaPrefix, 0) == 0)
			{
				return true;
			}
		}
		return false;
	}
}
