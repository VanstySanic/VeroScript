# VeroScript C++ examples

> **Documentation:** [English](../../../Resources/Docs/User/en-US/index.md) · [Français](../../../Resources/Docs/User/fr-FR/index.md) · [日本語](../../../Resources/Docs/User/ja-JP/index.md) · [한국어](../../../Resources/Docs/User/ko-KR/index.md) · [简体中文](../../../Resources/Docs/User/zh-CN/index.md) · [繁體中文](../../../Resources/Docs/User/zh-TW/index.md)

This directory owns the C++ host integration examples. Script-only `.vs`
examples remain in the core module's `Resources/Scripts/examples/`.

- `Embed/` is built inside the repository. It demonstrates native embedding,
  host bindings, and debugger integration, and also runs as an integration
  self-test.
  Its `ISessionDebugBridge` example debugs an existing Session call, inspects its
  arguments, and invokes the Session's actual native bindings. The DAP/TCP
  `LiveBindings` examples launch a separate debug VM; sharing a binding catalog
  alone does not attach to the Session. A Tooling live domain can implement the
  bridge for persistent attach and protocol-thread request scheduling.
  With Python 3.11 or newer, `VeroScriptEmbedTcpTests` tests the TCP demo in a
  separate child process with bounded network waits and guaranteed process
  cleanup. It verifies reconnect after disconnect and explicit demo-host shutdown.
  Run `VeroScriptEmbedExample selftest` for the core path, or
  `VeroScriptEmbedExample selftest <backend-file-or-directory>` for an explicitly
  selected JIT backend. CTest registers both when a backend target is available.
- `CppMinimal/` is a standalone consumer of the installed Core and C++ SDKs.
  It verifies build identities, legacy Engine function pointer signatures,
  explicit file backend selection, session JIT cache
  reuse, live debugger argument mutation/removal, and the Tools/Aot imported
  targets. Packaging smoke tests configure it without repository-private
  include paths or build targets. The consumer source stays in this repository;
  functional SDK packages contain the headers, libraries and tools it consumes.
  Configure this complete consumer with the Core SDK and a C++ SDK containing
  Runtime, Tools and Aot. Its Session consumer links the tool/AOT targets; its
  separate Engine consumer links only the Runtime target.
  The Session consumer runs with Mixed/VM by default. Pass a backend file or
  directory as its first argument to exercise JIT cache reuse. The Engine consumer
  takes `engine-compatibility.vs` as its first argument and an optional backend
  path as its second. Set `VEROSCRIPT_TEST_JIT_BACKEND` when configuring to register
  additional JIT CTest cases. The Engine tests also cover Unicode script paths.
  Windows examples convert wide command-line arguments to the UTF-8 paths expected
  by the public runtime APIs using `PathUtil::ToUtf8`, which supports C++17 and C++20.

From the repository root, build the standalone consumer with actual SDK paths:

```sh
cmake -S Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal -B build/cpp-sdk-example -DCMAKE_PREFIX_PATH="/absolute/path/to/CoreSDK;/absolute/path/to/CppSDK/Runtime"
cmake --build build/cpp-sdk-example --config Release
ctest --test-dir build/cpp-sdk-example -C Release --output-on-failure
```

Core and C++ SDKs must come from the same release build. Use this repository's
`CppMinimal` directory as the consumer source when verifying extracted SDKs.
Applications that need only embedding can link `VeroScript::VeroScriptCpp` and
omit the Tools/Aot components in their own consumer project. CMake is optional
for application integration; configure equivalent public include directories
and matching libraries when using another build system.

Keeping the two categories separate prevents an in-tree dependency from making
an incomplete SDK package appear valid.

`CppMinimal/memory.cpp` demonstrates an owned byte buffer, an RAII write lease,
a script read-only view, external invalidation and exactly-once release.
