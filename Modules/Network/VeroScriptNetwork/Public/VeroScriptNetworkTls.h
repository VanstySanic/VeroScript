// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"

namespace VeroScript::Network
{
	bool TlsAvailable(const FContext& Context) noexcept;
	struct FTlsOptions
	{
		bool bEnabled = false;
		bool bUseSystemTrust = true;
		// Owned PEM data. Loading resources/files is an explicit caller operation.
		std::string TrustPem;
		std::string CertificateChainPem;
		std::string PrivateKeyPem;
		std::string PrivateKeyPassword;
	};
} // namespace VeroScript::Network
