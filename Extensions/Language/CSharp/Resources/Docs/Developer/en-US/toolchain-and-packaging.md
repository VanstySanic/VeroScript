# C# toolchain and package contract

## Strict tool boundaries

When an entry point is present, the C# Transpiler lowers the one selected by the C# compiler, not a class-name heuristic. It accepts only a parameterless `static void Main()` or `static int Main()` declared in a static type; ambiguous, argument-taking, or instance-container entry points fail with `VSCT2001`. BindGen rejects C# indexers because Binding Catalog v1 has no host-index lowering. Neither tool silently omits an unsupported entry or member, and generation commits its output only after validation succeeds.

## Frameworks and SDKs

`VeroScriptCSharp` targets `net8.0` and `net9.0`, uses C# language version
12.0, generates XML documentation, and enables package API validation. The
runtime package is `VeroScript.CSharp`. CMake checks the installed .NET SDK
before building and runs managed tests for both target frameworks.

The C# frontend, Transpiler, and BindGen projects also target `net8.0` and
`net9.0`. Their build-time tools require a .NET SDK 9.0 or newer in the current
CMake toolchain. This is a tool-build prerequisite and must not be confused with
the runtime package's supported target frameworks. Packaged command-line tools
use `Major` runtime roll-forward when the requested .NET 8 or 9 runtime is not
installed, allowing them to run on a newer supported major runtime.

## Package contents

The runtime `.nupkg` contains the net8.0/net9.0 managed assemblies, XML API documentation, buildTransitive integration targets and license. Debug symbols are distributed separately in `.snupkg`. The source README.md, API.md and MOBILE.md are not packaged; obtain usage guides from the separate Resources documentation. The native Interop runtime remains a host deployment input. CMake installs the framework-specific managed outputs and generated NuGet packages into the SDK package area.

## C# AOT source bundles

`VeroScriptCSharpAot` exposes both a single generated C# compilation unit and a
deterministic `FCSharpAotSourceBundle`. A bundle contains relative source files
for runtime support, the lowered program, and the entry point, together with an
SDK project and machine-readable `ManifestJson`. `BuildCSharpSourceBundle`
packages already-emitted C# without rewriting the generated source.

Source, file, and checked-program entry points accept the same loader controls:
import paths, implicit import paths, `Defines`, circular-import policy, and
project-wide-access policy. The options-aware overloads additionally carry the
host catalog and code-generation settings. A failed load, semantic check,
lowering step, or bundle construction must return failure without a partial
build-ready bundle. Relative paths and manifest fields are output contract, not
presentation text, and must never be localized.

## Generated registration checks

BindGen packaging includes catalog generation, generated registration compilation
and runtime tests, C++ catalog import tests, and package verification. A change
to generated C# source or catalog shape is incomplete until all generated
registration consumers still compile and run.

## Source transpilation

`VeroScript.CSharpTranspiler` and `VeroScript.CSharpTranspiler.Tool` provide
strict C#-to-VeroScript conversion. Project mode resolves all source files as a
single Roslyn compilation, emits semantic cross-file imports, source maps, and a
deterministic manifest, and commits the output set atomically. Generated sources
are compiled by the real VeroScript validator by default.

## Tool dependency notices

The C# frontend, BindGen CLI and Transpiler CLI outputs retain the original Roslyn notices under `licenses/`; the frontend NuGet and both .NET tool packages include them too. These supplement the project MIT notice. When changing the Roslyn version, update the [notice sources](../../../Licenses/README.md) and preserve the complete `Roslyn-LICENSE.txt` and `Roslyn-ThirdPartyNotices.rtf` files, including the RTF encoding. Package verification compares them with the source notices. Do not localize upstream license text.

## Standard container generic arity

See [C# frontend — Standard container generic arity](../../User/en-US/frontend.md#container-mapping-arity).

## Integer lowering and numeric boundaries

The Transpiler preserves default unchecked integer addition, subtraction, multiplication, negation, increments, compound assignments and integral casts by wrapping at the resolved width. Shift counts are masked to 31 or 63. Compound assignments evaluate the target and its old value once before the right operand; automatic properties are emitted as fields. Integer division/remainder raise catchable script exceptions for zero divisors and signed-minimum/-1 overflow. Whole-valued `double` constants keep floating type. CLR/VeroScript execution parity tests cover these boundaries.

See [C# transpiler — Integer lowering and numeric boundaries](../../User/en-US/transpiler.md#numeric-and-console-boundaries).

The tool package gate installs the Transpiler and uses it to generate source, validate it with the real compiler and execute it. It checks unsigned division/remainder, signed wraparound, boolean text and newline behavior, catchable division by zero, and rejection of unsupported checked string constants even when validation is disabled.

C# AOT floating-point to integer conversions truncate toward zero and check the binary value against the target range before casting. They preserve representable 64-bit integer values without decimal rounding. Unsigned targets reject negative inputs, including negative fractions; NaN, infinity and out-of-range values fail. Regression checks cover integer boundaries in managed and NativeAOT execution.

See [C# AOT — Choosing C# AOT or the C# SDK](../../User/en-US/aot.md#aot-floating-conversions).

The C# transpiler applies Roslyn operand promotions before mixed integer and floating comparisons, preserving C# float rounding before the core comparison.

Numeric conditional branches apply the implicit conversions selected by Roslyn, preserving integer widths and float rounding; only the selected branch is evaluated. Nonconstant 64-bit integer to float conversions are also rejected within conditional branches.

See [C# runtime and Interop adapter — Retained callable disposal](runtime-and-interop.md#managed-marshalling-maintenance).

[Android and iOS / MAUI](../../User/en-US/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

Mobile-enabled packing uses this same project and package identity: `VeroScriptIncludeMobile=true` includes desktop .NET 8/9 and both .NET 10 Android/iOS targets with the verified native SDK.

When unused implementations imported through `core.vs` are removed, resolved bytecode calls retain executable dependencies, generic bodies retain their free-symbol dependencies, and type dependencies are collected from scoped AST declarations and expressions, with public type names resolved through the Core alias table. Calls such as `std::sqrt`, `std::algorithm::sort`, and types such as `std::delegate` retain their required implementations. Local names, instance member names, and ordinary string contents are not treated as global function references.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
