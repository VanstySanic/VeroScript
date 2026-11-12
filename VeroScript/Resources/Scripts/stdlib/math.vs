// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// math.vs - VeroScript standard math helpers (v0.1.1)
// abs / min / max are global builtins (int or double, like C++ overload at runtime).

export double __std_sqrt(double x)
{
	return __sqrt(x);
}

export double __std_sin(double x)
{
	return __sin(x);
}

export double __std_cos(double x)
{
	return __cos(x);
}

export double __std_tan(double x)
{
	return __tan(x);
}

export double __std_floor(double x)
{
	return __floor(x);
}

export double __std_ceil(double x)
{
	return __ceil(x);
}

export double __std_round(double x)
{
	return __round(x);
}

export double __std_trunc(double x)
{
	return __trunc(x);
}

export double __std_pow(double base, double exponent)
{
	return __pow(base, exponent);
}

export double __std_exp(double x)
{
	return __exp(x);
}

export double __std_log(double x)
{
	return __log(x);
}

export double __std_log2(double x)
{
	return __log2(x);
}

export double __std_asin(double x)
{
	return __asin(x);
}

export double __std_acos(double x)
{
	return __acos(x);
}

export double __std_atan2(double y, double x)
{
	return __atan2(y, x);
}

export bool __std_isfinite(double x)
{
	return __isfinite(x);
}

export bool __std_isnan(double x)
{
	return __isnan(x);
}

/** Mathematical constants are functions so no mutable global state is exposed. */
export double __std_pi()
{
	return 3.14159265358979323846;
}

export double __std_e()
{
	return 2.71828182845904523536;
}

export T __std_clamp<T>(T x, T lo, T hi)
{
	__numeric_type<T>();
	assert(lo <= hi, "clamp requires ordered, non-NaN bounds");
	if (x < lo)
	{
		return lo;
	}
	if (x > hi)
	{
		return hi;
	}
	return x;
}

export int __std_clamp_int(int x, int lo, int hi)
{
	return __std_clamp<int>(x, lo, hi);
}
