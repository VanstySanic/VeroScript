# Python 套件、反射與在地化

## 套件類型與建置結果

請參閱[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#package-container-format)。

`PackageKind.APPLICATION` 會建立可執行的應用程式套件，`PackageKind.LIBRARY`
會建立函式庫套件。`Session.build_package_file(...)` 回傳 `(data, RunResult)`；
建立 `Package(data)` 前應先檢查 `result.success` 和診斷。
`Package.from_file(path)` 會解析檔案，`package_kind` 表示套件類型。解析只驗證
容器結構，不驗證發行者真實性；載入不可信位元組前，請先驗證
`canonical_signed_bytes` 上的簽章。

解析後的 `Package` 是不可變快照。讀取其他位元組時應建立新物件；再次呼叫
`__init__` 會拋出 `veroscript.Error`。

## 簽名位元組與目錄

公開屬性 `canonical_signed_bytes` 回傳用於簽章驗證的標準位元組序列。
`merge_catalog_directory` 會加入應用程式目錄，不會取代 runtime 目錄。

`Package.from_file()` 讀取套件，`package_kind` 表示套件類型。工作階段可使用 `load_package`、`reload_package` 和 `build_package_file` 載入、重載與產生套件。套件是載入和分發邊界，不是安全沙箱；宿主仍須控制繫結、路徑、輸入與資源。

`reflection()` 回傳函式、型別、方法、欄位、參數和套件中繼資料；`bindings()` 回傳目前的宿主繫結視圖。

使用 `set_language`、`get_language`、`available_languages` 管理語言；`merge_catalog_directory` 可合併外部目錄。目錄應來自受信任的發布物，合併後驗證訊息鍵和格式參數。語言只影響診斷與格式化，不改變腳本語意。

執行前請檢查目前建置是否提供套件、反射、JIT、熱重載、序列化和執行治理等能力。執行治理包括協作式指令/時間限制、取消和標準函式庫能力策略。評估效能時，請分別測量冷啟動、熱呼叫、套件載入、快取命中、Python 物件轉換、GIL 與回呼成本。JIT 統計中的 `direct_native_calls` 表示在同一代 JIT 程式碼中繞過虛擬機器值橋、直接到達另一個原生入口的呼叫次數；這是診斷資料，不代表固定的效能提升。

## 傳回的物件參考

`enumerate_object_references(instance)` 接受指令碼物件的 `Value`，須在工作階段閒置時由建立執行緒呼叫。傳回 `ExternalReference` 元組，每項的 `kind` 為 `ExternalReferenceKind.SCRIPT_OBJECT` 或 `ExternalReferenceKind.HOST_OBJECT`，`value` 擁有複製後的 `Value` 包裝器。此處複製的是參考，保持物件身分，不會深層複製物件圖。元組及其值可在列舉結束後持有，不是回呼借用檢視；一般值、工作階段、釋放和執行緒規則仍然適用。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](packages.md)
