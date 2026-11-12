# C++ front end

`VeroScriptCppFrontend` parses a deliberately bounded C++17 subset into a
stable public AST. It is the shared declaration layer for [BindGen](bindgen.md)
and [Transpiler](transpiler.md). Unsupported constructs produce diagnostics;
the front end never silently approximates them.

## Public operations

The public header exposes:

- `ParseSource` and `ParseFile` for one translation unit;
- `ParseFiles` and `MergeTranslationUnits` for a project-level declaration set;
- `MapTypeToVeroScript` and `ResolveTypeAliases` for explicit type mapping and
  alias resolution;
- `EvaluateEnumValues` for checked, portable enum-constant evaluation, and
  `TryGetEnumValueRange` for the representable underlying-type range;
- `GetFunctionIdentity`, `FormatDiagnostic`, and `JoinTokens` for consumers
  that need stable identity and presentation helpers.

`FParseOptions` controls whether includes and `#pragma` are accepted, whether
unknown preprocessor directives are rejected, and whether function bodies may
be parsed. `FTypeMapOptions` controls the explicit policies for raw pointers to
known host types, rvalue references, known host or enum types, known type
parameters, and aliases used when declarations rely on ignored includes. Consumers
should set these policies deliberately instead of treating a permissive parse
as proof that the declaration is automatically bindable or transpilable.

The AST preserves source locations, diagnostics, namespaces, access, qualified
names, constructors, fields, functions, enums, base types, reference kinds,
pointer and pointee cv-qualification, and semantic modifiers such as
`constexpr`, `consteval`, `constinit`, and `noexcept`. Consumers can therefore
reject or preserve a modifier instead of losing it during parsing.

## Accepted C++ surface

The current provider accepts namespaces, `using Name = Type` and ordinary
`typedef Type Name` aliases, type-parameter class/function/alias templates with
trailing default type arguments, scoped and unscoped enums, classes and
structs, access specifiers, fields, constructors, methods, free functions,
globals, fixed-width integers, standard strings, and standard containers with a
VeroScript equivalent. Ordinary and `u8` raw string literals are scanned with
their full delimiters and normalized to UTF-8 escaped literals; wide raw
strings are rejected. Simple same-type declarator lists such as
`int A = 1, B = 2` are expanded, while mixed pointer/reference declarator
lists remain unsupported. Named forward declarations merge with their
definitions.

Out-of-class constructors, destructors, methods, operators, and traditional
static data-member definitions can be matched with declarations in project
mode and may be supplied in a different input file. Qualified namespace
function and variable definitions remain namespace declarations. C++17
attributes on records, enums, enumerators, fields, functions, and parameters
are retained structurally for downstream tools; they do not automatically
acquire VeroScript semantics.

Enum initializers support integer and portable single-character literals,
earlier enumerators, parentheses, unary operators, arithmetic, shifts,
bitwise, boolean literals, logical negation, relational, and equality operators.
Evaluation preserves the target ABI's integer literal widths and signedness,
uses C++ integer promotions, wraps unsigned arithmetic, and rejects signed
overflow and invalid shift counts. Results must fit the public `int64`
representation and the mapped enum underlying type. Shifts of negative signed
values and short-circuit `&&`/`||` expressions remain unsupported.

Unscoped enums without an explicit underlying type retain initializer types
for references to earlier enumerators; fixed enums use their underlying type
with integer promotion. MSVC hosts must use `/Zc:enumTypes` (VS 2022 17.4 or
later) for unfixed enums that depend on this distinction, or give those enums
an explicit underlying type. The option is off by default, including with
`/permissive-`; the SDK does not change the host's compiler flags.

The preprocessor boundary is intentionally small: `#pragma once`, includes,
conventional whole-file header guards, and `extern "C"` declaration blocks or
declarations are accepted. General conditional preprocessing and unknown
directives are rejected rather than parsed as if all branches were active.

## Project merging

Multi-file collection merges compatible external declarations and named
forward declarations. It rejects duplicate definitions, repeated input paths,
and internal-linkage collisions. Anonymous namespaces are rejected because
flattening file-private entities would change their visibility.

The merge is not a linker. It does not choose a definition based on input
order, rename internal entities, or resolve arbitrary C++ ownership and storage
semantics.

## Deliberate exclusions

Non-type template parameters, parameter packs, specializations, SFINAE,
concepts, dependent compile-time expressions, unions, C arrays, anonymous
namespaces, and unknown preprocessor directives are rejected. Type-parameter
class/function/alias templates are retained, including trailing default type
arguments; a non-default parameter after a default is rejected. Friend
type/function/member declarations are retained as record-level declarations,
but inline friend definitions are rejected. A raw-pointer policy may admit a
single mutable pointer to a known host type; scalar pointers,
cv-qualified pointers, pointer-to-const, and multi-level pointers remain
unsupported. Rvalue references are not suitable for automatic source
translation.

`std::shared_ptr<T>` is not erased to `T`; shared ownership requires an
explicit adapter. `std::size_t` and `std::ptrdiff_t` map according to the
active target ABI. Defaulted functions are limited to supported C++17
special-member signatures, and pointer/pointee cv-qualification remains part
of overload identity.

Type lookup searches the declaration scope first and supports relative qualified names and absolute `::` names. Declared user types take precedence over convenience mappings such as `string` and `vector`; unresolved qualified types produce errors.

Records, enums and bound template parameters take precedence over outer aliases.
For manual alias resolution, use the three-argument `ResolveTypeAliases` overload
with `KnownHostTypes`, `KnownEnumTypes` and `KnownTypeParameters` in its lookup options.

An empty `FType::DeclarationScope` denotes the global scope and remains unchanged
during alias substitution. `FBaseType::Type` retains structured base types,
including their lookup scopes and template arguments. `bQualifyNamedTypes` requests
absolute user type names for source emission; its default keeps canonical catalog names.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
