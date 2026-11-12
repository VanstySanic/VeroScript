// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export template<typename T>
class TemplateBox
{
public:
	template<typename U>
	U echo(U value);
};
