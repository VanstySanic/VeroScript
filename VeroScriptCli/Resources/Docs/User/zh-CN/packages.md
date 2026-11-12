# 包与分发

VeroScript 使用不同的包格式区分可运行应用和可供其他脚本导入的库：

| 格式 | 用途 |
| --- | --- |
| `.vsapp` | 可运行的应用包。 |
| `.vslib` | 供其他脚本导入的二进制库包。 |
| `.vsbc` | 字节码缓存文件，不是发布包。 |
| `.vspkg` / `.zip` | ZIP 安装分发包，根目录包含 `veroscript-package.json`；两种后缀均忽略大小写。 |

## 构建应用包

使用 `--emit-package` 指定输出文件：

```powershell
veroscript --emit-package DemoApp.vsapp main.vs
```

应用包通常包含入口源模块和它依赖的模块。应用包可以直接交给 `veroscript` 运行：

```powershell
veroscript DemoApp.vsapp
```

## 构建库包

库包的源文件应通过 `export` 明确公开 API，并且通常不提供应用入口：

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
```

其他脚本使用普通的 `import` 导入库包：

```VeroScript
import "DemoLibrary.vslib";
```

未使用 `export` 的声明保持模块私有，不会成为库包 API。

## 包选项

| 选项 | 作用 |
| --- | --- |
| `--package-file <file>` | 向包中添加一个源模块；可以重复使用。 |
| `--package-version <N>` | 设置包版本号。 |
| `--strip-package-debug` | 移除包中的源文件和调试行表信息。 |
| `package inspect <package>` | 检查 `.vsapp` 或 `.vslib` 的包信息。 |

```powershell
veroscript package inspect DemoLibrary.vslib
```

## 源包和二进制包

开发阶段可以直接导入 `.vs` 源模块；发布阶段可以将库编译为 `.vslib`。两种方式使用相同的模块可见性规则，但二进制包的版本必须与使用它的运行时和绑定保持兼容。

## 安装扩展分发包

可选包管理器默认安装到命令启动目录项目。`--path` 指定其他项目；`--root` 选择当前 CLI 所属且已验证的 Runtime／Core SDK，`--root --path` 指定另一组件根。相对路径以启动目录为基准。Runtime 包放在 `Packages`，项目／SDK 包放在 `.veroscript/packages`。运行、检查、eval、REPL 先发现项目，无项目时仅使用执行 CLI 自身组件，没有用户安装范围或隐式环境叠加。AOT 在自身支持的能力范围内使用相同选择器。

扩展分发包使用标准 ZIP 数据，支持 `.vspkg` 和 `.zip`，后缀忽略大小写。`pack` 默认生成 `<包名>-<版本>.vspkg`，仍可显式输出 `.zip`。`install`、`publish` 使用相同的归档内容和根目录 `veroscript-package.json` 校验；损坏或无效归档改名后仍会拒绝。下载 URL 无需归档后缀；摘要与离线缓存复用继续依据实际字节。

```sh
veroscript pack ./library
veroscript pack ./library --output ./library.zip
veroscript pack ./library --output ./library-0.1.1.vspkg
veroscript publish ./library-0.1.1.vspkg
veroscript install ./library.vspkg
veroscript install ./library.zip --path "./My App"
veroscript install ./library.vspkg --root
veroscript install ./library.vspkg --root --path "D:/VeroScript/SDK/Core/0.1.1"
veroscript --path "./My App" "./My App/main.vs"
```

[安装与管理扩展包](../../../../../VeroScriptPackageManager/Resources/Docs/User/zh-CN/index.md)

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](packages.md) · [zh-TW](../zh-TW/packages.md)
