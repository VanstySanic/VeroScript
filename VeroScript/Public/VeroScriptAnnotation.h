// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	// Annotation identifiers use locale-independent ASCII case folding. The @
	// spelling remains significant; argument names and metadata keys are separate.
	inline bool AnnotationNamesEqual(std::string_view A, std::string_view B, bool CaseSensitive = true)
	{
		if (CaseSensitive) return A == B;
		if (A.size() != B.size()) return false;
		const auto Fold = [](unsigned char C)
		{
			return C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C;
		};
		for (std::size_t I = 0; I < A.size(); ++I)
		{
			if (Fold(static_cast<unsigned char>(A[I])) != Fold(static_cast<unsigned char>(B[I])))
			{
				return false;
			}
		}

		return true;
	}
	enum class EAnnotationArgumentKind : std::uint8_t
	{
		Flag,
		Scalar,
		Group
	};

	enum class EAnnotationScalarKind : std::uint8_t
	{
		Identifier,
		String,
		Integer,
		Float,
		Boolean
	};

	struct FAnnotationSourcePosition
	{
		std::string FileLabel;
		std::uint32_t Line = 1, Column = 1;
	};
	struct FAnnotationSourceRange
	{
		FAnnotationSourcePosition Begin, End;
	};
	struct FAnnotationArgument
	{
		std::string Name;
		EAnnotationArgumentKind Kind = EAnnotationArgumentKind::Flag;
		EAnnotationScalarKind ScalarKind = EAnnotationScalarKind::Identifier;
		std::string RawText, Value;
		std::vector<FAnnotationArgument> Children;
		FAnnotationSourceRange Range;
	};
	struct FDeclarationAnnotation
	{
		std::string Name;
		std::string RawText, DefinitionId;
		std::vector<FAnnotationArgument> Arguments;
		std::uint64_t Ordinal = 0;
		FAnnotationSourceRange Range;
		FAnnotationSourcePosition NamePosition;
	};
	inline bool operator==(const FAnnotationArgument &A, const FAnnotationArgument &B)
	{
		return A.Name == B.Name && A.Kind == B.Kind && A.ScalarKind == B.ScalarKind &&
			   A.RawText == B.RawText && A.Value == B.Value && A.Children == B.Children;
	}
	inline bool operator==(const FDeclarationAnnotation &A, const FDeclarationAnnotation &B)
	{
		return A.Name == B.Name && A.Ordinal == B.Ordinal && A.Arguments == B.Arguments;
	}

} // namespace VeroScript
