# 安裝與使用 VeroScript

選擇符合你的作業系統與架構的套件。Runtime 用於執行腳本，包含 CLI、LSP 與 DAP；SDK 產品用於將 VeroScript 接入宿主應用程式。安裝程式部署所選元件；可攜套件可解壓縮到自行選擇的目錄。

## 選擇產品

| 產品 | 用途 |
|---|---|
| Runtime | 執行腳本；CLI/LSP/DAP 與標準庫 |
| Core SDK | 核心與 Tooling 的 C++ 標頭、程式庫和 CMake 套件 |
| Interop SDK | 原生或其他語言宿主使用的 C ABI |
| C++ SDK | C++ 配接器；需要相符建置的 Core SDK |
| C# SDK | .NET 配接器與相符的原生 Interop 執行階段 |
| Python SDK | CPython 配接器與 Python 工具 |
| WebAssembly SDK | JavaScript 宿主使用的 WebAssembly 執行階段與 TypeScript API |

Runtime 不會安裝 SDK。安裝程式提供套件內實際包含的工具、AOT 與符號；文件與範例請從 Resources 另行取得。SDK 預設使用含版本的安裝目錄，允許並行安裝。各產品版本可能不同，應遵守各 SDK 的相依性與 ABI 要求。

在 `Release/<business-route>/<target>/<version>/` 中，`installers` 存放桌面安裝程式，`archives` 存放可攜 Runtime 或模組封存，`sdks` 存放開發 SDK，`packages` 存放生態套件與編輯器擴充，`symbols` 存放偵錯符號。共用發行使用 `any`，可攜 WebAssembly SDK 使用 `wasm32`。選用 Modules 另外取得，不提供獨立安裝程式。

## 安裝、更新與遷移

Windows 啟動 `.exe` 安裝程式；Linux 解壓縮 `.tar.gz` 後執行其中的程式；macOS 開啟 `.dmg` 後啟動其中的 `.app`。按精靈依序選擇功能元件、安裝位置，按需設定出現的檔案關聯選項，核對資訊後執行安裝。透過「下一步」和「上一步」切換頁面；元件較多時分頁顯示，視窗無需捲動。安裝程式不提供文件與範例的安裝選項。安裝程式為自含式，目標電腦不需另裝 .NET Runtime。Runtime 的 `PATH` 整合預設關閉；啟用後請開啟新的終端機。

安裝程式只在目前元件目標目錄中尋找屬於本產品的有效記錄；找到相符安裝後才進入更新模式，同版本重新打包亦適用。更新保留使用者自行建立的檔案；若新套件檔案與使用者檔案衝突，會失敗，請先移走衝突檔案再重試。取消選取已安裝的選用元件會移除受管理檔案，但保留使用者檔案。清除 PATH 選項只會移除安裝記錄擁有的項目。

識別既有安裝時，僅為尚未手動修改的元件、PATH 和受支援的檔案關聯選項填入安裝記錄中的預設值。更新與解除安裝確認頁會明確說明是否移除此安裝擁有的 PATH 項目。

安裝根目錄是精靈中選取的父目錄，不是 `SDK/Core/<version>` 這類元件目錄。內建產品的元件目錄含版本；產品版本不同時，即使使用相同根目錄，也可與舊版並行安裝。安裝程式不會自動掃描、遷移或解除安裝其他版本。若要替換舊版，先安裝並確認新版可用，再使用舊版原安裝程式及安裝記錄解除安裝舊版。若要更換安裝根目錄，請在新根目錄安裝、複製自己的檔案，再依原記錄解除安裝舊安裝；不要直接拖曳受管理元件目錄來遷移安裝。

## Windows 原生執行階段檔案

Windows 套件在原生程式及函式庫旁隨附相符的 Visual C++ 執行階段 DLL。請完整解壓縮或使用安裝程式；部署時，將這些 DLL 及其 `licenses/MicrosoftVisualCpp/NOTICE.txt` 與對應程式或原生函式庫一起保留。只複製原生程式的執行檔、Interop DLL 或 Python 擴充套件可能遺漏必要相依檔案。作業系統仍須提供 Universal CRT。

## 修復與解除安裝

每個元件根目錄都包含 `.veroscript-install.json`。修復需要原始產品套件，版本、平台、架構及清單指紋必須與記錄完全相符，因此應保留原安裝程式。解除安裝不會重新讀取內嵌 payload，且修復與解除安裝模式不能同時指定。

請選擇下方的一種操作，並將安裝程式與記錄路徑替換為實際路徑。每條命令開啟對應視窗，確認後才執行。

僅修復:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --repair-record $record
```

僅解除安裝:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --uninstall-record $record
```

解除安裝只移除受管理檔案、安裝程式擁有的 PATH 項目，以及仍屬於此安裝的關聯與圖示登錄值；包含使用者檔案的目錄會保留。作業中斷後，讀取記錄、修復或解除安裝都會先恢復交易。不要手動刪除 `.veroscript-transaction-*.json`、暫存或備份目錄來繞過恢復。

## 以 CMake 使用 SDK

將解壓的 Core SDK 根目錄加入 `CMAKE_PREFIX_PATH`：

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScript)
```

Core SDK 提供 `VeroScript::VeroScript` 與 `VeroScript::VeroScriptTooling`，匯入目標會傳遞公開標頭的編譯要求。JIT 後端是獨立模組，Core 不匯出 `VeroScript::asmjit`。C++/AOT 程式庫屬於獨立 C++ 產品。使用安裝程式部署的 Core SDK 時，把 `<install-root>/SDK/Core/<version>` 加入 `CMAKE_PREFIX_PATH`；`<install-root>` 是精靈中選擇的安裝根目錄。範例需另行取得，不包含在目前的可攜 SDK 或安裝程式內。

C++ SDK 需要相符的 Core SDK 與 `Runtime` 目錄：

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScriptCpp)
```

`VeroScriptCppConfig.cmake` 精確要求 C++ 套件建置時記錄的 Core SDK 版本，並核對 Core 程式庫的 SHA-256 建置識別碼。該 Core 版本可能不同於 C++ 產品版本；請選擇 C++ 發行清單記錄的 Core 相依項目，僅版本號相同不能保證建置相容。可攜套件加入 `<cpp-package>/Runtime`，安裝程式預設配置加入 `<install-root>/SDK/Cpp/<version>/Runtime`。已安裝對應選用元件時，可要求 `COMPONENTS Tools Aot`。

Core SDK 工具以 `bin` 為基準尋找 `assets/stdlib` 及 `assets/localization`。在 Windows Core SDK 根目錄執行另行取得的 Resources 發行包範例時，請將絕對路徑換成實際位置：

```powershell
.\bin\veroscript.exe --bc-cache=off 'D:\Packages\Resources\Scripts\examples\standard_library_algorithms.vs'
```

## 離線文件與範例

另行取得相符的 Resources 分發內容，開啟 `Resources/Docs/README.md`，選擇語言並透過相對連結閱讀參考與教學。獨立文件 ZIP 解壓縮後使用套件內的 `docs/README.md`。Resources 另於 `Resources/Scripts/examples` 提供腳本範例。目前 SDK 歸檔與安裝程式不附帶文件/範例元件。線上原始碼參考需要網路；範例可能需要相符的 SDK 與相依項目。

## 安裝程式語言

安裝程式提供英語、法語、日語、韓語、簡體中文與繁體中文。可在視窗中選擇語言，也可啟動時傳入 `--language`。初始語言跟隨系統介面語言，無相符語言時使用英文。此設定僅控制安裝介面；腳本診斷請另用 CLI 的 `--lang` 或 SDK 語言 API。以下 Windows 範例的路徑及版本請替換為實際下載檔案：

```powershell
& 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe' --language zh-TW
```

## Linux/macOS 安裝

Linux 解壓縮相符的 `linux-x64` 或 `linux-arm64` `.tar.gz` 安裝程式後執行其中的程式；macOS 開啟相符的 `macos-x64` 或 `macos-arm64` `.dmg`，啟動其中的安裝程式 `.app`。安裝視窗需要圖形桌面。修復與解除安裝使用和 Windows 相同的記錄參數。下例使用 Linux；macOS 將 `installer` 設為原安裝程式的 `<installer>.app/Contents/MacOS/VeroScriptInstaller`，在唯讀映像中的應用程式上省略 `chmod`。所有範例路徑均需換成實際路徑。解壓縮可攜 Runtime/SDK 封存時亦應保留執行權限。

請選擇下方的一種操作，並將安裝程式與記錄路徑替換為實際路徑。每條命令開啟對應視窗，確認後才執行。

僅修復:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --repair-record "$record"
```

僅解除安裝:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --uninstall-record "$record"
```

## 授權聲明

本文件附帶專案的完整 [MIT 授權條款](../../../../../LICENSE)。安裝程式的相依套件聲明可透過下載的安裝程式查看。請單獨使用其中一個聲明選項；它會輸出原始聲明並結束，不開啟安裝視窗。將範例路徑替換為實際下載的檔案。聲明保留原文語言；`--language` 另行控制安裝介面的語言。

範例暫時使用 UTF-8，確保 Windows PowerShell 5.1 正確顯示聲明中的字元，並在結束後恢復終端原本的編碼。

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

## Windows 檔案關聯

Windows 安裝套件包含 Runtime 與對應圖示時，選擇 Runtime 後可以設定可選的檔案關聯。新安裝預設不啟用。可以登記 `.vs`、`.vsapp` 由該 Runtime 執行，或僅登記 `.vspkg` 圖示。開啟關聯的 `.vs` 或 `.vsapp` 會執行該檔案；`.vspkg` 選項不執行或安裝套件。

登記為目前使用者加入執行處理常式和圖示。預設應用程式由你在 Windows 設定中選擇，登記本身不指定系統預設值。可選的設定操作會在安裝成功後要求開啟該頁面；開啟失敗或登記不完整會在結果頁提示。Windows 也可能顯示所選預設應用程式的圖示。

更新會沿用記錄中尚未被你編輯的關聯選擇。取消選項、修復、回復與解除安裝均保留無關登記及後續使用者修改；只有本次安裝仍擁有且未被改動的值可以移除或還原。其他平台及缺少所需 Runtime 檔案的套件不提供這些選項。

`.vspkg` 圖示僅在尚未註冊圖示時新增。取消「安裝後選擇預設開啟程式」只取消本次開啟設定的要求，不重設已在 Windows 中確認的選擇。解除安裝不修改受保護的預設應用程式記錄，也不保證自動還原先前的預設程式。舊安裝記錄沒有關聯所有權欄位時，不執行關聯清理。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
