// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpBinding.h"
#include "VeroScriptHttp.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterHttpUpgrade(FState& S, FHostTypeId Exchange, std::string& Error)
	{
		return Member(
			S,
			Exchange,
			"prepare_upgrade",
			ENativeType::HostObject,
			{},
			[&S](const auto& C, auto& Out, auto& E)
			{
				const auto& V = This<FHandle<Http::FExchange>>(C);
				if (!Lease(S, V.Context, E))
				{
					return false;
				}
				const auto& Request = V.Value->Request();
				std::uint64_t Work = Request.Method.size() + Request.Target.size();
				for (const auto& H : Request.Headers.Fields())
				{
					Work += H.first.size() + H.second.size() + 4;
				}
				if (!CheckWork(C, Work, E))
				{
					return false;
				}
				const auto& Service = static_cast<FContextValue*>(V.Context->Payload)->Services;
				if (!Service)
				{
					E = "Network upgrade binding service is unavailable";
					return false;
				}
				Network::FError NativeError;
				auto Source = V.Value->PrepareUpgrade(NativeError);
				if (!Source)
				{
					E = NativeError.Message;
					return false;
				}
				return Service->MakeUpgradeSource(V.Context, std::move(Source), Out, E);
			},
			Error,
			"Network::upgrade_source",
			{},
			false,
			true);
	}
} // namespace VeroScript::NetworkBinding
