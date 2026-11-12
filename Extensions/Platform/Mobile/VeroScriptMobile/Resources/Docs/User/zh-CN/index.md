# 实验性移动嵌入

Android 和 iOS 适配器保持实验性状态。使用与目标匹配的 SDK 产物，并在实际部署环境中验证应用。`FSessionOptions` 提供绝对标准库目录和源码资源快照。真实路径或虚拟路径均可使用，但全部虚拟源码须通过 `Sources` 提供；入口与相对导入须使用一致的绝对标签。可选缓存目录须真实、可写且为绝对路径，空值禁用缓存；资源更新后重新创建 runner。

```cpp
VeroScript::Mobile::FSessionOptions options;
options.StdlibDirectory = "/app/resources/stdlib";
VeroScript::Mobile::FRunner runner(std::move(options));
auto result = runner.Execute("int main(){ print(42); return 0; }", "/app/scripts/main.vs");
```

`FRunner` 同时只允许一次执行，每次创建新 Session，全局状态不保留。Execute 调用同步完成；Android 与 Apple 平台可能在内部工作线程执行，宿主回调不能假定运行于调用线程或 UI 线程。构造及执行可能读取资源或编译，应安排在非 UI 线程。对象存活期间可由另一线程请求取消；销毁前等待执行与取消调用结束。`FSession` 的创建、普通调用和销毁属于创建线程，宿主须提供至少 8 MiB 原生编译栈，其取消状态会锁存。回调自行执行权限和取消检查；取消不抢占编译，也不强制结束阻塞原生回调。

启用缓存后，入口与导入源码的内容均参与失效判断。命中会复用字节码，但仍可能加载和解析资源以核对当前内容，不能假定完全跳过启动阶段的编译准备工作。不可用或损坏的缓存退回正常编译路径。

默认：VM、1,000,000 条指令、100 ms、调用深度 256。只允许控制台输出；输入、文件、环境、目录变更和任务禁用。输出及诊断共享 1 MiB，错误副本最多 64 KiB。源码资源与单次入口各限制 16 MiB，最多 4096 资源，路径最多 4096 字节。这些限额不是 VM 堆配额或完整沙箱。宿主须通过 `vs_interop_locale_load_catalog_directory` 显式加载解包目录并选择语言；没有环境、cwd 或可执行文件旁资源发现，未配置时诊断保持英文。iOS 真机与模拟器均禁止动态 JIT 库和原生发码。

## 移动本地化的显式配置

移动 SDK 携带六语言目录，但不隐式加载。创建引擎、运行脚本前，将完整目录解包或复制到应用管理的真实目录，再显式加载；不能传 APK 资源或虚拟源码路径。Android AAR 位于 `veroscript/localization`，iOS SDK 位于 `assets/localization`。Android 提供 `VeroScriptEngine.loadCatalogDirectory(path)`、`setLanguage(tag)`，返回布尔成功值；iOS/C++ 使用 `vs_interop_locale_load_catalog_directory`、`vs_interop_locale_set_language`，错误缓冲区用 `vs_interop_buffer_destroy` 释放。检查每个返回值。语言选择是进程级而非每引擎独立，失败保留原选择。未加载时未知消息保持英文；编译诊断及适配验证错误使用同一选择，脚本输出不变。内存分配失败可能保留英文回退。 即使适配器上下文已翻译，操作系统错误详情仍可能使用系统库语言。

## 受管理字节存储限额

默认单缓冲区 16 MiB、保留总量 32 MiB。Core 移动端选项通过 `MaxBufferBytes` / `MaxRetainedBytes` 设置，0 表示无限。配额计算拥有容量、外部字节及增长峰值，不约束整个 VM 堆。默认仅允许控制台输出的策略关闭原生地址。

## 受控原生执行与静态 AOT

<a id="mobile-jit-selection"></a>

移动默认保留 VM。Android 可显式选择独立的 `vs-asmjit` AAR，并传入 JIT 文件／目录。只保留一份匹配的 `libc++_shared.so`；通过文件系统路径加载 JIT 的应用须解包原生库（应用设置 `jniLibs.useLegacyPackaging = true`）。iOS 真机与模拟器均禁止动态 JIT。

<a id="mobile-core-aot"></a>

`VeroScriptCppCoreAot.h` 的 `CompileSourceToCoreAot`／`GenerateCoreAotSourceBundle` 输出目标 C++、精确匹配的 `.vsapp`／`.vsbc` 和覆盖报告 JSON。使用目标 SDK 编译生成代码，选择 Mixed 并执行配套包。未覆盖函数、模块及字段初始化使用 VM；严格原生模式拒绝未覆盖工作。这一路径复用 Core 语义，与独立源码导出不同。

平台 API：[Android](../../../../../VeroScriptAndroid/Resources/Docs/User/zh-CN/index.md#android-native-selection) · [iOS](../../../../../VeroScriptIOS/Resources/Docs/User/zh-CN/index.md#ios-native-selection)。

参阅[可选 JIT 后端 — 受控原生执行与静态 AOT](../../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/zh-CN/index.md#controlled-native-execution)。

完整移动 SDK 包提供用于编译生成 C++ 的 `aot/include`，包含 Core 服务头与匹配的目标构建身份；与 Interop 头一起加入包含路径。这只是头文件集合，仍只链接一份 Interop 运行时。仅安装 AAR 时，须另外取得匹配的头文件集合。

VM 选择独立于 `ExecutionBackend` 和 JIT。`FSessionOptions.VmBackendPath` 留空时保留产品显式装配的静态 VM；非空时只接受绝对原生库文件／目录，配置失败不会回退。Android 完整构造函数提供 `vmBackendPath`，iOS 对应 `VSMConfiguration.vmBackendPath`；iOS 默认继续使用静态组合。

<!-- docs-navigation -->

---

[用户文档](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
