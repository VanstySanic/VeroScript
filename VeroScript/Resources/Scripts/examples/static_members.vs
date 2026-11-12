// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// static_members.vs — static containers and C++-style static members
// Run from this directory: veroscript static_members.vs
// Enum, type, and namespace qualification uses the recommended `::` spelling.

enum class RunState : uint32
{
	Stopped = 0u,
	Running = 1u
};

// A static struct is non-instantiable and gives its members public access by default.
static struct Limits
{
	int RetryCount()
	{
		return Limits::MaxRetries;
	}

	int MaxRetries = 3;
};

class Counter
{
public:
	Counter()
	{
		Counter::Total = Counter::Total + 1;
		id = Counter::Total;
	}

	static int Peek()
	{
		return Counter::Total;
	}

	static int Total = 0;
	int id;
};

class Box<T>
{
public:
	static int Count = 0;
	T value;
};

class CachedItem
{
public:
	int read()
	{
		return 7;
	}
};

class Cache<T>
{
public:
	static CachedItem value = CachedItem();
};

// A static class or static struct is a non-instantiable utility container.
// Every declared member is static; the only distinction is the usual default
// member access (`class` private, `struct` public).
public static class Math
{
	int Add(int value)
	{
		return Math::Base + value;
	}

	int Base = 40;
};

namespace Util
{
	int Seed = 10;
}

// A using-directive imports direct names from this declaration to the end of its
// containing namespace. It does not re-export names or affect sibling namespaces.
// Explicit `Util::Seed` remains valid when qualification improves readability.
using namespace Util;

void main()
{
	print("Counter::Peek() = {}", Counter::Peek());
	Counter a = Counter();
	Counter b = Counter();
	print("ids {} / {}, Total = {}", a.id, b.id, Counter::Total);

	Box<int> bi = Box<int>();
	Box<int>::Count = Box<int>::Count + 1;
	Box<int>::Count = Box<int>::Count + 1;
	Box<string> bs = Box<string>();
	Box<string>::Count = Box<string>::Count + 1;
	print("Box<int>::Count = {}", Box<int>::Count);
	print("Box<string>::Count = {}", Box<string>::Count);

	print("Math::Add(2) = {}", Math::Add(2));
	print("Limits::RetryCount() = {}", Limits::RetryCount());

	Seed = Seed + 1;
	print("Seed via using namespace Util = {}", Seed);

	RunState state = RunState::Running;
	print("RunState::Running selected = {}", state == RunState::Running);

	// A generic static field retains its declared class type for chained calls.
	assert(Cache<int>::value.read() == 7);
	assert(Cache<string>::value.read() == 7);
}
