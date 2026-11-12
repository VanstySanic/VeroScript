// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Build as a binary library:
// veroscript --emit-package DemoLibrary.vslib package_library.vs

export enum class Operation : uint8
{
	Add = 1u,
	Multiply = 2u
};

export interface IReadable<T>
{
	virtual T read() const = 0;
};

export struct Box<T>
{
	Box(T initial)
	{
		value = initial;
	}

	T get() const
	{
		return value;
	}

	T value;
};

export T identity<T>(T value)
{
	return value;
}

export int shared_offset = 2;

// This remains private to the library because it is not exported.
int hidden_value()
{
	return 99;
}
