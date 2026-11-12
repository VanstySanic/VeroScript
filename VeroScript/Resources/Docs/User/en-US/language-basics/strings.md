# Strings

VeroScript provides two text types: `string` and `wstring`. They both represent text values, but they are distinct types and cannot be mixed in assignment, comparison, or concatenation without an explicit conversion. This page describes string behavior and the common helpers in `string.vs`; literal syntax is covered in [Literals](literals.md).

## Text types

Ordinary string literals have type `string`, while wide string literals have type `wstring`:

```VeroScript
string name = "VeroScript";
wstring title = L"Language reference";
```

Both `string` and `wstring` support `len`, equality comparison, and concatenation with values of the same text type. The operands of a concatenation must belong to the same text type:

```VeroScript
string message = "Hello, " + name;
wstring heading = L"Vero" + L"Script";
```

Use `to_wstring` or `to_string` for an explicit conversion between the two text types:

```VeroScript
wstring wide_name = to_wstring(name);
string narrow_name = to_string(wide_name);
```

Use `to_string` when converting numeric or boolean values to text as well. This keeps the conversion explicit instead of relying on implicit mixed-type concatenation:

```VeroScript
string summary = "count=" + to_string(3);
```

## String standard library

String helpers are provided by `string.vs`. Import the module before using them:

```VeroScript
import "string.vs";
```

Common functions include:

| Function | Description |
| --- | --- |
| `std::strings::substring(s, start, count)` | Extracts a specified number of characters starting at `start`. |
| `std::strings::find(s, needle)` | Finds a substring and returns its position; returns a negative value when not found. |
| `std::strings::char_code(s, index)` | Returns the character code at an index. |
| `std::strings::contains(s, sub)` | Tests whether a substring is present. |
| `std::strings::starts_with(s, prefix)` | Tests whether a string starts with a prefix. |
| `std::strings::ends_with(s, suffix)` | Tests whether a string ends with a suffix. |
| `std::strings::trim_left(s)` / `std::strings::trim_right(s)` | Removes whitespace from the left or right side. |
| `std::strings::trim(s)` | Removes whitespace from both sides. |
| `std::strings::split(s, separator)` | Splits a string into an `array<string>`. |
| `std::strings::join(parts, separator)` | Joins a string array using a separator. |
| `std::strings::to_lower(s)` / `std::strings::to_upper(s)` | Converts text to lower or upper case. |
| `std::strings::replace(s, from, to)` | Replaces matching substrings with the specified text. |
| `std::strings::last_find(s, needle)` | Returns the final matching byte offset, or `-1` when absent. |
| `std::strings::repeat(s, count)` | Repeats `s`; a negative count is a runtime error. |
| `std::strings::remove_prefix(s, prefix)` / `std::strings::remove_suffix(s, suffix)` | Removes a matching prefix or suffix, otherwise returns `s`. |

These functions return new values and do not modify the input string. For example:

```VeroScript
import "string.vs";

string raw = "  red,green,blue  ";
array<string> colors = std::strings::split(std::strings::trim(raw), ",");
string normalized = std::strings::join(colors, " | ");
print(normalized);
```

`trim_left`, `trim_right` and `trim` remove ASCII whitespace bytes `0x09` through `0x0D` and space (`0x20`). String indices are zero-based UTF-8 byte offsets, not Unicode code-point indices. `len`, `find` and `last_find` return `int64`; no match is `-1`. Slice offsets/counts, `char_code` indices and `repeat` counts use `int64`. See [String module](../standard-library/string.md) for bounds and return types.

## Formatting

VeroScript provides two independent formatting dialects. `format` uses brace fields; `formatf` and `printf` use printf-style conversions. Do not mix the two dialects in one format string.

### Brace formatting

The public signature in `string.vs` is:

```text
string format(string fmt, array<string> args);
```

Script code normally passes scalar arguments directly. The compiler converts `string`, booleans, floating-point values, and signed or unsigned integers to strings and collects them into the argument array:

```VeroScript
import "string.vs";

string message = format("name={}, score={}", "Ada", 97);
print(message);
```

If an `array<string>` already exists, it can be passed directly as the second argument:

```VeroScript
import "string.vs";

array<string> args = array<string>();
args.push("Ada");
args.push("97");
string message = format("name={}, score={}", args);
```

The field forms are:

| Form | Meaning |
| --- | --- |
| `{}` | Uses the next automatic argument, starting at index `0`. |
| `{N}` | Uses an explicit zero-based argument index, such as `{0}`. |
| `{:spec}` | Uses the next automatic argument with a format specification. |
| `{N:spec}` | Uses an explicit index with a format specification. |
| `{{` / `}}` | Emits a literal `{` / `}`. |

The format specification has the form `[alignment][width][.precision][type]`:

| Part | Accepted values | Meaning |
| --- | --- | --- |
| Alignment | `<`, `>`, `^` | Left, right, or center alignment; right alignment is the default. |
| Width | Decimal integer from `0` to `1024` | Pads a short result with spaces; it never truncates the value. |
| Precision | `.decimal-integer`, from `0` to `1024` | Parses a finite numeric value and emits fixed-point output with that many digits. |
| Type | `x`, `X` | Converts a decimal integer to lower- or upper-case hexadecimal. |

The `x`/`X` conversion in `format` parses a signed 32-bit decimal integer and then emits its 32-bit hexadecimal representation. Use `%x` or `%X` with `formatf` when the full unsigned 64-bit range is required.

For example:

```VeroScript
import "string.vs";

string right = format("{:>6}", "ok");   // "    ok"
string left = format("{:<6}", "ok");    // "ok    "
string center = format("{:^6}", "ok");  // "  ok  "
string fixed = format("{:.2}", "1.5");  // "1.50"
string hex = format("{:X}", "255");     // "FF"
string repeated = format("{0} / {0}", "same");
string braces = format("{{value}}", array<string>()); // "{value}"
```

The automatic index advances only when an automatic field is used. To avoid surprising results, use either automatic or explicit indexing consistently within one format string. Missing arguments, unmatched braces, invalid specifications, and values that cannot be parsed for a numeric specification produce runtime errors. Extra arguments that are not referenced by a field are ignored.

### printf formatting

The public signature of `formatf` is:

```text
string formatf(string fmt, array<string> args);
```

Script code can also pass scalar arguments directly. `printf` uses the same grammar but writes directly to standard output. A conversion has the following shape:

```text
%[flags][width][.precision][length]conversion
```

| Part | Accepted values or limits |
| --- | --- |
| Flags | `-` left-aligns, `+` shows a positive sign, space prefixes positive numbers with a space, `#` enables alternate prefixes, and `0` enables zero padding. |
| Width | Decimal integer from `0` to `1024`; dynamic width `*` is not supported. |
| Precision | `.digits`, from `0` to `1024`; dynamic precision `.*` is not supported. |
| Length | `h`, `hh`, `l`, `ll`, `j`, `z`, and `t` are accepted for source compatibility, but do not change VeroScript value width or ABI. |
| Conversion | `d`, `i`, `u`, `o`, `x`, `X`, `f`, `F`, `e`, `E`, `g`, `G`, `c`, and `s`. |

The conversions require these argument forms:

| Conversion | Argument and result |
| --- | --- |
| `%d`, `%i` | Signed integer. |
| `%u` | Non-negative integer. |
| `%o` | Octal integer. |
| `%x`, `%X` | Lower- or upper-case hexadecimal integer. |
| `%f`, `%F` | Fixed-point floating point; default precision is `6`. |
| `%e`, `%E` | Scientific notation; upper-case variants use upper-case letters. |
| `%g`, `%G` | Default floating-point representation; upper-case variants use upper-case letters. |
| `%c` | An integer from `0` to `255`, or a one-byte string; precision is invalid. |
| `%s` | A string; precision limits the number of emitted bytes. |
| `%%` | Emits a literal `%` without consuming an argument. |

`%c` and `%s` allow only the `-` flag and width; for numeric conversions, precision takes precedence over integer zero padding. A trailing `%`, missing arguments, unsupported conversions, or values that cannot be parsed for the requested conversion produce runtime errors. Floating-point conversions require finite numeric values. Extra arguments that are not consumed by a conversion are ignored.

```VeroScript
import "string.vs";

string text = formatf("d=%+06d x=%#08X f=%.2f s=%-5.3s c=%c", -42, 255, 3.14159, "abcdef", 65);
printf("%s%% done", text);
```

### Relationship to output functions

```VeroScript
import "string.vs";

print("name={}, score={}", "Ada", 97); // print's multi-argument form uses braces
printf("score=%03d", 7);                // printf always uses printf syntax
string value = format("score={}", 7);   // use format when a string is needed
```

With one value, `print` writes it directly; with a format string and additional arguments, it uses brace formatting. `format` and `formatf` return strings, while `printf` writes to standard output. `format`, `formatf`, and `printf` do not accept reference arguments or `void` expressions as formatting arguments.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](strings.md) · [fr-FR](../../fr-FR/language-basics/strings.md) · [ja-JP](../../ja-JP/language-basics/strings.md) · [ko-KR](../../ko-KR/language-basics/strings.md) · [zh-CN](../../zh-CN/language-basics/strings.md) · [zh-TW](../../zh-TW/language-basics/strings.md)
