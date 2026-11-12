# 安裝程式建置與 bundle 契約

## 建置產物

App 與 Tool 以 .NET 9 為目標，共同使用 `VeroScriptInstaller.Core`。App 將語言目錄內嵌為資源，將 bundle 作為自解壓縮內容封裝進單一執行檔；Tool 可在沒有圖形介面的情況下建立與檢查 bundle 歸檔。

`VeroScriptInstaller/global.json` 以 `latestFeature` 前滾策略選取 .NET SDK `9.0.100`。從儲存庫根目錄開始執行範例，其中的 `Push-Location` 使 SDK 解析以 Installer 目錄為起點。即使已安裝 .NET 10 行動工作負載，仍須保留 .NET 9 SDK；C# 行動工具鏈與此分開。

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

Tests 是回歸測試執行檔，應使用 `dotnet run`，不可假設 `dotnet test` 能探索這些測試。單獨建置 App 不會內嵌發行內容；製作安裝程式時請使用打包腳本。

## 完整 Windows 發行

完整流程支援 Release 設定的 `win-x64` 和 `win-arm64`，需要 CMake、MSVC、全產品所需的 .NET 8/9 SDK、Node.js/npm，以及重建 WebAssembly 所需的 Emscripten。先在 `Extensions/Platform/WebAssembly/VeroScriptWebAssembly` 執行 `npm ci`；瀏覽器測試亦需要可用瀏覽器，例如 Playwright Chromium。

```powershell
.\Resources\Tools\Packaging\package-veroscript-all.ps1 -ConfigurePreset windows-msvc-vs2026 -Runtime win-x64
```

Windows ARM64 使用 `-Runtime win-arm64`，並選擇相符的預設設定，例如 `-ConfigurePreset windows-msvc-vs2026-arm64`；同時須安裝 MSVC ARM64 工具和 Windows SDK。完整出版執行包含 Python wheel 矩陣的原生執行驗證，因此應在原生 Windows ARM64 宿主上準備全部必要的 ARM64 CPython 直譯器。僅在 x64 上交叉編譯原生成品不能完成這些驗證。`win-arm64` 表示原生 ARM64，不包含 ARM64EC 或模擬執行的 x64。

選擇符合 Visual Studio 的預設設定，或省略以自動選擇。預設輸出為 `Release`。`-SkipBuild` 重用既有原生與 Web 產物，但仍會出版受管理應用程式並驗證暫存的 WebAssembly SDK；僅可在既有產物與預期原始碼、設定相符時使用。產物位於 `Release/<business-route>/<target>/<version>/`，各版本依需要建立 `archives/`、`sdks/`、`packages/`、`installers/` 與 `symbols/`。共用分發使用 `any`，WebAssembly SDK 使用 `wasm32`；清單、校驗碼與集合索引繼續保留。產品版本各自獨立。Core/C++ 相依性、鎖定、版本清理及回復見[發行規則](release-layout.md)。

## Windows 原生執行階段相依性

MSVC Release 打包透過 `vs_install_windows_runtime` 安裝所選工具鏈的可轉散發 DLL。`VeroScriptWindowsRuntime` 元件負責 Runtime 根目錄；Interop、C++ 工具及 Python 使用各自的安裝元件。套件組裝也會在 SDK 和受管理執行階段配置的原生入口旁放置 DLL。請使用工具鏈提供的可轉散發檔案，不要從系統目錄複製 DLL；不隨附偵錯 CRT 或 Universal CRT。

發行驗證檢查原生架構，並要求每個隨套件發行的原生入口旁存在 `msvcp140.dll` 和 `vcruntime140.dll`，x64 目標還須有 `vcruntime140_1.dll`。ARM64 使用其原生 CRT，並排除 ARM64 可轉散發元件中附帶的 x64 `vcruntime140_1.dll`。同目錄還須包含 `licenses/MicrosoftVisualCpp/NOTICE.txt`。將原始檔 `Resources/Tools/CMake/WindowsRuntimeNotice.txt` 保留為獨立第三方聲明，專案 MIT 授權不能取代它。缺少相依檔案會導致出版失敗，缺少 MSVC 可轉散發檔案也會使 Windows wheel 設定失敗。Universal CRT 仍屬作業系統先決條件。

## Python 出版先決條件

完整發行選擇 Python 時，須在執行管線前準備完整 CPython 3.11–3.15 直譯器矩陣。`-SkipBuild` / `--skip-build` 仍會建置並驗證 wheel 及原始碼發行套件。直譯器選擇、預先檢查，以及各 wheel ABI 與 SDK 單一原生 ABI 的差異請見 [Python 矩陣契約](../../../../../Extensions/Language/Python/Resources/Docs/Developer/zh-TW/toolchain-and-packaging.md)。

## Bundle 輸入契約

清單定義產品身分、平台、架構、可選元件、相對路徑與雜湊。安裝與修復必須以 bundle 為依據，不可從目前原始碼樹重建發行內容。展開的 bundle 包含 `manifest.json` 與 `payload/`，並非可攜 Runtime/SDK 壓縮包。

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

`D:/Packages/Runtime` 表示已備妥的 Runtime 內容，包含必要 CLI/LSP/DAP、標準庫及在地化檔案。`--source-*` 選項須符合選定的產品設定。自訂產品見[產品定義](../../../Examples/Products/README.md)；Tool 的安裝、遷移、修復與解除安裝見[自動化](automation.md)。

## 單檔 bundle 承載

`App/InstallerBundleContent.targets` 將 `InstallerBundleArchive` 拆為最多 32 個、每個不超過 1 GiB 的內容分片，從 `VeroScriptInstaller.EmbeddedBundle.zip.0000` 連續編號。單檔發佈透過 `IncludeAllContentForSelfExtract` 將其納入執行檔，使用者無須另行下載分片。組件中繼資料記錄完整 ZIP 的 `VeroScriptInstaller.BundleSha256` 與 `VeroScriptInstaller.BundlePartCount`。

`EmbeddedInstallerBundle` 開啟已解出的分片，拒絕連結及非法數量／大小，核對完整 ZIP 的 SHA-256，並提供可定位串流，不將整個歸檔載入記憶體或另複製成 ZIP。Linux 下將 bundle 展開至 `AppContext.BaseDirectory/VeroScriptInstaller.Unpack`，其他平台使用 `Path.GetTempPath()/VeroScriptInstaller`；仍執行解壓縮與清單校驗。發佈目標會拒絕遺失或空白的單檔程式；這些保護不代表大型安裝程式已通過目標平台驗收。

失敗時預設移除本次擁有的發行暫存目錄。Windows 完整發佈的 `-KeepFailedStage` 或 Unix 完整發佈的 `--keep-failed-stage` 會保留失敗暫存並輸出路徑，供診斷使用；成功後仍會清理。保留檔案僅為候選，重用前須重新核對原始碼、設定及摘要。

## 驗證與可選簽章

內嵌與外部 bundle 必須通過相同的清單、路徑、雜湊、元件及平台驗證。變更壓縮布局、清單指紋或安裝記錄格式時，必須一併更新修復、遷移、解除安裝與打包測試。元件須攜帶目前的公開 API 和二進位產物，包括 Core 標頭、C++ session/CMake 套件、Python 擴充、型別資訊及相符的 Interop DLL。最終發行驗證亦檢查 session 快取 API、獨立與 NuGet 內的 C# 組件、符號套件，以及兩種 WebAssembly SDK 與型別宣告。

流程會對產生的安裝程式執行 `--verify-localization` 與 `--verify-embedded-bundle`，簽章後亦會再檢查。必須檢查結束碼；此為自檢，不等同人工圖形介面驗收。語言驗證涵蓋六種內建目錄、八種產品設定、元件資源鍵、非空值、格式參數、部分外部覆寫、中文文字體系與語言切換；不得以英文回退掩蓋內建翻譯缺失。

在地化資源鍵是清單指紋的一部分；變更時需一起重建 bundle 和安裝程式。保留原安裝程式以修復既有安裝。同版本但指紋不同的重建包會在修復模式被拒絕，正常更新模式則可使用。

簽章為可選。若要強制簽章，設定 `VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT` 與 `VEROSCRIPT_TIMESTAMP_SERVER`，完整流程使用 `-RequireSignedInstallers`，單產品使用 `-RequireSignature`。最終校驗碼在簽章後建立。獨立執行最終驗證時亦需 `-RequireSignedInstallers`，使所有安裝程式的 Authenticode 狀態均須為 `Valid`。

## Linux/macOS 原生發行

完整發行與獨立元件遵循[按業務發行規則](release-layout.md)。原生 Runtime/SDK 封存以 `.tar.gz` 保留執行權限，WebAssembly 使用 ZIP。Linux 安裝程式使用 `.tar.gz`，macOS 安裝程式使用包含標準 `.app` 的 `.dmg`；安裝程式均為自含式。Linux 採用絕對路徑的 `XDG_DATA_HOME`。Python ABI 與 Linux 相容範圍取決於記錄的建置環境。

```bash
bash Resources/Tools/Packaging/package-veroscript-all.sh --target linux-x64
python3 Resources/Tools/Packaging/verify-veroscript-release.py \
  --release-directory Release --target linux-x64
```

`target`: `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`.

桌面跨目標組包（包括 `win-x86`）使用 `Resources/Tools/Packaging/package-veroscript-desktop-cross.py --help`，輸入實際目標建置及相符依賴。建置／封存校驗與目標執行驗收分別記錄。


## 文件包與集成檢視

請參閱[文件維護 — 驗證與出版](../../../../../Resources/Docs/Developer/zh-TW/index.md#integrated-publication-views)。

完整發行透過 Resources 於 `Resources/Docs/` 發布 User 視圖，與 `Resources/Scripts`、`Resources/Assets` 並列。目前 Runtime/SDK/擴充歸檔與安裝程式不附帶編寫的文件與範例；酬載政策請參閱 Installer Developer 的 `release-layout.md`。

共用入口、Overview、Standards 與 Tutorials 保留於 Resources/Docs，詳細正本保留於各模組的 Resources/Docs。出版時組裝並驗證 `.tmp/docs-collection-*` 暫存集合，結束後清理；原始碼 Docs 不保留生成集合。

## 內嵌授權聲明

Core 專案內嵌儲存庫根目錄 `LICENSE` 和 `VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt` 的全文。App 與 Tool 將 `--license` 或 `--third-party-licenses` 作為唯一引數接受，並在 GUI 或 bundle 初始化前結束。聲明屬於原文資源，不是在地化目錄，不應翻譯或截短。Windows 與 Unix 出版入口會在發布安裝程式前，將兩個命令的輸出與原始聲明比較。相依套件變更時同步更新[聲明來源與維護記錄](../../../Licenses/README.md)，重新建置執行檔並重跑兩個聲明檢查；舊安裝程式保留原先內嵌的聲明。

自訂產品使用 Tool 的 --definition；業務 PowerShell 發行工具要求已設定的 VeroScript 業務名稱。InstallerProductDefinition.ReleaseGroup（JSON releaseGroup）為 Modules 時拒絕建立新安裝程式/安裝套件。產品定義指南提供完整自訂酬載範例，並區分模組歸檔策略。

- [散發配置與發布規則](release-layout.md)

Windows Python 負載契約要求套件進入點、型別存根、`py.typed` 標記及相符的 Interop DLL；只有原生擴充並不足夠。

<a id="publication-paths"></a>

`veroscript_business_release.guard_tree` 拒絕連結形式的出版根及項目。僅在 macOS，允許 root 所有、分別確切解析到 `/private/tmp`、`/private/var`、`/private/etc` 的 `/tmp`、`/var`、`/etc` 作為上層路徑。因此可使用一般暫存出版目錄，但不允許輸出根本身為連結或發行集合內部含連結；修改路徑檢查時須保留上層與輸出根的區別。

## 獨立安裝程式下載檔案

Windows Runtime 安裝程式使用 `package-veroscript-installer.ps1`，相符的 Linux/macOS 宿主使用 `package-veroscript-installer.sh`。預設受管輸出從 `Release/Runtime/<target>/<version>/archives/` 讀取 Runtime 封存，亦可透過 `-RuntimePackageDir` 或 `--runtime-package-dir` 提供已展開的負載。下載檔名採 `VeroScript<business>Installer-<version>-<target>`，Windows 副檔名為 `.exe`、Linux 為 `.tar.gz`、macOS 為 `.dmg`。Linux 發佈一份含可執行程式的封存，macOS 發佈一份含應用程式的磁碟映像。內嵌負載驗證見[安裝程式下載驗證](release-validation.md#installer-download-validation)。

## Windows 可執行檔的圖示輸入

Windows 上的 `veroscript`、`veroscript-runtime`、`veroscript-launcher`、`veroscript-lsp`、`veroscript-dap` 透過原始碼 CMake 輔助函式 `vs_use_product_icon` 內嵌 `Resources/Assets/icons/veroscript-icon.ico`；CLI 目標宣告以 `PRODUCT_ICON` 啟用。這些可執行目標需要 RC 語言及 Windows 資源編譯器。根專案在可用時偵測並啟用 RC；重用原始碼輔助函式的父專案應為 Windows 可執行目標啟用 RC。僅建置程式庫的設定可在沒有 RC 時完成設定。

修改 ICO 後須重新建置相關可執行檔。`VeroScriptProductIcon.rc.in` 記錄圖示 SHA-256，使產生的 RC 變更亦能觸發 Visual Studio 增量重建；C/C++ 編譯選項應依語言排除 RC。Installer 應用程式另透過專案資源使用 ICO 與 256px PNG，macOS 安裝程式 bundle 使用 ICNS。產生及格式細節由[資源指南](../../../../../Resources/Assets/icons/README.md)維護。`python Resources/Tools/Packaging/veroscript_icons.py check --require-tracked` 驗證已維護的原始碼檔案；發行稽核可在各建置宿主上讀取 Windows PE 圖示群組，將各影格與維護的 ICO 比較，無須啟動目標可執行檔。實際 shell 顯示及目標平台驗收另行記錄。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/build-and-bundle.md) · [fr-FR](../fr-FR/build-and-bundle.md) · [ja-JP](../ja-JP/build-and-bundle.md) · [ko-KR](../ko-KR/build-and-bundle.md) · [zh-CN](../zh-CN/build-and-bundle.md) · [zh-TW](build-and-bundle.md)
