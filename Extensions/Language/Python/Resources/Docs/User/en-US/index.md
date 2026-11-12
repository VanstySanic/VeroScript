# Python SDK

`VeroScriptPython` is the CPython adapter for the VeroScript Interop ABI. It
lets a Python application load and run VeroScript, call script functions,
expose typed Python callbacks and classes, inspect metadata, and work with
packages and values without using private C++ runtime types.

The package supports VM, mixed, and JIT execution; structured diagnostics;
typed scalar and container values; host functions and classes; source overlays;
hot reload; JIT policy and statistics; reflection; package operations; retained
callables; external serialization; mutation events; and localization.

## Installation and runtime boundary

Install the published wheel with:

```text
python -m pip install veroscript
```

The extension requires the native Interop ABI to match exactly: `0x00000101` (0.1.1). A mismatch raises `ImportError` when importing `veroscript`. Keep the wheel’s native libraries together; do not replace its Interop library with one from another release. After a successful import, `veroscript.ABI_VERSION` reports the ABI and `veroscript.RUNTIME_VERSION` reports the runtime version.

The wheel contains the Python extension, the matched Interop library, runtime
catalogs, type information, and standard-library files. The Python runtime is
provided by the host application. The native extension is CPython-minor
version specific, so install a wheel matching the running Python version and
architecture.

The package is typed: `py.typed` and the native `_veroscript.pyi` contract are
included for static type checkers. Python host callbacks reacquire the GIL;
callback exceptions are reported as `CallbackError` rather than unwinding
through the C ABI.

Published wheels support standard CPython 3.11–3.15 with the GIL enabled; debug and free-threaded interpreters are not supported. Choose a wheel matching your operating system, process architecture and CPython minor version. Linux wheels retain their native platform compatibility; do not assume that a wheel built on another Linux distribution will work on yours.

The native module in a Python SDK archive or installer matches only the CPython minor version used for that package. To use a different supported minor version, install its matching wheel in that interpreter's environment.

## Minimal execution

```Python
from veroscript import Backend, Session

source = """
int main(string[] args)
{
	print("arguments={}", len(args));
	return 0;
}
"""

with Session(backend=Backend.MIXED, script_args=("one", "two")) as session:
	result = session.load_source(source, "main.vs")
	if not result.success:
		raise RuntimeError(result.diagnostics)
	result = session.run()
	if not result.success:
		raise RuntimeError(result.diagnostics)
```

## Documentation map

- [Sessions and execution](session.md): configuration, loading, calls, results, backends, JIT policy, hot reload, and threading.
- [Values and containers](values.md): scalar values, typed containers, channels, ownership, and retained callables.
- [Host bindings](host-bindings.md): typed functions, decorators, Python classes, callbacks, and mutation handling.
- [Packages, reflection, and localization](packages.md): packages, metadata, reflection, serialization, references, and language catalogs.
- [Python AOT](aot.md): standalone Python generation, source bundles, supported semantics, and deliberate exclusions.
- [Python frontend](frontend.md): static typed-Python analysis and the declaration AST.
- [Python BindGen](bindgen.md): typed-Python declarations to Binding Catalog v1 and project artifacts.
- [Python transpiler](transpiler.md): strict typed-Python source conversion to validated VeroScript.

The language reference remains the source of truth for VeroScript syntax and
semantics. This module documents the Python boundary and does not turn Python
syntax into VeroScript syntax.

[Python on Android and iOS](mobile.md)

## Managed memory

Host API: `session.memory` / `current_memory()` / `set_memory_limits`. See the [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md) reference for ownership, quotas, view expiry and leases.

- [Managed memory from the host](memory.md)

## variant

See [Interop SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#exported-value-lifetime).

```python
from veroscript import Session, Value
with Session() as session:
	payload = Value(42, kind="int32")
	box = session.variant("int", payload)
	assert box.variant_has_value()
	assert box.variant_type_name() == "int"
	extracted = box.variant_get()
	assert extracted.value == 42
```

Load an application with `Session.load_application(absolute_path, options=...)` and check its result before calling `run()`. Paths in the host options must be absolute. Follow the [Application loading contract](../../../../../../../VeroScriptApplication/Resources/Docs/User/en-US/index.md) for environment, signature and trust configuration.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
