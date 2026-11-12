# Basic Syntax

This page describes the basic structure of a VeroScript source file, including statements, blocks, identifiers, and keywords.

## Source files

VeroScript source files use the `.vs` extension. A source file can contain global variables, functions, structs, classes, interfaces, enums, namespaces, and module declarations.

An application script normally provides a `main` entry point; a library module may export types and functions without defining an entry function. `main` may return `int` or `void`; `int` is recommended when the process exit code should be explicit.

```VeroScript
int global_value = 1;

int add(int left, int right)
{
	return left + right;
}

int main()
{
	print(add(global_value, 2));
	return 0;
}
```

## Recommended source style

Documentation VeroScript snippets use a Tab character per indentation level, opening braces on their own line, one ordinary statement per line, and a trailing semicolon after `class`, `struct`, `interface`, and `enum` declarations. The language accepts some concise forms without the trailing semicolon, but documented code should prefer the complete form. Standalone example files follow the formatter style described in the examples README.

## Statements and semicolons

Ordinary statements end with a semicolon:

```VeroScript
int value = 1;
value = value + 1;
print(value);
```

Function declarations, type declarations, and control-flow statements use braces for compound statements. Whitespace and line breaks normally do not change the meaning of a statement.

```VeroScript
if (value > 0)
{
	print("positive");
}
```

## Identifiers

Identifiers name variables, functions, types, fields, namespaces, and module members. An identifier may contain ASCII letters, digits, and underscores, but cannot start with a digit:

```VeroScript
int item_count = 3;
string display_name = "VeroScript";
```

Identifiers are case-sensitive. `item_count`, `ItemCount`, and `itemCount` are different names.

## Keywords

Keywords have language-defined meanings and cannot be used as ordinary identifiers. The keyword set includes:

```text
void bool char short int long float double string wstring
int8 uint8 int16 uint16 int32 uint32 int64 uint64
unsigned auto channel array map multimap set multiset
queue priority_queue stack ordered_map ordered_multimap deque ordered_multiset
fn function
if else while do for switch case default return break continue fallthrough goto
try catch finally throw struct class interface enum
import export using typedef namespace friend
new delete this const ref static explicit
public protected private virtual override final
where operator template in
true false nullptr
```

The keyword set is part of the language grammar. A new keyword can make an otherwise valid identifier unavailable, so library code should avoid using keywords as public names.

`do` is now reserved for `do ... while`; older source that used `do` as an identifier must rename it.

## Same-type declaration groups and raw strings

Top-level, namespace, and local declarations may introduce multiple variables when they share one complete leading type and the same `const` / `ref` qualifiers valid at that declaration location. Initializers are evaluated and names become visible from left to right:

```VeroScript
const int first = 1, second = first + 1;
for (int index = 0, end = 3; index < end; index++)
{
	print(index);
}
```

Every item keeps its own initializer and ordinary declaration checks. In particular, `auto first = 1, second = 2;` is intentionally rejected, and a trailing per-name array suffix is not a way to declare mixed shapes; put a shared array extent in the type prefix instead.

UTF-8 raw strings use the fixed `r"..."` form. They preserve backslashes and line breaks without escape processing:

```VeroScript
string path = r"C:\tools\new";
string text = r"first line
second line";
```

The first following `"` closes the literal, so raw text cannot contain a quote character. There are no wide raw strings, custom delimiters, or string interpolation.

## Deliberate syntax boundaries

VeroScript rejects a standalone empty statement `;`; use `{}` for an intentionally empty body. It does not support general comma expressions, named arguments such as `f(value: 1)`, inferred `auto` return types, structured bindings, or C++ layout operators such as `sizeof` and `alignof`. Use declaration groups only for same-type declarations, explicit return types, and language/container APIs instead.

## Parser safety limits

The parser accepts up to 256 nested expression levels, unary-expression levels,
and preprocessor `#if` expression levels. Statement nesting is limited to 32
levels, and an expression chain is limited to 256 links. When a limit is exceeded, compilation fails with
a diagnostic such as `expression nested too deeply`, `statement nested too
deeply`, `#if expression nested too deeply`, or `expression chain is too long`.
These limits protect the compiler from pathological input; simplify or split
deeply generated expressions and control-flow blocks when necessary.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](basic-syntax.md) · [fr-FR](../../fr-FR/language-basics/basic-syntax.md) · [ja-JP](../../ja-JP/language-basics/basic-syntax.md) · [ko-KR](../../ko-KR/language-basics/basic-syntax.md) · [zh-CN](../../zh-CN/language-basics/basic-syntax.md) · [zh-TW](../../zh-TW/language-basics/basic-syntax.md)
