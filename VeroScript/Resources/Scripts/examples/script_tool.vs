// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Minimal scripting-tool demo: arguments, environment variables, and strings.
// Requires environment-read permission; the host supplies script arguments.
// Interactive: import "io.vs"; string line = std::io::read_line();
// That optional interactive extension also requires console-input permission.

import "io.vs";

int main(string[] args)
{
	string who = std::environment::getenv("USERNAME");
	if (who == "")
	{
		who = std::environment::getenv("USER");
	}
	if (who == "")
	{
		who = "world";
	}
	std::print("hello, " + who);
	std::print("argc=" + std::len(args));
	if (std::len(args) > 0)
	{
		std::print("arg0=" + args[0]);
	}
	std::assert(who != "", "the selected name must not be empty");
	return 0;
}
