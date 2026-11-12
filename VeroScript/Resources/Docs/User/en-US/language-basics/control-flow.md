# Control Flow

Control-flow statements determine the order in which statements execute. VeroScript supports conditions, loops, `switch`, jumps, and function returns.

## `if`

`if` selects a block based on a condition. `else if` and `else` are optional:

```VeroScript
if (score >= 60)
{
	print("pass");
}
else if (score >= 0)
{
	print("fail");
}
else
{
	print("invalid score");
}
```

The condition must be an expression that can be evaluated by the language's boolean rules. Braces are recommended even for a single statement because they make the branch boundary explicit.

## `while`

`while` checks its condition before every iteration:

```VeroScript
int count = 3;
while (count > 0)
{
	print(count);
	count -= 1;
}
```

If the condition is false initially, the loop body does not execute.

## `do ... while`

`do` executes its body once before checking the condition. The trailing semicolon is required:

```VeroScript
int count = 0;
do
{
	count += 1;
}
while (count < 3);
```

The condition must be `bool`. A `continue` in the body proceeds to the condition check, while a `break` exits the loop.

## `for`

A traditional `for` statement contains an initializer, a condition, and an iteration expression:

```VeroScript
for (int i = 0; i < 10; ++i)
{
	print(i);
}
```

A variable declared in the initializer is scoped to the `for` statement. The condition and iteration expression may be omitted, but the loop must still have a clear termination condition.

The initializer and iteration clauses may each contain a comma-separated expression list, evaluated from left to right. This is a `for`-clause feature; VeroScript does not provide a general comma operator. The initializer may also use a same-type declaration group such as `int index = 0, end = 3`; its names become visible from left to right and are scoped to the `for` statement.

## Range-based loops

Range-based loops use `:` to iterate over an array or container:

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

Use a reference loop variable to modify elements in place:

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

Map containers also support key/value loop forms; see the container reference for the exact forms.

## `break` and `continue`

`break` exits the nearest enclosing loop or `switch`. `continue` skips the rest of the current iteration and targets the nearest enclosing loop; a `switch` is not a `continue` target:

```VeroScript
for (int value : values)
{
	if (value < 0)
	{
		continue;
	}
	if (value == 100)
	{
		break;
	}
	print(value);
}
```

## `switch`

`switch` selects a `case` based on an expression. The current implementation accepts integers, enums, strings, and wide strings as switch values:

```VeroScript
switch (state)
{
case 0:
	{
		print("off");
		break;
	}
case 1:
	{
		print("on");
		break;
	}
default:
	{
		print("unknown");
		break;
	}
}
```

VeroScript does not allow unmarked fallthrough. Use `fallthrough` explicitly when execution should continue into the next `case`:

```VeroScript
switch (value)
{
case 1:
	{
		print("one");
	}
	fallthrough;
case 2:
	{
		print("one or two");
		break;
	}
default:
	{
		break;
	}
}
```

`fallthrough;` must be the final direct statement of its `case`. When grouping branch contents with braces, place it after that block, outside nested blocks, conditions, or loops.

## Exceptions

VeroScript separates script exceptions from engine and runtime faults. `throw
expression;` raises any non-`void` script value. A `try` statement must be inside
a function and must contain at least one `catch` or `finally` clause:

```VeroScript
int main()
{
	try
	{
		throw "failed";
	}
	catch (const string& message)
	{
		print(message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

Catch clauses are checked in source order. A typed handler may be written as
`catch (Type name)`, `catch (const Type& name)`, `catch (Type& name)`, or
`catch (ref Type name)`. The binding name may be omitted, as in `catch (Type)`.
`catch (...)` accepts any script exception, has no binding, and must be the last
catch clause. A typed handler matches the thrown value's type; class and
interface handlers also accept an assignable derived or implementing object.
Numeric conversions are not performed to make a catch match.

A by-value binding is a copy. A reference binding observes the original value,
so modifying a writable reference affects the value that can be propagated. A
bare `throw;` is valid only inside a catch handler and rethrows the original
exception object, not a modified by-value copy. A thrown value propagates
through calls until a matching handler is found.

`finally` may follow the handlers or appear without a `catch`. It runs when
control leaves the protected `try` or `catch`, including normal completion,
`return`, loop transfers, script-exception propagation, and runtime-fault
propagation. Non-local transfers out of `finally`, such as `return`, `goto`, or
a `break`/`continue` targeting an outer loop, are rejected by the compiler.

Loops and nested `try`/`catch` blocks inside `finally` are allowed. An exception caught there leaves the pending outer return or exception intact. An uncaught script exception leaving `finally` replaces that pending transfer and propagates to an enclosing handler. These rules also hold when cleanup invokes a destructor that calls script code again.

Engine/runtime faults are not script values and cannot be swallowed by
`catch (...)`. A failed `assert` is one of these runtime errors; `finally` still
runs while the fault propagates.

## `goto` and labels

A label consists of a name followed by a colon. `goto` jumps to that label:

```VeroScript
int main()
{
	goto done;

	print("not reached");

	done:
	print("done");
}
```

Reserve `goto` for local control flow that genuinely benefits from it; excessive jumps make a function harder to analyze.

## `return`

`return` ends the current function. A non-`void` function must return a value compatible with its declared type. A `void` function may return without an expression:

```VeroScript
int square(int value)
{
	return value * value;
}

void log_done()
{
	print("done");
	return;
}
```

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](control-flow.md) · [fr-FR](../../fr-FR/language-basics/control-flow.md) · [ja-JP](../../ja-JP/language-basics/control-flow.md) · [ko-KR](../../ko-KR/language-basics/control-flow.md) · [zh-CN](../../zh-CN/language-basics/control-flow.md) · [zh-TW](../../zh-TW/language-basics/control-flow.md)
