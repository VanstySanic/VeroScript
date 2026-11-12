// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include <algorithm>

namespace VeroScript
{
	// Add newly emitted markers while preserving linked packages with stripped debug data.
	inline void CollectPauseMarkers(const std::vector<std::uint8_t>& Code,
	    const std::vector<FDebugLine>& Lines, std::vector<std::uint32_t>& Out)
	{
		for (std::size_t Ip = 0; Ip < Code.size();)
		{
			const auto Op = static_cast<EOpcode>(Code[Ip]);
			const auto Size = Bytecode::InstructionSize(Op);
			if (!Size || Size > Code.size() - Ip)
			{
				break;
			}
			if (Op == EOpcode::Nop && Ip + 1 < Code.size() &&
			    static_cast<EOpcode>(Code[Ip + 1]) != EOpcode::Nop &&
			    Ip < Lines.size() && Lines[Ip].Line != 0)
			{
				Out.push_back(static_cast<std::uint32_t>(Ip));
			}
			Ip += Size;
		}
		std::sort(Out.begin(), Out.end());
		Out.erase(std::unique(Out.begin(), Out.end()), Out.end());
		Out.erase(std::remove_if(Out.begin(), Out.end(), [&](const auto Offset)
		{
			return Offset >= Code.size() || Code.size() - Offset < 2 ||
			    static_cast<EOpcode>(Code[Offset]) != EOpcode::Nop ||
			    static_cast<EOpcode>(Code[Offset + 1]) == EOpcode::Nop;
		}),
		    Out.end());
	}
}
