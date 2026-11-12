// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

interface IReadCounter
{
	virtual int read() const = 0;

	int doubled() const
	{
		return read() * 2;
	}
};

struct Counter : public IReadCounter
{
	int read() const
	{
		return value;
	}

	int add(int delta)
	{
		value += delta;
		return value;
	}

	static int twice(int input)
	{
		return input * 2;
	}

	void add_to(int& target, const int& amount)
	{
		target += amount;
	}

	int& value_ref()
	{
		return value;
	}

	const int& read_ref() const
	{
		return value;
	}

	static int& increment_ref(int& target)
	{
		target++;
		return target;
	}

	void modify_copy(Counter copy)
	{
		copy.value += 100;
	}

	int value;
};

int main()
{
	type<Counter> info = type<Counter>();
	int valueIndex = -1;
	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Counter> candidate = field_info<Counter>(info.qualified_name, i);
		if (candidate.name() == "value")
		{
			valueIndex = i;
		}
	}
	int addIndex = -1;
	int twiceIndex = -1;
	int addToIndex = -1;
	int valueRefIndex = -1;
	int readRefIndex = -1;
	int incrementRefIndex = -1;
	int modifyCopyIndex = -1;
	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Counter> candidate = method_info<Counter>(info.qualified_name, i);
		if (candidate.name() == "add")
		{
			addIndex = i;
		}
		if (candidate.name() == "twice")
		{
			twiceIndex = i;
		}
		if (candidate.name() == "add_to")
		{
			addToIndex = i;
		}
		if (candidate.name() == "value_ref")
		{
			valueRefIndex = i;
		}
		if (candidate.name() == "read_ref")
		{
			readRefIndex = i;
		}
		if (candidate.name() == "increment_ref")
		{
			incrementRefIndex = i;
		}
		if (candidate.name() == "modify_copy")
		{
			modifyCopyIndex = i;
		}
	}
	assert(valueIndex >= 0 && addIndex >= 0 && twiceIndex >= 0);
	assert(addToIndex >= 0 && valueRefIndex >= 0 && readRefIndex >= 0 && incrementRefIndex >= 0);
	assert(modifyCopyIndex >= 0);

	type<IReadCounter> interfaceInfo = type<IReadCounter>();
	int doubledIndex = -1;
	for (int i = 0; i < interfaceInfo.method_count(); i++)
	{
		method_info<IReadCounter> candidate = method_info<IReadCounter>(interfaceInfo.qualified_name, i);
		if (candidate.name() == "doubled")
		{
			doubledIndex = i;
		}
	}
	assert(doubledIndex >= 0);

	field_info<Counter> field = field_info<Counter>(info.qualified_name, valueIndex);
	method_info<Counter> add = method_info<Counter>(info.qualified_name, addIndex);
	method_info<Counter> twice = method_info<Counter>(info.qualified_name, twiceIndex);
	method_info<IReadCounter> doubled = method_info<IReadCounter>(interfaceInfo.qualified_name, doubledIndex);
	Counter counter = Counter();
	field.set<int>(counter, 10);
	assert(field.get<int>(counter) == 10);
	assert(add.invoke<int>(counter, 5) == 15);
	assert(twice.invoke_static<int>(21) == 42);
	assert(doubled.invoke<int>(counter) == 30);
	assert(field.get<int>(counter) == 15);
	method_info<Counter> addTo = method_info<Counter>(info.qualified_name, addToIndex);
	method_info<Counter> valueRef = method_info<Counter>(info.qualified_name, valueRefIndex);
	method_info<Counter> readRef = method_info<Counter>(info.qualified_name, readRefIndex);
	method_info<Counter> incrementRef = method_info<Counter>(info.qualified_name, incrementRefIndex);
	int total = 3;
	addTo.invoke<void>(counter, &total, &total);
	assert(total == 6);
	valueRef.invoke<int&>(counter) += 2;
	const int& view = readRef.invoke<const int&>(counter);
	counter.value++;
	assert(view == 18);
	incrementRef.invoke_static<int&>(&total) += 2;
	assert(total == 9);
	method_info<Counter> modifyCopy = method_info<Counter>(info.qualified_name, modifyCopyIndex);
	modifyCopy.invoke<void>(counter, counter);
	assert(counter.value == 18);
	print("executable reflection ok");
	return 0;
}
