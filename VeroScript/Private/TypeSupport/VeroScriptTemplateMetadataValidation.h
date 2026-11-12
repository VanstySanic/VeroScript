// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace VeroScript
{
	struct FBytecodeModule;
	struct FTemplateInstanceInfo;
	bool CaptureTemplateInstanceValueTypes(FTemplateInstanceInfo &Instance);
	bool ValidateTemplateMetadata(const FBytecodeModule &Module, std::string &Error);
} // namespace VeroScript
