# C# coding standards (provisional)

Scope: [VeroScript project development and contributions](README.md).

C# follows the general principles in [GENERAL.md](GENERAL.md) and the structural idea in [CPP.md](CPP.md): relationships first, then types, then methods, then data. Where C# syntax does not correspond, this file takes precedence.

## Formatting

- Place `else`, `else if`, `catch`, `finally`, and the trailing `while` of `do…while` on new lines
  as required by [GENERAL.md](GENERAL.md). A block opening brace also starts on a separate line aligned with its clause.
- Use one literal Tab per logical nesting level, displayed as four columns by default; follow the indentation rules in [GENERAL.md](GENERAL.md).
- Block-bodied types, methods, properties, control branches, and `switch` use the same Allman brace layout as C++.
- A member, property accessor, or local mapping consisting of one direct, readable expression may use `=>`. Expression bodies must not hide multiple steps, resource management, logging, or complex control flow; use an Allman block for multi-step logic.
- Every `if`, `else`, loop, `using`, `lock`, `try`, `catch`, and `finally` with an execution body uses braces.
- The only exception is a single-line guard consisting solely of one direct `return` or `return <expression>`. Do not combine it with an `else` chain, logging, cleanup, or other statements. `break`, `continue`, `throw`, and other branches always require braces.
- Every `switch` statement requires `default`. A `switch` expression must handle its input completely: use a fallback `_` arm when preceding patterns are not provably exhaustive; do not add an unreachable `_` merely for form when the preceding patterns are exhaustive. Unknown-value handling must be explicit, without relying on unclear runtime failure.
- Each `case` and `default` execution body has an independent braced scope; do not rely on implicit fallthrough. Explain deliberately shared handling in an adjacent comment. The only exception is a label whose sole execution path directly ends the current method with `return` or `return <expression>`; no logging, cleanup, declarations, conditions, or other statements may be mixed in.
- Preprocessor directives such as `#if` stay at the same logical level as their enclosed code and add no indentation level.

## Type layout

Recommended order:

1. Attributes stay immediately adjacent to their target; bases and interfaces stay adjacent to the type declaration;
2. members with access modifiers follow `public` → `protected internal` → `protected` → `internal` → `private protected` → `private`, keeping each access level in one contiguous region where possible;
3. `override` and interface implementations come first in each region; explicit interface implementations cannot have access modifiers and belong at the start of their implementation group, ordered by interface inheritance;
4. nested types follow `enum` → `struct`/`record struct` → other types → `class`/`record`;
5. this type's constructors, methods, properties, and events;
6. fields last, never interleaved with methods, properties, or nested types.

- Group explicit interface implementations by interface. Use paired `//~ Begin <Name> Interface` and `//~ End <Name> Interface` comments for multiple members or unclear grouping.
- Do not interleave interface, base, and this type's implementation groups.
- Keep declaration order traceable in longer implementation files. Local helper types and private static helpers may use the most readable position.
- `file` applies only to file-local top-level types, not member access regions, and is excluded from member ordering. Generic classes and structs follow their respective categories, rather than “other types”.

## Comments and naming

- Comment types, methods, properties, and fields only when callers need semantics, ownership, threading, exceptions, or protocol constraints.
- Use XML documentation for non-obvious public API contracts and `//` for short internal explanations.
- Preserve established .NET naming: PascalCase for types/members, camelCase for parameters/locals, `_camelCase` for private fields.
- Retain native ABI, JSON, and protocol field names; internal naming consistency must not break external compatibility.

## Topics to add

Later additions may cover nullable handling, async/cancellation, resource disposal, Avalonia ViewModels, tests, and analyzer rules.
