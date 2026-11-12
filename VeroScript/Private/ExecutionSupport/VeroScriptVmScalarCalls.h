// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"

namespace VeroScript
{
	struct FVmCallableSignature
	{
		std::string Name;
		std::vector<std::string> Parameters;
		std::string ReturnType;
	};

	struct FVmScalarCallGraph;

	struct FVmScalarCallPlan
	{
		struct FInstruction
		{
			EOpcode Opcode = EOpcode::Nop;
			std::uint16_t Slot = 0;
			std::int32_t Value = 0;
		};

		std::vector<FInstruction> Instructions;
		std::size_t StackDepth = 0;
		bool bUsesReceiver = false;
		std::shared_ptr<const FVmScalarCallGraph> CallGraph;
	};
}
