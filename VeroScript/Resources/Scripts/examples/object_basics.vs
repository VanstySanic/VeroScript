// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Struct methods, a declaration with an instance, and nullable class references.

struct Counter
{
	void add(int amount)
	{
		this.value += amount;
	}

	int value = 0;
};

// A non-generic type definition may declare an instance on the same line.
struct Configuration
{
	bool enabled = true;
} defaultConfiguration;

class Node
{
public:
	int id;
};

// A generic class can refer to its own specialization through a nullable field.
class LinkedNode<T>
{
public:
	T value;
	LinkedNode<T> next;
};

void main()
{
	Counter counter = Counter();
	counter.add(3);
	assert(counter.value == 3);
	print(counter.value);

	Node node = nullptr;
	assert(node == nullptr);
	if (node == nullptr)
	{
		print("null ok");
	}

	assert(defaultConfiguration.enabled);
	print("default configuration: {}", defaultConfiguration.enabled);

	LinkedNode<int> first = LinkedNode<int>();
	first.value = 1;
	first.next = LinkedNode<int>();
	first.next.value = 2;
	assert(first.value == 1 && first.next.value == 2);
	assert(first.next.next == nullptr);
}
