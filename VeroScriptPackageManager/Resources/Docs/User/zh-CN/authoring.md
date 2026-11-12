# 编写分发包

`pack` 优先读取 `veroscript-package.json`；否则读取 `veroscript-project.json`，从 `packageManager.distribution` 取得分发元数据（可为对象或相对项目的 JSON 文件名）。项目打包会在归档内生成独立的 `veroscript-package.json`，不复制源项目配置。文件筛选必须包含生成的包清单及所有声明入口。

## 清单与依赖

ZIP 根放置 veroscript-package.json。名称为 2–64 位小写字母/数字及单连字符，排除设备保留名。版本为 major.minor.patch，可带预发行，不含构建元数据。要求支持精确版本或以空格分隔的比较式交集。运行时还核对路径、目标、身份及完整依赖图，不能只凭 Schema 判定可用。

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

依赖声明 runtime/compile/development 用途。--development 选择开发依赖；可选依赖需 `packages.<name>.enabled=true`。预发行需显式预发行要求或 --prerelease。同一环境的规范名称只对应一个版本和来源。部署只选择适用的 runtime 根声明，间接安装不赋予无关根声明直接访问权。

## 资源、输入与显示文本

entries.modules 暴露公开导入名，resources 声明具名资源组。包内路径须为可移植相对路径，不含越界、链接或设备别名。缺少可选资源不同于收据所属文件被删除/修改，完整性错误不能当作普通缺失。

本地化根含规范 BCP 47 目录及 en-US JSON 基线。译文可以缺少基线键，但不能新增键、跨文件重名或改变 formatId。普通字符串是字面量，花括号不自动格式化；格式消息显式使用 pattern 与 formatId=vero.named。标准键含 package.displayName、package.description、install.before/install.after、`inputs.<name>.description`；其具名消息获得 package.name 和 package.version。这套目录不改变脚本/编译器诊断。

```json
{
	"package.displayName": "Demo Math",
	"install.after": {
		"pattern": "{package.name} {package.version}",
		"formatId": "vero.named"
	}
}
```

输入保留 string、boolean、integer、enum 类型。普通输入用 `--input=name=<JSON>`，敏感字符串用 --secret-env=name=ENV_NAME。秘密仅留在本次操作，不用于声明式模板。`<package>@<version>.<input>` 优先于 `<package>.<input>` 和裸名；CLI 默认非交互。

## 安装处理与限额

声明式 copy/template 仅写新的 generated 区。脚本钩子需 --allow-hooks 及导出的非泛型 void entry(std::package::install_context)，只使用允许的资源和生成区写入；文件/环境/控制台/任务/地址/时钟服务及包原生登记不可用。取消与 VM 预算是协作机制，不是操作系统沙箱。

默认钩子 1,000,000 指令、2,000 毫秒，单次资源文本服务 16 MiB，钩子缓冲区 64 MiB，显示文本 65,536 字节；不限制整个堆。钩子获得类型化输入与独立显示快照。自定义可信处理器是带版本及实现身份的宿主服务。配置影响的生成物在改配置后必须重装。uninstall --skip-hooks 是明确的恢复选项并报告警告。

钩子编译复用核心的 `constexpr`、`if constexpr`、泛型及源码／`.vslib` 导入。每次编译共用一份常量求值预算：默认 1,000,000 工作量、64 层调用、256 层表达式、16 MiB 求值内存和 8 MiB 缓存。宿主通过 `FLimits.HookConstantEvaluationLimits` 配置；仅 `MaxCacheBytes` 可设为零以关闭缓存。预检与执行前编译、卸载钩子使用相同策略。编译遵守整个包操作的期限与取消检查；2,000 毫秒 VM 超时独立用于执行阶段。数值与缓冲区限额仍属于运行时策略。

## 仓库与发布

项目配置和锁文件均未指定官方源时，CLI 默认使用 https://veroscript.org/api。--registry-api 替换这一后备地址；packageManager.registries.official 配置项目源。嵌入宿主仍显式设置 FClientOptions.OfficialApi 或项目源。HTTPS 校验证书，--allow-http 仅供显式开发使用。--credential-env 读取 Authorization 头的值，例如 Bearer YOUR_TOKEN，不含字段名 Authorization:。省略 --credential-scope 时，凭据仅限 --registry-api 或内置官方 API；只有处于该来源与路径范围内的 URL 才会收到凭据。项目 API 或镜像位于此范围之外时，应显式设置 --credential-scope。凭据不得写入锁或源码清单。

配置的镜像需要 trustedMirror=true；--mirror 仅为本次操作授权官方源镜像。allowOriginFallback 默认为 false。重定向跨越来源或范围时会移除敏感请求头，且不会降级 HTTPS。

## 追加发布其他平台

使用 publish 创建版本，或向同一版本追加其他平台的制品。所有输入归档必须具有相同的包名、版本、依赖和冲突条件。清单中的目标不能彼此重叠，也不能与已发布制品重叠；不同的操作系统或架构约束可以区分平台，仅最低版本或能力要求不同不足以区分目标。可移植制品与所有目标重叠。

已发布制品的 ID、字节和元数据保持不变；完全相同的制品会直接复用，无需再次上传。修改已有目标的内容需要使用新的包版本，仅改归档文件名不会产生新目标。网络结果不确定时，请保留原归档与发布缓存，使用同一组文件重试；客户端会核对注册表后再确认成功。

每次命令中同一归档只提供一次；即使文件名不同，字节完全相同的两个输入也会被拒绝。

```text
veroscript install example-math@0.1.1
veroscript publish ./library.vspkg
veroscript publish ./gui-0.1.1-windows-x64.vspkg ./gui-0.1.1-linux-x64.vspkg
veroscript publish ./gui-0.1.1-macos-arm64.vspkg
veroscript install example-math@0.1.1 --registry-api=https://your-host/api/v1
```

[包结构定义](../../../Schemas/veroscript-package.schema.json) · [本地化结构定义](../../../Schemas/veroscript-package-localization.schema.json) · [清单示例](../../../Examples/Local/library/veroscript-package.json)

## 原生条目与 CLI 包

`entries.native` 必须提供 `id`、`path`、`linkage` 和 `roles`。可选的 `componentVersion` 用于核对原生描述符版本；分析 bindings/compiler 条目时，必须同时提供它和 `catalog`。角色支持 `bindings`、`compiler`、`provider`、`cli`，其集合须与库查询描述符一致。原生包需要匹配的目标身份，包括 OS、架构、环境、Core 构建及 `abis.native-extension`；使用对应 `veroscript env --json` 提供的值，不应只根据包版本推测。

`cli` 条目向 CLI 宿主服务注册命令及选项。包在选定环境安装并启用后，直接使用注册名称，无需指定库选择器；禁用包后不再发现其命令。分析模式及不提供 CLI 服务的宿主会跳过仅有 CLI 角色的条目。组合的 bindings/compiler 角色仍须满足分析目录及组件版本要求。安装过程照常验证不可变产物和加载器元数据。

[命令使用说明](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md)

VeroScript 官方安装分发包统一使用 `vs-` 前缀，例如 `vs-asmjit`；其 JIT 后端 ID 仍为 `asmjit`。官方仓库为授权的官方发布者保留此前缀。本地或私有源中的同名前缀不自动代表官方身份，仍须核对来源和产物。

原生条目 ID 由小写 ASCII 字母和数字组成，以单个连字符分段，不能以连字符开头或结尾。Schema 允许 2–64 个字符；原生描述符的 64 字节存储包含 NUL 终止符，因此实际原生模块最多使用 63 个字符。声明 ID 必须与描述符及预期 ID 相同。`entries.backends[].module` 引用已声明的原生提供者 ID，不是宿主服务 ID。

### 官方后端与 GUI 安装包

JIT 和启用脚本适配的 GUI／基础模块同时提供可安装 `.vspkg` 与带顶层产品目录的便携 ZIP／tar.gz。仅安装 `.vspkg`；普通发行 ZIP 不能通过改后缀变成安装包。安装默认使用当前项目，`--path` 支持相对或绝对项目路径，`--root` 选择 Runtime／SDK 环境。

```text
veroscript install ./vs-asmjit-0.1.1-win-x64.vspkg --path ./MyProject
veroscript --path ./MyProject --jit-backend asmjit --backend jit ./MyProject/main.vs
```

GUI 包沿用现有宿主服务契约：宿主需显式接入 GuiScript／`veroscript.gui.tools`，例如启用 GUI 的 CLI／Runtime。安装不会将 GUI 代码嵌入纯 Core；SDL3、RmlUi、Stb 和 FontBackend 可分别安装并由对应服务选择。离线安装需通过项目依赖或命令输入显式提供依赖归档，邻接文件不会自动成为依赖源。

## 后端条目与保留选择值

`entries.backends[]` 必须包含 `service` 和 `id`，遵循包 ID 语法（2–64 个小写 ASCII 字母／数字，以单个连字符分段）。`auto`、`none` 是保留选择值，不能作为后端 ID。两种条目形式互斥：`module` 引用已声明且具有 `provider` 角色的原生条目，不得再写 `path`、`contract`；或用 `path` 加 `contract` 声明专用 VM／JIT 库，不得写 `module`，且 `service` 与 `contract` 必须同时为 `vm` 或同时为 `jit`。仍需通过原生目标和 ABI 检查。项目 `services` 中省略 `backend` 继承自动选择，明确空值或 `none` 禁用，`auto` 请求自动候选。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md) · [en-US](../en-US/authoring.md) · [fr-FR](../fr-FR/authoring.md) · [ja-JP](../ja-JP/authoring.md) · [ko-KR](../ko-KR/authoring.md) · [zh-CN](authoring.md) · [zh-TW](../zh-TW/authoring.md)
