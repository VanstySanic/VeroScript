// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHostBinding.h"
#include "VeroScriptSourceMetadata.h"
#include <memory>

namespace VeroScript::Tooling::Detail
{
	// A query-only vocabulary view. It does not change native bindings or compile identity.
	std::shared_ptr<const IHostBindingCatalog> SourceMetadataQueryBindings(
		const IHostBindingCatalog* Bindings, const std::vector<FAnnotationDefinition>& Definitions);
	inline std::shared_ptr<const IHostBindingCatalog> SourceMetadataQueryBindings(
		const IHostBindingCatalog* Bindings, const FSourceMetadataSnapshot* Metadata)
	{
		static const std::vector<FAnnotationDefinition> Empty;
		return SourceMetadataQueryBindings(Bindings, Metadata ? Metadata->Definitions : Empty);
	}
}
