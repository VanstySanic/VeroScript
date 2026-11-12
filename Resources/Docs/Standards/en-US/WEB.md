# TypeScript and JavaScript coding standards (provisional)

Scope: [VeroScript project development and contributions](README.md).

TypeScript/JavaScript follow the grouping, comment, and minimal necessary explanation principles in [GENERAL.md](GENERAL.md). The clause newline rule below is mandatory; other brace placement provisionally follows the existing module style.

## Current provisional rules

- Place `else`, `else if`, `catch`, `finally`, and the trailing `while` of `do…while` on new lines
  as required by [GENERAL.md](GENERAL.md). A block opening brace also starts on a separate line aligned with its clause.
- Use one literal Tab per logical nesting level, displayed as four columns by default; follow the indentation rules in [GENERAL.md](GENERAL.md).
- Group types, functions, conditions, loops, and `switch` by meaning; avoid meaningless single-line compression.
- Every `switch` requires `default`. TypeScript exhaustiveness checks may live in `default`, but static checking does not permit omitting it. Handle unknown values explicitly there.
- Each `case` and `default` execution body uses an independent braced scope; do not rely on implicit fallthrough. Explain deliberately shared handling in an adjacent comment. The only exception is a label whose sole execution path directly ends the current function with `return`, with no other statements.
- All control branches use braces. The only exception is a single-line guard consisting solely of one direct `return` or `return <expression>`; do not combine it with `else`, logging, cleanup, or other statements.
- Module-level TypeScript types follow `enum` → `interface` → `type`/other aliases → `class`. Generic interfaces/classes retain their own categories. Where JavaScript has no corresponding type declarations, preserve equivalent clarity through export relationships and dependencies.
- Class contents follow the overall C++ order: related aliases/constants, inherited/interface implementations, nested types, methods, then fields. Fields belong at the end of their access-semantic region.
- Use paired `//~ Begin <Name> Interface` / `//~ End <Name> Interface` comments as needed for multiple interfaces or adapter groups.
- Exported types, interfaces, and classes use PascalCase; functions, fields, parameters, and locals use camelCase. External protocol fields retain their contractual names.
- Express public data, errors, and resource-disposal semantics through types or necessary comments.

## Topics to add

Later decisions may cover brace style, quotes/semicolons/trailing commas, `readonly`, async cancellation, Wasm resource lifetimes, and type-checking requirements.
