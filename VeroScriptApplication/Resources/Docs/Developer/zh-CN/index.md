# Application 服务维护

## 架构与边界

`VeroScriptApplication` 位于 Core 之上。Core 编码已编译映像和 `.vslib` 库；Application 管理标准 ZIP `.vsapp`、描述符、内容清单、准备流程和内容寿命。ZIP、原生密码适配器及 GUI 集成应留在 Core 外。`VeroScriptApplicationPackages` 将不依赖 HTTP 的 `VeroScriptPackageRuntime` 接入显式冻结环境；打开应用不能触发依赖求解或安装。

`BuildApplication` 返回已封存的文件集合，`WriteArchive` 序列化外层 ZIP；接受原始映像的重载必须保留映像字节及元数据。`FApplication::Open` 验证存储内容，`ReadEntryPackage` 仅检查编译元数据，不加载提供者或调用环境工厂。`Prepare` 保留环境和字节所有者，`FPreparedApplication::CompileSource` 应用自持的可移植源码默认值。Session 加载和重载应使用同一流程。

## 完整性、所有权与策略

目录、ZIP、单文件形式应采用一致的内容清单、路径规范化、ZIP 本地头、文件模式、大小／解压限制和哈希验证。拒绝额外文件、路径穿越、大小写冲突及特殊文件。可写缓存保留所有权凭据和协作进程租约，拒绝外来目录；原生内容消费者保留物化文件的租约。重载同时暂存代码与环境，失败时一并回滚；原生绑定目录发生变化时必须建立新的应用寿命。

保护服务先验证内容，再将明文交给 Core。签名信任与独立密钥阈值由消费者决定；缺少必需服务时失败，不能回退明文。重新组装已签名内容必须显式指定新签名策略。保留原生密码模块所有者，不序列化秘密密钥或回调，将内容签名与平台签名区分。源码入口保留可移植编译默认值，不引入开发专用文件系统路径。

## 集成与验证

根构建控制可选模块，`CMakeLists.txt` 分别导出 Application 与包适配器。同步维护 Core 身份、SDK 导出、`Resources/Schemas`、Interop／语言适配器、CLI 和 Runtime 委派。由根构建启用的 `VeroScriptApplicationTests` 及可选 `VeroScriptApplicationPackageServicesTests` 覆盖容器／准备流程及环境集成。修改相关流程时核查损坏输入、信任失败、所有者寿命、回滚、源码编译、迁移执行以及目录／ZIP／单文件一致性。平台启动器、签名和实机运行需要独立记录验证，静态文档检查不能代替这些结果。

`BuildApplication` 也接受已烘焙的 Core 应用映像，保留其字节和宿主元数据。`FApplicationServices::PreparePackage` 在环境准备完成后、Session 加载前配置编译入口的宿主服务；仅查看映像不会调用它。`ConfigurePackageApplicationServices` 的可选 `ProvidersReady` 回调向嵌入适配器提供已注册的提供者、其持有的环境及拥有快照的后端选择函数。选择顺序为宿主选项、应用服务配置、项目默认值，并保留内置部署的默认值及显式禁用选项。GUI 通过这些钩子接入标准 `.vsapp` 容器，Core 无需依赖 GUI 或 ZIP。 `PreparePackage` 仅准备绑定和资源，不能执行入口程序。

<!-- docs-navigation -->

---

[用户指南](../../User/zh-CN/index.md) · [开发文档](../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
