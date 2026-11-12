# String module

Module file: `string.vs`

Import it with:

```VeroScript
import "string.vs";
```

## Queries and slicing

| Function | Description |
| --- | --- |
| `std::strings::substring(string s, int64 start, int64 count)` | Returns up to `count` UTF-8 bytes starting at byte offset `start`. |
| `std::strings::find(string haystack, string needle)` | Returns the first matching UTF-8 byte offset as `int64`, or `-1` when absent. |
| `std::strings::char_code(string s, int64 index)` | Returns the byte at `index` as `int` (`0`–`255`). |
| `std::strings::contains(string s, string sub)` | Tests whether a substring is present. |
| `std::strings::starts_with(string s, string prefix)` | Tests whether a string starts with a prefix. |
| `std::strings::ends_with(string s, string suffix)` | Tests whether a string ends with a suffix. |

String indices are zero-based UTF-8 byte offsets, not Unicode code-point
indices. An out-of-range index produces a runtime error. `std::strings::find`, `std::strings::last_find`,
`std::strings::substring`, and `std::strings::char_code` use the same byte-offset contract.

## Splitting, joining, and transformation

| Function | Description |
| --- | --- |
| `std::strings::split(string s, string separator)` | Splits a string into an `array<string>`. |
| `std::strings::join(array<string> parts, string separator)` | Joins a string array using a separator. |
| `std::strings::trim_left(string s)` | Removes ASCII whitespace from the left (tab, LF, vertical tab, form feed, CR, space). |
| `std::strings::trim_right(string s)` | Removes whitespace from the right. |
| `std::strings::trim(string s)` | Removes whitespace from both sides. |
| `std::strings::to_lower(string s)` | Converts text to lower case. |
| `std::strings::to_upper(string s)` | Converts text to upper case. |
| `std::strings::replace(string s, string from, string to)` | Replaces matching substrings; returns the original string when `from` is empty. |
| `std::strings::last_find(string haystack, string needle)` | Returns the byte offset of the final occurrence, or `-1` when absent. An empty needle returns `len(haystack)`. |
| `std::strings::repeat(string value, int64 count)` | Repeats a string `count` times. A negative count is a runtime error. |
| `std::strings::remove_prefix(string value, string prefix)` | Removes `prefix` when it is present; otherwise returns the original string. |
| `std::strings::remove_suffix(string value, string suffix)` | Removes `suffix` when it is present; otherwise returns the original string. |

`std::strings::to_lower` and `std::strings::to_upper` change ASCII letters only and preserve all other UTF-8 bytes; they do not perform Unicode case folding.

## Formatting

This module provides two formatting functions: `format` uses brace fields and `formatf` uses printf-style conversions. The two grammars are independent and should not be mixed.

The public signatures are:

```text
string format(string fmt, array<string> args);
string formatf(string fmt, array<string> args);
```

Script code can pass `string`, booleans, floating-point values, and signed or unsigned integers directly; the compiler converts those scalars to strings and collects them into `array<string>`. An existing `array<string>` can also be passed directly as the second argument.

```VeroScript
import "string.vs";

string text = format("value={}, name={}", 42, "VeroScript");
string hex = format("{:8X}", 255);
string legacy = formatf("value=%d", 42);
```

### `format` fields

| Form | Meaning |
| --- | --- |
| `{}` | Uses the next automatic argument. |
| `{N}` | Uses an explicit zero-based index. |
| `{:spec}` / `{N:spec}` | Uses a specification with the form `[alignment][width][.precision][type]`. |
| `{{` / `}}` | Emits literal braces. |

Alignment is `<`, `>`, or `^`, with right alignment as the default. Width and precision are limited to values from `0` to `1024` and use space padding. The only type codes are `x` and `X` for lower- and upper-case hexadecimal. Precision parses a finite numeric value and emits fixed-point digits. Do not mix automatic and explicit indices; missing arguments, unmatched braces, and invalid specifications produce runtime errors.

<a id="integer-formatting"></a>

`format` x/X now handles full 64-bit decimal integers: positive values through UINT64_MAX, negative values through INT64_MIN, with 64-bit two’s complement for negatives. Thus -1 becomes 16 f characters. printf integer conversions also use direct 64-bit parsing. Width and precision remain bounded by 1024.

### `formatf` conversions

The conversion shape is `%[flags][width][.precision][length]conversion`. Supported flags are `-`, `+`, space, `#`, and `0`; width and precision are limited to values from `0` to `1024`, and dynamic `*` is not supported. Length modifiers `h`, `hh`, `l`, `ll`, `j`, `z`, and `t` are accepted for source compatibility but do not change VeroScript value width or ABI.

Supported conversions are:

| Conversion | Meaning |
| --- | --- |
| `%d`, `%i` | Signed integer. |
| `%u`, `%o`, `%x`, `%X` | Non-negative decimal, octal, or lower-/upper-case hexadecimal integer. |
| `%f`, `%F`, `%e`, `%E`, `%g`, `%G` | Floating point; default precision is `6`. |
| `%c` | An integer from `0` to `255` or a one-byte string; precision is invalid. |
| `%s` | String; precision limits emitted bytes. |
| `%%` | Literal `%` without consuming an argument. |

`printf` uses the same conversion grammar as `formatf` and writes directly to standard output. The multi-argument form of `print` uses `format`'s brace grammar. Extra arguments not consumed by a conversion are ignored. For the complete rules and error boundaries, see [String language behavior](../language-basics/strings.md#formatting).
`std::strings::trim_left`, `std::strings::trim_right`, and `std::strings::trim` remove ASCII bytes `0x09` through `0x0D` and space (`0x20`) at the corresponding ends; interior and non-ASCII bytes are preserved.

`len(s)`, `std::strings::find` and `std::strings::last_find` return `int64`; a missing match is `-1`. `std::strings::substring` uses an `int64` byte offset and count: `start` may equal `len(s)`, `count` must be nonnegative, and a count beyond the remaining bytes is truncated to the available suffix. `std::strings::char_code` takes an `int64` byte index and returns the byte value as `int` (`0`–`255`). `std::strings::repeat` takes an `int64` count and rejects lengths beyond the `int64` range; allocation limits still apply.

- [Standard library foundation](foundation.md)

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](string.md) · [fr-FR](../../fr-FR/standard-library/string.md) · [ja-JP](../../ja-JP/standard-library/string.md) · [ko-KR](../../ko-KR/standard-library/string.md) · [zh-CN](../../zh-CN/standard-library/string.md) · [zh-TW](../../zh-TW/standard-library/string.md)
