# General coding standards

Scope: [VeroScript project development and contributions](README.md).

This file contains rules shared by all languages. Language-specific indentation, braces, type layout, and syntax follow the respective language standard.

## Structure and nesting

- Group code by responsibility. Each block expresses one readily named concept, such as inherited interfaces, nested types, public methods, private helpers, or member data.
- Separate blocks with one blank line. Do not substitute repeated blank lines, long separators, or meaningless region titles for clear structure.
- Nested scopes, namespaces, types, methods, conditions, and loops must show their actual nesting; do not flatten structure to reduce line count.
- Where no rule is defined, prefer the nearest existing consistent style instead of introducing a new style in one change.

## Indentation

- Owned, manually maintained source, tests, examples, and build/tool scripts use one literal Tab per logical nesting level, displayed as four columns by default. Do not substitute space-only indentation for nesting.
- Tabs express nesting. Continuation lines may use alignment spaces where the language permits, but do not mix the two as block indentation.
- Language and file-format syntax takes precedence. [YAML requires spaces for nesting](https://yaml.org/spec/1.2.2/#61-indentation-spaces), an explicit exception to this rule; see [PYTHON.md](PYTHON.md) for Python parsing rules.
- Preserve Markdown document structure; code blocks follow their language standard. Do not mechanically convert strings, test inputs, upstream text, or generated output; adjust generated-source formatting through its owning template as needed.

## File line endings

- Repository-owned text files use LF (`\n`), including source, test code, examples, documentation, configuration, and build/tool scripts. Apply the same checkout and editing policy on Windows, Linux, and macOS. Tab indentation is unchanged.
- Keep `.editorconfig` and `.gitattributes` consistent. Generators maintaining repository files explicitly use LF. Convert only physical line endings; retain encoding and BOMs, including UTF-8 BOMs required by Windows PowerShell 5.1. Do not rewrite `\r\n` escapes, protocol separators, user input, or literal data, or reduce language/tool support for CRLF input.
- Do not mechanically convert upstream text, imported license/build-tool files, binary resources, or byte-sensitive fixtures. Preserve their contents and their own or existing Git line-ending policy. Project-authored license guides still use LF. Do not exempt ordinary test code merely because its directory is named Tests.
- Windows `.bat` and `.cmd` launchers use CRLF. Preserve existing bytes for other multiline literals or fixtures that depend on physical line endings, with justified exceptions recorded for exact paths in Git and editor configuration. Remove an exception only after verifying that the original line endings no longer matter. Do not change test inputs, output content, or runtime behavior to satisfy the LF policy.

## Naming

- Names must describe responsibility, state, or value. Do not use temporary stage names such as `New`, `Old`, `Temp`, `Final`, or `v2`.
- Use one stable name for a concept within a module. Protocol names at language boundaries retain their contractual spelling.
- Boolean names should express an affirmative meaning so conditions read directly.
- New abbreviations must be understandable locally. Preserve the case and spelling of established public abbreviations.

## Comments

- All comments in owned, manually maintained source must be in English, including line, block, documentation, and region comments. External protocol quotations, third-party licenses, and generated content are not translated under this rule.
- Explain **why**, invariants, boundaries, ownership, threading, compatibility, or non-obvious choices; do not narrate obvious code line by line.
- Add necessary comments to types, member methods, and member variables as needed. Prioritize public surfaces, complex state, resource ownership, and easily misused interfaces; avoid redundant comments on simple members whose names are sufficient.
- Use the language's conventional line-comment syntax for single-line explanations, such as `//` in C-family languages or `#` in Python. Use conventional documentation comments for continuous explanations, parameter/return semantics, or formatted documentation.
- Reserve multiline block comments for explanations that need to be read together, licenses, or embedded text; do not wrap ordinary code in them.
- Keep comments adjacent to the code they describe. Update or remove stale comments when code changes.

## File-header copyright notice

The notice template below applies only to this project’s code with the corresponding copyright ownership; the root `LICENSE` records the exact holder. External projects identify the actual holders of their own code. Copying or distributing this project's code still requires retaining existing copyright and license text under the applicable licenses.

- Owned, manually maintained source, tests, examples, and build/tool scripts licensed under MIT use the two-line SPDX header below. Add it to new files; replace old owned notices during a coordinated migration or related edits.
- Copy the holder’s exact spelling from the root `LICENSE` into `<copyright-holder>` in the templates before use. Omit years and URLs from source headers. Keep the two lines in this order; do not translate field names, the holder, or the license identifier.
- C-family languages, C#, VeroScript, TypeScript/JavaScript, Java, and Kotlin use `//`:

```cpp
// SPDX-FileCopyrightText: <copyright-holder>
// SPDX-License-Identifier: MIT
```

- Python, CMake, PowerShell, and Shell scripts use `#`:

```python
# SPDX-FileCopyrightText: <copyright-holder>
# SPDX-License-Identifier: MIT
```

- XML, XAML, and Avalonia `.axaml` source use two separate XML comment lines:

```xml
<!-- SPDX-FileCopyrightText: <copyright-holder> -->
<!-- SPDX-License-Identifier: MIT -->
```

- Place the header before the first ordinary source line, followed by a blank line. Keep position-sensitive directives such as a shebang, encoding declaration, PowerShell `#requires`, or XML declaration first, then place the two header lines.
- For generated source and type declarations, emit the header through the owning generation template as needed; do not hand-edit generated output to satisfy header formatting.
- Preserve existing copyright and license notices in third-party or externally imported code. Do not replace them with this project's name or MIT identifier.
- Markdown documents, JSON/YAML configuration, images, and other resources do not require a code header in every file.
- The two SPDX lines do not replace the full license text. Keep the copyright year and complete MIT text in the root `LICENSE`, retaining its original holder; retain complete copyright and permission notices in distributions as required by the license.

## Line breaks and readability

- Break lines to reflect semantic nesting, parameter groups, and reading direction, rather than mechanically meeting a line width.
- Split long parameters, template parameters, initializers, or conditions into logical units; make continuation lines visibly belong to the same expression.
- Blank lines are not required immediately after every opening brace or before every closing brace. Use them according to the first and last logical blocks.
- Each line carries one principal intent. Exceptions for simple guards, empty functions, or idiomatic shorthand require an explicit language-specific rule; otherwise do not use shorthand.
- Where supported by the language, place `else`, `else if` (`elseif` in PowerShell), `catch`, `finally`, and
  the trailing `while` of `do…while` on a new line aligned with the preceding closing brace; never use joined
  forms such as `} else`. A clause with a block body also places its opening brace on a separate aligned line,
  with body statements and the closing brace on separate lines. Do not mechanically rewrite strings, test
  inputs, or upstream text to satisfy formatting; preserve line continuation semantics in macros.

- Formatters, templates that emit source code, and current documentation examples must follow this clause layout too; retain the original coverage purpose of test inputs.

```cpp
if (bPrimary)
{
	HandlePrimary();
}
else
{
	HandleFallback();
}
```

## Change principles

- Change only what the current task requires; do not delete, overwrite, or reorder unrelated user changes.
- Changes to behavior, public interfaces, serialization, or language boundaries must update declarations, implementations, callers, and tests together.
- Rejection, recovery, and fallback must be observable to callers; do not disguise failure as success.
- Validate and clean up external input, file paths, process arguments, and resource lifetimes at the boundary that owns them.


## Editors and embedded platforms

Core owns language semantics, execution services and generic VM/JIT contracts. The bytecode interpreter loop belongs to `Modules/Execution/VeroScriptBytecodeVm`; concrete JIT compilation, IR, native emission and third-party backend dependencies belong to `Modules/Execution`. A backend consumes public contracts and retains module ownership across instances, workers and retained code. Dynamic selection must validate ABI before invoking a factory. Failed selection must preserve the previous backend; backend selection and configuration must not change during execution.

Editor clients own settings and protocol presentation. Mobile/Android/iOS adapters consume the public C ABI, supply resources explicitly and document actual platform acceptance. Preserve creator-thread ownership, callback lifetimes and cancellation/reset contracts; do not describe quotas as a complete sandbox. Kotlin/Java follow module conventions for naming and structure, and this file’s common indentation rule; Objective-C/Objective-C++ follow the platform's ownership conventions and the native boundary rules. Keep public protocol identifiers stable.

User pages describe tasks, observable behavior and public API use. Developer pages describe source architecture, builds, test gates and publication; previous versions and environment-specific validation records remain in version control or ignored evidence, outside published documentation. Update all six current locales with interface changes. Translate UI validation and diagnostics through owned catalogs, preserving IDs, option keys and placeholders. Verify source links and both All/User publication; skipped platform or IDE tests cannot support a release claim.
