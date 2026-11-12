# 開發者文件 — Rider

編輯器與原生宿主的公共職責及六語言維護統一見 [公共編輯器整合](../../../../../../../../Resources/Docs/Developer/zh-TW/editors.md)。

採用 `VeroScriptBundle`／`DynamicBundle` 和本地化 `.properties` 資源。驗證 `MessageFormat` 預留位置及單引號跳脫，允許執行時再驗證真實參數替換。

使用 JDK/JVM 25、Gradle 9.5.0、Kotlin 2.4.0、IntelliJ Platform 外掛 2.19.0。SDK 固定 Rider 2026.2.2；`sinceBuild=262.10315.191`、`strict-until-build=262.*` 將安裝限制於 262 分支，其他分支須另行驗證。`VEROSCRIPT_TEST_RUNTIME` 指向目前 Runtime 後執行原生測試，否則明確略過；可選 `VEROSCRIPT_TEST_JIT_BACKEND` 啟用真實模組測試。SDK 結構檢查和 ZIP 建置不能取代 Rider 介面驗收。

```sh
./gradlew test verifyPluginProjectConfiguration verifyPluginStructure buildPlugin
./gradlew verifyPlugin
```

`gradlew.bat` (Windows) · `bash ./gradlew` (Linux) · `-PlocalIdePath=/absolute/path/to/Rider`

[Standards](../../../../../../../../Resources/Docs/Standards/zh-TW/GENERAL.md)


原生回歸涵蓋 `std::memory` 的命名空間與成員補全、原始碼拼寫、懸停、診斷及同版本語意 token delta。DAP 測試透過外掛的 LSP4J 傳輸展開 buffer、view 和監看，驗證檢查項目拒絕修改，以及單步後物件身分保留、舊參照失效。中繼資料數值維持協定字串；`variablesReference` 是不透明控制代碼，不是記憶體位址。

有子項數量資訊時使用有界 DAP 分頁，收到短頁即結束展開；沒有數量資訊時取得一次完整快照並在本機分頁，不能僅憑回傳 100 項判斷支援分頁。非同步回應與快取頁回呼均須檢查目前暫停世代和節點生命週期。回歸涵蓋空清單、99/100/101/250 項、短回應、恢復執行後的舊快照，以及真實 Runtime 的 100 元素陣列。

被動偵錯懸浮提示只追蹤工作階段所屬專案的編輯器。繼續、逐步執行與暫停請求的錯誤只作用於該請求的執行世代；過期的失敗不會影響新的暫停。目前世代的暫停失敗會回報錯誤並保留連線，連線遺失仍獨立結束工作階段。協定與彈窗測試替身涵蓋這些邊界，包括暫停失敗回應與停止通知的兩種到達順序。

匯出別名回歸涵蓋透過原始碼門面使用 `export using` 與 `export typedef` 的補全、私有別名排除、懸停及定義導覽。CLI Run/Check 也驗證門面匯入由同一 Runtime 建置的 `.vslib`。這些是原生協定與程序測試，不能取代 Rider 介面驗收。

本機詞法表與核心的 `constexpr` 關鍵字同步。DAP 變數的 `presentationHint.attributes` 包含 `readOnly` 時不提供修改入口；其他顯示屬性不改變可寫性，宿主與執行域能力仍控制整體修改權限。回歸用例涵蓋關鍵字邊界，以及唯讀、可寫與附帶其他顯示屬性的變數子項。

## 編譯擴充與產生的原始碼

DAP `stackTrace` 堆疊框架的 `column` 已使用從 1 開始的 UTF-16 欄號，適用於本機檔案、正 `sourceReference` 唯讀快照及即時偵錯域。編輯器行文字可用時，用戶端只將計數起點改為 0，在該行範圍內限界並避免拆開代理對；不得再次按 UTF-8 位元組轉換。座標及控制器回歸用例涵蓋中文、補充字元和 CRLF。

偵錯原始碼引用在顯示首批或分頁堆疊框架之前非同步解析。正引用編號在單一連線內快取為不可變唯讀檔案；即使同時提供本機路徑，也優先使用引用。快照不參與專案操作或 LSP。原始碼讀取失敗保留目前暫停；版本檢查拒絕延遲的暫停結果，銷毀時釋放快取。原生回歸涵蓋原始碼編譯擴充匯入、產生成員的補全/懸停/導覽及進入產生方法的逐步偵錯，並涵蓋匯入公共巨集。獨立工具探索原始碼編譯擴充匯入；原生處理器登錄表與 `ModuleEnvironment` 屬於宿主 API 選項，不屬於 LSP 初始化 JSON 設定。`veroscript/moduleSource` 提供宿主環境模組；目前外掛啟動獨立 LSP，不連線至宿主 LSP 端點。 失敗的原始碼請求會移出快取，後續載入堆疊框架時可重試；同一堆疊頁內的重複引用只讀取一次。

註記名稱比對由 Tooling/Core 處理；用戶端保留原始碼拼寫與伺服器標籤。原生回歸涵蓋不重新啟動時切換目錄定義的 `caseSensitive` 策略、LSP/DAP 中混合大小寫的編譯註解、`matches_name`/`case_sensitive` 補全及可呼叫型別參數標籤。執行域協定測試替身也涵蓋 attach 回應之前到達的暫停事件，並驗證能力資訊可用及隨後恢復執行。

## 編輯器顯示

原始編輯器詞法依預處理器規則處理行註解接續，僅接受緊鄰反斜線的 LF／CRLF，不忽略尾端水平空白。迴歸涵蓋 token 重啟、截斷範圍、線性字元讀取量、GUI 內嵌著色，以及註解與已結束指令後的空區塊換行。原生 LSP 迴歸涵蓋巢狀可呼叫值、泛型、別名、陣列／map 與隱含接收者鏈，以及私有成員遮蔽；DAP 迴歸保留 variant 檢查項與監看子項的唯讀標記，並驗證一般區域變數仍可修改。

文件目標透過公開 LSP 傳輸非同步請求，只整理可識別的 Markdown 標題，保留伺服器簽章與說明。此描述器停用原生懸停目標以避免重複；以文件修改戳記、檔案有效性及用戶端狀態拒絕過期結果，並繼續傳遞取消。IDE Markdown 渲染器提供隨佈景主題顯示的程式碼區塊，偵錯數值彈窗獨立保留。 所有伺服器結果段落均只合併型別類別後可識別的在地化內建型別中繼資料，原始碼型別別名與圍欄程式碼範例保留原內容。

Enter 委派在 IDE 命令內執行一次取代，保留復原並使用專案縮排。詞法檢查排除註解與字串；指令、跨行標頭、選取範圍、多游標及需要掃描前 1 MiB 之外內容（含目前行後綴）的情況退回一般 Enter。專案大括號偏好不會重新啟動 LSP；全文及範圍格式化、診斷仍由伺服器負責。測試涵蓋渲染、非同步結果失效、游標位置、復原，以及從未完成函式到完整函式的原生診斷。Rider 介面驗收仍需另外執行。

## 情境設定

上下文設定只接受已儲存的本機檔案，將繼承範本規範為 Run/Check，清除附加專用欄位；Check
另清除腳本參數、輸入及 JIT 路徑，保留適用的工作目錄、環境和編譯設定。設定校驗原樣傳播
IDE 取消例外，不能將其包裝為設定錯誤。

Check 始終傳遞 `--backend=vm`，不改變已儲存的 Run 後端偏好。

<a id="rider-platform-sdk"></a>

未設定 `localIdePath` 時，兩個 Rider 專案透過 `rider(platformVersion) { useInstaller = false }` 取得多 OS SDK 封存檔，不能將平台安裝程式作為開發 SDK。`-PlocalIdePath=<directory>` 可選擇既有本機 IDE。仍須符合上文固定的 Rider／建置分支、外掛相依及 JDK 要求，並分別驗收目標 OS。

<!-- docs-navigation -->

---

[開發者文件](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
