// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Terminal controls need no import. Redirected output remains plain text.
void main()
{
	if (std::console::supports_color())
	{
		std::console::set_color("bright_green", "default");
	}
	print("VeroScript console");
	std::console::reset_style();

	if (std::console::supports_cursor() && std::console::supports_clear())
	{
		std::console::set_cursor_visible(false);
		std::console::write("Working...");
		std::console::flush();
		// Move within the visible window; clearing keeps the cursor in place.
		if (std::console::width() > 0 && std::console::height() > 0)
		{
			std::console::move_cursor(0, 0);
			std::console::clear_line("all");
		}
		std::console::set_cursor_visible(true);
	}
	std::console::write("Done\n");
	std::console::flush();
}
