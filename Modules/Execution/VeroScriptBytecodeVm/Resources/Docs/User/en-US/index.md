# Bytecode VM

`VeroScriptBytecodeVm` is the bytecode interpreter with backend ID `vs-bytecode` and distribution name `vs-bytecode-vm`. Selecting an interpreter is separate from choosing `Vm`, `Mixed` or `Jit` execution policy. A raw Core/C++ host has no implicit interpreter.

## Use the installed Runtime

Keep the complete matching Runtime, including `Vm/`. Automatic selection first uses enabled candidates from the selected installed environment; the shipped interpreter is a product fallback when no usable automatic VM candidate exists. Explicit selection or disable takes priority and is not replaced by that fallback. The commands below select the shipped ID or a compatible unpacked directory. Install a `.vspkg` into the chosen environment before selecting its backend ID. Installation alone does not replace a frozen execution domain; custom IDs require package support and the selected environment.

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

## Embed the static SDK

In an existing CMake host target named `app`, find both matching SDK packages and link both targets. Then explicitly configure each raw VM before execution. Linking the library alone does not select it. C++ Session/Engine hosts configure `FEngineConfig` or use the corresponding explicit selection API.

```cmake
find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeVmAssembly.h"
#include <sstream>

std::ostringstream output;
VeroScript::VeroScriptVm vm(output);
VeroScript::BytecodeVm::Configure(vm);
```

## Selection and compatibility

A domain freezes its interpreter when execution commits; Session `Clear()` and reload preserve that choice. Failed configuration preserves the previous selection. Use a new Session to change a frozen interpreter. Compilation that executes script processors also needs an explicit VM; ordinary parsing/compilation without those processors does not. Match SDK headers, Core, compiler/runtime, architecture and backend identity; matching version strings alone does not establish binary compatibility. Mobile/Web static products supply their own explicit assembly rather than loading a desktop library.

[C++](../../../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md) · [CLI](../../../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md)

Unified automatic, explicit and disabled selection rules are documented in [Service backend selection](../../../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` sets the product fallback fields. The Session overload uses `SetFallbackVmBackendFactory`, preserving explicit choices and environment candidates. In contrast, `Configure(vm)` on a raw `VeroScriptVm` directly calls `SetVmBackendFactory`; the low-level VM does not resolve installed candidates itself. This distinction also applies to the static SDK example above.

<!-- docs-navigation -->

---

[User](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
