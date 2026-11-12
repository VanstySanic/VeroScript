# 安装与管理扩展包

项目根依赖明确指定来源后，同名且未指定来源的传递依赖使用该来源，仍检查各自的版本和摘要约束；显式来源冲突仍报错。离线安装协议包时，须先明确声明本地 `vs-network` 归档，再安装 `vs-http` 或 `vs-websocket`。

`veroscript-project.json` 保存项目依赖、包启用状态及配置；分发包根使用 `veroscript-package.json`；构建图使用 `veroscript-build.json`。三个文件分别验证各自的格式与 Schema。

可选包管理器将脚本库、资源和原生扩展安装到明确的项目或 Runtime/Core SDK。分发物是以 .vspkg 或 .zip 命名的 ZIP，根含 veroscript-package.json；它不同于 .vslib 脚本库、.vsapp 应用和 .vsbc 缓存。

## 选择环境

命令默认把启动目录作为项目根。--path 选择其他项目；--root 选择当前 CLI 所属的已验证 Runtime/Core SDK，--root --path 选择另一组件。相对路径基于启动目录。项目/SDK 默认存储在 .veroscript/packages，Runtime 在 Packages。没有用户安装范围、隐式合并或运行时自动安装。

## 命令

```sh
veroscript env --json
veroscript install ./library.vspkg --path ./app
veroscript install --path ./app --locked --offline
veroscript list --path ./app --json
veroscript show example-math --path ./app
veroscript disable example-math --path ./app
veroscript enable example-math --path ./app
veroscript verify --path ./app
veroscript uninstall example-math --path ./app
veroscript clean --path ./app
veroscript pack ./library --output ./library.vspkg
veroscript --path ./app ./app/main.vs
veroscript deploy --path ./app --output ./deployment
```

install 求解并安装；--locked 保留锁定，--offline 禁止联网。远程产物需完整缓存，本地归档来源仍须存在；已锁定目录快照可在原目录移除后继续使用。--upgrade 重新选版本，不能与 --locked 合用。list/show 查看，verify 校验完整性；disable 保留文件和版本锁，但不能破坏活跃依赖，enable 恢复选择。clean 只回收无引用、无活动租约且属于本环境的旧代次。

pack 不覆盖已有输出。请使用新输出路径，或先移动旧归档，再生成替代归档。

## 升级、卸载与重复安装

```sh
veroscript install example-math --path ./app --upgrade
veroscript install --path ./app --upgrade
veroscript uninstall example-math --path ./app
```

第一条重新选择指定的根依赖，第二条重新选择整个依赖图；没有独立的 `update` 子命令。`--upgrade` 仍遵守声明的版本约束和来源，不能与 `--locked` 合用。首次按无版本名称新增远程依赖时，会固定到实际选中的精确版本；要越过该固定版本，应修改版本要求，或显式安装 `name@version`。本地目录／归档依赖仍使用本地来源。[本地样例](../../../Examples/README.md)可在不连接仓库的情况下演示这两种升级命令。

未指定 `--upgrade` 时，优先复用仍满足要求的锁定版本和相同包内容。版本要求、来源或本地内容变化时，可能需要重新求解。重复安装仍会创建新的安装代次，并再次执行安装动作、可信处理器及获准的钩子，不是检测到已安装就跳过全部处理。存在钩子时，每次执行仍需 `--allow-hooks`。`--locked` 约束锁定结果，`--offline` 禁止联网，两者均不跳过安装处理。

`uninstall` 接受已声明的根依赖，移除对应依赖声明，并裁剪其他根不再需要的间接依赖；仍被其他根使用的共享依赖会保留。不能按名称直接卸载仅作为间接依赖存在的包，应移除或调整依赖它的根。无引用的旧安装代次可在活动租约释放后另行通过 `clean` 回收。

## 运行与部署

运行、检查、eval、REPL、watch、字节码打包和受支持源码 AOT 消费选定环境。未显式选择时，脚本从入口发现项目，eval/REPL 从启动目录发现，随后回退到当前组件。LSP/DAP/Interop 接入取决于各适配器，不自动共享 CLI 包选择器；DAP 拒绝未支持的显式选择。AOT 仍需目标对应的原生桥。

deploy 要求新输出目录，复制启用的 runtime 依赖闭包、资源、许可及可移植装载快照，不含仅 compile/development 依赖。应用/.vsapp 另行提供。跨目标安装仅校验、复制目标数据，不执行目标库；各平台仍须独立验收。

导出已安装的跨目标环境时，通过 `deploy --target <file>` 提供安装时使用的完整目标描述符，或调用 `FInstalledEnvironment::DeployForTarget`。导出过程校验并复制依赖闭包数据。加载部署以执行时仍要求目标与宿主匹配。

## 配置、错误与语言

`recovery-conflict` 表示管理文件与中断事务日志记录的旧值、新值均不一致。外部编辑和 `transaction.json` 会保留；回滚受阻时可能报告 `recovery-required`。先备份冲突文件，根据保留的日志解决冲突后再重试，不要直接删除日志。

--packages-dir、--cache-dir、--lockfile 显式覆盖位置。不要编辑存储路径迁移已有安装，应在新环境重装或部署。environment-changed 保留并发修改，应重新读取后重试；layout-conflict 表示状态/锁/所有权路径重叠；restart-required 表示存活原生实现不能与替代版本共存。

包命令的 --lang 和可重复 --fallback-lang 选择包显示文本，与编译器诊断语言独立。包管理器内置诊断目前包含英文消息和稳定 code 字段，应按 code 分支。--json 区分机器结果与提示消息。关闭可选包管理器的构建不提供这些命令。

[编写分发包](authoring.md) · [从 C++ 消费已安装环境](embedding.md)

## 从依赖清单安装

```sh
veroscript install -r requirements.txt
veroscript install --requirement "config/dependencies.txt" --path ./app
veroscript install -r requirements.txt --root
veroscript install -r requirements.txt --locked --offline
```

支持重复使用 `-r FILE`、`-rFILE`、`--requirement FILE`、`--requirement=FILE`，仅用于 `install`，也可同时传入单个包。文件名不限，不强制 `.txt` 后缀。命令行清单路径相对命令启动目录解析，与 `--path`／`--root` 的安装位置独立。清单内部的嵌套清单、本地目录和归档相对各自所在清单的目录解析。支持绝对路径、中文和空格路径；包含空格或注释字符的路径建议加引号。引号内 Windows 反斜杠保持原样，也可使用正斜杠。

清单使用 UTF-8，支持 LF、CRLF 和可选的 UTF-8 BOM：

```text
# Registry requirements use VeroScript versions.
gui==0.1.1
math >= 0.1.1, < 0.2.0 # both comparisons apply
widgets@0.1.1-beta.1
-r "lists/common.txt"
"../archives/local library.vspkg"
tools @ "../tools"
assets @ https://example.com/assets/download
```

每个逻辑行是一项依赖或本地 `-r`／`--requirement` 包含。忽略空行和 `#` 注释；行尾注释的 `#` 前需有空白且不在引号内。行末未转义的反斜杠将下一物理行用空格接续，之后再移除注释。同名条目取版本约束交集；不同显式来源或设置报错，避免静默覆盖。`名称 @ 路径或URL` 会核对实际包清单名称。本地路径转换为相对所选项目的路径，写入 `veroscript-project.json`；归档和 URL 摘要照常记录。保留项目其他依赖，显式清单条目更新对应根依赖。全部条目共用一次依赖求解和安装事务；`--locked`、`--offline`、`--upgrade` 和钩子授权语义不变。失败不会提交部分依赖，提交前会检测清单是否被并发修改。

版本比较支持 `==`／`=`、`<`、`<=`、`>`、`>=`；逗号或空格分隔表示同时满足。`name@<约束>` 也支持原有 VeroScript 语法，包括 `name@*`。裸包名保留已有项目依赖；首次添加的无版本注册源包会固定到选中版本。直接来源采用带空格的 `name @ <路径或URL>`；没有斜杠的目录应使用 `./目录` 或引号，以区别于包名。

这是借鉴 pip 习惯的 VeroScript 清单，包名和版本遵循 VeroScript 规则。不支持 Python extras、环境标记、VCS／editable、约束文件、`~=`、`!=` 或 pip 专属选项。嵌套清单必须是本地文件，不展开环境变量、`~`、shell 转义或通配符。注册源、镜像、凭证和安装权限通过现有命令行／宿主选项配置。循环包含、非法 UTF-8 和语法错误提供文件／行号诊断。文件读取、包含深度和数量、条目数量、累计元数据、取消和超时复用现有限额。清单必须显式指定，普通 `install` 继续读取 `veroscript-project.json`，不会自动查找文本清单。

### 离线示例

将[本地包示例](../../../Examples/README.md)中的 `library` 目录复制到 `workspace/vendor/library`，创建以下两个文件，再从 `workspace` 执行命令。列表结果包含 `example-math`，依赖会保存到目标应用的 veroscript-project.json 和锁文件。

`workspace/requirements.txt`:

```text
# Local package list
-r lists/local.txt
```

`workspace/lists/local.txt`:

```text
"../vendor/library"
```

```sh
mkdir app
veroscript install -r requirements.txt --path ./app --offline
veroscript install --requirement=requirements.txt --path ./app --locked --offline
veroscript list --path ./app --json
```

包含循环、无效编码及资源超限会被拒绝。诊断在可用时提供源文件和起始物理行；`requirements-changed` 表示提交前清单已变化，请重新读取文件后重试。

显式 URL 条目在 `--locked` 或 `--offline` 下，会复用同一 URL 已记录的摘要；具名引用还需匹配包名。缓存字节仍会重新校验，未知或变更的 URL 不会借用其他 URL 的缓存身份。离线重复安装需要保留项目依赖记录及完整、可验证的归档缓存。

首次安装在创建项目／状态记录前中断也会返回 `recovery-required`，请使用显式包写入操作完成恢复。合法且已完整应用的 `committed` 日志不阻止读取。`combine-processing` 表示生成物或原生加载器对应不同依赖／配置，应在同一环境中安装所需依赖图。`pack` 不替换已有输出，包括并发创建的目标。

包存储及管理文件拒绝符号链接和特殊条目。macOS 仅在 root 所有的 `/tmp`、`/var`、`/etc` 指向预期 `/private/...` 目标时允许这些系统别名作为普通临时项目或缓存的祖先路径。包归档条目及所选存储内部链接仍遵守原有拒绝规则。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md) · [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
