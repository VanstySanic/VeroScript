// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompilerHostLookup.h"

namespace VeroScript::CompilerDetail
{
	const FHostTypeDesc* FindHostTypeWithNamespaceSpelling(const IHostBindingCatalog* Bindings, const std::string_view Name)
	{
		if (!Bindings) return nullptr;
		if (const auto* Exact = Bindings->FindType(Name)) return Exact;
		std::string Qualified(Name);
		for (std::size_t Dot = 0; (Dot = Qualified.find('.', Dot)) != std::string::npos;)
		{
			Qualified.replace(Dot, 1, "::");
			Dot += 2;
		}
		return Name.find('.') == std::string_view::npos ? nullptr : Bindings->FindType(Qualified);
	}
}
