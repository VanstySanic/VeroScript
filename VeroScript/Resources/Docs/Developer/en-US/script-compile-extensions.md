# Script compile extensions

Compile extensions are ordinary VeroScript modules using `std::compiler` to register annotation documentation and processors. Core loads them before business-source processing. Runtime execution uses the generated business bytecode.

## Loading and registration

Place a literal extension import at the start of the business entry, after optional comments, whitespace, BOM or earlier compiler imports:

```vs
import compiler "builder.vs";
```

Ordinary import supplies helper code; import compiler runs registration and expresses registration dependencies. Cycles are rejected. Later business modules may use only extensions already loaded by the entry or host configuration. Extensions cannot rely on their own unregistered syntax.

Hosts supply `FCompileOptions::ScriptCompileExtensions.Roots` (Path and optional RegistrationEntry). The loader, C++ Session and Codegen share this configuration. `bAllowSourceImports=false` disables source activation while retaining configured roots. Source overlays, import paths and standard-library roots use the existing loader.

An extension imports `compiler.vs` and exports its root registration entry:

```vs
import "compiler.vs";
export void register_extension(std::compiler::registration reg) {}
```

A processor factory takes no arguments and returns `fn(std::compiler::context,std::compiler::output)->void`. Every compile task creates fresh VM state, registers once and creates its factories once. Callbacks retain their task state across phases. Native and script processors share ordering and conflict checks. Documentation registration alone has no executable behavior.

## Services and ownership

Context exposes phase, event, module, declarations, types, parameters, defaults, modifiers, generics, annotations, comments and source ranges. Output supports add_declaration, edit_source, replace_declaration, complete_body, replace_body, set_metadata, report_diagnostic and submit_operation. The full operation carries child metadata and origin mappings. It still undergoes core permissions, snapshot, conflict and semantic checks.

Registration, context service access and output are valid only during their calls. Owned read-only values may be retained; stale scopes cannot edit later batches. UTF-8 byte ranges cannot split characters. Generics are processed once at declaration time, then substituted normally. User implementations, virtual overrides and default bodies follow the common pipeline.

Initialization, registration, factories and callbacks share a total instruction/time budget and cancellation control. Compile VMs prohibit console, direct file/environment access, tasks, directory changes, native addresses and clocks. Hosts explicitly supply additional native bindings; undeclared nondeterminism disables prepared-result reuse. This is not a complete sandbox for untrusted native code.

Configuration and read_dependency return owned read-only values. Resource paths are relative to the active extension root. Source, package bytes, configuration, service contracts, and present or missing dependencies enter cache validation.

## Packages and integration

CompileScriptCompileExtensionSource compiles an extension without executing registration. Build a Library package and copy CompileExtensionManifest to the package options. Loading validates the entry, version, service fingerprint, bindings and dependencies before initialization. Open-generic business packages replay generated operations without executing the producer extension.

C++ hosts use ScriptCompileExtensions options; C uses `vs_interop_session_set_script_compile_options`; C# uses SetScriptCompileExtensions and Python set_script_compile_extensions. Session configuration changes require an idle session on its creating thread. CLI supports `--compiler-extension`, `--compiler-config`, `--no-compiler-imports`, `--compiler-instructions`, `--compiler-timeout` and `compiler-package source.vs output.vslib`.

Tools and source AOT share Core's prepared program; standalone output does not retain the processor VM. JIT runs the business program, while compile extensions always use VM. Prepared caches revalidate processing configuration and dependencies as well as the host's own source/import key.

Examples: [builder.vs](../../../Examples/ScriptCompileExtensions/builder.vs), [main.vs](../../../Examples/ScriptCompileExtensions/main.vs).

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](script-compile-extensions.md) · [fr-FR](../fr-FR/script-compile-extensions.md) · [ja-JP](../ja-JP/script-compile-extensions.md) · [ko-KR](../ko-KR/script-compile-extensions.md) · [zh-CN](../zh-CN/script-compile-extensions.md) · [zh-TW](../zh-TW/script-compile-extensions.md)
