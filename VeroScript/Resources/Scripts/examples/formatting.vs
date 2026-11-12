// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

void main()
{
	// Brace-style formatting remains the general-purpose default.
	print("name={}, score={}", "Ada", 97);
	string message = format("hex={:8X}", 255U);
	print(message);

	// printf-style formatting uses a separate, explicit API and grammar.
	printf("value=%+06d, hex=%#08x", -42, 255U);
	string precise = formatf("ratio=%.2f, text=%-5.3s", 3.14159, "abcdef");
	print(precise);
}
