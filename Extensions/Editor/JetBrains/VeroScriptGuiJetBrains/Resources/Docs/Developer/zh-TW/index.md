# Rider GUI 插件開發

## 歸屬與依賴

[外掛描述檔](../../../META-INF/plugin.xml) 是外掛識別碼及基礎外掛相依性的正本。基礎 VeroScript 專案提供 Runtime 路徑、編譯設定及 VS 詞法著色，不重複打包其類別。[Gradle 的 localPlugin 專案相依性](https://plugins.jetbrains.com/docs/intellij/tools-intellij-platform-gradle-plugin-dependencies-extension.html#local-plugin) 用於開發、測試及 IDE 沙箱；兩個安裝 ZIP 仍然獨立。

使用 JDK/JVM 25、Gradle 9.5.0、Kotlin 2.4.0、IntelliJ Platform Gradle 插件 2.19.0。
SDK 固定為 Rider 2026.2.2，最低構建 `262.10315.191`，嚴格上界為 `262.*`；其他分支需
另行驗證。不內建原生工具或提供者。根 MIT 許可打入 `META-INF/LICENSE`，Gradle Wrapper
原始聲明保存在 `Resources/Licenses`。

## 設計與契約

`gui-compile` 輸出標準 `.vsapp` 應用容器。檢查與預覽將其交給原生 CLI，由後者讀取容器與
Core 位元組碼。Runtime、VM 原生程式庫與 GUI 提供者需要配套使用。

- `language/`：獨立、區分大小寫的標記和樣式詞法器及扁平 PSI；不引入 XML 命名空間診斷或
  瀏覽器 CSS 語意。內嵌腳本重用基礎 VS 詞法器，內嵌樣式使用 GUI 樣式詞法器。
- `GuiMarkupModel`：按檔案快取結構資料，服務補全、引用及文件，不做型別檢查。編輯時不掃描
  整個專案、不自動呼叫原生檢查或預覽。超過 1 MiB 或存在多游標時，跳過輸入便利處理。
- `Resources/Catalog/gui.json`：從 GUI 自有原始碼提取的內建補全中繼資料。執行
  `python Resources/Tools/update-catalog.py` 後審查差異。來源雜湊用於發現漂移；目錄不能取代
  `FGuiSchema`。自訂 Schema 由原生工具驗證，不可因未收錄而報錯。保留 Original/Mark
  拼寫以及繼承屬性、綁定和事件。
  提取會跟隨 `VeroScriptGui.vs` 的本地匯入與間接支援檔案，省略 Mark 時使用宣告名稱。
  類別邊界與註解/字面值遮罩確保各宣告的中繼資料歸屬正確。
  目錄格式 2 保留宣告的 `qualifiedName`，包括 `VS::Gui::Internal` 作用域。查找與控制項
  文件使用真實限定名，並保留短名與 Mark 別名；目錄須與讀取程式碼一起產生、打包。
- `run/`：繼承基礎 Runtime、語言與編譯設定，按各路徑來源解析。參數依序為全域編譯/
  環境選項（含 `--path`）、GUI 後端選項、`gui-*` 任務及任務選項，最後 `--` 與預覽腳本
  參數。保留重複 `--asset`，影格數遵循 `int64` 邊界。
- 檢查和烘焙不初始化執行腳本；預覽建立一個原生行程，由 Rider 行程處理器停止與終止。
  後端 ID 可留空。沒有 GUI 專用 DAP 啟動適配；宿主已準備偵錯域時，使用基礎插件附加。
  主控台只按行導覽，不將原生 ML 的 Unicode 碼點欄當作 UTF-16 位移。

中繼資料擷取接受 `@` 與註解名稱之間合法的語言空白，例如 `@GuiWidget` 和 `@ GuiWidget`，兩者產生相同的屬性、事件與繼承資訊。即使公開型別不變，原始碼摘要變更後仍須更新目錄並審查差異。

## 驗證與發行

編輯器不變量：兩種詞法器的 token 歸屬各自語言，掃描不得越過指定範圍，包括截斷的結束標記。
內嵌正文在下一處原始正文/CDATA 邊界停止，不反覆搜尋檔案餘部；以字元讀取次數的回歸案例約束
線性掃描，避免依賴機器計時。
補全消費 `=`/`:` 接受字元，保留既有值，Tab 取代完整名稱。普通本機 `src` 引用透過
`GuiStringLiteralManipulator` 修改路徑/重新命名，跳脫寫入內容，保留未閉合引號；含實體跳脫
的路徑仍不提供本機導覽。目前系統的絕對路徑從檔案系統根解析，重新綁定時寫入完整原生前綴，
不採用 SDK 的專案根語意；相對路徑保留所屬檔案基準，停用 SDK 的專案根式路徑轉換。
僅正規化原生分隔符，保留 POSIX 檔名中的反斜線；引用解碼保留 `?`，並沿用 SDK 的
macOS NFC 正規化。
相對路徑重新綁定保留 SDK 行為：無法形成相對路徑時不改寫引用文字。跨磁碟/檔案系統移動需
單獨複核和驗收，不宣稱具備自動遷移能力。
文件目標拒絕已編輯的快照，樣式懸停只識別屬性 token。上下文操作
清除不相容的任務/入口範本選項，保留適用的自訂輸出。靜態檢查只遍歷自有輸入，不遍歷
Gradle SDK 快取或沙箱。

在本模組目錄執行：

```sh
python -B Resources/Tests/test_catalog.py
python -B Resources/Tools/check-static.py
./gradlew :test :verifyPluginProjectConfiguration :verifyPluginStructure :buildPlugin
./gradlew :veroscript:buildPlugin
./gradlew :verifyPlugin
```

Windows 使用 `gradlew.bat`；POSIX 可用 `bash ./gradlew`。透過
`-PlocalIdePath=/absolute/path/to/Rider` 使用已安裝 SDK。明確的根任務路徑避免誤選所有子專案
測試。GUI 輸出為獨立的 `VeroScriptGuiJetBrains-0.1.1.zip`。倉庫綜合 IDE 打包已登記
GuiJetBrains；正式流程會建置基礎插件，並按各自版本聯合校驗兩個本機 ZIP。
獨立開發建置仍可使用上述命令。

基礎插件尚未發佈時，單獨執行 `:verifyPlugin` 無法從 Marketplace 解析這項相依性。
應使用官方 Plugin Verifier CLI 同時校驗兩個 ZIP：在 `plugins.txt` 中逐行寫入兩者的絕對路徑，
再執行下列命令。這樣使用本機基礎插件解析相依性，出版前需要確認兩個插件的校驗結果。

```sh
java -jar /absolute/path/to/verifier-cli.jar check-plugin @/absolute/path/to/plugins.txt /absolute/path/to/Rider
```

測試涵蓋詞法器重啟/不完整輸入、目錄繼承與樣式限制、可遷移路徑、影格數與入口驗證、CLI
選項順序與參數分隔、資源/覆寫檢查、持久化、註冊和六語言資源。SDK 夾具不能取代 Rider
實機驗收。需用 GUI Runtime 實測 ML、VS、產物的 Check/Compile/Preview、遷移、提供者、
空白及非 ASCII 路徑、停止/取消和診斷導覽；每個目標系統分別驗證。檢查內嵌著色、補全首項、
引號/標籤、`src` 導覽，以及兩插件同時安裝時普通 `.vs` 的行為。

請參閱[編輯器整合維護](../../../../../../../../Resources/Docs/Developer/zh-TW/editors.md#acceptance-evidence)。

編輯器夾具透過僅供測試使用的 SDK 模組初始化正常的 Rider 資料夾方案，使用真實本機檔案。
僅在測試 JVM 內停用 ReSharper；沿用 SDK 的設定損壞檢查，以 Rider 初始化後的設定作為基準。

Rider 互動介面、真實視窗的 Preview/Stop、遷移、其他作業系統和真實宿主整合仍需單獨驗收。
上述結果僅涵蓋已測試環境，不代表這些剩餘情境已經通過。

六語言介面、User 和 Developer 文件應同步維護；模組清單已註冊到共享文件目錄。
修改文件後應執行文件出版校驗。

[使用者指南](../../User/zh-TW/index.md) ·
[規範](../../../../../../../../Resources/Docs/Standards/zh-TW/GENERAL.md)

## 檢查／烘焙呼叫與儲存路徑

`GuiCommand.arguments()` 對 `check`、`compile` 在最前面加入 `--backend=vm`，先於 `--path` 準備套件環境，抑制這些工作的執行 JIT 選擇；預覽不傳執行後端或 JIT 覆寫，保留原生預設值。GUI 後端欄位仍是獨立服務選擇。`GuiCommand.persist()` 僅在專案相對儲存路徑中將 `java.io.File.separatorChar` 轉為 `/`，保留 POSIX 檔名中的字面反斜杠。維護選項順序、設定持續化後的省略／auto／none／自訂提供者及本機檔名往返情境。路徑行為應按目標 OS 分別驗收，Windows 結果不能證明 POSIX 行為。

[Rider SDK](../../../../../VeroScriptJetBrains/Resources/Docs/Developer/zh-TW/index.md#rider-platform-sdk)

<!-- docs-navigation -->

---

[開發者文件](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
