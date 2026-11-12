# Built-ins

See [Standard library foundation](foundation.md#foundation-imports).

The boundary between built-ins and the [standard-library modules](index.md) is intentional: use built-ins for the smallest runtime primitives, and prefer the corresponding modules for higher-level string, file, path, concurrency, and event functionality.

## Output

| Function | Description |
| --- | --- |
| `print(value)` | Writes one value to standard output. With a format string and additional arguments, it uses brace formatting; `{{` and `}}` emit literal braces. |
| `printf(format, ...)` | Writes output formatted with printf-style conversions; its format syntax is different from `print`. |
| `eprint(value)` | Writes one value to the error stream. |

```VeroScript
int main()
{
	int score = 97;
	print("score={}", score);
	printf("score=%03d", score);
	eprint("diagnostic");
	return 0;
}
```

Use the standard-library `format` or `formatf` functions when the formatted result must be stored in a variable. See [String language behavior](../language-basics/strings.md#formatting) and the [string module](string.md) for fields, conversions, width, precision, and error boundaries.

## Console streams

The `std::console` functions are runtime-provided and do not require an
import. They are namespace-only so that their stream and interactive-terminal
semantics remain explicit:

| Function | Description |
| --- | --- |
| `std::console::write(value)` | Writes one value to standard output without adding a newline. |
| `std::console::error(value)` | Writes one value to the error stream without adding a newline. |
| `std::console::flush()` | Flushes pending standard output. |
| `std::console::clear()` | Clears interactive output when the host supports it; returns whether the operation was performed. |
| `std::console::is_interactive()` | Returns whether both standard input and standard output are interactive. |

Use `std::console::flush()` when a progress fragment must be visible before
the next newline or before the script yields to a host tool. Debug adapters
forward flushed standard output and error output as separate output events.

### Terminal controls

| API | |
| --- | --- |
| `supports_color()`, `supports_cursor()`, `supports_clear()`, `is_output_terminal()` | Capability probes: color, cursor, screen/line clearing and output terminal. |
| `set_color(string foreground, string background)`, `reset_style()` | Set foreground/background; reset style. |
| `clear_line(string mode)` | Clear `all`, `before` or `after` the cursor, including its current cell; the cursor stays in place. |
| `move_cursor(int column, int row)`, `set_cursor_visible(bool visible)` | Move to zero based `(column, row)`; show/hide the cursor. |
| `width()`, `height()` | Visible columns/rows; `0` when unknown. |

Controls return `bool`: `false` means unsupported or failed. Redirected output never receives automatic terminal escapes. Input need not be interactive for output controls. `clear()` clears the visible screen and moves to its origin; it does not delete scrollback. Coordinates must be integers in `0..32766`; positions outside a known visible window return `false`. Invalid types, color names and line modes are errors, even on a plain stream. All mutations require console output permission; queries are read only.

Colors are `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`, their `bright_` variants and `default`. Supply both foreground and background. At the outer execution boundary, changed style and cursor visibility are restored even after errors, cancellation or instruction exhaustion. Restoration is best effort when output itself fails. POSIX/custom hosts restore default style and a visible cursor. Windows VM and all three AOT targets restore the original native state.

Embedded log/UI sinks expose no terminal controls by default. C++ hosts can override `QueryConsole`/`ExecuteConsole`; the C ABI session options and C#/Python options offer corresponding query/control callbacks. Optional exact text callbacks receive `write()` text immediately without an added newline. Keep callbacks alive until session disposal; worker callbacks may run concurrently. Mobile and browser capture sinks stay plain by default. See `console_demo.vs` in the examples.

```VeroScript
if (std::console::supports_color())
{
	std::console::set_color("bright_green", "default");
}
std::console::write("Ready\n");
std::console::flush();
std::console::reset_style();
```

## Conversions

| Function | Description |
| --- | --- |
| `to_int(value)` | Converts to `int`. Floating-point values are truncated toward zero; out-of-range and non-finite values are errors. |
| `to_double(value)` | Converts to `double`. |
| `to_float(value)` | Converts to `float`; the result is stored as binary32. |
| `to_unsigned(value)` | Converts to `unsigned int`, rejecting negative, out-of-range, and invalid values. Floating-point values are truncated toward zero. |
| `to_int64(value)` | Converts to `int64` with range checking. |
| `to_uint64(value)` | Converts to `uint64` with range checking. |
| `to_string(value)` | Converts a value to a string. |
| `to_wstring(value)` | Explicitly converts a `string` or `wstring` value to a wide string. |

```VeroScript
int main()
{
	int count = to_int(12.75);
	double ratio = to_double(3);
	unsigned int flags = to_unsigned(true);
	string text = to_string(count);
	wstring wide = to_wstring(text);
	print("count={}, ratio={}, flags={}", count, ratio, flags);
	print(wide);
	return 0;
}
```

Conversion functions do not parse arbitrary text as numbers. Use `std::io::read_int` or `std::io::read_double` for numeric input; for file or protocol text, define the parsing rules explicitly in the script.

## Numeric and collection helpers

| Function | Description |
| --- | --- |
| `abs(value)` | Returns the absolute value of a numeric value. |
| `min(a, b)` | Returns the smaller of two numeric values. |
| `max(a, b)` | Returns the larger of two numeric values. |
| `len(value)` | Returns the element count of a string, array, or standard container. For a multiset, it returns the total number of occurrences. |
| `range(int64 count)` | Accepts an `int64` count and returns an `array<int>` containing `0` through `count - 1`. A negative count produces an empty array; a count greater than `1000000` is a runtime error. |

```VeroScript
int main()
{
	array<int> values = range(4);
	print("size={}", len(values));
	print("low={}, high={}, distance={}", min(-2, 5), max(-2, 5), abs(-9));
	return 0;
}
```

`len(value)` returns a nonnegative `int64` (`0` through `9223372036854775807`). Null values, unsupported types and unrepresentable lengths report a runtime error. Existing container `size`, `capacity` and element-count queries also return `int64`. Use `int64` for lengths and linear indices. Assigning a result to `int` performs the ordinary checked numeric conversion and fails if the value does not fit; a wider type does not remove native allocation or backend container limits.

## Environment, time, and control flow

| Function | Description |
| --- | --- |
| `std::environment::getenv(name)` | Reads a host-process environment variable; a missing variable returns an empty string. The name must not be empty. |
| `std::environment::setenv(name, value)` | `setenv(name, "")` stores an existing empty value. Use unsetenv to delete; deleting an absent variable succeeds. try_getenv distinguishes absent from empty. Names must be nonempty, without = or NUL; values must not contain NUL. Windows environment names retain platform case-insensitive semantics; POSIX names remain case-sensitive. |
| `std::chrono::now()` | Returns the number of seconds, as a `double`, since the Unix epoch according to the system clock. |
| `std::chrono::steady_now()` | Returns a monotonic clock value in seconds. Use differences between readings for elapsed-time measurements; it is not a wall-clock timestamp. |
| `std::this_thread::sleep(milliseconds)` | Suspends the current script thread for the specified number of milliseconds. The argument must be non-negative. |
| `assert(condition[, message])` | Requires a `bool` condition and an optional `string` message. A `false` condition reports a runtime error; a `true` condition produces no output. |
| `exit(code)` | Requests script termination with the specified integer exit code. |

```VeroScript
int main()
{
	std::environment::setenv("VERO_MODE", "docs");
	print(std::environment::getenv("VERO_MODE"));

	double started = std::chrono::steady_now();
	std::this_thread::sleep(1);
	assert(std::chrono::steady_now() >= started);
	exit(0);
	return 0;
}
```

`assert` accepts one or two arguments. The first argument must be `bool`; when
present, the second argument must be `string`. On failure, a non-empty message
is included in the diagnostic. An empty message uses the default assertion
diagnostic. The failure is a runtime error, not a script exception, so it is not
caught by `catch (...)` or another typed handler.

Environment variables belong to the host process. Whether a change affects later host logic depends on the host platform and embedding mode.

## Standard input

| Function | Description |
| --- | --- |
| `std::io::read_token()` | Reads the next whitespace-delimited token from standard input. |
| `std::io::read_string()` | Reads the next string token. |
| `std::io::read_int()` | Reads and parses the next integer token. |
| `std::io::read_double()` | Reads and parses the next floating-point token. |
| `std::io::read_bool()` | Reads and parses `true` or `false`. |

End of input or an invalid token produces a diagnostic. These functions are intended for simple command-line input; more complex input protocols belong in the host or in the file-oriented standard-library APIs.

## Implementation-only names

Names beginning with `__` (for example, `__read_file` and `__format`) are reserved for the compiler, runtime, and standard-library implementation. Ordinary script modules cannot call them; use the public functions listed in the [standard-library reference](index.md).

- [Standard library foundation](foundation.md)

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](builtins.md) · [fr-FR](../../fr-FR/standard-library/builtins.md) · [ja-JP](../../ja-JP/standard-library/builtins.md) · [ko-KR](../../ko-KR/standard-library/builtins.md) · [zh-CN](../../zh-CN/standard-library/builtins.md) · [zh-TW](../../zh-TW/standard-library/builtins.md)
