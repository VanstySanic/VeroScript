# 应用容器与运行服务

使用标准 ZIP `.vsapp` 容器分发可运行应用。容器包含 `veroscript-app.json`、`veroscript-app.files.json`，入口通常为已编译的 `program/entry.bin`，也支持源码入口。`.vslib` 保存已编译库。可安装分发 `.vspkg/.zip` 使用独立的根清单 `veroscript-package.json`，需要先安装再使用。

```sh
veroscript /absolute/application.vsapp
veroscript --check /absolute/application.vsapp
veroscript-runtime /absolute/application.vsapp -- argument1
veroscript-runtime /absolute/release-directory
veroscript --application-options /absolute/host-options.json /absolute/protected.vsapp
```

同一内容支持容器、目录、发行 ZIP 和可选的 PE/ELF 单可执行文件。启动器、Runtime、依赖环境独立可选。加载只消费显式选定的环境，不求解、安装、执行安装钩子或扫描 PATH。

C++ 推荐使用 `VeroScriptSession::LoadApplication`；底层使用 `CreateApplicationServices()` → `FApplication::Open` → `Prepare`。源码入口调用 `FPreparedApplication::CompileSource`，以保留随应用保存的宏、隐式导入及编译选项；编译入口使用 `Prepared.Package`。`ReadEntryPackage` 仅检查映像，不加载原生工厂。宿主必须保留所有者和租约，并遵守 Session 所属线程和重入约束。 `VeroScriptSession::LoadFile` 和 `VeroScriptEngine::RunFile` 同样识别应用容器及目录。

参阅[C++ SDK — 集成顺序](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/index.md#explicit-vm-selection)。

C ABI 提供 `vs_interop_application_services_create/destroy`、`vs_interop_session_load_application` 和 `vs_interop_application_read`。C# 使用 `Session.LoadApplication(path, optionsJson)`，Python 使用 `Session.load_application(path, options=...)`。宿主选项支持绝对 `cacheRoot`、`stdlibSearchPaths`、`cryptoLibrary`、`keyring`，以及签名信任策略；格式见随附 Schema。

清单覆盖每个文件的长度、SHA-256 和可执行模式。额外或缺失文件、路径逃逸、大小写冲突、特殊文件、损坏 ZIP 和超预算输入均拒绝。内容路径使用 UTF-8 和 `/`，最多 511 字节。可写缓存与只读应用分离，带所有权收据和跨进程租约；可修复本应用拥有的损坏缓存，拒绝覆盖外来内容。

可选 libsodium 适配器提供 XChaCha20-Poly1305 IETF 加密及 Ed25519 签名。加密限编译映像、库和明确选择的资源，不加密外层 ZIP。签名数组包含 1–16 项，绑定种类、字节数、摘要和密钥身份。信任公钥和独立签名阈值由消费端决定，缺失服务或验签失败不回退到明文。不要将密钥文件加入发行物。目录和单文件程序使用内容签名；平台代码签名另设构建步骤。

启动器至多委托一次，转发参数、输出和退出码；`--check` 不执行入口。单文件内容需在 Authenticode 签名前加入；其他平台封装由提供者负责。`VeroScriptSession::ReloadApplication` 同步暂存代码和内容环境，失败一起回滚；原生绑定目录改变时使用新的应用生命周期。取消不能强行中断阻塞的宿主回调。

SDK 使用 `find_package(VeroScriptApplication 0.1.1 EXACT CONFIG REQUIRED)` 并链接 `VeroScript::VeroScriptApplication`；Build、PackageRuntime 和环境适配器分别导出。真实目标包含 Core 构建身份、OS、CPU、环境和 ABI，不能只按文件后缀判断平台。

重新组装已签名应用时，必须显式指定新产物的签名策略。

GUI 应用同样使用此 `.vsapp` 容器。`gui-compile` 提供编译后的 GUI 映像和资源，GUI 检查／预览与通用 CLI inspect／检查／运行读取同一文件。执行时需要启用 GUI 的宿主，以及手动选择的兼容提供者。

## 宿主选项与签名记录

C ABI 提供 `vs_interop_application_services_create`、`vs_interop_application_read`、`vs_interop_session_load_application` 及对应销毁函数。宿主选项内 `cacheRoot`、`stdlibSearchPaths`、`cryptoLibrary`、`keyring` 路径必须为绝对路径；信任字段为 `requireSignature`、`minimumSignatures`、`requiredKeys`、`policyIdentity`。普通应用 ZIP 也可由包字节 API 读取。内部内容签名使用 `veroscript-app.files.json.sig.json`，单个产物签名使用 `<filename>.sig.json`；同一密钥的别名不能计作独立签署者。Runtime 组件描述符包含 Core、OS、CPU、环境和 ABI 身份，与包安装描述符独立。

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

## 独立 Runtime 执行后端选项

宿主选项放在应用路径前，应用参数放在 `--` 之后。`--vm-backend`、`--jit-backend` 选择执行服务，与平台／图形／GUI／图像／字体／文本布局提供者分别设置。它们接受 `auto`、`none`、匹配的文件／目录，或在包环境提供解析器时接受已安装 ID。自动 VM 选择可使用随附产品后备，显式禁用不会被覆盖。下列命令先检查／准备应用而不调用入口，再禁用 JIT 执行。GUI 容器仍需匹配且启用 GUI 的宿主与配套提供者。

```sh
veroscript-runtime --check --vm-backend auto --jit-backend none /absolute/application.vsapp
veroscript-runtime --vm-backend auto --jit-backend none /absolute/application.vsapp -- argument1
```

应用内容与缓存路径须使用真实文件系统条目。macOS 仅允许 root 所有、分别解析到 `/private/tmp`、`/private/var`、`/private/etc` 的系统别名 `/tmp`、`/var`、`/etc`。其他符号链接与特殊条目仍会被拒绝；这不允许应用内容或缓存内部出现链接。

<!-- docs-navigation -->

---

[用户文档](../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
