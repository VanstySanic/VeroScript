// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitBackend.h"

namespace VeroScript
{
	// The immutable factory may be shared by sessions and workers. Every execution
	// domain receives independent code, direct-target tables and scratch storage.
	std::shared_ptr<const IJitBackendFactory> CreateAsmJitBackendFactory();
}
