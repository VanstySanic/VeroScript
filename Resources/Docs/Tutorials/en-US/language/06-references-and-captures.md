# 06: References, reference returns, and lambda captures

This tutorial uses several small runnable programs to explain the difference between an alias to an existing value and a closure that shares outer state. References are useful for modifying an existing object or element in place; lambda captures determine how a closure stores an enclosing binding.

## Step 1: create a local reference alias

```VeroScript
int main()
{
	int score = 40;
	int& alias = score;
	alias += 2;

	print("score={}, alias={}", score, alias);
	return 0;
}
```

`alias` is not a second integer. It is a reference alias for `score`, so writing through `alias` changes `score`. A reference cannot be rebound to another integer after initialization; use an ordinary variable when you need an independent value.

## Step 2: pass a reference to a function

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	int value = 40;
	add_one(&value);
	print(value);
	return 0;
}
```

`int&` declares a writable reference parameter. The call uses `&value` to make the aliasing operation explicit. For read-only access, use `const int&`; it avoids a copy while preventing the function from mutating the object through that reference.

## Step 3: return a writable reference

A reference can also be a function return value, but the returned object must still exist after the call finishes:

```VeroScript
int counter = 1;

int& counter_ref()
{
	return counter;
}

int main()
{
	counter_ref() += 4;
	int& alias = counter_ref();
	alias += 1;
	print(counter);
	return 0;
}
```

This returns the global `counter`, so the caller can continue to modify it through the result. Do not return a reference to an ordinary local variable: that local has left its scope when the function returns. Container elements and object members can also be returned by reference while their owner remains valid.

## Step 4: modify a container through reference iteration

```VeroScript
int main()
{
	array<int> values = array<int>();
	values.push(1);
	values.push(2);

	for (int& value : values)
	{
		value += 10;
	}

	print(values[0]);
	print(values[1]);
	return 0;
}
```

`for (int& value : values)` makes `value` an alias for each array element, so assignments in the loop write back to the array. Without `&`, the loop variable receives a value and its changes do not write back. Passing an element by index uses the same explicit form, for example `add_one(&values[0])`.

## Step 5: let a lambda capture outer state

```VeroScript
function() -> int make_counter()
{
	int running = 40;
	return [&]() -> int
	{
		running++;
		return running;
	};
}

int main()
{
	function() -> int next = make_counter();
	print(next());
	print(next());
	return 0;
}
```

`[&]` captures used enclosing locals by reference. VeroScript promotes a captured local binding to safe shared storage, so after `make_counter` returns, `next` can still safely access and modify `running`.

If only one variable needs shared mutation, write `[&running]`. For a mixed policy, use `[=, &running]` so other variables are captured by value while only `running` is captured by reference.

## Step 6: capture a member-function receiver

Receiver capture follows the type's own semantics:

```VeroScript
class Counter
{
public:
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

struct Snapshot
{
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

int main()
{
	Counter counter = Counter();
	counter.value = 1;
	function() -> int live = counter.getter();
	counter.value = 2;

	Snapshot snapshot = Snapshot();
	snapshot.value = 3;
	function() -> int copied = snapshot.getter();
	snapshot.value = 4;

	print(live());
	print(copied());
	return 0;
}
```

The class closure retains the object and prints `2`; the struct closure keeps a
value snapshot and prints `3`. A captureless `[]` lambda cannot access `this`.

## Choosing between them

- Use an ordinary value or value capture when you need an independent copy.
- Use a writable reference parameter when a function must modify a value owned by its caller.
- Use a reference range variable when a loop must update container elements in place.
- Use a reference capture when a returned closure must continue sharing mutable outer state.
- Consider `const T&` for read-only access without copying a large object, while ensuring the referred object remains alive long enough.

## Exercises

1. Write `swap_values(int& left, int& right)` and call it with `swap_values(&a, &b)`.
2. Use a reference range loop over `array<int>` to multiply every element by `2`.
3. Add a value-captured `step` to `make_counter` and implement the `[=, &running]` policy.
4. Try returning a reference to an ordinary local, read the `--check` diagnostic, and change it to return a global or a still-valid member.
5. Change `Counter` to mutate its field after creating the closure, then compare it with the struct result.

See [References and reference semantics](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/references.md) and [Lambdas and function values](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/lambdas.md) for complete rules. Runnable examples are [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs), [`examples/reference_returns.vs`](../../../../../VeroScript/Resources/Scripts/examples/reference_returns.vs), and [`examples/lambda.vs`](../../../../../VeroScript/Resources/Scripts/examples/lambda.vs).

Next: [Modules, imports, and the preprocessor](07-modules-and-preprocessor.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](06-references-and-captures.md) · [fr-FR](../../fr-FR/language/06-references-and-captures.md) · [ja-JP](../../ja-JP/language/06-references-and-captures.md) · [ko-KR](../../ko-KR/language/06-references-and-captures.md) · [zh-CN](../../zh-CN/language/06-references-and-captures.md) · [zh-TW](../../zh-TW/language/06-references-and-captures.md)
