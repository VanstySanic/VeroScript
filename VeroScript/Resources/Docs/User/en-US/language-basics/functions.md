# Functions

A function is a named callable declaration with a parameter list and a return type. Functions can be defined at module scope or as members of a struct, class, or interface.

## Function declarations

A function declaration contains a return type, a name, a parameter list, and a body:

```VeroScript
int add(int left, int right)
{
	return left + right;
}
```

`void` indicates that the function does not return a value:

```VeroScript
void print_message(string message)
{
	print(message);
}
```

## The `main` entry point

An application entry point may return `int` or `void` and may take at most one `string[]` parameter. An `int` return value becomes the process exit code; `void` is treated as `0`. The entry parameter cannot be a reference, variadic, or defaulted parameter:

```VeroScript
int main(string[] args)
{
	if (len(args) == 0)
	{
		return 2;
	}
	print(args[0]);
	return 0;
}
```

Only the entry module may provide `main`; a library module cannot define an application entry point.

When `int main` returns normally, or reaches its closing brace without an explicit return, the result code is the returned value or `0`. `exit(n)` takes precedence over the `main` return value, and `exit(0)` still records that the script explicitly requested an exit. For engine APIs, a non-zero `ExitCode` does not by itself mean that the engine failed; a host should inspect `bSuccess`, `ExitCode`, and `bExitRequested` together.

Parameters and local variables in the function body follow the normal function and block-scope rules.

## Parameters and return values

Arguments are matched to parameters in declaration order. Their types must satisfy the language's conversion rules:

```VeroScript
double average(double total, int count)
{
	return total / count;
}
```

The type of a `return` expression must be compatible with the function's return type. A `void` function can use `return` without an expression to end execution early.

## Default arguments

A parameter can provide a default value. If the caller omits that argument, the declared default expression is used:

```VeroScript
int clamp(int value, int minimum = 0, int maximum = 100)
{
	if (value < minimum)
	{
		return minimum;
	}
	if (value > maximum)
	{
		return maximum;
	}
	return value;
}

int main()
{
	print(clamp(120));
	print(clamp(-1, -10, 10));
}
```

Default parameters should form a trailing sequence in the parameter list. Do not place a required parameter after an optional one.

## Function overloading

Multiple functions with the same name can be declared in one scope when their parameter lists distinguish them:

```VeroScript
int abs_value(int value)
{
	return value < 0 ? -value : value;
}

double abs_value(double value)
{
	return value < 0.0 ? -value : value;
}
```

When a function is called, the compiler selects an overload using the name, argument count, argument types, and available conversions. If multiple candidates match equally well, compilation fails with an overload ambiguity.

## Reference parameters

Reference parameters allow a function to modify an object owned by its caller. Public examples prefer `T&`; `ref T` is the equivalent keyword-style spelling:

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
}
```

The argument uses the matching `&` or `ref` marker at the call site. VeroScript has no general-purpose script pointer type, and references do not support pointer arithmetic.

## Variadic parameters

A function can use `T... name` to declare a sequence of variadic arguments with one element type:

```VeroScript
void print_all(string... values)
{
	for (string value : values)
	{
		print(value);
	}
}
```

Inside the function, the variadic parameter behaves as an array of its element type. A variadic parameter must be last.

## Function values and lambdas

Functions can be passed as values, and lambdas can create anonymous functions. A function type describes its parameter list and return type:

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

int main()
{
	function(int) -> int increment = function(int value) -> int => value + 1;
	print(apply(increment, 41));
}
```

`fn` is a shorthand spelling for `function`. Capture rules for function values and lambdas, as well as generic functions and member functions, are covered in dedicated references.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](functions.md) · [fr-FR](../../fr-FR/language-basics/functions.md) · [ja-JP](../../ja-JP/language-basics/functions.md) · [ko-KR](../../ko-KR/language-basics/functions.md) · [zh-CN](../../zh-CN/language-basics/functions.md) · [zh-TW](../../zh-TW/language-basics/functions.md)
