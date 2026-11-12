# 包管理器维护

客户端负责获取、ZIP、求解、收据、事务、发布和环境组合。核心负责已解析模块/资源/原生契约，不依赖 HTTP、归档或求解。服务端负责账户、授权、不可变存储、策略和索引。

## 构建与归属

桌面默认 VEROSCRIPT_BUILD_PACKAGE_MANAGER=ON；嵌入式/移动/Wasm/Python 精简构建默认关闭。固定的静态 curl、miniz、nlohmann/json、PicoSHA2 及许可证由模块维护。VEROSCRIPT_PACKAGE_MANAGER_USE_SYSTEM_CURL 或已有 CURL 目标要求 CURL 7.85+ 和匹配运行依赖。自带 curl 源码固定 SHA-256。

VeroScriptPackageManager 安装组件导出头、库、Schema、依赖身份与法律声明，不再安装示例和 README；六语言文档通过 Resources 出版。使用内置 curl 时也安装其构建组件，Core SDK 打包会执行两步。正式打包随后根据已发布 Core SDK 版本及实际暂存 Core 库的 SHA-256 确定依赖，包括去除符号后的库；仅比较原生产品版本或沿用较早构建的摘要不足以证明匹配。外部 Core SDK 消费测试还会创建 HTTP transport，以实际验证包管理器 ZIP/HTTP 链接依赖。源码 API 的英文诊断及结构化 code 契约仍独立于包显示快照。

## 事务与寿命

先暂存源码快照和生成物，再公布不可变内容，以可恢复日志提交需求/锁/状态/所有权。提交前复核所有规划输入及存储身份，保留外部修改并拒绝过时规划。恢复/回滚不能沿用已取消令牌；读者必须拒绝未完成事务。

每代保留跨进程文件租约。clean 检查收据、全部目标配置、所有权、内容及活动租约，保留被修改或外部文件。恢复只能回收具备完整所有权收据及租约元数据的已公布孤儿代次；遗弃暂存由宿主另管。回调是同步借用访问，不得重入同一环境；原生实现经最终析构保持快照/模块租约。

事务重放在写入前校验整份日志（唯一且获准的目标、string/null 数据及单记录预算）和当前文件字节。每个文件必须等于日志中的 `old` 或 `new`；其他内容返回 `recovery-conflict`，保留日志与外部编辑。逐项写入前再次复核，已恢复的值直接跳过；回滚受阻时以 `recovery-required` 保留待恢复事务。先备份外部编辑，将冲突记录恢复为日志中的匹配值，完成恢复后再应用编辑。`JournalRecoveryTests` 覆盖部分提交、回滚、文件缺失、无效日志、重复路径及应用期间的外部编辑。

## 钩子编译

`PackageInstallProcessing.cpp` 复用核心公开的编译、分析控制和 VM 服务。每次预检／执行前编译都将 `FLimits.HookConstantEvaluationLimits` 传给 `FCompileOptions.ConstantEvaluationLimits`；它是宿主策略，不属于包 JSON 或锁文件字段。工作量、调用深度、表达式深度和求值内存必须为正数，允许零缓存。编译遵守整个包操作的期限／取消，并保留宿主分析观察器与原始取消诊断；运行时指令、时间、内存和数值策略独立生效。`ConstantHookTests` 覆盖源码与二进制库导入、泛型丢弃分支、精确 uint64 常量、限额、一次性取消、关闭缓存后重试及卸载事务。

## 验证

构建 VeroScriptPackageManagerTests，运行 VeroScriptPackageManagerCliTests 与 VeroScriptPackageManagerSdkTests。覆盖不可变来源、离线缓存、目标求解、可选用途、归档与名称、凭据范围、语言/格式、类型化/秘密输入、钩子限额、中断事务、并发编辑、清理租约、原生分析/静态/动态登记、显式 JIT 和部署闭包。SDK 消费者须以不匹配核心验证拒绝；平台/设备验收独立进行。

```sh
cmake -S . -B build/package-manager -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVEROSCRIPT_BUILD_PACKAGE_MANAGER=ON
cmake --build build/package-manager --config Release --target VeroScriptPackageManagerTests VeroScriptCli
ctest --test-dir build/package-manager -C Release --output-on-failure -R VeroScriptPackageManager
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

[仓库协议](registry-protocol.md) · [用户文档](../../User/zh-CN/index.md) · [结构定义](../../../Schemas) · [示例](../../../Examples)

## 依赖列表文件

`PackageRequirements.cpp` 在环境变更前展开位置参数和 `FRequest.RequirementFiles`。保留 UTF-8 校验、包含环检测、源文件/起始行诊断及 MaxManifestBytes/MaxMetadataBytes/MaxFiles/MaxDependencyDepth/MaxEdges 预算；仅合并来源和设置兼容的重复约束。保存输入摘要并在提交前重检，并发编辑返回 requirements-changed。列表内容不得启用 hooks 或修改注册表策略。覆盖 CLI 短/长选项、C++ 请求、嵌套及引用本地路径、约束求解和 locked/offline 重放。协议字段及诊断 code 保持稳定；列表内容属于用户输入，不是本地化目录。

## 依赖清单服务

`FRequest.RequirementFiles` 提供与 CLI `-r`／`--requirement` 相同的安装专用服务。顶层相对路径需要 `Environment.WorkingDirectory`；嵌套路径相对所在清单目录。`Private/PackageRequirements.cpp` 在管理文件写入前解析输入，通过 `FLimits`／`FWork` 限制 UTF-8 读取、包含、条目数和累计元数据，并在安装提交前复核来源摘要。规范化依赖和来源已保存在 `veroscript-project.json`／`veroscript.lock`，因此普通锁定项目无须附带原始清单；再次显式使用 `-r` 时仍需保留指定清单及本地归档来源。不涉及核心、注册源端点或格式版本变更。库、CLI 和已安装 SDK 验收需覆盖[依赖清单用法](../../User/zh-CN/index.md)。

## 环境与产物完整性

只读选择会拒绝中断的首次安装，即使项目／状态记录尚未创建。残留 `committed` 日志必须通过身份、目标和全部当前文件与 `new` 字节的校验后才能读取；读者不删除日志。首次写入前按最长阶段校验日志预算。

在线刷新将制品 `signatures` 视为不可变锁定元数据。`pack` 和 `deploy` 原子公布暂存产物，不替换已有目标。文件可安全回退到硬链接；目录需要系统原生支持无覆盖重命名。不支持时明确失败，不回退到覆盖写入。

环境组合复用快照已验证的依赖边，包括已裁掉仅编译期依赖的部署图。安装处理结果要求自身配置及完整已选依赖上下文匹配；无安装处理的原生加载器仅要求实际引用的外部库提供包具有相同不可变身份。不匹配时返回 `combine-processing`，应在同一环境中安装所需依赖图。无关脚本依赖变化不会使加载器失效。

部署同时在包值与项目快照中保留各包配置，作为 Overlay 复用时保持一致；部署输出目录允许末尾分隔符。`list` 和 `show` 在状态文件创建前也会拒绝中断的首次安装。提交前复核所有已写及保留的管理记录，期间不再调用宿主回调，继续遵守原期限。回滚保留外部编辑；与已写记录冲突时保留恢复日志。

## 应用与构建接入

`VeroScriptPackageRuntime` 提供冻结环境、部署校验、资源与原生注册，不链接 curl，不包含求解、安装、钩子或发布。完整管理器链接它。`FClientOptions.ArtifactDecoder` 负责归档信任；`FCoreEnvironmentOptions.ArtifactDecoder` 负责受保护库读取。锁文件不保存私钥或回调。要求签名的策略同样适用于缓存命中，签名归档缺少信任服务会失败。

部署默认导出 runtime；源码应用保留 runtime＋compile。消费方保留 `AcquireDeploymentDirectoryLease` 的共享租约，发布替换持有独占租约。`Build::FServices` 提供 `Deploy` 和 `LeaseDeployment` 回调，不通过安装补救缺失环境。

### 注册源需要配合的内容

现有 URL 不变。在制品记录中增加可选 `signatures`，为 1–16 项非空数组，绑定 `distribution`、最终归档 SHA-256、十进制字节数、算法、密钥 ID／版本。上传申请、提交、版本详情、确认、恢复、锁文件和镜像须完整保留同一记录。服务器自行计算长度及摘要，验证结构和绑定；不可修改已发行制品的清单、目标、依赖、摘要、大小或签名。同版本追加仍需元数据相同、目标不重叠，并原子执行与保留幂等语义。不发送私钥，发布者授权与消费端签名信任分别管理；不支持该字段时明确拒绝，不能静默丢弃。

CLI 使用 `veroscript install name --application-options /absolute/trust-options.json`。配置引用绝对密码库与密钥环路径，`requireSignature` 属于消费策略。本地归档使用 `<archive>.sig.json`。直接 URL 安装不猜测第二个签名下载 URL；使用注册源元数据，或将归档和签名明确下载到本地。外层安装 ZIP 保持标准可读；应用容器不自动成为安装包。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md) · [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
