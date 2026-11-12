// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// random.vs - deterministic, independently seeded pseudo-random state

// Reserved module identity, retained when package paths are sanitized.
void __std_random_implementation()
{
}

export array<unsigned int> random_make_engine(unsigned int seed_value)
{
	array<unsigned int> engine = array<unsigned int>();
	engine.push(seed_value == 0u ? 0x6d2b79f5u : seed_value);
	return engine;
}

export void random_seed(array<unsigned int> engine, unsigned int seed_value)
{
	unsigned int state = seed_value == 0u ? 0x6d2b79f5u : seed_value;
	if (len(engine) == 0)
	{
		engine.push(state);
	}
	else
	{
		engine[0] = state;
	}
}

export unsigned int random_next_uint(array<unsigned int> engine)
{
	return __random_next_uint(engine);
}

/**
 * Return an unbiased value in [0, upper_exclusive).
 * Rejection sampling avoids the distribution bias introduced by `% bound`
 * when the bound does not divide the complete uint32 range.
 */
export unsigned int random_next_bounded_uint(array<unsigned int> engine, unsigned int upper_exclusive)
{
	assert(upper_exclusive > 0u, "random upper bound must be positive");

	// xorshift32 visits only the non-zero uint32 states. Shift its output to
	// [0, 2^32 - 1) before rejection sampling; treating it as 2^32 samples
	// makes zero unreachable for the largest supported bound.
	uint64 range_size = 4294967295u64;
	uint64 bound = to_uint64(upper_exclusive);
	uint64 accepted_limit = range_size - range_size % bound;
	uint64 sample = to_uint64(random_next_uint(engine)) - 1u64;
	while (sample >= accepted_limit)
	{
		sample = to_uint64(random_next_uint(engine)) - 1u64;
	}
	return to_unsigned(sample % bound);
}

/** Return an unbiased integer in [minimum, maximum_exclusive). */
export int random_next_int(array<unsigned int> engine, int minimum, int maximum_exclusive)
{
	assert(minimum < maximum_exclusive, "random integer range must be non-empty");
	int64 width = to_int64(maximum_exclusive) - to_int64(minimum);
	unsigned int offset = random_next_bounded_uint(engine, to_unsigned(width));
	return to_int(to_int64(minimum) + to_int64(offset));
}

export double random_next_double(array<unsigned int> engine)
{
	return to_double(random_next_uint(engine)) / 4294967296.0;
}

/** Return a floating-point value in [minimum, maximum_exclusive). */
export double random_next_double_range(array<unsigned int> engine, double minimum, double maximum_exclusive)
{
	assert(minimum < maximum_exclusive, "random floating-point range must be non-empty");
	assert(__isfinite(minimum) && __isfinite(maximum_exclusive), "random floating-point range endpoints must be finite");
	double width = maximum_exclusive - minimum;
	while (true)
	{
		double unit = random_next_double(engine);
		double value = minimum;
		if (__isfinite(width))
		{
			value += width * unit;
		}
		else
		{
			// A convex combination works when maximum - minimum overflows.
			value = minimum * (1.0 - unit) + maximum_exclusive * unit;
		}
		// Rounding can produce the exclusive endpoint for narrow intervals.
		if (value >= minimum && value < maximum_exclusive)
		{
			return value;
		}
	}
	// The loop returns once rounding yields an in-range representable value.
	return minimum;
}

export bool random_next_bool(array<unsigned int> engine)
{
	return (random_next_uint(engine) & 1u) != 0u;
}

/** Sample an array index without narrowing a 64-bit container length. */
export int64 __std_random_index(array<unsigned int> engine, int64 upperExclusive)
{
	return __random_index(engine, upperExclusive);
}
