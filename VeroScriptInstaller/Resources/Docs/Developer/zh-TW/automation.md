# 安裝程式自動化

面向維護者與部署工具作者。Tool 命令需要原始碼儲存庫與 .NET 9 SDK；安裝程式 bundle 並非可攜 Runtime 壓縮檔。圖形安裝程式操作見[安裝指南](../../User/zh-TW/index.md)。

請勿以手動移動檔案作為遷移。安全的更新或遷移須向 Tool 明確提供舊安裝記錄：

此例展示驗證、首次安裝、遷移、修復和解除安裝的完整流程，請只執行需要的步驟。範例僅選擇 `runtime`，因此亦適用於只提供 `--source-runtime` 建立的 bundle。僅當清單包含 `examples` 時，才將其加入 `--components`；更新時省略的選用元件會被移除。`--root` 是產品根目錄，Runtime 元件安裝於其中的 `Runtime` 子目錄。

```powershell
$tool = '.\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj'
$bundle = 'D:\Bundles\runtime-installer-bundle'
dotnet run --project $tool -c Release -- verify --bundle $bundle
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScript' --components runtime
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScriptMoved' --existing-record 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json' --components runtime
dotnet run --project $tool -c Release -- repair --bundle $bundle --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
dotnet run --project $tool -c Release -- uninstall --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
```

Tool 使用的安裝程式 bundle 包含 `manifest.json` 與 `payload/`，不是一般 Runtime ZIP。`--add-to-path` 是不帶值的明確開關；在更新或遷移時省略它會移除先前安裝所擁有的 PATH 項目。降級預設會被拒絕，僅在確有需要時使用 `--allow-downgrade`。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/automation.md) · [fr-FR](../fr-FR/automation.md) · [ja-JP](../ja-JP/automation.md) · [ko-KR](../ko-KR/automation.md) · [zh-CN](../zh-CN/automation.md) · [zh-TW](automation.md)
