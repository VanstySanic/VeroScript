# 核心架構與編譯管線

[可替換 VM 後端](vm-backends.md)

位元組碼採用固定標識 `VSBC`，格式版本跟隨產品版本，目前均為 `0.1.1`。舊開發建置的產物須重新產生。

核心擁有語言實現，是原生介面卡的依賴根。其公共範圍包括 lexer、parser、AST、preprocessor、compiler、bytecode、VM、程式碼生成、JIT、模組載入、包、除錯、宿主繫結、反射、序列化、併發、變更機制與區域設定選擇。

## 管線

實現會解析原始檔、覆蓋層、匯入和包輸入；在保留源位置的同時進行 token 化與預處理；解析公共 AST；完成語義分析和型別/泛型/可見性檢查；發射位元組碼和除錯後設資料；連結模組/包資訊；最後經由 VM 或選定的 JIT 策略執行。

每個階段都會返回結構化診斷。消費者不得從流訊息或部分填充的位元組碼物件推斷操作成功。

## 模組圖與載入器輸入

`VeroScriptModuleLoader` 會在合併程式前解析主模組、普通匯入、隱式匯入、stdlib 模組及包/模板模組圖。匯入路徑和 stdlib 根目錄會規範化並去重。源覆蓋層按照規範化路徑替代磁碟讀取，使編輯器或 Web 宿主無需寫入檔案即可編譯記憶體中的模組圖。

前處理器 `-D` 定義和結構化預定義宏在影響源指紋前會被規範化。宿主型別名稱會在解析前提供，因此諸如 `Counter c = Counter(1);` 的宣告能依據宿主繫結目錄正確分類。迴圈匯入和專案範圍訪問策略是顯式的載入器設定，不是隱藏的全域性編譯器狀態。

引入 stdlib 根或模組資源時，載入器可合併相鄰的 `localization/` 樹。區域設定合併按規範目錄 ID 進行，並保留 `en-US` 回退；因此資源包必須將其 stdlib 與本地化目錄一同部署。

## 公共/私有邊界

`Public/` 包含受支援的標頭檔案。`Private/` 包含編譯器、parser、VM、JIT 和內部資料結構。公共標頭檔案測試確保已安裝標頭檔案不依賴缺失的私有檔案。外部 tooling 和 adapter 必須使用公共契約；原始碼樹內確實需要 compiler AST 或 runtime internal 的 target，必須由 CMake 明確授予 private include path，並視為經審查的 friend dependency，而不是 SDK consumer。

<a id="core-vm-ownership"></a>

Core 負責程式、值、邏輯框架、語言操作、執行控制與排程；完整位元組碼解譯迴圈屬於 `Modules/Execution/VeroScriptBytecodeVm` 的 `VeroScriptBytecodeVm`。後端 ID 是 `vs-bytecode`，套件名稱是 `vs-bytecode-vm`。純 Core 不隱式選擇 VM 或 JIT。Runtime 明確裝配 `Vm/` 下的官方動態庫；Interop、Web 與行動靜態產品明確注入 VM。 JIT: `Modules/Execution/VeroScriptAsmJit`; Core: `Private/JitSupport`.

## 靜態宣告、泛型與捕獲

靜態欄位會註冊為模組擁有的全域性變數，並透過模組初始化完成初始化。靜態訪問可以使用型別 receiver 或已求值物件 receiver；後者在讀取後備全域性變數前，仍會為副作用求值 receiver。泛型型別例項化會替換欄位型別，並獨立註冊其靜態儲存。

編譯器會在 AST 中記錄 lambda 捕獲與泛型邊界，並將其源位置帶入位元組碼診斷。靜態型別限制、捕獲有效性、型別引數邊界和繼承規則都是編譯器語義。frontend、AOT emitter 或工具鏈消費者必須使用記錄的宣告身份，不能從顯示名稱重建這些規則。

## 作用域分析控制與相依元資料

`FAnalysisControlScope` 擁有呼叫執行緒上同步分析的取消和耗時回呼。巢狀作用域會還原上一層控制；回呼擷取的物件必須在作用域期間有效，所有 `FAnalysisStage` 必須先於控制作用域結束。取消述詞回傳 true 時，`CheckAnalysisCancellation()` 擲出 `FAnalysisCancelled`，與語法診斷分開處理；它不取消 VM 執行。被取消的詞法器可以再次分詞，不會保留不完整的 token 串流。

`FAnalysisStage` 在同一執行緒回報毫秒耗時，其名稱必須在階段期間有效。觀察回呼的例外會被忽略，避免在堆疊展開時覆蓋取消或剖析例外。巢狀階段的時間有重疊，不能直接相加作為獨立工作量。

`VeroScriptModuleLoader::GetLoadedFileStamps()` 提供實體原始碼或套件讀取之前記錄的修改時間和大小；記憶體輸入和覆蓋層沒有磁碟記錄。回傳參照僅在下一次載入或載入器銷毀之前有效。快取使用方必須拒絕 `bValid == false`，並在重用 AST 前重新檢查磁碟元資料，包括剖析期間發生的變更。這些記錄是元資料檢查而非位元組雜湊；文件版本、選項和覆蓋層仍需獨立失效處理。

## 長度與索引

集合長度和線性索引使用 `int64`。底層儲存保持 `size_t`，縮窄轉換前同時檢查腳本範圍和平台範圍。C# 容器仍受 CLR 限制。對應語義的位元組碼配置標識為 `0x43425356`（`VSBC`）。使用舊 32 位元長度語義的快取必須從原始碼重新編譯。C++ 宿主及 AOT 產物需使用更新後的標頭檔與標準函式庫重新建置。

## 公開巨集

請參閱[預處理器 — 公開巨集](../../User/zh-TW/language-basics/preprocessor.md#exported-macro-contract)。

VSBC 0.1.1 也在尾部保存經過驗證的公開巨集表；原始碼快取與 `.vslib` 保留此表，`.vsapp` 寫入空表。舊二進位套件與位元組碼快取都需重新產生。模組預處理於最終輸出的匯入路徑暫停，待目標介面準備完成後繼續；模組準備使用有界的堆積栈。

## 編譯期註記處理

凍結後的原生處理器登錄表透過 `FCompileOptions.AnnotationProcessing`、C++ Engine/Session、Tooling 或 `FCodegenOptions` 接入。裸 `@Name` 等同於 `@Name()`，保留有序標記、具名純量參數及巢狀群組；未知註記仍為中繼資料。單一宣告階段先繫結使用者補定義，再產生方法本文及輔助方法，之後沿用一般檢查、虛擬方法派送及泛型代入。跨檔案定義須載入宣告與實作模組。套件保留產生成員及開放範本，取用時不需要生產者處理器。Tooling 公開唯讀 `GeneratedDocuments`，嵌入工具負責顯示虛擬文件；獨立工具不會自動載入原生處理器，由宿主登錄。C# / Python 透過 Interop 接入原生服務控制代碼。VSBC 0.1.1 儲存結構化註記及產生操作，舊快取及二進位套件須重新產生。原生範例位於 `VeroScript/Resources/Examples/AnnotationProcessing`。

CLI 回歸涵蓋兩個庫各自編譯屬性實例化，以及移除偵錯資訊後的應用套件執行。

## 宿主多載

請參閱[宿主綁定與綁定目錄 — 宿主多載](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/bindings.md#host-overload-resolution)。

## 共用樣板服務

`VeroScriptTemplate.h` 提供 Type／Value 有序參數表、精確有型別常數、有界純求值、引數編碼及開放宣告／實例快照。編譯器擁有具現化結果，不修改輸入 AST；預設項在宣告上下文繫結。實例鍵包含外層與自有引數，VM、JIT、Core AOT 使用具體指令和配置。

`ModuleMeta::EnumerateTemplateDeclarations` 返回擁有自身資料的快照，不執行主體。函式和型別的 `TemplateInstance` 區分自有及外層引數。型別使用規範名稱；值使用精確 `@value:` 編碼，移除前綴後交由 `DecodeTemplateArgument`，不經 double。快照與返回物件同壽命；處理器視圖僅於回呼期間有效。

原生 `TemplateParameters` 與腳本 `declaration_info.template_parameters` 保留一致的種類、預設項及外層標識。編譯庫保留一般匯出型別的開放方法及必要私有依賴；載入閉合應用不執行處理器。內部配置／建置身分會更新，產品及 VSBC 版本維持 0.1.1。消費 AST 的模組須統一重建；獨立原始碼匯出器於輸出前拒絕未支援樣板，Core AOT 使用已選具體實例。

`VeroScriptTemplateQuery.h` 的 `FTemplateCallTypeQuery` 為已準備的唯讀程式提供簽章查詢，重用核心推導、預設項及多載選擇；不編譯主體、不執行處理器或宿主呼叫。呼叫端負責可見性與存取篩選。回傳字串採查詢用型別拼寫，不能作為顯示文字或序列化身分；程式與繫結目錄須長於查詢執行個體，每個執行個體只供單一分析執行緒使用，取消遵循既有契約。

## 執行時最佳化邊界

短整數呼叫計畫僅用於無除錯、無熱重載、無執行治理的有界呼叫，並保留呼叫深度限制。參數和值捕獲須為 int；整數欄位試算失敗不提交寫入，交回普通 VM 以保留錯誤前的副作用。有變更觀察器時不使用欄位計畫。計畫不跨模組或程式世代重用，借用模組在執行邊界清空快取。BigInt 雜湊在不可變負載上按需原子快取，數值複製仍產生獨立包裝並保留自訂協定路徑。

FScriptObject 透過 GetHostBase()/SetHostBase() 存取宿主基底；普通物件不配置宿主／複製建構的附加記錄，複製後修改這些記錄會分離狀態。C++ SDK 與 JIT 後端須以匹配的標頭和 Core 身分重新建構；C ABI 不變。

物件型別身分透過 `FScriptObject::GetTypeName()/SetTypeName()` 存取。型別名經有界名稱快取共用不可變儲存，在 VM／模組釋放後仍有效。借用的名稱參照在該物件更換型別名時失效；更換名稱會清除執行期配置提示。可呼叫簽章快取以模組、世代及實際簽章驗證，並在重入期間保留不可變記錄；參數、參照及回傳值檢查維持原有規則。

## 欄位預設值與靜態相依準備

`RegisterStaticMethodDeclarations` 在一般欄位配置前公布可呼叫簽章。閉合泛型 struct／class／interface 先登錄靜態儲存並公布所屬描述符與方法宣告，再由 `CompileOwnFieldDefaults` 繫結實例預設值。保留命名空間、存取內容、型別／值替換及欄位中繼資料；可能擴充描述符表的回呼後按索引重新取得。剖析器按詞法宣告身分解析自身型別，同時保留明確外層實參、根限定與值繫結。

`FStaticInitializationDeferral` 阻止模組初始化程式碼進入逐實例欄位緩衝區。`bStaticFieldInitializationStarted` 區分早期配置與靜態初始化階段，`PendingGenericStaticFieldInits` 保留待處理所屬型別。`FlushPendingGenericStaticFieldInitializers(RequiredType)` 僅取出指定所屬型別，維持無關型別的順序。初始化程式碼按底層全域儲存產生一次，不內嵌於每個欄位預設值，也不隨執行期靜態讀取反覆產生。

`Private/Compiler/VeroScriptStaticInitializationDependencies.cpp` 從分離的初始化器低階位元碼準備相依。`PrepareStaticInitializationDependencies` 追蹤選定的呼叫／建構、欄位預設值、複製和區域解構，並以狀態修訂追蹤函式值來源、捕獲與參照寫入。儲存的閉包、保留的配置、可為空的欄位和空容器不能僅因宣告型別而引入執行／解構相依。未知回呼目標不構成完整執行期呼叫圖，分析也不執行原生回呼副作用。

先將消費者運算式編譯至暫存緩衝區，準備相依後再按相符的偵錯位置及全域初始化範圍附加。所需泛型初始化保持在短路分支及欄位緩衝區外。分析檢查取消，遞迴準備前快照描述符，以活動／修訂保護取代執行業務程式碼。保留無關初始化的正常順序，不承諾任意循環相依求解。

維護時檢查欄位預設值呼叫後續靜態宣告、巢狀自身型別、別名、const／參照存取、選定建構、函式值／參照寫入、複製／解構生命週期及無關所屬型別。分別驗證 Core VM、所選 JIT 與 Core AOT；獨立原始碼匯出屬於另一契約。原始碼場景在 `Private/Tests/VeroScriptStaticMemberTests.cpp`、後端差分套件及 C++ Core AOT 套件中；存在這些場景不代表已經執行通過。

## 十進位轉換實作

`Shared/Private/VeroScriptFloatParsing.h` 的 `FromCharsDecimal` 由剖析器與純量文字服務共用。偵測浮點 `std::from_chars` 支援；後備路徑直接轉換至目標寬度，以有效前綴與 sticky 位限制輸入，使用 C 數字地區及最近捨入，並復原呼叫者狀態。非零次正規數可接受；非零輸入捨入成零時回報範圍失敗且不指派輸出。`ParserDetail::ParseFloatLiteral` 個別保留 `f` 常值仍可由 double 表示的下溢例外。中點、負零、長尾數、地區設定及失敗時保留輸出應分別驗證；原始碼情境存在不代表平台驗收通過。

Core 繫結目錄 JSON 與 Tooling 的 LSP／DAP JSON 亦使用這項轉換。`vs_use_shared_private_headers` 提供私有 include 路徑，共用標頭檔不屬於公開 SDK API。

`Shared/Private/VeroScriptJsonString.h` 為繫結目錄、Interop 與原生語言工具提供不依賴 Core 執行階段的 JSON 字串跳脫。附加操作只寫入字串內容，不負責外層引號或 UTF-8 驗證；呼叫端保留既有控制字元跳脫形式，並傳入與輸出緩衝區不重疊的輸入。這是私有建置輸入，必須隨原始碼分發並納入各使用端的實作指紋。

<a id="floating-formatting-support"></a>

`Shared/Private/VeroScriptFloatFormatting.h` 為套件巨集序列化、應用程式 i18n、Tooling JSON、Json 和 GUI 數字控制項提供 `ToCharsDecimal`／`ToCharsGeneral`。存在浮點 `std::to_chars` 多載時使用該多載；相容路徑使用 C 數字地區設定，按原始 float／double 位元寬度選擇可往返的表示，並還原呼叫端的 errno 與捨入狀態。這是私有實作支援，不是公開 SDK API。將其納入 Core／目標 AOT、Json 繫結、GUI 編譯指紋及原始碼發行，並分別驗證相容路徑與標準函式庫 charconv。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/architecture-and-pipeline.md) · [fr-FR](../fr-FR/architecture-and-pipeline.md) · [ja-JP](../ja-JP/architecture-and-pipeline.md) · [ko-KR](../ko-KR/architecture-and-pipeline.md) · [zh-CN](../zh-CN/architecture-and-pipeline.md) · [zh-TW](architecture-and-pipeline.md)
