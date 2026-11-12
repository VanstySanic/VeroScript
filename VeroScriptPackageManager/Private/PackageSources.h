// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "PackageInternal.h"
namespace VeroScript::Packages::Detail
{
void StableUrl(const std::string&);
J RegistrySettings(const J&, const FClientOptions&);
}
