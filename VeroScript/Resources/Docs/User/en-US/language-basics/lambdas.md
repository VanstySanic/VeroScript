# Lambdas and function values

A lambda is a function value that can be stored, passed, and invoked. Function types use the `function` spelling; `fn` is its compact alias.

## Function types

A function type combines its parameter list and return type. Keep spaces around the arrow in public code so longer declarations remain readable:

```VeroScript
function(int) -> int increment = function(int value) -> int => value + 1;
function() -> string get_name = function() -> string => "VeroScript";
function(int&, int) -> void mutate = function(int& value, int amount) -> void
{
	value += amount;
};
```

Function values can be parameters, return values, or local variables:

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

function(int) -> int double_value = function(int x) -> int => x * 2;
int result = apply(double_value, 21);
```

`fn(int) -> int` and `function(int) -> int` are equivalent. Public documentation and longer declarations generally use the full `function` spelling.

## Lambda expressions

A lambda can use either an expression body or a block body:

```VeroScript
function(int) -> int increment = function(int x) -> int => x + 1;

function(int) -> int square = function(int x) -> int
{
	return x * x;
};
```

Invoke a function value with the same parenthesis syntax used for an ordinary function:

```VeroScript
int a = increment(41);
int b = square(5);
```

When a function type has a reference parameter, the call must use an explicit reference marker. Public examples prefer `T&` and `&expression`; `ref T` and `ref expression` remain supported spellings:

```VeroScript
void add_to(int& value, int amount)
{
	value = value + amount;
}

function(int&, int) -> void mutate = add_to;
int value = 40;
mutate(&value, 2);
```

## Value captures

A lambda can capture local variables from an enclosing scope. An ordinary lambda and `[=]` request value capture; an explicit list captures only the names listed:

```VeroScript
int base = 40;

function(int) -> int add = function(int x) -> int => x + base;
function(int) -> int add_explicit = [base](int x) -> int => x + base;
```

Value captures initialize when the closure is created and are readonly by default. Put `mutable` after the parameter list to modify owned captures and retain changes across calls. Copying a function value shares its environment; evaluating the creation expression again creates an independent environment. A const source remains const for an ordinary capture.

VM, Mixed, JIT and Core-backed AOT use the same runtime services. Standalone C++/C# source export supports its value captures and scalar local statics; initialized reference captures, complex static objects and references to enclosing statics require a Core path. Python source export still rejects lambdas and local statics.

Standalone C++ export also requires Core AOT when a lambda returns a reference and owns captures or a receiver: its native reference cannot retain the closure's storage after the function value is released.

An empty capture list `[]` captures no enclosing locals, while global names remain available through normal name lookup:

```VeroScript
function(int) -> int increment = [](int x) -> int => x + 1;
```

## Reference captures

VeroScript supports reference captures. `[&]` captures the enclosing local variables it uses by reference, and `[&name]` captures only the named variable by reference. A default capture can be combined with explicit value captures:

```VeroScript
int running = 40;
function() -> int next = [&]() -> int
{
	running++;
	return running;
};

print(next());
print(running);

int step = 2;
function() -> int advance = [=, &running]() -> int
{
	running += step;
	return running;
};
```

The available capture forms are:

- `[&]`: capture used enclosing locals by reference.
- `[&name]`: capture `name` by reference.
- `[=]` or an ordinary `function(...)` lambda: capture used enclosing locals by value.
- `[=, &name]`: capture by value by default, except for `name`.
- `[&, name]`: capture by reference by default, except for `name`.
- `[]`: capture no enclosing locals.

A reference-captured local is promoted to safe shared storage. A closure can therefore outlive the function that created it without retaining a dangling stack slot; the enclosing scope, another closure over the same binding, and the current closure observe the same mutable state. If the enclosing binding is const, the reference capture preserves its read-only nature.

Names in an explicit capture list must refer to permitted enclosing bindings. A capture cannot redundantly repeat a form already covered by a default capture. An empty capture list cannot access an enclosing local; globals and object members remain subject to their own name lookup and handle semantics.

## Returning a lambda from a member function

A member function can return a function value that captures the current object state:

```VeroScript
class Counter
{
public:
	int value;

	Counter(int initial)
	{
		value = initial;
	}

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

Counter counter = Counter(42);
function() -> int get = counter.getter();
int current = get();
```

Here `value` is an object member rather than an ordinary local that has gone out of scope. The closure still follows class-handle and object-lifetime semantics; reference capture changes how a binding is accessed, not whether an arbitrary object is automatically copied as a value.

If a lambda uses the receiver itself, the receiver follows the enclosing type's
semantics. A class receiver is retained as a shared handle, so the closure can
observe later field mutations. A struct receiver is captured as a value
snapshot, so later mutations to the original struct do not change the captured
receiver. Nested lambdas retain or copy the already captured receiver according
to the same rule. A captureless lambda cannot use `this`.

## Relationship to references

The call rules for reference parameters, reference returns, and safe reference sources are covered in [References and reference semantics](references.md). Reference capture is a closure rule and cannot be replaced with ordinary value capture.

See the Lambda entry in the [Examples index](../examples.md) for a complete example.

## Initialized captures, mutable and local statics

With `auto`, write an explicit Lambda return type, such as `() mutable -> void { ... }`. An expected function type such as `function() -> void` allows the Lambda return arrow to be omitted. This follows the existing contextual typing rule; `mutable` does not infer a return type from the body.

For local static constants, `static const int value = 42;` and `const static int value = 42;` are equivalent. Prefer `static const` for consistent spelling.

```VeroScript
function() -> int make_counter()
{
	return [count = 0]() mutable -> int { return ++count; };
}

int remember_first(int seed)
{
	static int value = seed;
	return ++value;
}

void capture_example()
{
	auto next = make_counter();
	auto alias = next;
	assert(next() == 1 && alias() == 2);
	int total = 0;
	auto advance = [&target = total, step = 2]() -> int { target += step;
		return target; };
	assert(advance() == 2 && total == 2);
	assert(remember_first(10) == 11 && remember_first(99) == 12);
}
```

Capture initializers evaluate once, from left to right, in the enclosing scope. New names are visible only in the body. `[&name = expression]` requires a retainable lvalue and preserves source constness. Globals, type static fields and local statics remain direct name lookups; use an initialized capture to snapshot them.

A local static initializes on first reach and is shared by later calls. Closures from the same lambda expression share its function statics. Failed initialization can retry after successful cleanup; recursive initialization reports an error. Program states, sessions and workers are isolated. Clearing state starts again. Static reference variables are unsupported.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](lambdas.md) · [fr-FR](../../fr-FR/language-basics/lambdas.md) · [ja-JP](../../ja-JP/language-basics/lambdas.md) · [ko-KR](../../ko-KR/language-basics/lambdas.md) · [zh-CN](../../zh-CN/language-basics/lambdas.md) · [zh-TW](../../zh-TW/language-basics/lambdas.md)
