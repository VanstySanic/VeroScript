// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHttpBackend.h"

namespace VeroScript::Curl
{
	std::shared_ptr<Http::Backend::IClientService> CreateHttpClientService();
} // namespace VeroScript::Curl
