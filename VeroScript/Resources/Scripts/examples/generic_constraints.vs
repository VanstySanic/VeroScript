// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Generic constraints with interface and multi-interface bounds.
// Run from this directory: veroscript generic_constraints.vs

interface IId
{
	virtual int id() = 0;
};

interface IName
{
	virtual string name() = 0;
};

class Person : public IId, public IName
{
public:
	int id()
	{
		return 42;
	}
	string name()
	{
		return "Ada";
	}
};

class Nameless
{
public:
	int id()
	{
		return 0;
	}
};

string describe<T>(T x) where T : IId, IName
{
	return x.name() + "#" + x.id();
}

class TagBox<T> where T : IId
{
public:
	int tag()
	{
		return item.id();
	}

	T item;
};

void main()
{
	Person p = Person();
	print(describe(p));
	TagBox<Person> box = TagBox<Person>();
	box.item = p;
	print(box.tag());
}
