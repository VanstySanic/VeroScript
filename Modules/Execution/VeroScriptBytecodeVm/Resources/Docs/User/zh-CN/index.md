# 字节码 VM

`VeroScriptBytecodeVm` 是字节码解释器，后端 ID 为 `vs-bytecode`，发行名称为 `vs-bytecode-vm`。选择解释器与 `Vm`／`Mixed`／`Jit` 执行策略相互独立；原始 Core／C++ 宿主不会隐式选择解释器。

## 使用已安装 Runtime

保留匹配的完整 Runtime，包括 `Vm/`。自动选择先使用所选安装环境中的已启用候选，没有可用的自动 VM 候选时才以随附解释器作为产品后备。显式选择或禁用优先，不会被该后备覆盖。下列命令选择随附 ID 或匹配的已解压目录。选择 `.vspkg` 中的后端 ID 前，先将包安装到所选环境；安装本身不替换已固定的执行域。自定义 ID 需要包支持及所选环境。

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

## 嵌入静态 SDK

在已有 CMake 宿主目标 `app` 中查找两个匹配 SDK 包并链接两个目标，然后在执行前显式配置每个原始 VM；仅链接不会选中。C++ Session／Engine 宿主可配置 `FEngineConfig` 或使用对应显式选择 API。

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

## 选择与兼容性

提交执行时解释器选择冻结，Session 的 `Clear()` 与重载保留该选择；配置失败保留原选择，冻结后更换解释器需创建新 Session。执行编译脚本也需要显式 VM，不执行处理器的普通解析／编译无需 VM。SDK 头、Core、编译器／运行库、架构和后端身份必须匹配，仅版本同为 `0.1.1` 不能说明二进制兼容。移动／Web 静态产品显式装配自己的 VM，不加载桌面动态库。

[C++](../../../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/embedding.md) · [CLI](../../../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md)

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` 设置产品后备字段；Session 重载使用 `SetFallbackVmBackendFactory`，保留显式选择与环境候选。直接对原始 `VeroScriptVm` 调用 `Configure(vm)` 则通过 `SetVmBackendFactory` 明确装配静态工厂，低层 VM 自身不解析已安装候选。上面的静态 SDK 示例属于后一种情况。

<!-- docs-navigation -->

---

[User](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
