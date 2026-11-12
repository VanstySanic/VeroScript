# VeroScript Introduction

VeroScript is a cross-platform scripting language intended for embedded use. It uses a familiar C++-style syntax, but has its own type system, module system, and runtime. Source files use the `.vs` extension.

Native Runtime and SDKs target Windows, Linux and macOS. Browser and JavaScript hosts use the [WebAssembly SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/en-US/index.md); Android and iOS use the [experimental mobile adapters](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/en-US/index.md). Available components, execution backends and packages depend on the target and product version; see [Installation and distribution](../../../../../VeroScriptInstaller/Resources/Docs/User/en-US/index.md) and the relevant SDK guide for deployment requirements.

VeroScript is useful when part of an application's logic should be developed, checked, run, and reloaded independently of the host program. Typical examples include game logic, editor tools, automation tasks, and application extensions.

VeroScript is not a subset of C++, nor is it an interpreter that can execute arbitrary C++ source. Its syntax and object model are influenced by C++, but the language defines its own type semantics, memory model, and runtime capabilities.

## Execution model

A script generally follows this path from source to execution:

```text
.vs source file
  -> preprocessing
  -> lexical and syntactic analysis
  -> semantic analysis and compilation
  -> bytecode
  -> execution by the VeroScript VM
```

The preprocessor supports conditional compilation, object-like and function-like macros, stringification, token pasting, and file inclusion. The compiler performs type checking, overload resolution, generic instantiation, inheritance checks, and other semantic analysis. Modules are organized with `import` and `export`; the module loader resolves module paths, implicit imports, package dependencies, and import cycles.

Regular execution is based on the virtual machine and uses the mixed backend by default. On supported platforms, a faster execution backend can be used and the VM remains available as a fallback. VeroScript can also generate bytecode caches to reduce compilation work on later launches.

See [Command-line tool — Backend selection](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md#cli-backend-policy).

Performance is workload- and backend-dependent. Measure a representative release-style run, keep the input and backend fixed, and allow normal warm-up before drawing conclusions. Compilation caches reduce startup compilation cost; they do not change the cost of the script's actual work.

## Language scope

The table below gives a quick overview of the language. Detailed syntax and rules belong in the corresponding language reference.

| Area | Main features |
| --- | --- |
| Primitive values | Integers, floating-point values, booleans, strings, wide strings, enums, and `nullptr` |
| Data structures | Arrays, maps, sets, queues, stacks, deques, priority queues, and channels |
| Type system | `struct`, `class`, `interface`, namespaces, type aliases, and access control |
| Object orientation | Inheritance, interfaces, virtual methods, overrides, static members, constructors, and destructors |
| Generics and overloads | Generic types and functions, `where` constraints, default type parameters, and function/method/operator overloading |
| Functions | Default arguments, variadic arguments, reference parameters, reference returns, reference aliases, function values, lambdas, and reference captures |
| Control flow | Conditions, loops, `foreach`, `switch`, jumps, labels, and return statements |
| Standard library | Strings, paths, mathematics, formatting, files, input/output, and concurrency |

Use `struct` for data with value semantics and `class` for data with shared identity and lifetime. Interfaces, inheritance, virtual dispatch, and generics are the main mechanisms for building reusable abstractions in scripts.

## Running a script

Use the command-line tool to run a script directly:

```powershell
veroscript hello.vs
```

Common commands include:

```powershell
# Check without executing
veroscript --check hello.vs

# Start debugging mode
veroscript --debug hello.vs

# Package as a runnable application
veroscript --emit-package app.vsapp hello.vs
```

A script should provide a `main` entry point. The entry point may return `int` or `void`. `int main` is recommended because its return value becomes the process exit code; `void main` remains supported and is treated as a successful exit. The two common forms are:

```VeroScript
int main()
{
	print("Hello, VeroScript!");
	return 0;
}
```

```VeroScript
int main(string[] args)
{
	print(len(args));
	return 0;
}
```

Command-line arguments are available through `args`; the script path itself is not included in `args`.

VeroScript uses three common file formats: `.vsapp` is a runnable application package, `.vslib` is a library package imported by other scripts, and `.vsbc` is a bytecode cache file. A library package cannot be run directly as an application.

## Embedding in a C++ host

One of VeroScript's primary use cases is embedding it in a C++ application. The host can load or compile scripts, call script functions, methods, and constructors, and expose host functionality to scripts. Available bindings include native functions, host types, fields, methods, operators, generic types, and type aliases.

`VeroScriptEngine` provides high-level entry points for checking and running scripts. `VeroScriptSession` is intended for integrations that need finer control over modules, bindings, and runtime state. The host can also receive diagnostic, output, and debugging callbacks, and inspect or modify script types, fields, and globals.

Non-C++ hosts can use the `VeroScriptInterop` C11/C++17 ABI. It provides cross-language calls, host bindings, packages, reflection JSON, serialization, and callbacks through opaque session and value handles without requiring C++ object layouts. Browser and WebAssembly integrations use the separate `VeroScriptWebAssembly` SDK, which provides a TypeScript wrapper, language-catalog discovery, in-memory source checking/running, and virtual imported files.

Hot reload is opt-in and disabled by default. Compatible globals, objects, containers and closures can migrate, including captured `this`, when function identity and capture names and types remain compatible. Active channels, incompatible type changes and changed capture layouts are rejected. Native host side effects remain outside the script-state transaction.

## Tooling and AOT

The language tools share the compiler's preprocessing and semantic-analysis foundations. They can provide diagnostics, document outlines, semantic tokens, navigation, hover information, completion, signature help, reference search, rename, and formatting. The debugger uses DAP and runs through the VM path.

For deployments that do not need dynamic loading, the AOT backend can generate C++17 source and pass the result to a native compiler. AOT and VM execution are different deployment choices; the appropriate one depends on the host application's build, distribution, and debugging requirements.

## Version notes

This document describes the current public capabilities of VeroScript. Scripts, bytecode caches, packages, and host bindings should use compatible versions.

VeroScript deliberately maintains its own runtime boundary. The current release does not provide script-pointer semantics or standalone C++ concept syntax; `__VA_OPT__` for variadic macros, reference captures, reference returns, and runtime reflection are public capabilities subject to VeroScript's own type and lifetime rules. Graphical user interfaces are outside the core runtime.

Start with the [documentation home](../../../../../Resources/Docs/User/en-US/index.md). See [Language Basics](language-basics/index.md) for syntax and semantics, [Standard Library Reference](standard-library/index.md) for library APIs, and [Toolchain](../../../../../Resources/Docs/User/en-US/toolchain/index.md) for the command line, debugging, AOT, host integration, WebAssembly/WASM, and Interop SDKs.

<a id="backend-selection"></a>

## Service backend selection

When a host or CLI supplies an installed or deployed module environment, an omitted backend inherits that environment's `services.<service>.backend`. With no configured choice, `auto` tries enabled candidates in ascending backend-ID order and chooses the first compatible factory that can be created. Explicit IDs, factories and file/directory requests take precedence; failure does not select another ID. IDs identify service backends, such as `asmjit`; installation names identify packages, such as `vs-asmjit`.

`auto` explicitly overrides an environment default. `none` disables a service; an explicitly stored empty backend value also means disabled. An omitted value is different from an empty stored value. A product's assembled VM is a fallback only when automatic package selection finds no usable interpreter. Missing optional JIT leaves Mixed on the VM; forced JIT requires an available compatible backend. A required service reports failure if no candidate works.

Selection runs during factory creation and preparation, never while business execution is running. It does not replay operations after a runtime error. Interpreter domains remain fixed after execution begins. Quotas, cancellation, bounded cleanup, debugging and hot reload retain their existing execution rules. Factories and instances retain their module/environment owners.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](introduction.md) · [fr-FR](../fr-FR/introduction.md) · [ja-JP](../ja-JP/introduction.md) · [ko-KR](../ko-KR/introduction.md) · [zh-CN](../zh-CN/introduction.md) · [zh-TW](../zh-TW/introduction.md)
