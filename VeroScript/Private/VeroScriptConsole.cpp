// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptConsoleNative.h"

namespace VeroScript::Console
{
	bool IsInputTerminal(const std::istream& Input)
	{
		return Detail::IsInputTerminal(Input);
	}
	FInfo Query(const std::ostream& Output)
	{
		return Detail::Query(Output);
	}
	bool Execute(std::ostream& Output, const EOperation Operation,
		const std::int32_t First, const std::int32_t Second)
	{
		return Detail::Execute(Output, Operation, First, Second);
	}
}
