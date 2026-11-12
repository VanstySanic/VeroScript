# Preprocessor

The preprocessor runs before lexical and semantic analysis. It supports conditional compilation, object-like macros, function-like macros, stringification, token pasting, and module inclusion.

## Object-like macros

An object-like macro replaces a name with a predefined token sequence:

```VeroScript
#define VERSION 2
#define MAX_COUNT 64

int version = VERSION;
int capacity = MAX_COUNT;
```

Macro expansion occurs before the compiler performs type checking. A macro has no VeroScript static type of its own; the expanded expression must still satisfy ordinary syntax and type rules.

## Function-like macros

A function-like macro accepts arguments. Parenthesize arguments in compound expressions so that expansion does not change their precedence:

```VeroScript
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int value = MAX(3, 8);
```

Use macros for simple token substitution. Prefer a function or generic function when type checking, overloads, or debugging behavior is important.

## Stringification and token pasting

Inside a function-like macro, `#` converts an argument to text and `##` pastes adjacent tokens into one token:

```VeroScript
#define STR(x) #x
#define CAT(a, b) a##b

int VALUE = 9;
string name = STR(hello);
int result = CAT(VAL, UE);
```

Stringification and token pasting happen during macro expansion. The pasted result must still be a valid VeroScript token.

## Variadic macros

A function-like macro can use `...` to accept additional arguments and expand them through `__VA_ARGS__`:

```VeroScript
#define LOG(fmt, ...) print(fmt, ##__VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

The current implementation supports `##__VA_ARGS__` to remove the extra comma when no variadic arguments are provided. It also supports `__VA_OPT__(tokens)`: the tokens inside the parentheses are emitted when the variadic arguments are non-empty and omitted otherwise:

```VeroScript
#define LOG(fmt, ...) print(fmt __VA_OPT__(,) __VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

`__VA_OPT__` is valid only in the replacement list of a variadic macro and must be followed by a balanced pair of parentheses.

## Dynamic built-in macros

The compiler also supplies two macros generated from the current source-module context:

| Macro | Expansion |
| --- | --- |
| `__FILE__` | The normalized logical path of the current module; `<memory>` for source text without a path. |
| `__LINE__` | The physical source line containing the macro invocation, starting at `1`. |

They are computed in each module's preprocessing context rather than being ordinary global macros:

```VeroScript
#define REPORT(name) print("{}:{} {}", __FILE__, __LINE__, name)

int main()
{
	REPORT("ready");
	return 0;
}
```

`__FILE__`, `__LINE__`, `__VA_ARGS__`, and `__VA_OPT__` are reserved names. A script, command-line initial definition, host predefined macro, or function-macro parameter cannot redefine them.

## Conditional compilation

Use `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, and `#endif` to select which source participates in later compilation:

```VeroScript
#define VERSION 2

#if VERSION >= 2
int feature_level = 2;
#else
int feature_level = 1;
#endif
```

The complete source-directive set is `#include`, `#define`, `#define_export`, `#undef`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, and `#error`. In conditions, both `defined(NAME)` and `defined NAME` test whether a macro exists. Macros are expanded first; any identifier that remains undefined evaluates to zero.

`#if` expressions use 64-bit integer constants (decimal, C-style leading-zero octal, or hexadecimal), parentheses, unary `+ - ! ~`, arithmetic `* / % + -`, shifts, comparisons, equality, bitwise `& ^ |`, short-circuit `&& ||`, and `?:`. They do not use the script literal rules for binary prefixes or digit separators. Division by zero and overflow in an evaluated branch are errors; short-circuited branches are not evaluated.

An active `#error message` stops preprocessing and preserves the trimmed message in the diagnostic; an inactive conditional branch ignores it. The message is not macro-expanded. `#warning`, `#line`, and `#pragma` (including `#pragma once`) are not supported and are reported by name.

The command-line tool can provide a simple object-like macro with `-D` or `--define`. Source files may still define the function-like and variadic macros described above:

```powershell
veroscript -D ENABLE_LOGGING=1 script.vs
```

## Including a module

`#include` is an alias for `export import`: it exposes and re-exports public declarations and public macros, without textual inclusion.

```VeroScript
#include "math.vs"

int main()
{
	print(std::clamp_int(15, 0, 10));
}
```

Use `import` and `export import` when expressing module dependencies and re-exports. Do not treat `#include` as a C++-style text-inclusion mechanism.

## Public macros

<a id="exported-macro-contract"></a>

Use `#define_export` with the same object-like, function-like and variadic syntax as `#define`. Ordinary definitions remain private. An explicit `import` exposes the target’s final public macros immediately after its path string; `export import` also re-exports them. `#include` is an alias for `export import`, including both public declarations and public macros. These rules also apply to `.vslib` libraries. Implicit host imports also provide completed public macros from the start of consumer source, without re-exporting them. Providers and their explicit dependencies establish their interfaces first; macro dependencies between providers require ordinary `import`. Private macros stay private, and conflicting definitions follow ordinary import rules. For example, `veroscript --implicit-import settings.vs main.vs` makes the public macros in `settings.vs` available without an import in `main.vs`.

```VeroScript
// settings.vs
#define PRIVATE_VALUE 7
#define_export VALUE 21
#define_export DOUBLE(x) ((x) * 2)

```

```VeroScript
// main.vs
import "settings.vs";
int main()
{
	print(DOUBLE(VALUE));
	return 0;
}
```

A replacement body is expanded in the caller’s macro environment; private helper macros are not captured. Export helpers explicitly when required. `__FILE__` and `__LINE__` describe the invocation. Different definitions with the same name conflict, even with identical bodies. Use `#undef` before replacing an imported definition. Repeated imports of the same original definition are idempotent; importing again after `#undef` restores it. Redefining with ordinary `#define` makes the final definition private; `#undef` removes it from the public interface.

Cycles without public macro definitions remain supported. A cyclic source closure containing any `#define_export`, including an inactive directive, is rejected because its public interface cannot be determined. Remove the source import cycle. Libraries freeze conditional exports at build time; application packages do not carry a public macro interface.

<a id="preprocess-resource-limits"></a>

## Preprocessing resource limits

By default, preprocessing limits each file’s expanded output, including intermediate replacement text, to 32 MiB. A compilation shares a work budget of 268,435,456 internal units across its files. Exceeding either limit reports a preprocessing error and rejects compilation. Work units are neither VM instructions nor a time guarantee. Native embedding integrations can supply `FPreprocessContext::Budget` with an `FPreprocessBudget`; zero is a zero limit, not an unlimited setting. These limits are separate from script compilation-extension execution budgets.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](preprocessor.md) · [fr-FR](../../fr-FR/language-basics/preprocessor.md) · [ja-JP](../../ja-JP/language-basics/preprocessor.md) · [ko-KR](../../ko-KR/language-basics/preprocessor.md) · [zh-CN](../../zh-CN/language-basics/preprocessor.md) · [zh-TW](../../zh-TW/language-basics/preprocessor.md)
