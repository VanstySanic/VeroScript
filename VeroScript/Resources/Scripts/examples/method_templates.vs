// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// A method owns U independently of its owner's T. Prefix and suffix syntax agree.
template<typename T>
class Box
{
public:
	template<class U = T>
	U echo(U value)
	{
		return value;
	}

	static U make<U = T>(U value)
	{
		return value;
	}
};

template<typename T>
T identity(T value)
{
	return value;
}

void main()
{
	Box<int> box = Box<int>();
	print(box.echo(7));
	print(box.echo<string>("method"));
	print(Box<int>::make(9));
	fn(int) -> int free_function = identity<>;
	auto bound_method = box.echo<string>;
	print(free_function(11));
	print(bound_method("bound"));
}
