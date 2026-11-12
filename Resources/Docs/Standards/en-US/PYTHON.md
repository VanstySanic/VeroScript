# Python coding standards (provisional)

Scope: [VeroScript project development and contributions](README.md).

Python follows the structure, comment, and minimal necessary explanation principles in [GENERAL.md](GENERAL.md). Without braces and access modifiers, declaration order within modules and classes expresses the corresponding nesting.

## Formatting

- Use one literal Tab per logical nesting level, displayed as four columns by default; follow the indentation rules in [GENERAL.md](GENERAL.md).
- [Python parses Tabs at eight-column boundaries](https://docs.python.org/3/reference/lexical_analysis.html#indentation), independently of editor display width. Use Tabs consistently for block nesting; preserve block boundaries during migration rather than replacing indentation by screen columns.
- Correctly indent every `class`, `def`, condition, loop, `try`, and context-manager body. Do not compress logic into single-line compound statements.
- Bodies of `if`, `for`, `while`, `try`, and similar branches use independent indented blocks. Python does not use the C++ single-line `return` guard exception.
- When `match` provides switch-like branching, include `case _` as the default and explicitly handle unknown or unsupported input there.
- Keep conditional imports and version branches in the logical region they control; do not split a complete semantic block.

## Module and type layout

Recommended module order:

1. Module docstring and `__future__` imports;
2. standard-library, third-party, then project imports;
3. constants, type aliases, and protocols;
4. `Enum`;
5. `dataclass` or other data types;
6. other helper types;
7. classes;
8. module functions and the command-line entrypoint.

Recommended class order:

1. Class constants and class-level types;
2. methods required by inheritance or protocols;
3. nested `Enum`, data types, other types, then classes;
4. `__init__`, alternative constructors, public methods, and properties;
5. protected and private helpers;
6. class variables; establish instance state centrally in `__init__`, rather than adding it sporadically among methods.

- Group multiple base/protocol implementations in inheritance-list order. Use paired `#~ Begin <Name> Interface` / `#~ End <Name> Interface` comments as needed, or short explanations more idiomatic to Python.
- Use Python naming conventions: `snake_case` for modules/functions/variables, `PascalCase` for classes, `UPPER_SNAKE_CASE` for constants, and a leading underscore for internal implementation.

## Comments

- Use docstrings for non-obvious contracts of modules, public classes, and public functions.
- Use `#` for local reasons; do not repeat information already expressed by variable or function names.
- Type annotations express public inputs/outputs and complex data structures; their strictness may be defined separately later.

## Topics to add

Later additions may cover exceptions, file I/O, paths, safety boundaries, test frameworks, formatters, and static typing rules.
