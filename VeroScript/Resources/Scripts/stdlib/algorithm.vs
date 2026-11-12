// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// algorithm.vs - generic array algorithms for the VeroScript standard library

import "random.vs";

export void algorithm_sort<T>(array<T> values)
{
	int64 count = __array_prepare_sort(values);
	if (count < 2)
	{
		return;
	}

	for (int64 start = count / 2 - 1; start >= 0; start--)
	{
		int64 root = start;
		while (root < count / 2)
		{
			int64 child = root * 2 + 1;
			int64 largest = root;
			if (values[largest] < values[child])
			{
				largest = child;
			}
			if (child + 1 < count && values[largest] < values[child + 1])
			{
				largest = child + 1;
			}
			if (largest == root)
			{
				break;
			}
			T heapValue = values[root];
			values[root] = values[largest];
			values[largest] = heapValue;
			root = largest;
		}
	}
	for (int64 end = count - 1; end > 0; end--)
	{
		T temporary = values[0];
		values[0] = values[end];
		values[end] = temporary;
		int64 root = 0;
		while (root < end / 2)
		{
			int64 child = root * 2 + 1;
			int64 largest = root;
			if (values[largest] < values[child])
			{
				largest = child;
			}
			if (child + 1 < end && values[largest] < values[child + 1])
			{
				largest = child + 1;
			}
			if (largest == root)
			{
				break;
			}
			T heapValue = values[root];
			values[root] = values[largest];
			values[largest] = heapValue;
			root = largest;
		}
	}
}

export void algorithm_reverse<T>(array<T> values)
{
	__array_reverse(values);
}

export int64 algorithm_find_index<T>(array<T> values, T value)
{
	for (int64 index = 0; index < len(values); index++)
	{
		if (values[index] == value)
		{
			return index;
		}
	}
	return -1;
}

export int64 algorithm_count<T>(array<T> values, T value)
{
	int64 matches = 0;
	for (int64 index = 0; index < len(values); index++)
	{
		if (values[index] == value)
		{
			matches++;
		}
	}
	return matches;
}

export int64 algorithm_lower_bound<T>(array<T> values, T value)
{
	int64 first = 0;
	int64 count = len(values);
	while (count > 0)
	{
		int64 step = count / 2;
		int64 middle = first + step;
		if (values[middle] < value)
		{
			first = middle + 1;
			count -= step + 1;
		}
		else
		{
			count = step;
		}
	}
	return first;
}

export bool algorithm_binary_search<T>(array<T> values, T value)
{
	int64 first = 0;
	int64 count = len(values);
	while (count > 0)
	{
		int64 step = count / 2;
		int64 middle = first + step;
		if (values[middle] < value)
		{
			first = middle + 1;
			count -= step + 1;
		}
		else
		{
			count = step;
		}
	}
	return first < len(values) && !(values[first] < value) && !(value < values[first]);
}

export void algorithm_fill<T>(array<T> values, T value)
{
	for (int64 index = 0; index < len(values); index++)
	{
		values[index] = value;
	}
}

export array<T> algorithm_copy<T>(array<T> values)
{
	array<T> result = array<T>();
	result.reserve(len(values));
	for (T value : values)
	{
		result.push(value);
	}
	return result;
}

export bool algorithm_contains<T>(array<T> values, T value)
{
	for (T candidate : values)
	{
		if (candidate == value)
		{
			return true;
		}
	}
	return false;
}

export bool algorithm_equal<T>(array<T> left, array<T> right)
{
	if (len(left) != len(right))
	{
		return false;
	}
	for (int64 index = 0; index < len(left); index++)
	{
		if (!(left[index] == right[index]))
		{
			return false;
		}
	}
	return true;
}

export bool algorithm_is_sorted<T>(array<T> values)
{
	for (int64 index = 1; index < len(values); index++)
	{
		if (values[index] < values[index - 1])
		{
			return false;
		}
	}
	return true;
}

/** Return -1 for an empty array. */
export int64 algorithm_min_index<T>(array<T> values)
{
	if (len(values) == 0)
	{
		return -1;
	}
	int64 best = 0;
	for (int64 index = 1; index < len(values); index++)
	{
		if (values[index] < values[best])
		{
			best = index;
		}
	}
	return best;
}

/** Return -1 for an empty array. */
export int64 algorithm_max_index<T>(array<T> values)
{
	if (len(values) == 0)
	{
		return -1;
	}
	int64 best = 0;
	for (int64 index = 1; index < len(values); index++)
	{
		if (values[best] < values[index])
		{
			best = index;
		}
	}
	return best;
}

/** Rotate so that the element at middle becomes the first element. */
export void __std_algorithm_rotate<T>(array<T> values, int64 middle)
{
	__array_rotate(values, middle);
}

/** In-place Fisher-Yates shuffle using caller-owned deterministic state. */
export void algorithm_shuffle<T>(array<T> values, array<unsigned int> engine)
{
	__array_shuffle(values, engine);
}
