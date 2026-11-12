// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "bigint.vs";

export std::rational __std_numeric_pow<E>(std::rational value, E exponent)
{
	return __std_numeric_rational_pow(value, exponent);
}
