# 安装程序构建与捆绑契约

## 构建产物

安装程序应用和工具以 .NET 9 为目标，并使用 `VeroScriptInstaller.Core`。图形应用将本地化目录嵌入为资源，将 bundle 作为自解压内容封装进单文件可执行程序；工具无需图形宿主即可构建或检查 bundle 归档。

以下命令均从仓库根目录执行。构建并运行安装程序回归可执行文件：

`VeroScriptInstaller/global.json` 以 `latestFeature` 前滚策略选择 .NET SDK `9.0.100`。从仓库根目录开始执行示例，其中的 `Push-Location` 使 SDK 解析以 Installer 目录为起点。即使已安装 .NET 10 移动工作负载，仍须保留 .NET 9 SDK；C# 移动工具链与此分开。

```powershell
Push-Location VeroScriptInstaller
try
{
	dotnet build App/VeroScriptInstaller.csproj -c Release -warnaserror
	dotnet build Tool/VeroScriptInstaller.Tool.csproj -c Release -warnaserror
	dotnet run --project Tests/VeroScriptInstaller.Tests.csproj -c Release
}
finally
{
	Pop-Location
}
```

`Tests` 项目是控制台测试运行器；应使用 `dotnet run`，不能假定 `dotnet test` 能发现它。仅构建 App 不会嵌入可发行负载；必须使用打包脚本生成可分发的安装程序。

## 完整 Windows 发行版

完整管线支持 Release 配置的 `win-x64` 和 `win-arm64`。它需要 CMake、受支持的 MSVC 安装、用于完整产品集的 .NET 8/9 SDK、Node.js/npm、Web 测试依赖项，以及用于重建 WebAssembly 的 Emscripten。请在 `Extensions/Platform/WebAssembly/VeroScriptWebAssembly` 中执行 `npm ci` 准备 Web 测试依赖项；浏览器冒烟测试还需要可用浏览器，例如 Playwright Chromium。

```powershell
.\Resources\Tools\Packaging\package-veroscript-all.ps1 `
  -ConfigurePreset windows-msvc-vs2026 -Runtime win-x64
```

Windows ARM64 使用 `-Runtime win-arm64`，并选择匹配的预设，例如 `-ConfigurePreset windows-msvc-vs2026-arm64`；同时须安装 MSVC ARM64 工具和 Windows SDK。完整出版执行包括 Python wheel 矩阵在内的原生运行验证，因此应在原生 Windows ARM64 宿主上准备全部必需的 ARM64 CPython 解释器。仅在 x64 上交叉编译原生产物不能完成这些验证。`win-arm64` 表示原生 ARM64，不包括 ARM64EC 或模拟运行的 x64。

请选择与已安装 Visual Studio 对应的预设，或省略它以自动选择。`-OutRoot` 默认为 `Release`。业务版本、产物目录、清单与发布规则见[按业务发布](release-layout.md)。

`-SkipBuild` 会跳过原生和 Web 输出的重建，但仍会发布托管应用/安装程序并验证暂存的 WebAssembly SDK。仅当现有原生和 Web 输出确认与目标源代码及配置一致时才能使用它；它不是干净构建验证。

- Runtime、CoreSDK、Interop、Cpp、CSharp、Python、WebAssembly 各自版本目录下的归档与安装器。
- CoreSDK 中的公共原生符号归档，以及 Resources 中的松散资源。
- 每个业务的目标平台清单、校验和及集合索引。

正式产物位于 `Release/<business-route>/<target>/<version>/`，版本目录内按需创建 `archives/`、`sdks/`、`packages/`、`installers/` 和 `symbols/`。公共分发使用 `any`，WebAssembly SDK 使用 `wasm32`；构建中间产物仍位于 `build/` 和 `.tmp/`。发布锁、版本清理与回滚见[当前发布规则](release-layout.md)。

先决条件与操作步骤见本页；资源过滤和输出所有权见[按业务发布](release-layout.md)，验证边界见[发行验收](release-validation.md)。

## Windows 原生运行库依赖

MSVC Release 打包通过 `vs_install_windows_runtime` 安装选定工具链的可再发行 DLL。`VeroScriptWindowsRuntime` 组件负责 Runtime 根目录；Interop、C++ 工具及 Python 使用各自的安装组件。包组装还会在 SDK 和托管运行时布局的原生入口旁放置 DLL。使用工具链提供的可再发行文件，不要从系统目录复制 DLL；不附带调试 CRT 或 Universal CRT。

发行校验检查原生架构，并要求每个随包发布的原生入口旁存在 `msvcp140.dll` 和 `vcruntime140.dll`，x64 目标还须有 `vcruntime140_1.dll`。ARM64 使用其原生 CRT，并排除 ARM64 可再发行组件中附带的 x64 `vcruntime140_1.dll`。同目录还须包含 `licenses/MicrosoftVisualCpp/NOTICE.txt`。将源文件 `Resources/Tools/CMake/WindowsRuntimeNotice.txt` 保留为独立第三方声明，项目 MIT 许可证不能替代它。缺少依赖会导致出版失败，缺少 MSVC 可再发行文件还会使 Windows wheel 配置失败。Universal CRT 仍属于操作系统前置条件。

## Python 出版前置条件

完整发行选择 Python 时，须在运行流水线前准备完整 CPython 3.11–3.15 解释器矩阵。`-SkipBuild` / `--skip-build` 仍会构建并验证 wheel 及源码分发包。解释器选择、预检，以及各 wheel ABI 与 SDK 单一原生 ABI 的区别见 [Python 矩阵契约](../../../../../Extensions/Language/Python/Resources/Docs/Developer/zh-CN/toolchain-and-packaging.md)。

## 捆绑输入契约

捆绑清单是产品标识、平台和架构、组件 ID、相对文件路径、哈希和可选择产品组件的来源。安装程序在安装或修复期间不得根据当前仓库树重新构建发行内容。

安装程序捆绑是包含 `manifest.json` 和 `payload/` 的展开目录；它不同于可移植运行时/SDK ZIP。使用工具构建、验证和归档它；`--source-*` 名称必须与选定配置文件匹配：

```powershell
Push-Location VeroScriptInstaller
try
{
	$tool = 'Tool/VeroScriptInstaller.Tool.csproj'
	dotnet run --project $tool -c Release -- build-bundle --output ../.tmp/runtime-installer-bundle --version 0.1.1 --profile runtime --source-runtime D:/Packages/Runtime
	dotnet run --project $tool -c Release -- verify --bundle ../.tmp/runtime-installer-bundle
	dotnet run --project $tool -c Release -- pack-bundle --bundle ../.tmp/runtime-installer-bundle --output ../.tmp/runtime-installer-bundle.zip
}
finally
{
	Pop-Location
}
```

上例中的 `D:/Packages/Runtime` 只是已准备运行时负载的占位符；该负载包括所需 CLI/LSP/DAP、标准库与本地化文件。定制产品和通用单产品发布器请参阅[产品定义](../../../Examples/Products/README.md)。

## 单文件 bundle 承载

`App/InstallerBundleContent.targets` 将 `InstallerBundleArchive` 拆为至多 32 个、每个不超过 1 GiB 的内容分片，从 `VeroScriptInstaller.EmbeddedBundle.zip.0000` 连续编号。单文件发布通过 `IncludeAllContentForSelfExtract` 将它们纳入可执行程序，无需用户另行下载分片。程序集元数据记录完整 ZIP 的 `VeroScriptInstaller.BundleSha256` 与 `VeroScriptInstaller.BundlePartCount`。

`EmbeddedInstallerBundle` 打开已解出的分片，拒绝链接及非法数量／大小，核对完整 ZIP 的 SHA-256，并提供可定位流，不将整个归档装入内存或另复制成 ZIP。Linux 下将 bundle 展开到 `AppContext.BaseDirectory/VeroScriptInstaller.Unpack`，其他平台使用 `Path.GetTempPath()/VeroScriptInstaller`；仍执行解压与清单校验。发布目标会拒绝缺失或空的单文件程序；这些保护不代表大型安装器已通过目标平台验收。

失败时默认移除本次拥有的发行暂存目录。Windows 完整发布的 `-KeepFailedStage` 或 Unix 完整发布的 `--keep-failed-stage` 会保留失败暂存并输出路径，供诊断使用；成功后仍会清理。保留文件只是候选，复用前重新核对源码、配置和摘要。

## 验证与可选签名

嵌入式捆绑和外部捆绑必须通过相同的清单、路径、哈希、组件和平台验证。改变归档布局、清单指纹或安装记录架构时，必须同时更新修复、迁移、卸载和包测试。

公共 API 变更还必须更新组件负载契约。当前 Core 组件要求聚合头、JIT、值和 VM 公共头；C++ 要求其会话头与 CMake 包；Windows Python 运行时要求包入口点、类型存根、`py.typed` 标记和 Interop DLL。最终发行验证器还会检查 Core/C++/Interop/Python 归档中的会话缓存 API 标记，以及松散的和 NuGet 所含的 C# 目标框架程序集。它也要求所有 C# NuGet/符号包，以及两种 WebAssembly SDK 变体和声明文件。这些检查有意拒绝由过期 API 输出组装的包。

打包管线会在生成的 EXE 上执行 `--verify-localization` 和 `--verify-embedded-bundle`，包括可选签名之后。必须检查退出代码：这些开关执行自检，并非交互式安装或人工 GUI 验收测试。

本地化验证检查所有六个内置目录、全部八个产品配置文件及其组件资源键、非空值和一致的格式占位符。它还演练部分外部覆盖层、中文语言标记回退，以及进度/结果/通用失败的语言切换。缺失的内置翻译不得通过回退到英语而静默通过。

本地化资源键属于清单指纹。更改它们时必须一起重建捆绑和安装程序 EXE。应保留原始安装程序以修复现有安装：同一版本但指纹不同的重建包会被有意拒绝用于修复；正常更新模式支持在同版本上安装重建包。

当前发行版的签名为可选项。未配置证书时，Windows 脚本会警告并继续。以后若要启用强制签名，请配置 `VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT` 和 `VEROSCRIPT_TIMESTAMP_SERVER`，然后在完整管线中使用 `-RequireSignedInstallers`，或在单产品脚本中使用 `-RequireSignature`。最终校验和在签名后生成。完整管线也会将 `-RequireSignedInstallers` 传给最终发行验证器；独立验证暂存发行版时也必须传入该开关。它要求每个发布安装器的 Authenticode 状态均为 `Valid`。

完整 Windows 发行通过 `Resources/Tools/Packaging/package-veroscript-all.ps1` 手动构建。Linux/macOS 使用 `Resources/Tools/Packaging/package-veroscript-all.sh`，在相符的原生主机上构建，并校验选定平台的业务发行目录。仅通过 Windows 检查并不能认证 Unix 输出。所需检查与平台限制见[发行验收流程](release-validation.md)，图形安装、修复和卸载见[用户安装指南](../../User/zh-CN/index.md)，Tool 安装与迁移命令见[自动化](automation.md)。

## Linux/macOS 原生发行

完整发行和独立组件遵循[按业务发布规则](release-layout.md)。原生 Runtime/SDK 归档使用保留执行权限的 `.tar.gz`，WebAssembly 使用 ZIP。Linux 安装器使用 `.tar.gz`，macOS 安装器使用包含标准 `.app` 的 `.dmg`；安装器均为自包含。Linux 遵循绝对路径的 `XDG_DATA_HOME`。Python ABI 与 Linux 兼容范围取决于记录的构建环境。

```bash
bash Resources/Tools/Packaging/package-veroscript-all.sh --target linux-x64
python3 Resources/Tools/Packaging/verify-veroscript-release.py \
  --release-directory Release --target linux-x64
```

`target`: `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`.

桌面跨目标组包（包括 `win-x86`）使用 `Resources/Tools/Packaging/package-veroscript-desktop-cross.py --help`，输入真实目标构建及匹配依赖。构建／归档校验与目标运行验收分别记录。


## 文档包与集成视图

参阅[文档维护 — 校验与出版](../../../../../Resources/Docs/Developer/zh-CN/index.md#integrated-publication-views)。

完整发行通过 Resources 在 `Resources/Docs/` 发布 User 视图，与 `Resources/Scripts`、`Resources/Assets` 并列。当前 Runtime/SDK/扩展归档和安装器不附带编写的文档与示例；载荷政策见 Installer Developer 的 `release-layout.md`。

共享入口、Overview、Standards 和 Tutorials 保留在 Resources/Docs，详细正本保留在各模块的 Resources/Docs。出版时组装并验证 `.tmp/docs-collection-*` 临时集合，结束后清理；源码 Docs 不保留生成集合。

## 内嵌许可证声明

Core 项目内嵌仓库根目录 `LICENSE` 和 `VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt` 的全文。App 与 Tool 将 `--license` 或 `--third-party-licenses` 作为唯一参数接受，并在 GUI 或 bundle 初始化前退出。声明属于原文资源，不是本地化目录，不应翻译或截短。Windows 与 Unix 出版入口会在发布安装器前，将两个命令的输出与源声明比较。依赖变化时同步更新[声明来源与维护记录](../../../Licenses/README.md)，重新构建可执行文件并重跑两个声明校验；旧安装器保留原先内嵌的声明。

自定义产品使用 Tool 的 --definition；业务 PowerShell 发布器要求已配置的 VeroScript 业务名。InstallerProductDefinition.ReleaseGroup（JSON releaseGroup）为 Modules 时拒绝新建安装器/安装包。产品定义指南提供完整自定义载荷示例，并区分模块归档策略。

- [发行布局与发布规则](release-layout.md)

<a id="publication-paths"></a>

`veroscript_business_release.guard_tree` 拒绝链接形式的出版根及条目。仅在 macOS，允许 root 所有、分别准确解析到 `/private/tmp`、`/private/var`、`/private/etc` 的 `/tmp`、`/var`、`/etc` 作为祖先路径。由此可使用普通临时出版目录，但不允许输出根本身为链接或发行集合内部含链接；修改路径检查时须保留祖先与输出根的区别。

## 独立安装器下载文件

Windows Runtime 安装器使用 `package-veroscript-installer.ps1`，匹配的 Linux/macOS 宿主使用 `package-veroscript-installer.sh`。默认受管输出从 `Release/Runtime/<target>/<version>/archives/` 读取 Runtime 归档，也可通过 `-RuntimePackageDir` 或 `--runtime-package-dir` 提供已展开的载荷。下载文件名采用 `VeroScript<business>Installer-<version>-<target>`，Windows 后缀为 `.exe`，Linux 为 `.tar.gz`，macOS 为 `.dmg`。Linux 发布一份包含可执行程序的归档，macOS 发布一份包含应用的磁盘映像。嵌入载荷核验见[安装器下载校验](release-validation.md#installer-download-validation)。

## Windows 可执行文件的图标输入

Windows 上的 `veroscript`、`veroscript-runtime`、`veroscript-launcher`、`veroscript-lsp`、`veroscript-dap` 通过源码 CMake 辅助函数 `vs_use_product_icon` 嵌入 `Resources/Assets/icons/veroscript-icon.ico`；CLI 目标声明通过 `PRODUCT_ICON` 启用。这些可执行目标需要 RC 语言与 Windows 资源编译器。根项目在可用时探测并启用 RC；复用源码辅助函数的父项目应为 Windows 可执行目标启用 RC。仅构建库的配置可在没有 RC 时完成配置。

修改 ICO 后须重新构建相关可执行文件。`VeroScriptProductIcon.rc.in` 记录图标 SHA-256，使生成的 RC 变化也能触发 Visual Studio 增量重建；C/C++ 编译选项应按语言排除 RC。Installer 应用另通过项目资源使用 ICO 与 256px PNG，macOS 安装器 bundle 使用 ICNS。生成与格式细节由[资源指南](../../../../../Resources/Assets/icons/README.md)维护。`python Resources/Tools/Packaging/veroscript_icons.py check --require-tracked` 校验已维护的源码文件；发行审计可在各构建宿主上读取 Windows PE 图标组，将各帧与维护的 ICO 比较，无需启动目标可执行文件。实际 shell 展示和目标平台验收另行记录。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/build-and-bundle.md) · [fr-FR](../fr-FR/build-and-bundle.md) · [ja-JP](../ja-JP/build-and-bundle.md) · [ko-KR](../ko-KR/build-and-bundle.md) · [zh-CN](build-and-bundle.md) · [zh-TW](../zh-TW/build-and-bundle.md)
