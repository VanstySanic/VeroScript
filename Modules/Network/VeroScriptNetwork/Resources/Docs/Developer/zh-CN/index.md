# 网络基础: 源码职责

Network 拥有不透明上下文、操作、配额及通用流／文件契约；Http/WebSocket 拥有协议门面和绑定。Boost 拥有 Asio/Beast、OpenSSL 集成和 BoostRuntime；Curl 拥有 HTTP 客户端及 curl 源码。模块卸载期间仍须保留服务／基础实现身份、创建镜像的寿命锚点和返回对象的控制块。

手工注册宿主仅给前端注册传入 FBackendCatalog::Binding()，注册全部提供者后调用 Update。提供者注册不得保留该目录；包管理器负责目录组合和项目服务选择。契约、基础实现或匹配 Core 身份变化后须重新构建所有参与镜像。

`Public` · `Internal` （明确版本化的提供者扩展契约） · `Private` · `Resources/Docs` · `Resources/Examples` · `Resources/Localization` · `Resources/Tests` · `Resources/Tools/CMake`

`VeroScriptNetwork/Internal` → `VeroScript::NetworkProvider` （不透明 context/operation、配额、流及升级契约）。

`Modules/Network/Backend/VeroScriptBoost/Private/Runtime` → `VeroScriptBoostRuntime`; `Modules/Network/Backend/VeroScriptCurl/Private` → HTTP 客户端。共用的前端／提供者构建助手归 `VeroScriptNetwork/Resources/Tools/CMake`。Network、Http 和 WebSocket 的包配置模板各归自身的 `Resources/Tools/CMake`；分类根目录仅协调 CMake 目标。

SDK 安装 Public 及 include/VeroScript*/Backend 下明确版本化的不透明扩展契约；具体 context/operation 布局仍然私有。禁止引用其他模块的 Private。发行前使用匹配 Core 配套重建后端和绑定，平台资格以实际测试结果为准。

## 源码构建与原生模块注册

根构建的 `VEROSCRIPT_BUILD_NETWORK`、`VEROSCRIPT_BUILD_HTTP` 和 `VEROSCRIPT_BUILD_WEBSOCKET` 默认 OFF。选择任一网络构建选项后 Boost 默认 ON，Curl 默认跟随 HTTP；可用 `VEROSCRIPT_BUILD_BOOST` 和 `VEROSCRIPT_BUILD_CURL` 显式选择后端。单独构建 `Modules/Network` 时，Boost、HTTP 和 WebSocket 默认启用。`VEROSCRIPT_NETWORK_BUILD_BINDINGS` 仅在 Core target 已存在时默认 ON；其他情况下须显式启用，并提供精确匹配的已安装 Core SDK。Curl 拥有 `VEROSCRIPT_CURL_USE_SYSTEM_LIBRARY`，默认 OFF，启用时要求 curl 8.13 或更高版本。内置静态库启用 PIC；Linux 内置配置要求 OpenSSL 开发文件。

先注册 Network，再注册协议模块，并声明可见依赖。静态宿主使用各模块的 `QueryModule` / `GetImplementationIdentity`，动态加载使用 `veroscript_extension_query`；静态 `ContentIdentity` 标识实际实现。后端模块不应链接第二份 Core。BoostRuntime 与前端基础实现必须匹配参与的宿主映像；私有后端符号和编译设置不进入前端 SDK。

<a id="network-execution-assembly"></a>

Core 提供执行契约，官方字节码 VM 已拆为独立后端。选择字节码执行的宿主须通过 `LoadVmBackend` / `SetVmBackendFactory` 显式提供匹配的工厂；编译期执行脚本扩展时，也须设置 `FCompileOptions::ScriptCompileExtensions.VmBackendFactory`。独立构建绑定测试须指定 `-DVEROSCRIPT_TEST_VM_LIBRARY=<VM 库绝对路径>`；完整 Core SDK 的 `bin/Vm` 包含对应平台的库。`BUILD_TESTING=OFF` 可仅构建绑定；仅数据层构建不需要 VM。

分别检查无后端前端 SDK、能力开关、缺失／不兼容服务、显式选择、动态／静态寿命、保留结果、移位 SDK 和精确依赖。允许执行时再验证原生协议及各平台；静态文档检查不代表运行时验收。

<a id="cross-image-results"></a>

操作结果由生产映像中的 `CreateTypedResult`／`OwnResult` 解包，保留 `FOwnedResult`、上下文及载荷所有权。`FOperation::Result<T>()` 比较生产方类型名、大小和对齐，不依赖跨映像 `std::any_cast` 的 RTTI 地址一致性；仍须匹配 C++ ABI／foundation 构建，不构成可移植 C ABI。头文件或结果存储改变后重建全部消费者／提供者。保持通过 swap 提取回调，使终态转换仅释放一次。

POSIX 原子不覆盖输出优先使用可用的 `renameat2(RENAME_NOREPLACE)`，仅在系统调用／文件系统不支持时退回硬链接；保留明确失败及临时文件清理。

<a id="provider-facade-ownership"></a>

提供者包装 facade 时须传入所属不透明 context，并保留创建映像的寿命锚点。HTTP 前端提供类型化 exchange 工厂，使 `shared_from_this()` 和 HTTP upgrade 共享同一控制块。仅在提供者分配的控制块中以成员持有 context，不足以保证安全卸载；返回对象和保留结果须使用共用工厂及所有权辅助接口。后端目录通过弱引用缓存工厂，避免与环境形成引用环；提供者注册不能持有该目录。

## 本地化资源暂存

源码辅助文件 `Resources/Tools/CMake/VeroScriptLocalization.cmake` 选择 `network`、`http`、`websocket`、`network-all` 和独立的 `json` profile。HTTP/WebSocket profile 包含 Network；`network-all` 合并三个网络模块。这些属于构建与部署输入，并非运行时 SDK API。暂存会替换目标 profile 并删除多余项，因此应先选择完整的资源根集合，不能依次叠加 profile。使用者的资源挂载方法由 User 诊断参考维护。

<!-- docs-navigation -->

---

[User](../../User/zh-CN/index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
