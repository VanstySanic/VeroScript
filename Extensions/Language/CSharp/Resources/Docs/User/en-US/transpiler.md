# C# transpiler

`VeroScriptCSharpTranspiler` converts a strict, statically analyzable C# 12
subset into VeroScript source. It uses Roslyn semantic information together
with `VeroScriptCSharpFrontend`; it does not perform textual token
replacement. The transpiler is a source-conversion tool, separate from the
C# runtime SDK, BindGen, and the VeroScript-to-C# AOT backend.

## Single-file conversion

```powershell
veroscript-csharp-transpiler Program.cs -o Program.vs `
	--source-map Program.vs.map.json `
	--veroscript veroscript
```

`--veroscript` selects the VeroScript executable or SDK root used to validate
the generated source. Validation is enabled by default. The command writes
the VeroScript file and source map only after parsing, lowering, validation,
and output staging succeed; an unsuccessful conversion does not replace an
existing output.

The public library API provides `CSharpTranspiler.TranspileSource` and
`CSharpTranspiler.TranspileFile`. Results contain generated source, source-map
JSON, and `CSharpDiagnostic` values. `CSharpTranspileOptions` controls
validation, the validator, the generated path, timeout, and C# parse options.

## Project conversion

```powershell
veroscript-csharp-transpiler Models/User.cs Services/UserService.cs `
	--project-root . `
	--output-dir generated/veroscript `
	--veroscript veroscript
```

Project mode parses all inputs as one C# compilation. It resolves cross-file
symbols, derives explicit imports, preserves paths relative to
`--project-root`, and writes one generated source file and one `.map.json`
source map per input. The default manifest is
`transpile.manifest.json`; use `--manifest` to select another path. The
generated files and manifest are committed as one output set and are rolled
back if staging fails.

The project API is `CSharpTranspiler.TranspileProject` with
`CSharpTranspileProjectOptions`. Its result contains generated file records,
the deterministic manifest JSON, and diagnostics. Each input must be inside
the project root and must occur only once.

If two inputs would produce the same relative output name, the later name
receives a deterministic path-hash suffix.

## Command-line options

| Option | Scope and meaning |
| --- | --- |
| `-o`, `--output` | Single-file output path. Required in single-file mode and not allowed in project mode. |
| `-h`, `--help` | Prints usage information and exits. |
| `--version` | Prints the transpiler version and exits. |
| `--source-map` | Single-file source-map path. Defaults to `<output>.map.json`; not allowed in project mode. |
| `--output-dir` | Enables project mode and selects the generated output directory. |
| `--manifest` | Project manifest path. Defaults to `transpile.manifest.json` inside the output directory. |
| `--project-root` | Root used to calculate project-relative output names. Every input must be inside it. |
| `--reference` | Adds a metadata assembly; repeat the option for multiple assemblies. |
| `--language-version` | Selects the C# language version used for parsing. |
| `--veroscript` | Selects the VeroScript validator executable or SDK root. |
| `--timeout` | Sets the generated-source validation timeout in seconds; the default is 120. |
| `--no-validate` | Skips compiler validation; use only when another explicit stage validates the generated source. |

Project mode is selected when more than one input is supplied, or when
`--output-dir` or `--manifest` is present. It always requires `--output-dir`.

## Output metadata

A source map records the source and generated paths and an array of mappings.
Each mapping identifies a generated line and the corresponding source path,
line, and column:

```json
{
	"version": 1,
	"source": "Program.cs",
	"generated": "Program.vs",
	"mappings": [
		{
			"generatedLine": 1,
			"sourcePath": "Program.cs",
			"sourceLine": 1,
			"sourceColumn": 1
		}
	]
}
```

The project manifest uses the following stable fields:

```json
{
	"version": 1,
	"kind": "veroscript.csharp-transpiled-project",
	"projectWideAccess": true,
	"files": [
		{
			"input": "Models/User.cs",
			"output": "Models/User.vs",
			"sourceMap": "Models/User.vs.map.json"
		}
	]
}
```

The paths in the manifest are project-relative and use `/` as the separator.
Keep the manifest, generated sources, and source maps from the same
transpilation run together.

## Supported C# surface

The supported subset includes:

- namespaces, enums, classes, structs, and static classes used as
  namespace-level declarations;
- public, protected, and private fields, symmetric automatic properties,
  constructors, static members, instance members, and methods;
- unconstrained generic types and free functions, primitive types, and the
  standard VeroScript container types;
- local type inference, calls, object and collection creation, indexing,
  casts, arithmetic, assignments, conditions, loops including `do`/`while`
  and classic `for` loops with multiple declarations or iteration expressions,
  `foreach`, and `switch`;
- ordinary `throw`, `try`, `catch`, and `finally` control flow;
- type aliases, constant locals, `params`, delegate/function types, and
  expression or block lambdas;
- reference-returning methods, reference locals, labels, and `goto`, lowered
  to the corresponding VeroScript aliasing and control-flow forms.

Explicit generic member invocations are preserved with their concrete type
arguments when the call is representable, for example
`Helpers.Pick<long>(1)` becomes `Helpers::Pick<int64>(1)`. Nested type
declarations are rejected with a diagnostic rather than emitted as invalid or
ambiguous VeroScript; move the declaration to namespace scope when it must be
transpiled.

`Console.Write` and `Console.WriteLine` retain their distinct newline
behavior. C# by-reference closure behavior is also preserved where the
supported lambda forms require it.
String literals retain NUL and other supported control characters. Unpaired
UTF-16 surrogates fail closed because VeroScript has no lossless UTF-8
representation for them.

Generated VeroScript follows the normal VeroScript syntax and indentation
rules. For example, a supported C# method is emitted as a VeroScript function
with explicit types:

```VeroScript
int Add(int left, int right)
{
	return left + right;
}
```

## Explicit boundaries

Features whose semantics cannot be represented faithfully are rejected with a
`VSCT2001` diagnostic rather than silently approximated. The current rejection boundary
includes:

- `async`/`await`, tasks, iterators, and LINQ query syntax;
- delegate declarations and events;
- nullable reference semantics, unsafe or pointer code, reflection, and
  `dynamic`;
- generic constraints and interfaces;
- catch filters;
- asymmetric or computed properties;
- constructor chaining; and
- partial types split across files.

`--reference` may be repeated for metadata assemblies, and `--language-version` selects the C# syntax version. These options do not expand the supported VeroScript semantics. `--timeout` bounds generated-source validation. Use `--no-validate` only when another explicit stage performs compiler validation; successful source generation alone does not establish that the output is executable.

## Integer lowering and numeric boundaries

The Transpiler preserves default unchecked integer addition, subtraction, multiplication, negation, increments, compound assignments and integral casts by wrapping at the resolved width. Shift counts are masked to 31 or 63. Compound assignments evaluate the target and its old value once before the right operand; automatic properties are emitted as fields. Integer division/remainder raise catchable script exceptions for zero divisors and signed-minimum/-1 overflow. Whole-valued `double` constants keep floating type.

<a id="numeric-and-console-boundaries"></a>

Explicit `checked`/`unchecked` contexts, nonconstant floating division/remainder or floating-to-integer conversions, and non-finite floating constants fail with `VSCT2001`, including when validation is disabled. Finite compile-time numeric constants are folded by Roslyn. Analysis uses the default unchecked compilation setting and does not import a consuming project’s overflow-check option.

Integral enum casts wrap through their declared underlying width. Unsigned 64-bit enums, enum arithmetic/bitwise mutations, nonconstant `double`-to-`float` narrowing and nonconstant 64-bit-integer-to-`float` conversion fail with `VSCT2001`. Console accepts one string, integer or boolean argument and empty `Console.WriteLine()`. `Console.Write` uses `std::console::write` without a newline; `Console.WriteLine` uses `std::print` with a newline. Boolean text is `True`/`False`, also in string concatenation and `+=`. Standard function qualification prevents same-named source functions from intercepting output or integer text conversion. Formatted/multi-argument Console overloads and floating, enum, object or array formatting fail closed instead of changing CLR text semantics.

The C# transpiler applies Roslyn operand promotions before mixed integer and floating comparisons, preserving C# float rounding before the core comparison.

Numeric conditional branches apply the implicit conversions selected by Roslyn, preserving integer widths and float rounding; only the selected branch is evaluated. Nonconstant 64-bit integer to float conversions are also rejected within conditional branches.

## Linked output paths

On Windows, `\\?\`, `\\.\` and volume GUID path aliases are compared by their final filesystem location. These aliases cannot bypass the same checks; distinct output paths remain supported.

The BindGen and Transpiler CLIs resolve existing directory and file links before staging output. Paths through symlinks or Windows junctions cannot overwrite an input source or name the same output twice. Distinct outputs inside linked directories remain supported.

`--timeout` and `ValidationTimeout` cover validator exit and reading both output streams for each invocation. If a launcher exits while a child keeps a redirected pipe open, validation still fails with `VSCT2004` when the deadline expires. Failed validation does not replace CLI output files.

Static fields and automatic properties accept omitted or compile-time constant initializers. Nonconstant static initializers and static constructors fail with `VSCT2001`, even when validation is disabled, because VeroScript global initialization cannot preserve CLR type initialization timing, side effects or dependency ordering. Initialize dynamic static state explicitly from `Main` or another application-controlled method.

String, array and mapped collection fields and automatic properties require an explicit nonnull initializer. Mapped collections include `List<T>`, `Dictionary<TKey, TValue>`, `SortedDictionary<TKey, TValue>`, `HashSet<T>`, `Queue<T>` and `Stack<T>`. Omitted initialization and assigning null to these types fail with `VSCT2001`, even without validation, because replacing CLR null references with empty strings or containers changes equality and exception behavior. Equality and inequality comparisons of supported nonnull arrays and mapped collections against null remain supported; string comparisons against null are outside the subset.

## Output-name collisions

Transpiler project output names are compared without case and after Unicode NFC normalization on every host. Inputs are allocated in path order. A collision receives a deterministic path-hash suffix; if that name is already occupied, an additional numeric suffix is used. Read the returned records or manifest for the actual filenames rather than reconstructing them.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
