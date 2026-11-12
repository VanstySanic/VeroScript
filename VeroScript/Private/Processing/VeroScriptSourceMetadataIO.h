// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptSourceMetadata.h"
#include "VeroScriptCompileProcessing.h"
namespace VeroScript
{
	// Resolves the immediate mapping without allocating the full origin chain.
	// Invalid ranges or line origins return false without changing the output.
	bool TryResolveFirstSourceOrigin(const FSourceMetadataSnapshot&, const FSourceRange&, FSourceOrigin&);
	// Resolves copied generated spans, including snapshots whose source text was discarded.
	bool MapGeneratedDiagnostic(const FSourceMetadataSnapshot&, FScriptError&);
	bool EncodeSourceMetadata(const FSourceMetadataSnapshot&, std::vector<std::uint8_t>&, std::string& Error);
	std::shared_ptr<const FSourceMetadataSnapshot> DecodeSourceMetadata(const std::uint8_t*, std::size_t, std::string& Error);
	bool EncodeCompileOperations(const std::vector<FCompileGenerationOperation>&, std::vector<std::uint8_t>&, std::string& Error);
	bool DecodeCompileOperations(const std::uint8_t*, std::size_t, std::vector<FCompileGenerationOperation>&, std::string& Error);
}
