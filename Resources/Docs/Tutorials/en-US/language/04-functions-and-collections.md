# 04: Functions and collections

When `main` starts to grow, extracting code with a clear responsibility is usually more useful than adding more comments. This tutorial writes a sum function and stores a variable number of values in `array<int>`.

## Step 1: write a sum function

```VeroScript
int sum(array<int> values)
{
	int result = 0;
	for (int value : values)
	{
		result = result + value;
	}
	return result;
}

int main()
{
	array<int> values = array<int>();
	values.push(10);
	values.push(20);
	values.push(30);

	print("sum={}", sum(values));
	return 0;
}
```

A function declaration gives the return type, name, and parameters. `sum` receives an `array<int>` and returns an `int`.

## Step 2: let the collection grow

`array<int>()` creates an empty dynamic array, and `push` adds an element at the end. You can also use brace initialization:

```VeroScript
int main()
{
	array<int> values = array<int>{10, 20, 30};

	values.push(40);
	for (int& value : values)
	{
		value = value + 1;
	}

	print("size={}", len(values));
	print("first={}", values[0]);
	return 0;
}
```

The ordinary range loop `for (int value : values)` receives an element copy. Adding `&` creates an alias to the element, so changes are written back to the container.

## Step 3: use a default parameter

```VeroScript
void print_values(array<int> values, string label = "values")
{
	print(label);
	for (int value : values)
	{
		print(value);
	}
}

int main()
{
	array<int> values = array<int>{1, 2, 3};
	print_values(values);
	print_values(values, "numbers");
	return 0;
}
```

Default parameters let callers omit common configuration. The default must be compatible with the parameter type.

## Choosing a container

Start with `array<T>` for ordered, growable data. Consider `map<K, V>` for key lookup and `set<T>` for uniqueness. Do not add a more complex container before the access pattern requires it.

## Exercises

1. Write an `average` function that returns `0.0` for an empty array.
2. Write a function that changes every negative value to `0` using an alias range loop.
3. Store scores in `map<string, int>` and print the score for one name.

See [`examples/dynamic_array.vs`](../../../../../VeroScript/Resources/Scripts/examples/dynamic_array.vs) and [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs). The [Data types](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/data-types.md) page summarizes the available containers.

Next: [Structs, classes, and interfaces](05-structs-classes-and-interfaces.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](04-functions-and-collections.md) · [fr-FR](../../fr-FR/language/04-functions-and-collections.md) · [ja-JP](../../ja-JP/language/04-functions-and-collections.md) · [ko-KR](../../ko-KR/language/04-functions-and-collections.md) · [zh-CN](../../zh-CN/language/04-functions-and-collections.md) · [zh-TW](../../zh-TW/language/04-functions-and-collections.md)
