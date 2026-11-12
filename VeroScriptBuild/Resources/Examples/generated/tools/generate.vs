// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int main()
{
	string source = build_read_text("template", "");
	build_write_text("sources", "main.vs", source);
	build_message("Generated main.vs");
	return 0;
}
