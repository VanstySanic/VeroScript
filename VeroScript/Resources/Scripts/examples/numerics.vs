// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "core.vs";

void main()
{
	std::bigint large = std::bigint("18446744073709551616");
	std::bigint power = std::numeric::pow(std::bigint(2), 100);
	auto parts = std::numeric::divmod(power, std::bigint(3));
	assert(parts.first * std::bigint(3) + parts.second == power);
	print("large={}, bits={}", large, power.bit_length());

	std::rational exact = std::rational(1, 3) + std::rational(1, 6);
	assert(exact == std::rational(1, 2));
	print("exact={}, decimal={}", exact, exact.to_decimal(4));
	assert(std::rational::from_double_exact(0.1).to_string() == "3602879701896397/36028797018963968");

	std::bigint value = large;
	assert(!std::bigint::try_parse("invalid", ref value));
	assert(value == large);
}
