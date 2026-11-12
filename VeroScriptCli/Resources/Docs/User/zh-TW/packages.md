# 套件與發佈

VeroScript 使用不同的套件格式區分可執行應用程式和可供其他腳本匯入的程式庫：

| 格式 | 用途 |
| --- | --- |
| `.vsapp` | 可運作的應用程式套件。 |
| `.vslib` | 供其他腳本匯入的二進位函式庫套件。 |
| `.vsbc` | 位元組碼快取檔案，不是發佈套件。 |
| `.vspkg` / `.zip` | ZIP 安裝散發套件，根目錄包含 `veroscript-package.json`；兩種副檔名均不區分大小寫。 |

## 建置應用程式套件

使用 `--emit-package` 指定輸出檔：

```powershell
veroscript --emit-package DemoApp.vsapp main.vs
```

應用程式套件通常包含入口來源模組和它所依賴的模組。應用程式套件可以直接交給 `veroscript` 運作：

```powershell
veroscript DemoApp.vsapp
```

## 建置函式庫套件

函式庫套件的來源檔案應透過 `export` 明確公開 API，並且通常不提供應用程式入口：

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
```

其他腳本使用普通的 `import` 匯入函式庫套件：

```VeroScript
import "DemoLibrary.vslib";
```

未使用 `export` 的宣告保持模組私有，不會成為函式庫套件 API。

## 套件選項

| 選項 | 作用 |
| --- | --- |
| `--package-file <file>` | 在套件中新增一個來源模組；可以重複使用。 |
| `--package-version <N>` | 設定套件版本號。 |
| `--strip-package-debug` | 移除套件中的來源檔案和偵錯行表資訊。 |
| `package inspect <package>` | 檢查 `.vsapp` 或 `.vslib` 的套件資訊。 |

```powershell
veroscript package inspect DemoLibrary.vslib
```

## 來源套件和二進位套件

開發階段可以直接匯入 `.vs` 來源模組；發佈階段可以將函式庫編譯為 `.vslib`。兩種方式使用相同的模組可見性規則，但二進位套件的版本必須與使用它的執行階段和綁定保持相容。

## 安裝擴充分發套件

可選套件管理器預設安裝到命令啟動目錄專案。`--path` 指定其他專案；`--root` 選擇目前 CLI 所屬且已驗證的 Runtime／Core SDK，`--root --path` 指定另一元件根。相對路徑以啟動目錄為基準。Runtime 套件位於 `Packages`，專案／SDK 套件位於 `.veroscript/packages`。執行、檢查、eval、REPL 優先尋找專案，無專案時僅使用執行 CLI 自身元件，不使用使用者安裝範圍或隱式環境合併。AOT 在支援能力內使用相同選擇參數。

擴充散發套件使用標準 ZIP 資料，支援 `.vspkg` 和 `.zip`，副檔名不區分大小寫。`pack` 預設產生 `<套件名>-<版本>.vspkg`，仍可明確輸出 `.zip`。`install`、`publish` 使用相同的封存內容和根目錄 `veroscript-package.json` 驗證；損壞或無效封存改名後仍會拒絕。下載 URL 無需封存副檔名；摘要與離線快取重用仍依據實際位元組。

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

[安裝與管理擴充套件](../../../../../VeroScriptPackageManager/Resources/Docs/User/zh-TW/index.md)

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](packages.md)
