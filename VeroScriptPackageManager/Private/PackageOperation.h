// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "PackageInternal.h"
namespace VeroScript::Packages::Detail
{
struct FOperationScope
{
explicit FOperationScope(std::string);
~FOperationScope();
std::string Root;
};
}
