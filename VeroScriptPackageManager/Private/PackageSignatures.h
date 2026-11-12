// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "PackageInternal.h"

namespace VeroScript::Packages::Detail
{
	void ValidateDistributionSignatures(const J&, const std::string& Sha256, std::uint64_t Bytes, const FWork&);
	void VerifyDistributionArchive(const fs::path&, const J* Signatures, const FWork&);
}
