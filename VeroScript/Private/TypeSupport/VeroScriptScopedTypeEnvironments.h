// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

namespace VeroScript
{
	struct FProgram;
	struct FScriptError;
	bool BindScopedTypeEnvironments(FProgram& Program, FScriptError& Error);
} // namespace VeroScript
