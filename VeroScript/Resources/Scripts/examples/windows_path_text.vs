// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Windows-style path text on any platform; this does not access the filesystem.

void main()
{
	wstring path = L"C:\\Temp\\test.txt";
	string text = std::to_string(path);
	assert(text == "C:\\Temp\\test.txt");
	assert(std::len(path) == std::len(text));
	print(text);
	print(std::len(path));
}
