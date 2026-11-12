# 安装器自动化

面向维护者与部署工具作者。Tool 命令需要源码仓库和 .NET 9 SDK；安装器 bundle 不是便携 Runtime 压缩包。图形安装器操作见[安装指南](../../User/zh-CN/index.md)。

## 开发/自动化入口

Tool 使用已展开的安装器 bundle（包含 `manifest.json` 和 `payload`），不是普通
Runtime ZIP。以下例子针对 runtime profile，`--root` 是产品根目录：

此例展示验证、首次安装、迁移、修复和卸载的完整流程，请只执行需要的步骤。示例仅选择 `runtime`，因此也适用于只提供 `--source-runtime` 构建的 bundle。仅当清单包含 `examples` 时，才将其加入 `--components`；更新时省略的可选组件会被移除。`--root` 是产品根目录，Runtime 组件安装在其中的 `Runtime` 子目录。

```powershell
$tool = '.\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj'
$bundle = 'D:\Bundles\runtime-installer-bundle'
dotnet run --project $tool -c Release -- verify --bundle $bundle
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScript' --components runtime
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScriptMoved' --existing-record 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json' --components runtime
dotnet run --project $tool -c Release -- repair --bundle $bundle --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
dotnet run --project $tool -c Release -- uninstall --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
```

Tool 中 `--add-to-path` 是无值开关，需要时显式添加；更新/迁移时省略它会清除之前
安装器拥有的 PATH 条目。降级默认拒绝，只有确有需要时才加 `--allow-downgrade`。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/automation.md) · [fr-FR](../fr-FR/automation.md) · [ja-JP](../ja-JP/automation.md) · [ko-KR](../ko-KR/automation.md) · [zh-CN](automation.md) · [zh-TW](../zh-TW/automation.md)
