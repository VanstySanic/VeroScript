# Build 服务维护

## 规划与执行

`FBuilder::Plan` 冻结依赖图、源码／产物身份、提供者和启动器模板，不执行处理器、工厂或子进程。`FBuildRequest` 的服务边界路径为绝对路径；配置内路径相对所选项目根解析，不改变进程目录。`FServices` 持有宿主绑定目录、回调和显式工具链／环境身份；提供者的 `Plan` 不能产生执行副作用。

`FBuilder::Execute` 重新核对冻结输入，输入变化后必须重新规划。提供者通过 `FStepContext::TrackInput` 报告附加输入，不能替换既有摘要。仅在声明的输入字段解释产物引用，提供者 `options` 保持不透明 JSON。缓存身份包含配置、上游内容、提供者身份与输出哈希；自定义缓存需显式声明确定性和工具链身份。动态处理器及密码／信任敏感编译保守地重新执行。

## 发布与目标策略

每个目标在其卷内携带所有权凭据原子发布，不能覆盖外来或被修改的输出。后续目标失败不撤销已发布的独立目标。内容使用期间保留产物所有者和部署租约；`Deploy` 与 `LeaseDeployment` 消费已安装的冻结环境，不能回退安装。

应用内容按所选目标的平台／ABI 验证，而非构建宿主；实际原生执行仍需对应目标验证。源码应用保留可移植默认值及 runtime+compile 用途，拒绝开发专用搜索路径。Runtime 组件和启动器模板是显式验证输入，模板变化使规划／缓存失效。原生平台封装、签名与内容组装分开处理。

进程接收可执行文件和参数数组，不经过 shell。受限脚本仅使用声明的输入输出辅助接口及配额；可信原生提供者和进程不构成操作系统沙箱。资源变换前验证既有产物签名；重新加密只解码已有信封一次，改变签名内容需新策略，未变化的复制保留签名。验证失败时禁止发布，不得把秘密密钥序列化到配置或产物。

## 集成与验证

同步维护 `Public/VeroScriptBuild.h`、规划／执行、`Resources/Schemas/veroscript-build.schema.json`、CLI 解析及 Application／包适配器。根构建启用 `VeroScriptBuildTests`；`Resources/Examples/hello` 与 `Resources/Examples/generated` 展示应用／库／发行输出和脚本生成器。维护时核查过期规划、缓存失效、所有者租约、目标选择、签名失败与原子发布。CLI 操作和可运行示例归用户指南；平台执行及签名需另行记录验证。

<!-- docs-navigation -->

---

[用户指南](../../User/zh-CN/index.md) · [开发文档](../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
