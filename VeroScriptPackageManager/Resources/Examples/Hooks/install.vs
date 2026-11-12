// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export void install(std::package::install_context context)
{
	context.write_text("copied.txt", context.read_text("data", "source.txt"));
}
