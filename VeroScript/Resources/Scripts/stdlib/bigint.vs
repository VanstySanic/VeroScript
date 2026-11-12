// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Big integer values and operations are supplied by the Core native catalog.

export pair<std::bigint, std::bigint> numeric_divmod(std::bigint dividend, std::bigint divisor)
{
	std::bigint quotient = std::bigint();
	std::bigint remainder = std::bigint();
	__std_numeric_bigint_divmod(dividend, divisor, ref quotient, ref remainder);
	pair<std::bigint, std::bigint> result = pair<std::bigint, std::bigint>();
	result.first = quotient;
	result.second = remainder;
	return result;
}

export std::bigint __std_numeric_pow<E>(std::bigint value, E exponent)
{
	return __std_numeric_bigint_pow(value, exponent);
}
