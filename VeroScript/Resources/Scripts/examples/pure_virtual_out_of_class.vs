// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Explicit pure virtual declarations and same-file out-of-class definitions.
// Run from this directory: veroscript pure_virtual_out_of_class.vs

interface IValue
{
public:
	virtual int Get() const = 0;
};

class AbstractValue : public IValue
{
public:
	virtual int Get() const override = 0;

	AbstractValue(int initial);
	int Add(int amount) const;
	virtual ~AbstractValue() = 0;

	int value;
};

// Like C++, a pure virtual method may still provide an explicit base body.
int AbstractValue::Get() const
{
	return value;
}

AbstractValue::AbstractValue(int initial)
{
	value = initial;
}

int AbstractValue::Add(int amount) const
{
	return value + amount;
}

AbstractValue::~AbstractValue()
{
	print("AbstractValue destroyed");
}

class ConcreteValue : public AbstractValue
{
public:
	int Get() const override
	{
		return Add(1);
	}

	ConcreteValue(int initial) : AbstractValue(initial)
	{
	}
	~ConcreteValue()
	{
		print("ConcreteValue destroyed");
	}
};

class Box<T>
{
public:
	Box(T initial);
	T Read() const;

	T value;
};

Box<T>::Box(T initial)
{
	value = initial;
}

T Box<T>::Read() const
{
	return value;
}

void main()
{
	ConcreteValue value(6);
	print(value.Get());

	Box<string> text("generic out-of-class definition");
	print(text.Read());
}
