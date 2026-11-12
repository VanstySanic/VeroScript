// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>
#include <vector>

namespace VeroScript::Tooling
{
	class VeroScriptWorkspace;

	/**
	 * Flattened workspace symbol list for IDE symbol search.
	 * Collects document outlines across open documents (or an explicit document set).
	 */
	class VeroScriptWorkspaceSymbols
	{
	public:
		static FWorkspaceSymbolsResult CollectFromDocuments(
			const std::vector<FDocumentOverlay>& Documents,
			const FWorkspaceSymbolsOptions& Options = {});

		static FWorkspaceSymbolsResult CollectFromWorkspace(
			const VeroScriptWorkspace& Workspace,
			const FWorkspaceSymbolsOptions& Options = {});
	};
}
