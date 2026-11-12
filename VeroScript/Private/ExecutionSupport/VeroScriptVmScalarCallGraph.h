// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptValue.h"

namespace VeroScript
{
	struct FVmScalarCallGraph;

	std::shared_ptr<const FVmScalarCallGraph> BuildScalarCallGraph(
	    const FBytecodeModule& Module, std::uint16_t FunctionIndex);
	bool TryExecuteScalarCallGraph(const FVmScalarCallGraph& Graph, const FValue* Arguments,
	    std::size_t CallRoom, std::size_t OperandRoom, std::uint64_t RunSerial, FValue& OutResult);
}
