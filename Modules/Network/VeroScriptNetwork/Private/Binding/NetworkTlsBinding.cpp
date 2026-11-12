// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkBinding.h"
#include "VeroScriptNetworkTls.h"

namespace VeroScript::NetworkBinding
{
	bool RegisterNetworkTls(FState& S, FHostTypeId Api, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Tls;
		if (!ValueType<Network::FTlsOptions>(S, "Network::tls_options", Tls, Error))
		{
			return false;
		}
		if (!Member(
				S,
				Api,
				"tls_available",
				T::Bool,
				{},
				[&S](const auto&, auto& O, auto& E)
				{
					auto Backend = BackendService<Network::Backend::IService>(S, Network::Backend::ServiceId, "", Network::Backend::ContractId, E);
					if (!Backend) return false;
					O = Backend->TlsAvailable();
					return true;
				},
				Error,
				"",
				{},
				true) ||
			!Member(
				S,
				Api,
				"make_tls_options",
				T::HostObject,
				{},
				[&S](const auto&, auto& O, auto& E)
				{
					Network::FTlsOptions Value;
					Value.bEnabled = true;
					return Make(S, "Network::tls_options", std::move(Value), O, E);
				},
				Error,
				"Network::tls_options",
				{},
				true))
		{
			return false;
		}
		for (const char* Name :
			{"set_trust_pem", "set_certificate_pem", "set_private_key_pem", "set_private_key_password"})
		{
			if (!Member(
					S,
					Tls,
					Name,
					T::Void,
					{T::String},
					[Name](const auto& C, auto&, auto& E)
					{
						const auto& Text = String(C, 0);
						if (Text.size() > 4 * 1024 * 1024)
						{
							E = "TLS configuration string limit exceeded";
							return false;
						}
						auto& V = This<Network::FTlsOptions>(C);
						if (std::string_view(Name) == "set_trust_pem")
						{
							V.TrustPem = Text;
						}
						else if (std::string_view(Name) == "set_certificate_pem")
						{
							V.CertificateChainPem = Text;
						}
						else if (std::string_view(Name) == "set_private_key_pem")
						{
							V.PrivateKeyPem = Text;
						}
						else
						{
							V.PrivateKeyPassword = Text;
						}
						return true;
					},
					Error,
					"",
					{},
					false,
					true))
			{
				return false;
			}
		}
		return Member(
			S,
			Tls,
			"use_system_trust",
			T::Void,
			{T::Bool},
			[](const auto& C, auto&, auto&)
			{
				This<Network::FTlsOptions>(C).bUseSystemTrust = std::get<bool>(C.Args[0]);
				return true;
			},
			Error,
			"",
			{},
			false,
			true);
	}
} // namespace VeroScript::NetworkBinding
