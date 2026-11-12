// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>
namespace VeroScript::Cli
{
	int RunBuild(const std::vector<std::string>&, const std::string& StartupDirectory, const std::string& Executable);
}
