// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptSymbolQuery.h"

namespace VeroScript::Tooling::Detail
{
	// One reference/rename pass has fixed options. Keep only its current file's
	// exact program; recovered or processor-driven queries retain their normal path.
	// Options must outlive the session. Nothing is retained between requests.
	class FSourceQuerySession
	{
	public:
		explicit FSourceQuerySession(const FSymbolQueryOptions& Options) : Options(Options)
		{
		}
		FSymbolQueryResult QueryAtSource(std::string_view Source, FTextPosition Position, std::string_view FileLabel);

	private:
		const FSymbolQueryOptions& Options;
		std::string Source;
		std::string FileLabel;
		std::shared_ptr<const FSemanticQuerySnapshot> Snapshot;
	};
}
