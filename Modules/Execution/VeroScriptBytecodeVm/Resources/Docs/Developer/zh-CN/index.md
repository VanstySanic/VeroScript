# 字节码 VM 维护

Core 管理语言操作、值、逻辑帧、执行治理、清理和分派契约。本模块拥有 `Private/VeroScriptBytecodeVmExecution.cpp` 中的完整解释器，以及成员执行和诊断。静态库与动态模块只链接头文件执行契约 `VeroScriptExecutionContract`，不链接第二份 Core 二进制；`BytecodeVm::CreateFactory` 接收实际所属 Core 的服务。

`FVmInvocation` 接收 Core 已准备的帧，须保留域／上下文／程序代次、调用令牌和帧边界，不能重复准备参数或再次进入帧。首个语义操作之前设置 `bEntered`；只有尚未进入执行时才能返回 `NotEntered`。继续执行、插桩、治理以及未完成清理／reset 仍遵守 Core 契约。

每个活动上下文有独立实例，同步重入使用同一活动实例。官方后端可在同域／同线程完全空闲时重绑定；替换前解除旧程序，实例存续期间保留工厂／模块。分配实例的工厂负责销毁实例，查询描述符提供分配模块中的工厂销毁函数。

动态入口为 `veroscript_vm_query`。工厂使用前检查产品 `CoreVersion`、描述符大小、`BuildVmModuleAbi()`、ID／版本及能力。产品编码为 major:8/minor:8/patch:16，与 C ABI 版本编码不同；生成的实现身份跟踪模块源码。即使版本均为 `0.1.1`，公开头、编译器／STL／CRT、平台与布局也须匹配。

`VS_VEROSCRIPT_VM_VERSION` 是独立 VM 产品的 CMake 缓存设置，使用数字 `major.minor.patch` 格式，默认 `0.1.1`。它提供生成的 `VEROSCRIPT_VM_VERSION`、工厂／模块版本描述及已安装 CMake 包版本。全量打包器传入已登记的 `Vm` 业务版本，并拒绝旧缓存或不匹配产物。该产品版本与 `CoreVersion`、服务 ABI、实现／公开头指纹分别维护；修改版本号不能使不兼容二进制变得可用。

独立 CMake 工程消费已安装的 `VeroScriptExecutionContract`，根集成还向测试提供实际 Core。`VeroScriptVmBackendStaticTests` 与可选 `VeroScriptVmBackendDynamicTests` 覆盖两种装配；`Resources/Tests/Contract.cpp` 和 `Package.py` 涉及契约及包消费。修改时核对能力、进入前回退、重入、治理、清理／reset、所有者寿命与安装消费者。iOS／Web 使用静态装配，桌面结果不能代替目标平台和设备验收。

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/zh-CN/vm-backends.md)

<!-- docs-navigation -->

---

[Developer](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
