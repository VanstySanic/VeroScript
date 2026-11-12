// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmExecution.h"

namespace VeroScript
{
	void FBytecodeVmExecution::ReportError(const char* Message, const FSourceLocation& Location)
	{
		Services.ReportError(Message, Location);
	}
}
