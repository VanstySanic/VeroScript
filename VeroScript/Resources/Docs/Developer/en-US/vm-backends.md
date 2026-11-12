# Replaceable VM backends

<a id="vm-provider-assembly"></a>

Core owns programs, values, logical frames, language operations, governance and dispatch. The complete bytecode interpreter belongs to `VeroScriptBytecodeVm` in `Modules/Execution/VeroScriptBytecodeVm`. Its backend ID is `vs-bytecode`; its package name is `vs-bytecode-vm`. Pure Core selects neither a VM nor a JIT implicitly. Runtime explicitly assembles the official dynamic library in `Vm/`; Interop, Web and mobile static products explicitly inject their VM.

## Static assembly

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptBytecodeVm.h"
#include "VeroScriptVm.h"

auto factory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
VeroScript::VeroScriptVm vm(output);
std::string error;
if (!vm.SetVmBackendFactory(factory, error))
{
	throw std::runtime_error(error);
}
```

## Dynamic assembly

```cpp
if (!vm.SetVmBackendPath("./Vm", error))
{
	throw std::runtime_error(error);
}
```

```text
veroscript --vm-backend vs-bytecode app.vs
veroscript --vm-backend ./custom-vm app.vs
veroscript --vm-backend ./Vm/VeroScriptVmBytecode.dll app.vs
```

Selection belongs to an execution domain and freezes when script execution commits. Session.Clear() does not thaw it. Each active context has a separate instance; synchronous reentry uses the same active instance. The official backend can reuse one fully idle instance in the same domain and thread. Failed configuration preserves the previous choice; a failure before entry remains correctable.

Script compilation processors also require an explicit execution choice: FCompileOptions::ScriptCompileExtensions.VmBackendFactory or .VmBackendPath. Parsing, ordinary compilation and analysis that do not execute processors need no VM. Workers and isolated teardown contexts inherit the selection and existing governance rules.

## Standalone build and SDK

The execution contract is header-only and links no Core binary. Backends call language operations through services supplied by their actual Core owner. Hosts link Core plus a static VM, or link Core and load a dynamic VM. A combined SDK may contain both; linking alone performs no registration.

```text
cmake --install <build> --component VeroScriptExecutionContract --prefix <sdk>
cmake --install <build> --component VeroScriptBytecodeVmSdk --prefix <sdk>
cmake -S Modules/Execution/VeroScriptBytecodeVm -B <vm-build> -DCMAKE_PREFIX_PATH=<sdk>
```

## Execution and publication constraints

EExecutionBackend::Vm/Mixed/Jit remains a separate policy. No JIT is selected by default. Missing VM/capability reports an error without preparing arguments twice or hiding fallback. Limits, cancellation, permissions and shared bounded cleanup retain Core semantics. Incomplete cleanup or invalid backend state requires Session reset. Product version is 0.1.1; binaries must also match public-header fingerprints, compiler, STL, CRT, platform and layouts. Windows uses matching /MD settings; iOS/Web use static assembly. Desktop tests do not replace mobile toolchain/device validation.

The independent distribution provides a portable ZIP/tar archive and an installable `.vspkg` with its manifest at the archive root. Install the latter with `veroscript install ./vs-bytecode-vm-0.1.1-<target>.vspkg --path=<project>`. Installation records the `vm` service and `vs-bytecode` ID; it does not change an existing execution domain. A portable extraction can be selected directly by file or directory.

## Compilation without a VM

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript)
```

```cpp
#include "VeroScriptCompile.h"

auto compiled = VeroScript::VeroScriptCompile::CompileSource(
	"int answer(){return 42;}", "answer.vs");
// compiled.bSuccess / compiled.Error / compiled.Module
```

GUI and other C++ SDK hosts pass their selected VM through `FEngineConfig`; markup scripts use `Gui::FMarkupScriptOptions::Engine`. Adapters preserve that choice; the raw C++ SDK does not select a backend implicitly. Standalone repository module tests can explicitly supply `-DVEROSCRIPT_TEST_VM_LIBRARY=<matching dynamic backend file>`.
