// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkBinding.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterNetworkUpgrade(FState& S, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Source;
		if (!Type(S, "Network::upgrade_source", Source, Error))
		{
			return false;
		}
		for (const char* Name : {"consumed", "protocol", "backend", "request_head"})
		{
			const std::string_view N = Name;
			if (!Member(
					S,
					Source,
					Name,
					N == "consumed" ? T::Bool : T::String,
					{},
					[N](const auto& C, auto& Out, auto& E)
					{
						const auto& Source = *This<FUpgradeSourceValue>(C).Value;
						if (N == "consumed")
						{
							Out = Source.IsConsumed();
							return true;
						}
						const auto& Text = N == "protocol"	? Source.Info().Protocol
										   : N == "backend" ? Source.Info().BackendIdentity
															: Source.Info().RequestHead;
						if (!CheckWork(C, Text.size(), E))
						{
							return false;
						}
						Out = Text;
						return true;
					},
					Error))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::NetworkBinding
