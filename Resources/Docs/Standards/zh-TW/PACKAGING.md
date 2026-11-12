# 打包與發行規範

適用範圍：[VeroScript 本專案開發與貢獻](README.md)。本規範定義官方發行集合、候選建置與出版驗收，不改變外部專案的授權或打包方式。

## 規範與實作狀態

目錄、分類與新格式已由統一產品登錄及發行指令碼實作；純規劃與靜態回歸不能取代成功的實際建置與承載內容完整性驗證；目標執行與安裝生命週期驗收另行記錄真實結果，包括失敗與未執行。 僅接受新契約，不提供舊格式相容、轉換、雙寫或遷移；清理舊產物後從原始碼重建。

同步產品登錄、版本解析、純規劃、產生器、安裝負載、資訊清單、驗證、發行交易、清理與 CI。明確設定 schemaVersion，新讀取器僅接受新契約並安全拒絕舊格式，不保留相容分支。產品與格式版本獨立，產品版本以發行登錄為準。入口見[發行布局](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-TW/release-layout.md)，舊 `migrate-layout` 不屬新流程。

## 目錄與職責

正式根目錄為原始碼根下的 `Release/`，不依賴外層名稱 `Script` 或 `VeroScript`。移動或更名檢出目錄時保留內部相對路徑；受編譯路徑影響的快取須重新建置。候選、建置快取與驗收記錄存於 Release 外的專用暫存目錄。

```text
Release/
  release-index.json
  indexes/<target>.json
  Runtime/<target>/<version>/
  CoreSDK/<target>/<version>/
  Interop/<target>/<version>/
  Resources/any/<version>/Resources/
  Modules/
    Execution/{Jit,Vm}/<target>/<version>/
    Network/{Json,Network,Http,WebSocket}/<target>/<version>/
    Network/Backend/{Boost,Curl}/<target>/<version>/
    Multimedia/{Platform,Graphics,Audio,Image,Font,Gui}/<target>/<version>/
    Multimedia/Backend/{Sdl3,Stb,FontBackend,RmlUi}/<target>/<version>/
  Extensions/
    Language/{Cpp,CSharp,Python}/<target>/<version>/
    Editor/VSCode/{VSCode,GuiVSCode}/<target>/<version>/
    Editor/JetBrains/{JetBrains,GuiJetBrains}/<target>/<version>/
    Platform/Mobile/{Android,iOS}/<target>/<version>/
    Platform/WebAssembly/<target>/<version>/
```

`<business-route>` 包含已登錄的業務分類路徑及業務名；`<target>` 表示分發範圍，可為原生／行動目標、`any` 或 `wasm32`，不表示建置主機。`{A,B}` 表示分別建立目錄。

業務版本目錄僅建立實際需要的分類，資訊清單與摘要位於版本根部：

| 路徑 | 用途 |
| --- | --- |
| `archives/` | 直接解壓部署的執行期分發歸檔 |
| `sdks/` | 含開發介面、建置匯出與必要相依性的 SDK 歸檔 |
| `packages/` | `.vspkg`、`.vsix`、外掛 ZIP、`.nupkg`、wheel、sdist、AAR 等生態分發包 |
| `installers/` | 受支援產品的安裝程式與選定下載封裝 |
| `symbols/` | 所屬業務的符號分發，包含 `.snupkg` |
| `Resources/` | 僅 Resources 業務使用的鬆散 Assets、Docs、Scripts、Examples 樹 |
| `release-manifest.json`、`checksums.txt` | 此業務、範圍與版本的庫存及摘要 |

依用途與資訊清單角色分類，不只看副檔名。生態包不機械套入相同內容的 ZIP；需要額外標頭或建置資源的 SDK 可保留完整歸檔。Unix 封裝保留執行權限與必要程式庫連結；預設僅保留安裝程式的一種下載形式，有獨立使用需求才同時發佈裸檔與壓縮包。發行分類不自動改變安裝負載路徑。

Runtime、CoreSDK、Interop、Resources 直接歸根目錄；Cpp、CSharp、Python、WebAssembly、四種編輯器產品及 Android/iOS 歸 Extensions；執行、網路與多媒體模組歸 Modules。套件用戶端仍屬 Runtime/CoreSDK，不新增業務。登錄必須涵蓋新產品、相依性、角色、目標能力及排除理由；完整性依所選目標與發行設定判定，不要求每個平台包含全部業務。

目錄拼寫固定：`archives`、`sdks`、`packages`、`installers`、`symbols`、`indexes` 均用複數。Modules、Extensions 及 Execution、Network、Multimedia、Language、Editor、Platform、Backend 等業務分類沿用原始碼拼寫，業務 ID 與包名維持穩定。

## 目標範圍與共用產物

原生分發依作業系統與架構區分，例如 `win-x64`、`win-arm64`、`linux-x64`、`linux-arm64`、`macos-x64`、`macos-arm64`。其他已登錄架構也須聲明實際支援的設定，登錄不等於整套發行已可用。`android` 為多 ABI 分發；`ios` 包含真機與模擬器，iPadOS 共用同一 SDK；WebAssembly SDK 使用 `wasm32`，原生安裝程式仍歸宿主目標。

`any` 是不依單一原生目標拆分的共用分發位置，可收錄平台中立檔案或涵蓋多目標的同一生態包，但必須正確聲明 `supportedTargets`、ABI、TFM、執行期相依性及限制。它不代表所有平台皆受支援；Android 多 ABI AAR 與 iOS XCFramework 仍歸各自平台。

每個業務與版本的共用分發僅存一份，以資訊清單參照重用。現有四種編輯器外掛、平台中立 Resources、清理後的 Python sdist，以及涵蓋 Android/iOS 的同一 C# NuGet 採共用位置。JS/WASM SDK 統一產生後重用，不將原生安裝程式標成通用包。混合業務可同時擁有 `any` 與原生目標目錄，例如 Python sdist 放在 `any`，wheel 依實際平台與 ABI 分發。

只有業務、版本、用途、支援範圍、相依性及 ABI 契約一致，且最終摘要相同時才合併；同名或相同副檔名不足以去重。依明確產生契約先形成規範位元組，再產生資訊清單、簽章與摘要。自有文字採 UTF-8/LF，保留必要 BOM 及 batch 例外；二進位、簽章檔、第三方原文與位元組敏感夾具不得機械改寫。正式分發不依賴跨目錄符號或硬連結來實現共用參照。

獨立安裝程式、wheel 與離線 SDK 仍攜帶聲明的必要執行期相依性。獨立可用性優先於磁碟去重，不得將必要程式庫搬至相鄰 Release 目錄並假設下載者擁有該目錄。

分別記錄 `supportedTargets` 的實際負載目標與 `consumerTargets` 的可引用發行目標；例如 wasm32 SDK 可供原生宿主索引引用。組合原生與公共分發時維持真實且匹配的生態版本。

## 獨立版本與相依性

各業務、各目標允許獨立版本；`ReleaseVersions.json` 提供預設值，不是集合統一產品版本。共用分發也有明確版本，平台更新不得靜默覆寫其他目標仍參照的內容。索引固定選定版本及資訊清單摘要，不隱式選擇最新版。NuGet、wheel、sdist、外掛與二進位保留真實版本，不只改檔名或偽造平台標籤。

原生版本、業務版本、公開 ABI、建置識別及格式版本分別記錄。Cpp/Core、GUI 附加外掛/基礎外掛及模組相依性，依實際版本、ABI、建置識別或程式庫摘要核對。對外發行的同一套件識別不得靜默替換為不同內容；重建未發行的本機候選必須更新摘要並重做受影響驗收，產品版本策略仍須明確。

同一生態包識別必須對應唯一內容，不能將同 ID/版本的不同 NuGet 或 sdist 放在多個平台目錄，再以目錄名稱區分。多目標差異使用生態的 TFM、RID、ABI 或平台標籤；無法合併時明確定義不同分發識別，不破壞既有包 ID/版本契約。

共用 `any`／`wasm32` 資訊清單發佈至受管集合後，同一業務／範圍／版本的身分不可變，包括選用符號與宣告的支援範圍。應於封存前收齊共用庫存；增刪或替換內容須使用新的業務版本。僅對尚未對外發佈的本機產物，明確清理集合後可按已設定版本乾淨重建；刪除本機檔案不能授權修改已分發給使用者的同一套件身分。

`any`／`wasm32` 清單不記錄發行宿主的原生版本。C# 的原生版本脈絡記錄在各目標索引的業務項目中，共用包身分由其內容與契約決定。

## 索引、資訊清單與參照

`release-index.json` 使用格式版本 **1**，產品識別為 `VeroScript Release Set`；`indexes/<target>.json` 使用版本 **5**，業務 `release-manifest.json` 使用版本 **3**。暫存候選清單使用版本 **2**、產品識別 `VeroScript Release Candidate`。`checksums.txt` 按業務、範圍與版本產生。索引業務的 `manifests[]` 逐項記錄範圍、真實版本、相對路徑與 SHA-256；分類和產物角色來自統一產品登錄。

根 `release-index.json` 是集合入口，記錄布局/格式版本、世代識別及 `indexes/<target>.json` 的相對路徑與 SHA-256，不宣稱所有業務共用產品版本。目標索引各自記錄目前選擇，共用索引管理共用分發。

同一目標業務可參照多份資訊清單，例如 Python 的目標 wheel 與 `any` sdist。每筆參照包含明確 `target`、`version`、資訊清單相對路徑及 SHA-256，不保留一業務只對應一份同目標清單的舊假設。不同業務、不同目標可獨立選擇版本；同一業務在同一目標索引中組合的共用與原生元件須保持生態系／產品版本一致，並共同通過相容性與完整性驗證。

業務清單記錄業務、分發範圍、真實版本、建置/原始碼識別、`supportedTargets`、相依性、角色與完整檔案庫存。每檔含分類後相對路徑、大小、SHA-256；原生記錄作業系統、架構、ABI、最低系統/API、工具鏈及切片，生態包補充 Python ABI、TFM 等選擇條件。`checksums.txt` 由同一庫存產生，目標選擇、路徑、生態中繼資料與二進位識別必須一致。

清單內檔案相對業務版本根，索引內清單相對 Release 根解析。拒絕路徑穿越、絕對路徑、連結逃逸、大小寫或保留名別名、重複項、未知檔及參照環。跨範圍只參照受管理且支援該目標的清單；完整驗證涵蓋全部相依/參照閉包，不從目錄名稱推測內容。

庫存涵蓋實際產物與鬆散資源，排除本清單及 `checksums.txt` 自身以避免摘要自我參照；上級索引固定清單摘要，根入口固定索引摘要。依格式記錄或驗證檔案類型、執行權限及安全相對程式庫連結，不只比較一般檔案位元組而遺失可執行性。摘要驗證不取代發行者身分與實際簽章策略。

索引須區分所選元件已發行與整平台完整發行，記錄設定要求的業務集合、缺項、partial 及未驗收項；元件成功不認證整平台。無正式產物的目標不得偽造成功索引，建置/驗收缺口保留在候選報告。

## 功能負載與原始碼包

Runtime、SDK、Extensions、Modules 與安裝程式僅含功能、執行資源、中繼資料及法律聲明，不附 README、手冊、教學、範例、測試、基準或歷史。Resources 提供整合 User 文件及相關資源；Standards 僅供開發貢獻，透過 All 提供，不進入 User 或功能包。保留授權、NOTICE、必要本地化、圖示、語法、片段與執行資料；Node.js/Sharp 等開發期圖示產生工具不成為 Runtime/SDK 相依性。

VS Code VSIX 根目錄的 `extension/readme.md` 屬於擴充套件詳細資料頁中繼資料。這一特定展示檔案可隨包提供，須與模組 README 正本文字一致，並連結包內資源或線上指南。其他 README、手冊及 Developer/Tutorials 目錄仍不進入功能包。

所有 Modules（包含 Jit 及未來模組）不提供獨立產品安裝程式，符號依所屬業務獨立分發。安裝程式不提供文件/範例選項，固定分頁精靈及所有權規則維持；取消或解除安裝檔案關聯、圖示及 PATH 只清理本次安裝擁有的設定。

sdist 是可建置原始碼的明確例外，須含獨立解壓後可建置的最小原始碼、標頭、建置指令碼、中繼資料與法律聲明，不帶無關說明與範例。排除並檢查 `.cxx`、`.gradle`、`.externalNativeBuild`、CMake 快取、物件、編譯產物及本機路徑；不只依賴 `.gitignore` 或宿主恰巧乾淨。核對實際歸檔成員，必要時從獨立 sdist 重建消費者。

模組安裝、套件啟用和執行後端／服務提供者選擇是不同操作。宿主設定遵循省略／auto／none／明確選擇的統一規則；auto 可以從已安裝且啟用的相容候選中自動選擇。發行或安裝須保留宿主的後端設定及停用選擇，不得將選用的 Jit／提供者靜默加入 Runtime/CoreSDK 的基礎負載。

圖示以正式 Assets 及自動資源清單為來源，新增檔案納入版本控制與 CI，不固定歷史數量。SVG/PNG/ICO/ICNS 等原始或二進位資源逐項比對來源；安裝參照須解析至穩定產品負載路徑。嵌入圖示變更後重建引用程式/安裝程式，替換外部 ICO 不能代替 EXE 重建。關聯、重新整理與解除安裝維持安裝程式所有權契約。

原始碼驗證也會逐幀核對 Windows CLI、Runtime、launcher、LSP、DAP 和安裝程式 EXE 的內嵌圖示，重用建置亦須通過。VS Code 宣告的產品 PNG 直接與正式資源比對。

## Android、iOS 與模組 SDK

Android 主 SDK 整合核心/Interop、共用 Mobile 適配、JNI、AAR、標準庫、必要資源及授權；iOS 整合對應靜態適配與 XCFramework。Mobile 共用層不是使用者須額外安裝的產品，主 SDK 也不因此包含所有可選模組。

`Modules/<category-route>/<business>/<android|ios>/<version>/sdks/` 是可選原生 CMake 開發分發，不是另一套主 SDK，也不是桌面 `.vspkg`。保留必要標頭、CMake 匯出及聲明的相依閉包，區分內含與外部相依性，避免同一應用衝突或重複連結；不能因主 AAR 存在而刪除須獨立提供的 C++ API 或模組開發支援。

完整移動發行涵蓋平台適配、CoreSDK、Cpp、Interop、Vm、CSharp、Python 及所選可選模組。一般應用只消費主 SDK 與必要模組，無須下載全部開發分發。Android 目前含 arm64-v8a/x86_64；iOS 含 device-arm64、simulator-arm64、simulator-x86_64。逐切片驗證真實二進位與消費者連結；同 CPU 不代表同作業系統或真機/模擬器 ABI。iOS 明確排除不可用的原生 JIT，不以更名假裝支援。

C# 按實際 TFM、原生程式庫與資源選擇，Python 按目前聲明的 CPython 矩陣及移動切片選擇。配對 .NET、Xcode、NDK 及目標相依性，分別記錄 SDK 編譯版本與最低執行系統。

## 交叉建置與驗收

流水線須區分原始碼/設定預檢、真實建置、候選組裝、靜態校驗、目標執行驗收、發布與最終複核。可交叉編譯的目標應產生真實目標產物；缺少本機執行環境不代表不能編譯。成功建置且完整性通過的套件可納入 Release，包括尚未驗收或執行驗收失敗的套件，但須保留明確狀態，不得將建置成功視為驗收通過。

分別記錄建置宿主與目標識別。使用目標編譯器、sysroot、標頭及完整相依性，需執行的產生器使用宿主版本。原生程式、C# AOT、Python wheel、安裝程式及第三方庫各自確認支援，不只改 RuntimeIdentifier、名稱或標籤。桌面 ARM64 不承接移動 ARM64 的驗收。

驗收區分 `passed`、`failed`、`not-run`、`not-applicable`，記錄原因、環境、工具鏈、設定與證據摘要。交叉編譯/連結、模擬器、實機、圖形桌面、安裝生命週期、簽章/公證、C# AOT/裁剪及 Python 應用分別記帳。真實建置及包體、架構、來源、相依性、清單與 SHA-256 校驗是分發前必要條件；執行或裝置驗收缺口不再阻止已建置產物一併分發。未執行、失敗、未簽章或工具鏈缺失不得寫成通過。業務 `qualification` 使用版本 1，綁定 `manifests[]` 的路徑與摘要；載荷變更使舊記錄失效。目標 `complete` 僅表示必要產物庫存完整，`qualificationStatus` 彙總驗收；僅當庫存完整且全部必要檢查通過時 `qualified` 才為 true。缺少記錄為 `not-recorded`，不得推定成功。

macOS 安裝器以 `.dmg` 提供標準 `.app`、正式 ICNS、Info.plist 與執行載荷；Linux 使用保留執行權限的 `.tar.gz`，Windows 使用 `.exe`。SDK 歸檔格式與安裝器形式分別決定。先處理程式碼簽章，再封裝並校驗映像；ad-hoc 不等於 Developer ID 或公證。非 macOS 主機需建置工具 7-Zip（`VEROSCRIPT_7ZIP` 或 PATH）審計 DMG，不新增產品執行相依性。

桌面交叉組包入口為 `Resources/Tools/Packaging/package-veroscript-desktop-cross.py`。ABI 由目標編譯器對正式標頭表達式求值，不能複製宿主 ABI。不能執行目標時，僅在來源逐檔相同且至少兩個原生平台的宣告目錄一致時重用中立目錄；32 位目標須匯出自身目錄。可執行工具或模擬器以 `--target-tool` 接入，目錄查詢不代表完整執行驗收。宿主工具可跨目標建置、校驗與封裝；實際安裝仍須匹配平台與架構。

完成簽章、公證及其他改變位元組的後處理後，才記錄最終分發摘要並綁定驗收證據；內部負載庫存仍獨立驗證。簽章前摘要不能認證簽章後檔案。發行後複核最終目錄、歸檔、參照路徑與嵌入負載，任何位元組變更均使對應證據失效。

## 發行、取消與清理

發行和清理共用集合鎖。先驗證候選、支援範圍、參照閉包及來源，再於受管理暫存區準備檔案、清單與索引，以可復原交易提交業務、目標索引及最後的根入口；失敗恢復本次內容與參照。讀者使用鎖或世代/摘要複核，更新中不一致時重試，不宣稱全集合一次不可分割交換。

取消只停止本次任務的建置子程序與傳輸服務，不發行未完成候選、不覆寫正式產物；保留必要診斷並恢復暫時環境設定。續行前核對原始碼、設定、工具鏈及候選摘要，不重用過期驗收。

清理依全部目標索引與清單相依的參照閉包。預設刪除無參照舊版，但保留其他平台、Cpp 或 GUI 附加外掛仍使用的版本；共用包最後一筆參照消失才可清理。遇未知/非受管理檔案或路徑驗證失敗即停止，不依年齡或檔名推斷所有權。僅刪已驗證專用暫存目錄，不刪原始碼、使用者檔案或外部相依目錄。

刪除舊格式前核對所有權、原始碼保護、程序占用及可重用工具鏈。清理舊 Release 與無用候選/快取，保留必要診斷但不保留舊包復原副本，不再依舊索引相依維持舊集合。從乾淨暫存依新規則重建、驗收及發行，產品版本以發行登錄為準。新集合仍被參照的有效版本依閉包保護，不構成舊規範相容。

## 實作門禁

新實作須有與產生器共用規則的純規劃、共用參照/版本選擇、分類/生態驗證、跨範圍相依/鎖/復原/參照清理測試、污染原始碼與舊格式拒絕、目標能力/驗收、乾淨重建與失敗復原。完成前不發行新產物，也不以舊規劃認證新布局。

僅更新規範不啟動編譯、安裝程式產生或發行打包。同步六語、導覽及現有實作說明，執行下列暫時文件整合驗證，不向 Release 發佈產物：

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```
