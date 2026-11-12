# 可替换 VM 后端

<a id="vm-provider-assembly"></a>

Core 提供程序、值、逻辑帧、语言操作、执行控制和调度；完整字节码解释循环属于独立模块 `VeroScriptBytecodeVm`（`Modules/Execution/VeroScriptBytecodeVm`）。后端 ID 为 `vs-bytecode`，安装包名为 `vs-bytecode-vm`。纯 Core 不隐式选择 VM 或 JIT。普通 Runtime 显式装配 `Vm/` 下的官方动态库；Interop、Web 和移动静态组合由产品层显式装配。

## 静态装配

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

## 动态装配

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

选择属于执行域。首次实际执行脚本后固定，Session.Clear() 不解除固定；每个活动上下文使用独立实例，同上下文同步重入复用活动实例。官方后端允许已完全结束的同域同线程上下文复用一个空闲实例。失败的配置保持原选择；未进入执行的失败允许修正。

编译处理器也需要显式执行配置：`FCompileOptions::ScriptCompileExtensions.VmBackendFactory` 或 `.VmBackendPath`。纯解析、普通编译及不执行处理器的分析不需要 VM。worker 和析构辅助上下文继承选择及现有治理规则。

## 独立构建和 SDK

执行契约只有头文件，不链接 Core；后端通过实际所属 Core 的服务表调用语言操作。宿主同时链接 Core 和静态 VM，或只链接 Core 并加载动态库。组合 SDK 可以携带两者，但链接本身不会自动注册。

```text
cmake --install <build> --component VeroScriptExecutionContract --prefix <sdk>
cmake --install <build> --component VeroScriptBytecodeVmSdk --prefix <sdk>
cmake -S Modules/Execution/VeroScriptBytecodeVm -B <vm-build> -DCMAKE_PREFIX_PATH=<sdk>
```

## 执行与发行约束

`EExecutionBackend::Vm/Mixed/Jit` 仍是独立的执行策略。JIT 无默认后端。VM 缺失或能力不足时明确报错，不重复准备参数、不隐藏回退。限额、取消、权限及共享的有界清理保持 Core 语义；清理未完成或后端破坏状态时必须重置 Session。版本使用 0.1.1，二进制还须匹配公开头指纹、编译器、STL、CRT、平台和布局。Windows 使用匹配的 /MD 配置；iOS/Web 使用静态装配。桌面测试不能替代移动工具链及设备验证。

独立发行同时提供便携 ZIP/tar 和可安装的 `.vspkg`；后者的包清单位于压缩包根目录。使用 `veroscript install ./vs-bytecode-vm-0.1.1-<target>.vspkg --path=<项目目录>` 安装。安装记录 `vm` 服务及 `vs-bytecode` 代号，不修改已有执行域的选择；便携包解压后也可直接按文件或目录指定。

## 仅编译，不装配 VM

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

GUI 等 C++ SDK 宿主通过 `FEngineConfig` 传入所选 VM；标记脚本使用 `Gui::FMarkupScriptOptions::Engine`。适配器保留宿主的选择，原始 C++ SDK 不隐式选后端。仓库独立模块测试可用 `-DVEROSCRIPT_TEST_VM_LIBRARY=<匹配的动态后端文件>` 显式装配测试 VM。
