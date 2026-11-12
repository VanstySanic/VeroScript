# VeroScript Interop SDK examples

[User documentation](../Docs/User/en-US/index.md)

`InteropMinimal/` is a standalone C11 consumer of `VeroScript::VeroScriptInterop`.
It creates a session, compiles source, performs one-shot initialization, calls a
function and releases owned handles. It validates the installed CMake business
version and the configured native runtime version independently.
The example requires `VS_INTEROP_CAP_MEMORY` and validates a quota-limited
buffer with an unaligned big-endian uint64 round trip and an explicit read lease.
Every lease and value is released on both successful and failed runs.

The default run uses the VM without an implicit backend. Pass a compatible,
independently installed JIT library file or directory as the executable's sole
argument to validate explicit backend execution. Configuring
`VEROSCRIPT_SDK_JIT_BACKEND` registers this additional CTest check.

On Windows, the build copies runtime DLLs, localization resources and runtime
license notices from the SDK beside the executable. Keep these deployed files
with the application so it can run without a developer toolchain installation.

Both standalone and complete-release packaging build this source project
against the staged SDK and run its CTest without copying it into the SDK.
Final product archives omit this example; obtain the source project separately
for integration with a published SDK.
Packagers supply `VEROSCRIPT_SDK_ABI_TEST_SOURCE` with the module's C11 header
test to add a compile/link/run proof for every public function against the
staged SDK header and runtime.
