# 18: Choose and use the C++ toolchain modules

This tutorial finishes a native integration workflow. You will decide whether
you need embedding, binding generation, C++-to-VeroScript conversion, or AOT,
then run the smallest tool that completes the task.

Prerequisites: matching `veroscript`, Cpp BindGen and Transpiler tools on your executable search path, plus a writable working directory. The paths below describe your inputs: prepare supported declarations in `Include/Types.hpp` and `Include/Api.hpp`, source definitions in `Source/Types.cpp` and `Source/Game.cpp`, and a valid `script.vs`, or replace every path with your existing files. Commands run from that working directory. Expected results are a merged `bindings.json` with API references and a manifest, generated VeroScript modules with source maps, and `generated.cpp` for the AOT step; generating these files does not register runtime host callbacks.

## Step 1: choose the boundary

Use the [C++ SDK documentation](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/index.md) as the
reference map:

- use `VeroScriptCpp` when a C++ application owns a live session or calls script
  functions;
- use `VeroScriptCppBindGen` when C++ declarations are the source of a binding
  catalog;
- use `VeroScriptCppTranspiler` when a supported C++ source subset should become
  VeroScript modules;
- use `VeroScriptCppAot` or `veroscript aot` when VeroScript should become C++17
  source for a native build.

These tools have different directions. BindGen describes a native API,
Transpiler converts source behavior, and AOT converts VeroScript for native
compilation. Do not use one as a substitute for another.

## Step 2: generate a binding catalog

Start with the C++ declarations that are part of the host interface:

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

Open the catalog and one API reference. The catalog is consumed by checking and
tooling; the API reference is for people. It does not register callbacks for
you. Add the matching native registrations in the host, then validate the
catalog and check a script that calls one registered function.

```powershell
veroscript bindings validate build/bindings.json
veroscript --check script.vs --binding-catalog build/bindings.json
```

If BindGen reports an unsupported declaration, keep the boundary explicit:
adapt the C++ API or register a deliberate host-facing wrapper. Do not weaken
the catalog to make an unsupported pointer or ownership rule appear valid.

## Step 3: convert a supported C++ source subset

When the source itself should become VeroScript, run the Transpiler and request
a source map:

```powershell
VeroScriptCppTranspiler Source/Types.cpp `
  -o build/Types.vs `
  --source-map build/Types.vs.map.json
```

For several files, use project mode so declarations are parsed and merged
before output is committed:

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp `
  --output-dir build/generated `
  --manifest build/generated/transpile.manifest.json
```

Open the generated `.vs` file, run `veroscript --check`, and inspect the source
map before integrating it into a workspace. Project output is not a linker;
internal-linkage collisions and unsupported ownership, pointer, exception,
template, or storage semantics remain errors.

## Step 4: generate native source from VeroScript

If the input is VeroScript and the deployment target is a native build, use AOT:

```powershell
veroscript aot script.vs -o build/generated.cpp
```

The result is C++17 source, not an executable. Compile it in the target host
project and verify callback lifetime, host synchronization, and runtime linkage.
For deterministic multi-file source artifacts, use the programmatic
`FCppAotSourceBundle` API and keep its entry source, generated fragments, and
manifest together.

Generated top-level globals and class static fields remain isolated per native
worker. AOT initializes each worker's global accessors in declaration order
before `main`; do not use generated globals as an implicit cross-thread sharing
mechanism. Use channels or synchronized host state when workers must exchange
data.

When a member lambda uses its receiver, AOT preserves the source type's
semantics: a class receiver remains a live retained object, while a struct
receiver is a value snapshot. Nested lambdas preserve the already captured
receiver, and a captureless lambda cannot use `this`. C++ AOT also rejects a
class-receiver capture from a destructor body because the receiver is already
entering destruction.

## Exercises

1. Generate a catalog and compare one catalog entry with the host registration.
2. Transpile two C++ inputs in project mode and inspect the manifest’s generated
   modules and source maps.
3. Generate AOT source for a script that uses a channel, then document which
   host state must be synchronized when the generated code runs on workers.

Next, return to [Host bindings and script integration](../advanced/12-host-bindings.md)
for the step-by-step session and catalog workflow. Use the [C++ SDK reference](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/index.md)
when you need the complete API boundary.

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](01-cpp-toolchain.md) · [fr-FR](../../fr-FR/cpp/01-cpp-toolchain.md) · [ja-JP](../../ja-JP/cpp/01-cpp-toolchain.md) · [ko-KR](../../ko-KR/cpp/01-cpp-toolchain.md) · [zh-CN](../../zh-CN/cpp/01-cpp-toolchain.md) · [zh-TW](../../zh-TW/cpp/01-cpp-toolchain.md)
