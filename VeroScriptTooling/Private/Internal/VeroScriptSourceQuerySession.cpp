// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSourceQuerySession.h"

#include <optional>

namespace VeroScript::Tooling::Detail
{
	FSymbolQueryResult FSourceQuerySession::QueryAtSource(const std::string_view CurrentSource,
		const FTextPosition Position, const std::string_view CurrentFileLabel)
	{
		if (Source != CurrentSource || FileLabel != CurrentFileLabel)
		{
			Snapshot.reset();
			Source = CurrentSource;
			FileLabel = CurrentFileLabel;
		}
		std::optional<FSemanticQueryScope> Scope;
		if (Snapshot && IsSemanticQuerySnapshotCurrent(Snapshot))
		{
			Scope.emplace(Snapshot);
			return VeroScriptSymbolQuery::QueryAtSource(Source, Position, FileLabel, Options);
		}
		return VeroScriptSymbolQuery::QueryAtSource(Source, Position, FileLabel, Options, &Snapshot);
	}
}
