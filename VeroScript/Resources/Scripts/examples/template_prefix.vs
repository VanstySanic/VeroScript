// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// `template<...>` and `Name<...>` declarations share the same generic rules.
// Trailing type parameters may have defaults and can reference earlier parameters.

template<T, U = int>
class PairBox
{
public:
	PairBox(T firstValue, U secondValue)
	{
		First = firstValue;
		Second = secondValue;
	}

	T First;
	U Second;
};

template<T>
T Identity(T value)
{
	return value;
}

template<T = int, U = T>
U DefaultValue()
{
	return 6;
}

template<T, U = int>
U SelectValue(T ignored, U value)
{
	return value;
}

void main()
{
	PairBox<string> defaulted("vero", 7);
	PairBox<int, string> explicitValue(8, "script");
	print("{} {}", Identity<string>(defaulted.First), defaulted.Second);
	print("{} {}", explicitValue.First, explicitValue.Second);
	print(DefaultValue());

	// Explicit arguments are a prefix; U is deduced as string instead of using int.
	print(SelectValue<int>(1, "deduced"));
}
