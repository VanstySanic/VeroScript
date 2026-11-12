// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValueRoot.h"
#include <unordered_set>

namespace VeroScript::ValueGraphInternal
{
	// Share traversal state only across roots of the same immutable cleanup snapshot.
	bool Visit(const FValue& Root, const std::function<bool(const FValue&, const void*)>& Visitor,
			   std::unordered_set<const void*>& Seen);
}
