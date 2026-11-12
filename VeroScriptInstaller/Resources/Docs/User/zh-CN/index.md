# 安装与使用 VeroScript

选择与你的操作系统和架构匹配的包。Runtime 用于运行脚本，包含 CLI、LSP 与 DAP；SDK 产品用于将 VeroScript 接入宿主应用。安装器部署所选组件；便携包可解压到自行选择的目录。

## 选择产品

| 产品 | 用途 |
|---|---|
| Runtime | 运行脚本；CLI/LSP/DAP 与标准库 |
| Core SDK | 核心与 Tooling 的 C++ 头文件、库和 CMake 包 |
| Interop SDK | 原生或其他语言宿主使用的 C ABI |
| C++ SDK | C++ 适配器；需要匹配构建的 Core SDK |
| C# SDK | .NET 适配器与匹配的原生 Interop 运行时 |
| Python SDK | CPython 适配器与 Python 工具 |
| WebAssembly SDK | JavaScript 宿主使用的 WebAssembly 运行时与 TypeScript API |

Runtime 不会安装 SDK。安装器提供其包内实际包含的工具、AOT 与符号；文档和示例请从 Resources 另行获取。SDK 默认使用含版本的安装目录，允许并行安装。各产品版本可能不同，应遵守各 SDK 的依赖与 ABI 要求。

在 `Release/<business-route>/<target>/<version>/` 中，`installers` 存放桌面安装器，`archives` 存放便携 Runtime 或模块归档，`sdks` 存放开发 SDK，`packages` 存放生态包和编辑器扩展，`symbols` 存放调试符号。公共分发使用 `any`，便携 WebAssembly SDK 使用 `wasm32`。可选 Modules 单独获取，不提供独立安装器。

## 安装、更新与迁移

Windows 启动 `.exe` 安装器；Linux 解压 `.tar.gz` 后运行其中的程序；macOS 打开 `.dmg` 后启动其中的 `.app`。按向导依次选择功能组件、安装位置，按需设置出现的文件关联选项，核对信息后执行安装。通过“下一步”和“上一步”切换页面；组件较多时分多页展示，窗口无需滚动。安装器不提供文档和示例的安装选项。安装器为 self-contained，目标机无需另外安装 .NET Runtime。Runtime 的 PATH 默认关闭；启用后请在新开的终端使用。

安装器只在当前组件目标目录中查找属于本产品的有效记录；找到匹配安装后才进入更新模式，同版本重新打包也适用。更新保留用户自行创建的文件；若新包文件与用户文件冲突，会提示失败，需先移走冲突文件再重试。取消选择已安装的可选组件，会移除其受管理文件并保留用户文件。清除 PATH 选项只会移除该安装记录声明拥有的条目。

识别已有安装时，仅为尚未手动修改的组件、PATH 和受支持的文件关联选项回填安装记录中的默认值。更新和卸载确认页会明确说明是否移除此安装拥有的 PATH 条目。

安装根目录是向导中选择的父目录，不是 `SDK/Core/<version>` 这样的组件目录。内置产品的组件目录带版本；产品版本不同时，即使使用相同根目录，也可以与旧版并行安装。安装器不会自动扫描、迁移或卸载其他版本。需要替换旧版时，先安装并确认新版可用，再使用旧版原安装器及安装记录卸载旧版。若要更换安装根目录，在新根目录安装、复制自己的文件，然后按原记录卸载旧安装；不要直接拖动受管理组件目录来迁移安装。

## Windows 原生运行库文件

Windows 包在原生程序及库旁附带匹配的 Visual C++ 运行库 DLL。请完整解压压缩包或使用安装器；部署时，将这些 DLL 及其 `licenses/MicrosoftVisualCpp/NOTICE.txt` 与对应程序或原生库一起保留。只复制原生程序的可执行文件、Interop DLL 或 Python 扩展可能遗漏必需依赖。操作系统仍须提供 Universal CRT。

## 修复与卸载

每个已安装组件根目录保存 `.veroscript-install.json`，其中记录整套安装的组件位置。
可以使用任意一个组件内的有效记录。以下假设 Runtime 的安装根目录为
`D:\Apps\VeroScript`，请替换成实际路径；EXE 路径也请按保存位置调整：

请选择下面的一种操作，并将安装器和记录路径替换成实际路径。每条命令打开对应窗口，确认后才执行。

仅修复:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --repair-record $record
```

仅卸载:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --uninstall-record $record
```

修复必须使用原先安装的同一份产品包（版本、平台、架构和 manifest 指纹一致），
因此应保留原安装器。卸载不需要重新读取嵌入 payload。修复和卸载模式不能同时指定。
当前未提供 Windows“已安装的应用”注册或开始菜单卸载入口，请使用以上命令入口。

卸载只移除受管理文件、安装器拥有的 PATH 条目，以及仍归属本安装的关联和图标注册值；有用户文件的目录会保留。
若上次安装/卸载异常中断，再次读取安装记录、修复或卸载时会先恢复事务，再进入操作；
不要手动删除 `.veroscript-transaction-*.json`、stage 或 backup 目录来绕过恢复。

## Core SDK 的 CMake 使用方式

将解压后的 SDK 根目录加入 `CMAKE_PREFIX_PATH`：

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScript)
```

Core SDK 导入目标提供头文件目录、C++17 要求及系统链接依赖；配置宿主时使用该导入目标。

Core SDK 提供 `VeroScript::VeroScript` 和 `VeroScript::VeroScriptTooling`，导入目标会传递公开头文件的编译要求。JIT 后端是独立模块，Core 不导出 `VeroScript::asmjit`。C++/AOT 库属于独立 C++ 产品。使用安装器部署的 Core SDK 时，把 `<install-root>/SDK/Core/<version>` 加入 `CMAKE_PREFIX_PATH`；`<install-root>` 是向导中选择的安装根目录。示例需另行获取，不包含在当前便携 SDK 或安装器内。

## C++ SDK 的 CMake 使用方式

C++ SDK 是独立产品，但其静态库与 Core SDK 使用同一套公开 C++ ABI。将匹配的
Core SDK 根目录和 C++ 产品的 `Runtime` 目录同时加入 `CMAKE_PREFIX_PATH`：

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScriptCpp)
```

`VeroScriptCppConfig.cmake` 精确请求 C++ 包构建时记录的 Core SDK 版本，并核对 Core 库的 SHA-256 构建标识。该 Core 版本可能不同于 C++ 产品版本；请选择 C++ 发行清单记录的 Core 依赖，仅版本号相同不能保证构建兼容。便携包加入 `<cpp-package>/Runtime`，安装器默认布局加入 `<install-root>/SDK/Cpp/<version>/Runtime`。已安装对应可选组件时，可请求 `COMPONENTS Tools Aot`。

Core SDK 工具相对于 `bin` 自动发现包内的 `assets/stdlib` 和 `assets/localization`。运行另行获取的 Resources 示例时，把以下绝对路径替换为实际位置：

```powershell
.\bin\veroscript.exe --bc-cache=off 'D:\Packages\Resources\Scripts\examples\standard_library_algorithms.vs'
```

## 离线文档与示例

另行获取匹配的 Resources 分发内容，打开 `Resources/Docs/README.md`，选择语言并通过相对链接阅读参考与教程。独立文档 ZIP 解压后使用包内的 `docs/README.md`。Resources 还在 `Resources/Scripts/examples` 提供脚本示例。当前 SDK 归档和安装器不附带文档/示例组件。在线源码参考需要网络；示例可能需要匹配的 SDK 与依赖。

## 安装器语言

安装器提供英语、法语、日语、韩语、简体中文和繁体中文。可在窗口中选择语言，也可启动时传入 `--language`。初始语言跟随系统界面语言，无可匹配语言时使用英文。此设置只控制安装界面；脚本诊断请另用 CLI 的 `--lang` 或 SDK 语言 API。以下 Windows 示例的路径和版本请替换为实际下载文件：

```powershell
& 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe' --language zh-CN
```

## Linux/macOS 安装

Linux 解压匹配的 `linux-x64` 或 `linux-arm64` `.tar.gz` 安装器后运行其中的程序；macOS 打开匹配的 `macos-x64` 或 `macos-arm64` `.dmg`，启动其中的安装器 `.app`。安装窗口需要图形桌面。修复和卸载使用与 Windows 相同的记录参数。下例使用 Linux；macOS 将 `installer` 设为原安装器的 `<installer>.app/Contents/MacOS/VeroScriptInstaller`，在只读映像中的应用上省略 `chmod`。所有示例路径均需替换为实际路径。解压便携 Runtime/SDK 归档时也应保留执行权限。

请选择下面的一种操作，并将安装器和记录路径替换成实际路径。每条命令打开对应窗口，确认后才执行。

仅修复:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --repair-record "$record"
```

仅卸载:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --uninstall-record "$record"
```

## 许可证声明

本文档附带项目的完整 [MIT 许可证](../../../../../LICENSE)。安装器的依赖声明可通过下载的安装器查看。请单独使用其中一个声明选项；它会输出原始声明并退出，不打开安装窗口。将示例路径替换为实际下载的文件。声明保留原文语言；`--language` 另行控制安装界面的语言。

示例临时使用 UTF-8，确保 Windows PowerShell 5.1 正确显示声明中的字符，并在结束后恢复终端原来的编码。

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$noticeEncoding = [Console]::OutputEncoding
try {
	[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
	& $installer --license | Out-Host
	& $installer --third-party-licenses | Out-Host
}
finally
{
	[Console]::OutputEncoding = $noticeEncoding
}
```

<a id="file-associations"></a>

## Windows 文件关联

Windows 安装包包含 Runtime 与对应图标时，选择 Runtime 后可以设置可选的文件关联。新安装默认不启用。可以登记 `.vs`、`.vsapp` 由该 Runtime 运行，或仅登记 `.vspkg` 图标。打开关联的 `.vs` 或 `.vsapp` 会执行该文件；`.vspkg` 选项不运行或安装包。

登记为当前用户添加运行处理程序和图标。默认应用由你在 Windows 设置中选择，登记处理程序本身不指定系统默认值。可选的设置操作会在安装成功后请求打开该页面；打开失败或登记不完整会在结果页提示。Windows 也可能显示所选默认应用的图标。

更新会沿用记录中尚未被你编辑的关联选择。取消选项、修复、回滚与卸载均保留无关登记及后续用户修改；只有本次安装仍拥有且未被改动的值可以移除或恢复。其他平台及缺少所需 Runtime 文件的包不提供这些选项。

`.vspkg` 图标只在尚无图标注册时添加。取消“安装后选择默认打开程序”仅取消本次打开设置的请求，不重置已在 Windows 中确认的选择。卸载不修改受保护的默认应用记录，也不保证自动恢复以前的默认程序。旧安装记录没有关联所有权字段时，不执行关联清理。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
