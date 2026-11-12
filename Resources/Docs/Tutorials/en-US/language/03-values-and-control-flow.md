# 03: Values, conditions, and loops

This tutorial combines variables, static types, branching, and repetition in a small score calculator. The goal is to learn to split a problem into state, decisions, and repeated actions.

## Step 1: prepare the state

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	print("total={}", total);
	return 0;
}
```

`total` and `count` are both `int`. `i` exists only inside the `for` loop; a local scope keeps temporary state from leaking into unrelated code.

## Step 2: add a decision

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	if (total >= 15)
	{
		print("pass: {}", total);
	}
	else
	{
		print("retry: {}", total);
	}
	return 0;
}
```

The condition of `if` must produce a boolean result. Comparisons produce `bool`; use explicit comparisons instead of relying on an integer being treated as a condition.

## Step 3: move the data into an array

```VeroScript
int main()
{
	int scores[4] = {72, 88, 91, 65};
	int total = 0;

	for (int i = 0; i < len(scores); i = i + 1)
	{
		total = total + scores[i];
	}

	double average = to_double(total) / len(scores);
	print("average={}", average);
	return 0;
}
```

`scores` is a fixed-size native array. Its indexes start at `0`, and `len(scores)` returns its length. Use `array<int>` when the number of elements must change at runtime.

## Step 4: use fractional literals

The integer part can be omitted from a floating-point literal when `.` is immediately followed by a decimal digit. The suffix still determines the storage type: `.5f` is a `float`, while `.5` and `.5e2` are `double` values.

```VeroScript
int main()
{
	float ratio = .5f;
	double half = .5;
	double fifty = .5e2;

	print("ratio={}, half={}, fifty={}", ratio, half, fifty);
	return 0;
}
```

`0.5f` and `.5f` have the same value and type. Keep the decimal digit after the dot; a standalone `.` is still used for member access.

## Step 5: handle a script exception

Use `throw` for a failure that the script can describe and recover from. The
exception value can cross a function call and be handled by a matching `catch`.
`finally` is for cleanup that must run whether the operation succeeds or leaves
through an exception:

```VeroScript
void validate_score(int score)
{
	if (score < 0)
	{
		throw "score must not be negative";
	}
}

int main()
{
	try
	{
		validate_score(-1);
	}
	catch (const string& message)
	{
		print("handled: {}", message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

This prints the handled message and then `cleanup`. Use a typed handler when
the exception type matters; use `catch (...)` only as a final fallback. A bare
`throw;` inside a handler rethrows the original value.

For a complete exception example, see [`examples/exceptions.vs`](../../../../../VeroScript/Resources/Scripts/examples/exceptions.vs).

## Step 6: check an invariant with `assert`

`assert(condition[, message])` is a runtime check. The condition must be
`bool`, and the optional message must be `string`:

```VeroScript
int main()
{
	int total = 15;
	assert(total >= 0, "total must be non-negative");
	print("ready");
	return 0;
}
```

A true condition continues without output. A false condition reports a runtime
error and stops the current execution; it is not a script exception, so
`catch (...)` cannot handle it.

## The control-flow ideas you used

- `if` chooses a path based on a condition.
- `for` repeats work with an explicit initialization, condition, and increment.
- `try`/`catch` handles script values raised by `throw`, and `finally` performs cleanup.
- `assert` checks a boolean invariant and reports a runtime error when it fails.
- `return` exits a function with a result; the next tutorial uses it heavily.

## Exercises

1. Calculate the highest and lowest score.
2. Count only scores greater than or equal to `60`.
3. Replace the fixed array with `array<int>` and use range-based iteration.
4. Replace a decimal literal such as `0.5` with `.5`, then use `.5f` where a `float` value is required.
5. Add a typed `catch` and a `finally` block to a function that rejects a negative score.
6. Add an `assert` with a message for an invariant in your score calculation.

For a complete array example, see [`examples/arrays.vs`](../../../../../VeroScript/Resources/Scripts/examples/arrays.vs). The reference pages are [Variables](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/variables.md), [Control flow](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/control-flow.md), and [Arrays](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/arrays.md).

Next: [Functions and collections](04-functions-and-collections.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](03-values-and-control-flow.md) · [fr-FR](../../fr-FR/language/03-values-and-control-flow.md) · [ja-JP](../../ja-JP/language/03-values-and-control-flow.md) · [ko-KR](../../ko-KR/language/03-values-and-control-flow.md) · [zh-CN](../../zh-CN/language/03-values-and-control-flow.md) · [zh-TW](../../zh-TW/language/03-values-and-control-flow.md)
