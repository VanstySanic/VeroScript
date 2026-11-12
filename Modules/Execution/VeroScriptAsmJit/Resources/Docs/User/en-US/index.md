# Optional JIT backend

## Selection and compatibility

AsmJit is an optional JIT backend, distributed separately from Runtime. Choose an execution policy independently of the backend. The [CLI backend reference](../../../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md#cli-backend-policy) defines inherited project choices, `auto`, `none`, explicit IDs/paths and fallback behavior. This guide covers the AsmJit package, native compatibility and manual loading.

With a compatible JIT installed and enabled in `MyProject`, these commands use the inherited choice and explicitly request automatic selection, respectively:

```text
veroscript --path ./MyProject --backend=jit ./MyProject/main.vs
veroscript --path ./MyProject --jit-backend=auto --backend=mixed ./MyProject/main.vs
```

For manual deployment from a portable archive, download the functional archive from `Release/Modules/Execution/Jit/<target>/<version>/archives/` and extract it separately from Runtime. No standalone installer is provided. Choose the extracted library or directory explicitly. A matching version string alone does not prove compatibility: use a backend matched to the host’s Core, architecture, compiler, standard library and runtime configuration. A selected portable backend may optimize without emitting native machine code.

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
veroscript --jit-backend="/path/to/backend-library" --backend=jit main.vs
```

## Paths and replacement

These file/directory loader rules apply to explicit paths, separately from module-environment ID/automatic selection. A library file pins one module. A directory searches its root and up to two child levels in UTF-8 lexical path order; the first compatible usable candidate wins. Windows names begin `VeroScriptJit`; Linux/macOS names begin `libVeroScriptJit`. At most 4096 candidates are accepted. Unreadable child entries are skipped. Relative paths use the current working directory. The path loader does not search environment variables, the registry or implicit default locations. Change selection only while idle; failure preserves the previous selection. An empty path in embedding APIs disables it.

## Embedding APIs

C++ uses `SetJitBackendPath`; C uses `vs_interop_session_set_jit_backend` and `vs_interop_session_get_jit_backend_info`; C# uses `SetJitBackend` and `GetJitBackendInfo`; Python uses `set_jit_backend` and `get_jit_backend_info`. Choose the execution policy separately. The module stays loaded while factories, instances, workers or retained executable artifacts need it.

The official package name and release archive prefix are `vs-asmjit` (for example `vs-asmjit-0.1.1-win-x64.zip`). The service backend ID used for name-based selection remains `asmjit`.

## Controlled native execution and static AOT

<a id="controlled-native-execution"></a>

Execution limits, cancellation, permissions, frames and values remain Core-owned. Unsupported/uninstrumented functions fall back before entry; errors never replay a function. Controlled Mixed JIT is opt-in through `FJitPolicy::bEnableControlledMixed` (C ABI `enable_controlled_mixed`, C# `EnableControlledMixed`, Python `enable_controlled_mixed`); the initial per-step native baseline can be slower than VM. Explicit Jit is independent of that policy. `ControlledNativeExecutions` / `AotExecutions` count actual native entries. Android/iOS SDK and device acceptance remain pending; desktop tests are insufficient.

See [Experimental mobile embedding — Controlled native execution and static AOT](../../../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/en-US/index.md#mobile-jit-selection).

Unified automatic, explicit and disabled selection rules are documented in [Service backend selection](../../../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[User documentation](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
