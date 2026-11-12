// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Protected identity used when retaining only referenced standard-library code.
void __std_numeric_implementation()
{
}

// numeric.vs - aggregate operations for numeric arrays

/** Convert each input to R before checked addition in R. */
export R numeric_accumulate_as<R, T>(array<T> values, R initial)
{
	__numeric_type<R>();
	__numeric_type<T>();
	R total = initial;
	for (T value : values)
	{
		total += cast<R>(value);
	}
	return total;
}

export R numeric_sum_as<R, T>(array<T> values)
{
	__numeric_type<R>();
	__numeric_type<T>();
	assert(len(values) != 0, "std::numeric::sum_as requires a non-empty array");
	R total = cast<R>(values[0]);
	for (int64 index = 1; index < len(values); index++)
	{
		total += cast<R>(values[index]);
	}
	return total;
}

export T numeric_accumulate<T>(array<T> values, T initial)
{
	T total = initial;
	for (T value : values)
	{
		total += value;
	}
	return total;
}

export T numeric_sum<T>(array<T> values)
{
	assert(len(values) != 0, "std::numeric::sum requires a non-empty array");

	T total = values[0];
	for (int64 index = 1; index < len(values); index++)
	{
		total += values[index];
	}
	return total;
}

export double numeric_average<T>(array<T> values)
{
	assert(len(values) != 0, "std::numeric::average requires a non-empty array");

	double average = to_double(values[0]);
	for (int64 index = 1; index < len(values); index++)
	{
		double value = to_double(values[index]);
		double count = to_double(index + 1);
		if (!__isfinite(average) || !__isfinite(value))
		{
			// Preserve IEEE NaN/infinity behavior instead of inf - inf here.
			average += value;
		}
		else if ((average >= 0.0 && value >= 0.0) || (average <= 0.0 && value <= 0.0))
		{
			// Same-sign subtraction cannot overflow for finite inputs.
			average += (value - average) / count;
		}
		else
		{
			// Opposite-sign subtraction may overflow even though the mean is
			// finite, so form the weighted sum without a large difference.
			average = (average - average / count) + value / count;
		}
	}
	return average;
}
