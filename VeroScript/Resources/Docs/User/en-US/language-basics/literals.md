# Literals

A literal is a value written directly in source code. VeroScript supports integer, floating-point, character, string, wide-string, boolean, and `nullptr` literals.

## Integer literals

An integer without a prefix is decimal. `0x`/`0X` selects hexadecimal and `0b`/`0B` selects binary:

```VeroScript
int decimal = 42;
int hexadecimal = 0x2A;
int binary = 0b101010;
```

Suffixes can make the range or signedness explicit:

```VeroScript
unsigned int unsigned_value = 100u;
long long_value = 100L;
int64 wide_value = 9223372036854775807i64;
uint64 unsigned_wide_value = 18446744073709551615u64;
```

The supported integer suffixes include case-insensitive forms of `u`, `l`, `ul`, `lu`, `i64`, and `u64`. `l`/`L` selects the language's 64-bit `long`; `i64`/`u64` directly select a signed/unsigned 64-bit literal. Unsuffixed and `u` literals use the 32-bit path. The literal must be representable by its target type.

An underscore may separate two digits that are valid for the current base, including digits in the fraction and exponent of a floating-point literal: `1_000`, `0xFF_FF`, `0b1010_0101`, and `1_2.5_0e1` are valid. An underscore cannot touch a base prefix, decimal point, exponent marker/sign, suffix, or another underscore. A leading zero does not select octal; `010` is decimal ten.

## Floating-point literals

A number with a decimal point or exponent is parsed as a floating-point literal:

```VeroScript
double pi_approximation = 3.14159;
double scientific = 1.25e2;
double half = .5;
float small = .5f;
double fifty = .5e2;
```

The integer part may be omitted when a decimal point is immediately followed by a decimal digit. `.5`, `.5f`, and `.5e2` are therefore valid floating-point literals and mean `0.5` as a `double`, `0.5` as a `float`, and `50.0` as a `double`. A standalone `.` remains member-access punctuation, and integer suffixes such as `i64` do not apply to this form.

The exponent uses `e` or `E` and may include `+` or `-`. An `f` or `F` suffix identifies a `float` literal; an unsuffixed literal is a `double`.

Decimal floating literals use `.` independently of the host locale. An `f`/`F` literal rounds directly to binary32; an unsuffixed literal rounds to binary64. Overflow and values outside the accepted binary64 range are compile errors. A value representable as `double` but too small for `float` may round to zero, for example `1e-100f`; this literal rule does not imply that runtime text-parsing APIs accept zero underflow.

## Character literals

A character literal has static type `char`, the language's signed 8-bit integer type:

```VeroScript
char letter = 'A';
char newline = '\n';
char byte_ff = '\xFF';
```

A direct character must be one ASCII byte. `\xNN` writes exactly one 8-bit pattern, so `\x80` through `\xFF` have negative `char` values. Empty, multi-character, non-ASCII, Unicode-escaped, and wide forms such as `L'A'` are rejected.

## Strings and wide strings

Ordinary strings use double quotes. A wide string adds `L` before the opening quote:

```VeroScript
string text = "VeroScript";
wstring wide_text = L"text";
```

String literals support escape characters, including newlines and quotes:

```VeroScript
string message = "first line\nsecond line";
string quoted = "say \"hello\"";
```

| Escape | `string` | `wstring` | `char` |
| --- | --- | --- | --- |
| `\\`, `\"`, `\'` | escaped slash or quote | escaped slash or quote | escaped slash or quote |
| `\0`, `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v` | one byte | one UTF-16 code unit | one byte |
| `\xNN` | exactly one raw byte | `U+00NN` | exactly one 8-bit pattern |
| `\uNNNN`, `\UNNNNNNNN` | Unicode scalar encoded as UTF-8 | Unicode scalar encoded as UTF-16 | not supported |

Unknown or incomplete escapes are compile errors. Unicode escapes reject surrogate values and values above `U+10FFFF`. A `string` is a length-aware byte sequence: source text and text APIs use UTF-8, but `\0` and `\xNN` can create embedded NUL or arbitrary bytes, so a runtime `string` is not guaranteed to be valid UTF-8. A `wstring` is a length-aware UTF-16 code-unit sequence. Explicit UTF-8/UTF-16 conversion reports invalid input instead of silently replacing it.

String literals cannot span source lines. `string` and `wstring` are different types and cannot be mixed directly; use an explicit conversion when needed.

Array, map, set, and explicit typed brace initializers may have one trailing comma, for example `{1, 2,}` or `{'A': 1,}`. Empty initializers remain `{}`. A leading comma, a doubled comma, or a trailing comma in function arguments is not accepted.

## Booleans and null

The boolean literals are `true` and `false`:

```VeroScript
bool enabled = true;
bool finished = false;
```

`nullptr` represents a null object handle or null runtime value. It is commonly used with class objects and nullable runtime values:

```VeroScript
class Node
{
public:
	int value;
};

int main()
{
	Node node = nullptr;
	if (node == nullptr)
	{
		print("empty");
	}
}
```

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](literals.md) · [fr-FR](../../fr-FR/language-basics/literals.md) · [ja-JP](../../ja-JP/language-basics/literals.md) · [ko-KR](../../ko-KR/language-basics/literals.md) · [zh-CN](../../zh-CN/language-basics/literals.md) · [zh-TW](../../zh-TW/language-basics/literals.md)
