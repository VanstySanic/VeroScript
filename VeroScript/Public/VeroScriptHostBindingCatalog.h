// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

/**
 * Serialize / deserialize IHostBindingCatalog for IDE / LSP / DAP.
 * Imported natives use a stub Callback so Analyze/Compile can resolve names;
 * executing them fails with a clear error until a live host registers real callbacks.
 *
 * Per-type optional JSON fields (catalog version 1, backward compatible):
 * - inheritable: allow `class/struct X : Host` (default false)
 * - hasClone / hasEquals / hasHash / hasSubclass: restore the corresponding
 *   capability stubs when true
 *
 * Optional root array `annotations`: [{ name, detail }] - the host's declaration
 * annotation vocabulary (`UCLASS`, `@rpc`, ...). Tooling-only; the parser accepts
 * any annotation regardless.
 *
 * Functions, types, and members may also carry an optional `annotations` array
 * containing actual FHostAnnotationInstance values. Functions and members may
 * carry `paramAnnotations`, an array parallel to `paramTypes`. These additive
 * fields preserve version-1 compatibility; unknown annotation names remain opaque.
 */

#include "VeroScriptHostBinding.h"

#include <string>
#include <string_view>

namespace VeroScript
{
	// Write catalog JSON (UTF-8). On allocation/size failure, returns false and
	// clears OutJson.
	bool ExportBindingCatalogJson(
		const IHostBindingCatalog& Catalog,
		std::string& OutJson);

	// Write catalog JSON to a UTF-8 file.
	bool SaveBindingCatalogFile(
		const IHostBindingCatalog& Catalog,
		std::string_view FilePath,
		std::string& OutErrorMessage);

	/**
	 * Parse catalog JSON into Registry.
	 * Import is transactional in both replace and append modes. On failure,
	 * OutErrorMessage describes the problem and Registry remains unchanged.
	 */
	bool ImportBindingCatalogJson(
		std::string_view Json,
		FHostBindingRegistry& Registry,
		std::string& OutErrorMessage,
		bool bClearFirst = true);

	// Load a UTF-8 catalog file into Registry.
	bool LoadBindingCatalogFile(
		std::string_view FilePath,
		FHostBindingRegistry& Registry,
		std::string& OutErrorMessage,
		bool bClearFirst = true);

	// Stub native used by catalog import (compile-only).
	bool BindingCatalogStubNative(
		const FNativeCallContext& Context,
		FValue& OutReturn,
		std::string& OutError);
}
