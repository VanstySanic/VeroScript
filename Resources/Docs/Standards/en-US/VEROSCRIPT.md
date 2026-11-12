# VeroScript coding standards (provisional)

Scope: [VeroScript project development and contributions](README.md).

VeroScript type layout and logical grouping follow [CPP.md](CPP.md). This file defines only currently established VeroScript writing conventions; language- or formatter-specific details may be added later.

## Formatting

- Place `else`, `else if`, `catch`, `finally`, and the trailing `while` of `do…while` on new lines
  as required by [GENERAL.md](GENERAL.md). A block opening brace also starts on a separate line aligned with its clause.
- Use one literal Tab per logical nesting level, displayed as four columns by default; follow the indentation rules in [GENERAL.md](GENERAL.md).
- Each namespace, type, method, condition, loop, and local scope adds one indentation level.
- Opening braces occupy their own line; closing braces align with their declaration or control statement.
- Conditions and loops with an execution body use braces. The only exception is a single-line guard whose body contains solely one direct `return`; do not combine it with an `else` chain, logging, cleanup, or other statements.
- Every `switch` includes `default`. Put ordinary `case`/`default` execution statements in independent braced scopes; a branch containing only one direct `return` that ends the current method may omit the block.
- An explicit `fallthrough;` must be the label’s final direct statement, after its braced block; never nest it in a block, condition, or loop. This follows the language contract; keep other executable statements in the independent scope.
- Type declarations `class`, `struct`, `interface`, and `enum` end with a semicolon.

## Type layout

Arrange type contents in this order:

1. Type relationships and aliases;
2. `public`, `protected`, `private` regions, in that order;
3. inherited methods first within each region;
4. nested types in `enum` → `struct` → other types → `class` order;
5. this type's methods;
6. member variables.

- Group inherited/interface methods by base/interface inheritance order. Use paired `//~ Begin <Name> Interface` / `//~ End <Name> Interface` markers as needed.
- Multiple top-level types in a file also follow `enum` → `struct` → other types → `class` where complete-type dependencies permit.
- This standard does not currently specify VeroScript forward declarations: native language support has not been confirmed. Do not introduce them merely because C++ has that rule; add a separate rule after confirming the syntax.

## Naming and comments

- Types/interfaces use PascalCase; functions, fields, parameters, and locals use lowerCamelCase.
- Follow [GENERAL.md](GENERAL.md) for comments: explain language behavior, constraints, ownership, or example intent, rather than repeating surface code.
- Public standard-library and host-binding names are compatibility surfaces; renaming requires a separate assessment.

## Lengths and indices

Use `int64` for collection lengths, linear indices, slice offsets/counts and capacities. Keep map keys and task IDs in their declared types; a count becoming 64-bit does not widen unrelated APIs. Check sign and range before converting to native `size_t` or narrower backend indices. Use checked narrowing when an `int` is required, and preserve allocation limits and documented missing-result sentinels. Update standard-library declarations, documentation and regression tests together when changing these contracts.

## Topics to add

Later additions may cover import order, standard namespaces, exceptions, references, concurrency, formatter behavior, and example publication rules.
