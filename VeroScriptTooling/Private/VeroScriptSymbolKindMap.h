// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "Internal/VeroScriptDocumentSymbols.h"
#include "VeroScriptToolingTypes.h"

#include <cstdint>

namespace VeroScript::Tooling
{
	/**
	 * Detail::EDocumentSymbolKind and EOutlineSymbolKind stay in lockstep (same order/values).
	 */
	inline EOutlineSymbolKind MapDocumentSymbolKind(const Detail::EDocumentSymbolKind Kind)
	{
		static_assert(
			static_cast<std::uint8_t>(Detail::EDocumentSymbolKind::Label)
				== static_cast<std::uint8_t>(EOutlineSymbolKind::Label),
			"EDocumentSymbolKind / EOutlineSymbolKind must stay aligned");
		static_assert(
			static_cast<std::uint8_t>(Detail::EDocumentSymbolKind::Destructor)
				== static_cast<std::uint8_t>(EOutlineSymbolKind::Destructor),
			"EDocumentSymbolKind / EOutlineSymbolKind Destructor must stay aligned");
		static_assert(
			static_cast<std::uint8_t>(Detail::EDocumentSymbolKind::Keyword)
				== static_cast<std::uint8_t>(EOutlineSymbolKind::Keyword),
			"EDocumentSymbolKind / EOutlineSymbolKind Keyword must stay aligned");
		static_assert(
			static_cast<std::uint8_t>(Detail::EDocumentSymbolKind::Macro)
				== static_cast<std::uint8_t>(EOutlineSymbolKind::Macro),
			"EDocumentSymbolKind / EOutlineSymbolKind Macro must stay aligned");
		static_assert(
			static_cast<std::uint8_t>(Detail::EDocumentSymbolKind::AnnotationArgument)
				== static_cast<std::uint8_t>(EOutlineSymbolKind::AnnotationArgument),
			"EDocumentSymbolKind / EOutlineSymbolKind AnnotationArgument must stay aligned");
		static_assert(
			static_cast<std::uint8_t>(Detail::EDocumentSymbolKind::Namespace)
				== static_cast<std::uint8_t>(EOutlineSymbolKind::Namespace),
			"EDocumentSymbolKind / EOutlineSymbolKind Namespace must stay aligned");
		return static_cast<EOutlineSymbolKind>(static_cast<std::uint8_t>(Kind));
	}
}
