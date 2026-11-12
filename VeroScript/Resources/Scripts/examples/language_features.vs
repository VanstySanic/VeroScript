// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Language showcase: constraints, boxing, overloads, defaults, and variadics.
// Run from this directory: veroscript language_features.vs

interface ISpeak
{
	virtual void speak() = 0;
};

// Value-copy box: ISpeak x = s; methods via I do not mutate the original struct.
struct Point : ISpeak
{
	void speak()
	{
		print("Point({})", n);
	}
	void add(int amount)
	{
		this.n = this.n + amount;
	}

	// Inside an instance method, an unresolved bare call falls back to implicit `this`.
	void bump()
	{
		add(1);
	}

	int n;
};

class Base1<T>
{
public:
	T a;
};

class Base2<T>
{
public:
	T b;
};

class PairBox<T> : public Base1<T>, public Base2<T>
{
public:
};

// Generic class constraint.
class SpeakerBox<T> where T : ISpeak
{
public:
	void shout()
	{
		item.speak();
	}

	T item;
};

void show()
{
	print("show#0");
}
void show(int x)
{
	print("show#int {}", x);
}
void show(string s)
{
	print("show#string {}", s);
}

void greet(string name = "world")
{
	print("hello, {}", name);
}

// Generic function constraint.
void speakAll<T>(T x) where T : ISpeak
{
	x.speak();
}

void sum(int... xs)
{
	int t = 0;
	for (int64 i = 0; i < len(xs); i = i + 1)
	{
		t = t + xs[i];
	}
	print("sum={}", t);
}

void main()
{
	// Free-function overloads + default parameters
	show();
	show(42);
	show("ok");
	greet();
	greet("VeroScript");
	sum(1, 2, 3);

	// Struct-to-interface boxing copies the value.
	Point p = Point();
	p.n = 1;
	ISpeak x = p;
	x.speak();
	speakAll(p);

	// Calling through the original struct still mutates that value.
	p.bump();
	print("p.n after bump = {}", p.n);

	// A generic class may inherit multiple specialized generic bases.
	PairBox<int> box = PairBox<int>();
	box.a = 3;
	box.b = 4;
	Base1<int> b1 = box;
	Base2<int> b2 = box;
	print("bases {} / {}", b1.a, b2.b);

	SpeakerBox<Point> sb = SpeakerBox<Point>();
	sb.item = p;
	sb.shout();
}
