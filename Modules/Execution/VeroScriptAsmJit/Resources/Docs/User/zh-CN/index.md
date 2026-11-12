# 可选 JIT 后端

## 选择与兼容性

AsmJit 是与 Runtime 分开发行的可选 JIT 后端。执行策略与后端分别选择；项目配置继承、`auto`、`none`、显式 ID／路径及回退行为见 [CLI 后端参考](../../../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md#cli-backend-policy)。本页介绍 AsmJit 包、原生兼容性与手动加载。

在 `MyProject` 中已安装并启用兼容 JIT 时，以下命令分别使用继承的选择和显式自动选择：

```text
veroscript --path ./MyProject --backend=jit ./MyProject/main.vs
veroscript --path ./MyProject --jit-backend=auto --backend=mixed ./MyProject/main.vs
```

手动部署便携归档时：从 `Release/Modules/Execution/Jit/<target>/<version>/archives/` 获取功能包，独立于 Runtime 解压，不提供独立安装器；显式选择解压后的库文件或目录。仅版本号相同不能保证兼容：须匹配宿主的 Core、架构、编译器、标准库与运行库配置。可移植后端可以在不生成原生机器码的情况下优化执行。

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
veroscript --jit-backend="/path/to/backend-library" --backend=jit main.vs
```

## 路径与更换

以下文件／目录加载规则针对显式路径，与模块环境的 ID／自动选择相互独立。指定文件可固定一个模块。指定目录则按 UTF-8 字典序搜索根目录及至多两层子目录，选择第一个兼容且可用的候选。Windows 库名以 `VeroScriptJit` 开头，Linux/macOS 以 `libVeroScriptJit` 开头；最多接受 4096 个候选。不可读子项会跳过。相对路径以当前工作目录为基准；不通过环境变量、注册表或隐式默认路径选择后端。只在空闲时更换；失败保留原选择。嵌入 API 中的空路径表示禁用。

## 嵌入 API

C++ 使用 `SetJitBackendPath`；C 使用 `vs_interop_session_set_jit_backend` 和 `vs_interop_session_get_jit_backend_info`；C# 使用 `SetJitBackend` 和 `GetJitBackendInfo`；Python 使用 `set_jit_backend` 和 `get_jit_backend_info`。执行策略单独设置。只要工厂、实例、工作线程或保留的可执行产物仍需要模块，模块就保持加载。

官方包名和发布归档前缀统一为 `vs-asmjit`（例如 `vs-asmjit-0.1.1-win-x64.zip`）。按名称选择时使用的服务后端 ID 仍为 `asmjit`。

## 受控原生执行与静态 AOT

<a id="controlled-native-execution"></a>

限额、取消、权限、帧和值仍由 Core 管理。不支持或未插桩的函数在进入前回退；故障后不重放函数。受控 Mixed JIT 须通过 `FJitPolicy::bEnableControlledMixed` 显式启用（C ABI `enable_controlled_mixed`、C# `EnableControlledMixed`、Python `enable_controlled_mixed`）；首版逐步骤原生基线可能慢于 VM。显式 Jit 不受这个成本策略限制。`ControlledNativeExecutions`／`AotExecutions` 统计真实原生入口。Android／iOS SDK 与设备验收仍待完成，桌面测试不足以代替。

参阅[实验性移动嵌入 — 受控原生执行与静态 AOT](../../../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/zh-CN/index.md#mobile-jit-selection)。

发行同时生成 `vs-asmjit-0.1.1-<平台>.vspkg` 与带顶层产品目录的便携 ZIP／tar.gz。`.vspkg` 使用标准 ZIP、根包清单和真实 JIT ABI，由现有包管理器校验、安装及缓存。示例：`veroscript install ./vs-asmjit-0.1.1-win-x64.vspkg --path ./MyProject`，随后 `veroscript --path ./MyProject --jit-backend asmjit --backend jit ./MyProject/main.vs`。手动解压便携包后按库文件或目录指定后端的方式保持不变。

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[用户文档](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
