// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace VeroScript::Cli
{
	int RunProject(const std::vector<std::string>& Arguments, const std::string& StartupDirectory,
	    std::ostream& Output, std::ostream& Errors);
}
