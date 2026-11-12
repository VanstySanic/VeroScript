# 命令行工具

命令行程序名称为 `veroscript`。它可以运行脚本、只做编译检查、执行源代码字符串、进入交互模式、启动 DAP 调试器、构建包、校验绑定目录和生成 AOT 源文件。

## 初始化项目

```sh
veroscript project init
veroscript project init --path "./MyProject"
veroscript build --path "./MyProject"
```

初始化会补齐 `veroscript-project.json` 和 `veroscript-build.json`，版本为 `0.1.1`，
依赖为空，默认可移植应用目标输出 `app.vsapp`。新建默认构建配置时，还会为缺少入口的项目
生成最小 `main.vs`。已有普通文件显示为 `Kept`，不会覆盖；已有构建配置继续使用自己的入口，
不会额外生成 `main.vs`。文件目标被目录或符号链接占用时返回错误。重复执行可补齐缺少的配置。
`project --help` 和 `project init --help` 只显示帮助，不写文件。

省略 `--path` 时使用当前工作目录。相对路径以命令启动目录为基准，同时支持绝对路径和
`--path=<目录>`；不存在的目录会创建。目录名规范化为有效的小写 ASCII 项目名，必要时回退到
`my-project`，不会重命名目录。初始化只在本地生成文件，不安装依赖或执行脚本。
`veroscript.lock` 由成功安装依赖后生成，`.veroscript/` 由包管理操作按需创建。
执行构建需使用包含构建服务的 CLI。参见[最小项目样例](../../../Examples/Project/README.md)。

## 命令形式

```powershell
# Run a source script or application package
veroscript [options] hello.vs [args...]
veroscript [options] DemoApp.vsapp [args...]

# Check without executing
veroscript --check hello.vs

# Execute source text
veroscript -e 'print(1 + 2);'

# Enter the REPL
veroscript -i

# Generate C++17 AOT source (the default target)
veroscript aot hello.vs -o generated.cpp

# Generate standalone Python source
veroscript aot --target=python hello.vs -o generated.py

# Generate a deterministic C# source bundle
veroscript aot --target=csharp hello.vs --output-dir generated --bundle-name App

# Validate or format a binding catalog
veroscript bindings validate catalog.json
veroscript bindings format catalog.json -o formatted.json

# Inspect a package
veroscript package inspect DemoApp.vsapp
```

带 `string[] args` 的 `main` 会接收脚本路径之后的参数；脚本路径本身不包含在 `args` 中。无参数 `main` 会忽略额外参数。`int main` 的返回值成为进程退出码，`void main` 正常完成时返回成功码。

CLI 的进程退出码直接反映脚本的最终结果码：`int main` 的返回值会被返回，执行 `exit(n)` 时 `n` 优先，`void main` 或执行到 `int main` 末尾时为 `0`。这与嵌入 API 的 `FRunResult` 语义不同；嵌入宿主应把 `bSuccess` 作为“引擎是否正常完成”的判断，再单独读取 `ExitCode`。

## 常用选项

| 选项 | 作用 |
| --- | --- |
| `-e <source>` / `--eval <source>` | 执行源代码字符串；不能与 `-i`、`--check` 或监视模式同时使用。 |
| `-i` / `--interactive` | 进入 REPL。 |
| `--repl-strict` | 要求与 `-i` 一起使用；当最后一条输入失败时以非零码退出。 |
| `-I <path>` / `--import-path <path>` | 追加模块搜索路径；可以重复使用。 |
| `--stdlib-path <path>` | 添加标准库搜索根目录。 |
| `--implicit-import <file>` | 为每个编译源隐式导入指定文件；可以重复使用。它不修改源文件，也不会自动重新导出声明。 |
| `--no-circular-imports` | 拒绝循环模块依赖；默认允许循环依赖。 |
| `--allow-circular-imports` | 显式恢复允许循环模块依赖的默认行为。 |
| `--allow-project-wide-access` | 允许项目范围内访问非导出声明；应谨慎使用。 |
| `--binding-catalog <file>` | 加载宿主绑定目录；可以重复使用和合并。普通脚本运行或检查可使用，不能与 `-e`、`-i` 或监视模式组合。 |
| `-D name[=value]` / `--define name[=value]` | 定义对象宏或条件编译符号。 |
| `--macro NAME(params)=body` | 定义结构化宿主风格函数宏；`--predefined-macro` 是兼容别名。 |
| `--check` | 编译并报告诊断，不执行脚本；不能与 `-e`、`-i` 或监视模式组合。 |
| `--lang <tag>` | 选择诊断语言；标签必须能解析到当前加载目录中的文化目录。默认资源由发行包随附的文化目录决定；自定义目录可以提供更多语言。 |
| `-o <path>` / `--output <path>` | AOT 或绑定目录格式化的输出路径。 |
| `--output-dir <path>` | C# 或 Python AOT 多文件 bundle 目录。 |
| `--bundle-name <name>` | 源码 bundle 名称；省略时使用输入文件名。 |
| `--manifest <path>` | C# bundle manifest 路径；省略时使用输出目录。 |
| `--target <name>` | AOT 目标：`cpp`（默认）、`csharp` 或 `python`。 |
| `-h` / `--help` / `-?` | 显示帮助。 |
| `--version` | 显示版本、平台和 JIT 能力信息。 |

## 运行后端和缓存

| 选项 | 作用 |
| --- | --- |
| `--backend=vm` 或 `--backend vm` | 只使用字节码虚拟机。 |
| `--backend=mixed` 或 `--backend mixed` | 支持时使用 JIT，否则回退到 VM；这是默认后端。 |
| `--backend=jit` 或 `--backend jit` | 需要兼容的 JIT 模块，可显式指定或由模块环境自动选择；积极编译符合条件的代码，不支持的函数在进入前回退 VM。 |
| `--bc-cache=off` / `--bc-cache=on` | 关闭或启用 `.vsbc` 字节码缓存；默认启用。 |
| `--bc-cache-dir <path>` | 指定共享字节码缓存目录；未指定时使用平台用户缓存目录。 |
| `--vm-backend <id\|file\|directory>` / `--vm-backend=<id\|file\|directory>` | 选择解释器：随附 `vs-bytecode`、兼容库文件／目录，或在启用包支持时选择已安装后端 ID；与执行策略独立。 |

完整 Runtime 在 `Vm/` 中提供匹配的解释器，部署 CLI 时需保留该目录。`--vm-backend=vs-bytecode` 选择随附版本；其他不含路径分隔符及点号的 ID 仅在启用包支持时，从显式选定的已安装环境解析，安装本身不会自动选中它。文件／目录路径相对启动目录解析，必须与 Runtime 匹配。`--vm-backend` 选择解释器，`--backend=vm|mixed|jit` 选择执行策略，`--jit-backend` 选择 JIT 实现。执行脚本编译处理器也需要所选解释器；不执行这类处理器的普通检查不会运行入口程序。

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

调试使用 VM。`--watch` 在监视的文件变化时按选定的执行策略重新运行。

## 监视模式

`--watch` 在源文件或相关模块变化后重新运行脚本。`--watch-once` 等同于监视模式，并在首次非零运行结果后退出。检查模式、评估模式、REPL、调试模式和包文件不能与监视模式组合；已构建的包也不支持 `--watch`。

## 调试

标准输入输出 DAP：

```powershell
veroscript --debug hello.vs
```

TCP DAP：

```powershell
veroscript --debug --dap-port=4711 hello.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 hello.vs
```

`--dap-port` 的范围为 `1`–`65535`；`--dap-bind` 指定监听地址，默认值为 `127.0.0.1`，必须与 `--debug --dap-port` 一起使用。调试模式通过 DAP 的 `launch` 或 `attach` 传递程序和参数；命令行形式中的脚本路径会预置程序，路径后面的命令行参数则会成为该请求的默认脚本参数。`veroscript-dap` 是同一调试主机的薄封装。详细规则见[调试](../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/debugging.md)。

## 包、AOT 与绑定目录

应用包和库包使用 `--emit-package` 构建，`--package-file` 可以重复添加源模块，`--package-version` 设置无符号版本号，`--strip-package-debug` 移除源文件和调试行表信息。这些选项不能与 `-e`、`-i`、`--check`、监视或调试模式组合。详见[包与分发](packages.md)。

`aot` 子命令需要源脚本。默认目标是 C++17，并通过 `-o`/`--output` 生成翻译
单元。使用 `--target=python` 可生成 Python 3.11-3.15 独立源码，使用
`--target=csharp` 可生成面向 .NET 8/9 的 C# 源码。Python 和 C# 还支持使用
`--output-dir` 与 `--bundle-name` 生成源码 bundle。结果是源码而不是已链接的
可执行文件。详见 [C++ AOT](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/aot.md)、[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/aot.md) 和
[C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/aot.md)。

`bindings validate` 校验目录，`bindings format` 规范化目录；后者可通过 `-o`/`--output` 输出到新文件。详见[宿主绑定](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/bindings.md)。

## 诊断和环境变量

```powershell
veroscript --lang zh-CN --check script.vs
veroscript --version
```

常用环境变量包括：

| 变量 | 作用 |
| --- | --- |
| `VERO_PATH` | 模块搜索路径；Windows 使用 `;`，Unix 使用 `:`。 |
| `VERO_STDLIB_PATH` | 标准库搜索根目录。 |
| `VERO_CACHE_DIR` | 字节码缓存目录。 |
| `VERO_LANG` | 诊断语言，例如当前目录中已加载的任意规范 BCP 47 语言标签。 |
| `VERO_LOCALIZATION_DIR` | 本地化目录。 |
| `VERO_MAX_CALL_DEPTH` | 调整总递归调用深度上限。 |

命令行选项覆盖对应的环境设置；不受支持的系统语言或 `VERO_LANG` 会回退到英语，但显式指定的不受支持 `--lang` 值会作为参数错误被拒绝。需要查看当前安装版本时使用 `veroscript --version`。

本地化目录必须使用 `localization/<culture>/<component>.json` 布局；目录校验、搜索顺序和模块级目录见[诊断本地化](../../../../../VeroScript/Resources/Docs/User/zh-CN/localization.md)。

## 性能测量时选择后端

VM、mixed 和 JIT 优先后端具有相同的语言语义。mixed 会让符合条件的代码使用优化层，同时保留 VM 回退；JIT 优先会更早请求编译，但不保证每个函数都会被编译。

测量性能时请使用非调试的普通运行。调试使用 VM。字节码缓存影响编译时间，不影响脚本稳定运行时的实际工作成本。比较时应固定工作负载、进程生命周期、缓存状态和后端。

## Unicode 路径与参数

Windows CLI 接收 Unicode 进程参数，并在分派前转换为 UTF-8，因此非 ASCII 脚本路径、导入模块路径和传给 `main(string[] args)` 的参数能够保持原文。源文件和本地化 JSON 使用 UTF-8；读取重定向 stdout 或 stderr 的工具应按 UTF-8 解码这些流。

## 后端选择

<a id="cli-backend-policy"></a>

`vm`、`mixed` 和 `jit` 选择执行策略，与 JIT 后端选择相互独立。Runtime 不内置 JIT。存在已安装模块环境时，省略 `--jit-backend` 会继承 `services.jit.backend`；未配置时默认为 `auto`，按后端 ID 升序尝试已启用的候选，选择首个兼容且可用的后端。`--jit-backend=auto` 覆盖配置，`--jit-backend=none` 禁用 JIT。也可以显式指定后端 ID、库文件或目录；无效的显式选择会报错。`auto` 遇到无效候选 ID 或同一后端 ID 的活动提供者冲突时也会报错。自动选择需要模块环境。禁用 JIT 或自动选择找不到可用后端时，`mixed` 使用 VM，强制 `jit` 在执行前失败。CLI 选项须放在脚本路径之前。调试使用 VM。不支持的代码可在进入前回退；进入后的失败不会重放副作用。

自动、显式选择及禁用的统一规则见 [服务后端选择](../../../../../VeroScript/Resources/Docs/User/zh-CN/introduction.md#backend-selection).

```sh
veroscript --vm-backend=auto --jit-backend=auto main.vs
veroscript --jit-backend=none --backend=mixed main.vs
```

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
```

## 脚本编译扩展

参阅[脚本编译扩展](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/compiler.md#source-extension-activation)。

<a id="cli-compiler-options"></a>

`--compiler-config <key=value>` 每次添加一个字符串配置项，不同键可重复指定；值不按 JSON 解析，重复键会被拒绝，--compiler-instructions 和 --compiler-timeout 限制扩展工作，--no-compiler-imports 禁止源码激活但保留宿主配置的扩展。扩展使用编译 VM、协作取消和受限服务，普通运行时导入不会获得编译权限。生成代码仍接受普通语言规则检查；独立导出执行生成后的业务代码，不保留扩展 VM。处理器编写见开发指南。

[脚本编译扩展](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/compiler.md)

扩展整体默认预算为 1,000,000 条 VM 指令和 2,000 毫秒。0 表示关闭对应预算；这些限制不能强行中断阻塞的原生回调。

`install -r <file>` / `install --requirement=<file>`: [依赖列表文件](../../../../../VeroScriptPackageManager/Resources/Docs/User/zh-CN/index.md).

## 原生模块命令

已安装且启用、声明原生 `cli` 角色的包，可在选定的项目或组件环境中直接提供命令。使用该包实际注册的命令和选项名称。选择其他项目时，将 `--path <directory>` 放在模块选项之前。

本地库使用可重复的 `--cli-module <file>`。相对路径以启动目录为基准；加载器接受库文件，不扫描目录或 PATH。Windows 使用 `.dll`，Linux/macOS 使用匹配的 `.so`/`.dylib`。参数顺序为环境/后端选项及库选择、模块选项、命令或脚本；后续参数原样传递。

模块命令优先于同名文件；执行该文件时使用 `./name`。内置名称及重复注册名称不能被覆盖。模块命令不能与 check、eval、REPL、watch、debug/DAP 或字节码输出模式组合。`--help` 列出已注册项，`<command> --help` 不配置或执行命令，只显示用法；注册过程仍会加载原生模块。输出及描述由模块提供，`--lang` 将所选语言交给回调，不会自动翻译模块文本。Core CLI 帮助及部分校验消息仍为英文。

```text
veroscript my-command arg
veroscript --path ./project --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll my-command --help
```

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
