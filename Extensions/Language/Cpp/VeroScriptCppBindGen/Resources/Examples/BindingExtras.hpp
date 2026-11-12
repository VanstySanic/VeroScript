// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

namespace Sample
{
	enum class EPermission : unsigned int
	{
		None = 0,
		Read = 1 << 0,
		Write = 1 << 1,
		All = Read | Write
	};

	struct Counter;

	void Observe(const Counter& Value);
}
