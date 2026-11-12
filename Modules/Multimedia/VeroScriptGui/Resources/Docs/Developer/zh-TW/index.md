# GUI 維護

`#include "module.vs"` 與 `export import "module.vs";` 使用相同的需求判定，別名本身不載入框架；二進位 include 仍檢查公開巨集。根建置啟用 GUI 時，一般 Core 範例與套件用戶端迴歸使用僅供測試的 `VeroScriptCliCoreTests` 宿主。GUI 整合案例繼續使用實際發行 CLI，單獨驗證巨集與產生器的保守匯入策略，不延長 Core 測試期限。

Gui 負責控制項/屬性/事件、範圍/Schema、ML/CSS 與生命週期。Platform/Graphics/Image/Font 是公開服務，RmlUi/Stb/FontBackend 為獨立提供者。維持模組環境、代次根、所屬執行緒清理、型別/值參數與編譯身分。分別檢查 JIT 派發與明確成本模型回退，僅因不支援而回退不能視為驗收通過。

使用獨立 CMake 或根 VEROSCRIPT_BUILD_GUI；VEROSCRIPT_GUI_SCRIPTING 啟用編譯/腳本整合。VEROSCRIPT_GUI_USE_VISUAL_SDK 重用已安裝基礎庫，VEROSCRIPT_GUI_BUILD_RESOURCE_BACKENDS 控制 Stb/FontBackend。ABI/位元碼改變後以匹配 Core/C++ Runtime 重建。分別驗證原生生命週期、腳本、Schema、ML、安裝後使用及繪製/輸入/字型/平台，靜態檢查不代替執行或 IDE 驗收。

腳本宿主透過 `FEngineConfig::VmBackendFactory` 或 `VmBackendPath` 明確選擇 VM；標記腳本使用 `FMarkupScriptOptions::Engine`。GUI 將宿主選擇傳給執行及編譯處理器，並保留既有設定。官方位元碼 VM 是獨立提供者 `vs-bytecode`；連結或安裝 Gui 不會替宿主選擇後端。原生控制項樹及純 ML/CSS 解析無需 VM。

腳本回歸按獨立場景登記，使用 `ctest --test-dir BUILD -R VeroScriptGuiScriptTests --parallel 4 --output-on-failure` 可涵蓋全部場景。直接執行 `VeroScriptGuiScriptTests` 預設執行全部，也可傳入一個場景名稱。場景拆分保留原有斷言，測試時限不改變應用程式的執行期限。

核心可能因成本、參照或 finally 語意將 getter、轉送方法留在 VM 中。JitSynchronization 分別輸出 NativeLeafExecutions／NativeCfgExecutions、派發、Linear／Bridge／VM trampoline 與回退，並在重用 Session 的連續呼叫中驗證受支援的原生入口與逐次執行計數。ScalarNativeFastPathCalls 只統計宿主純量呼叫，不代表所有指令碼原生執行；巢狀計數不能相加當作獨立呼叫。詳細診斷預設關閉；僅在獨立診斷執行中設定 FJitPolicy.Diagnostics 並讀取 GetLastJitDiagnostics()，吞吐量測量時保持關閉。

`ApplicationOptions.FontBackend` 與 `TextLayoutBackend` 獨立選擇服務。欄位為空時繼承 `FApplicationFactories.DefaultBackend` 或宿主／CLI 選擇（`--font-backend`、`--text-layout-backend`）；明確的 `auto` 選擇候選，`none` 停用選用服務。僅當原生宿主沒有為該服務提供預設選擇時，`FApplication::Create` 才用 `font-backend` 作為常規後備。提供 `FApplicationFactories.Font`／`TextLayout` 或原生 `Fonts`／`TextLayouts` 實例。存活的 RmlUi 上下文共用不可變實例，註冊工廠使用弱快取；更換設定前關閉上下文。`FTextLayout::Create` 首參是選定版面服務，段落快取包含其實例身分。按 GUI `0.1.1`、Font `0.1.2` 和 text-layout `0.1.0` 契約配套重建提供者與消費者；`ContractVersion` 參照 Gui 的 `Version`。靜態審查不代表執行驗收。

## 回歸與生命週期細節

測試涵蓋直接啟用、數值失焦提交、巢狀捲動、Tab 循環及浮層焦點。

應核對實際 JIT 派發／回退統計，分別測量預熱後的更新與繪製，排除編譯和顯示等待時間。

Context.Post 透過核心的程式代次根值保留腳本回呼；執行或捨棄後，其擷取值由核心在安全點清理。Context.SetFocus 保留目前的指令碼呼叫器，以便同步傳送控制項通知。

[User](../../User/zh-TW/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/zh-TW/GENERAL.md)

## 原始碼匯入策略

一般 CLI 設定保留原生繫結，依原始碼需求載入指令碼框架。GUI 名稱、註解及產生的 ML 請求框架，註解文字與字串本身不觸發載入；以 .vs 結尾的原始碼匯入使用相同偵測。較小的本機 .vslib 匯入透過已記錄依賴檢查公開巨集，一般宣告及可直接判定的無參數常值常數巨集不請求框架。可直接判定為數值、字元或字串常值常數的無參數巨集與一般條件指示詞本身不請求框架；帶參數、替換內容含名稱的原始碼／主程式／匯入函式庫公開巨集，以及權杖串接、字串化、未知指示詞，主程式巨集搭配不透明隱含提供者，無法解析／含跳脫／邏輯／較大匯入及自訂編譯處理器仍保守保留完整匯入。明確的 gui-* 工作及 ConfigureGuiScriptEngine 仍使用隱含匯入。維護時同步編譯中繼資料和語意快取身分；回歸須涵蓋無 GUI 原始碼及經匯入或處理器引入 GUI 的原始碼。

固定屬性同步在回呼前記錄輸入欄位版本，僅在回呼成功且登記未被替換時提交。求值期間的變化在下一輪仍可觀察，失敗與重新登記不會提交過期快照。輸入緩衝區重複使用，僅在切換來源時清空曾保留的預設設定。動態 getter 仍在每個同步輪次執行。

`BakeGuiApplication` 保持為供嵌入使用的內部 Core 映像介面。CLI 使用 `Application::BuildApplication` 和 `WriteArchive` 封裝這些位元組，不再直接將映像寫為 `.vsapp`。應用程式準備掛鉤在套件提供者可用後還原 GUI 資源服務，通用 CLI 與啟用 GUI 的 Runtime 共用此流程。驗收同時涵蓋 ZIP 結構、inspect／檢查與移除原始碼後的搬移執行。未啟用 Application 時仍可檢查／預覽 GUI 原始碼，編譯或載入應用程式容器會明確回報缺少能力。

原始碼使用 `import compiler` 產生器時，Core 的編譯擴充期限也涵蓋所在 GUI 原始碼圖的準備過程。較大的原始碼圖應由宿主設定適當預算，例如 CLI 的 `--compiler-timeout 20000`，或原生宿主的 `FCompileOptions::ScriptCompileExtensions.TimeoutMilliseconds`。

輸入身分和版本已驗證未變時，重用回呼前的欄位快照，避免再次掃描。設定未變時可重用繫結來源分類，不能跨同步輪次重用 getter 結果。原生回呼中的確切數值框架控制項可在 getter 之外，使用剛完成同步的屬性設定原生狀態；手動同步、衍生控制項和遞迴查詢繼續走一般 getter 路徑。繼承屬性仍逐輪求值父層 getter，未變且已驗證的值省去複製和重複驗證。 原生屬性派發依活動遮罩分組檢查，維持 getter 順序並涵蓋全部已啟用的繫結。

原生數值、布林和字串屬性在同步與查詢時使用直接值快照與參考輸出，避免暫時的 optional 物件。空結果維持後備行為，並在下一求值輪次繼續讀取。在已驗證的原生作用域內，確切的框架數值控制項可省去產生的空變更鉤子；衍生控制項和已宣告鉤子繼續進行虛擬呼叫。

固定的原生 attribute 設定不再要求逐輪輪詢腳本。設定替換、原生狀態修改和重新掛載仍會喚醒同步；綁定 getter 與可空 getter 持續輪詢，空結果也不會停止求值。

各控制項獨佔原生快取參考物件，內部快照更新維持輸入設定追蹤穩定；setter 替換設定時仍會喚醒同步。

確切的基礎 Widget 型別也可暫停同步，未帶註解的衍生型別繼續保留保守的自訂同步路徑。

attribute 設定的分類只快取到 Set／Reset 取代該設定為止。分類不會求值 getter；繫結與可空 getter 仍在每個必要求值世代執行。

取代時保留舊負載，直到新設定及其分類完整發佈；解構重入可讀取或再次取代新狀態，之後不會被外層舊狀態覆蓋。

## 通知式繫結與批次更新

原生來源圖使用弱控制項訂閱並合併通知。控制項持有訂閱權杖與一般 attribute 讀取器，權杖解構不須指令碼終結回呼。Bridge 單調計數器分配修訂號，固定相依集合的最大修訂可精確判斷變更，不需雜湊或每次建立指令碼陣列。共用 Binding 僅快取成功讀取（含 optional 無值），原有動態 attribute 的 epoch 行為不變。

IFrameBindingService 在全部 BeginFrame 後新增發布階段，不變更既有 `IFrameService` 虛介面。發布對控制項去重並喚醒既有生命週期觀察者，不逐屬性呼叫指令碼工作或強制版面失效。既有同步比較屬性結果、維持生命週期並重試失敗 Getter。指令碼觀察者亦支援宿主直接更新。原生批次深度防止宿主提交中間狀態，指令碼包裝層會提早拒絕更新。普通 setter 與 ClearX 解除訂閱；內部安裝在釋放舊擷取前發布替換設定。 FromAttribute 保留回退層輪詢，讓預設 attribute 與原生樣式可獨立變動；傳入 Getter 的結果仍依通知修訂快取。

## 同步輸出與繪製提交

原生同步僅重用已解析的屬性輸出；Getter、繼承與變更鉤子仍先執行。數值變更、動畫取樣、停止或結束均更新該輸出。通知發布保留有效設定快照。每個 Widget 持有一個穩定的快取容器，各原生屬性快取在首次使用時建立；這些輸出寫入不會使輸入設定快照失效。一般動態 attribute 與 FromAttribute 回退層保留原有求值及重試規則。

RmlUi 透過可選 IRenderSubmission2D 集中提交同一渲染器的連續通道。其他渲染器的合成器讀取前，先提交生產通道，再繼續記錄；失敗或例外會取消未結束的範圍。表面暫無交換鏈影像時，可取消尚未提交且僅寫內部暫存層的工作；持久圖層擷取仍須完成。SDL_GPU 及其模組提供者支援此能力，其他渲染器維持一般路徑。每次 Render 仍執行 OnPaint。

腳本快取失效標記先於原生通知推進，避免通知生效後的執行中斷讓共用結果被誤判為有效；原生相依修訂仍用於判斷實際變更。這個順序不代表腳本中斷具有交易回復語義。

Observable.Set 在 finally 中發布通知，涵蓋儲存值寫入後的執行中斷。失敗或中斷的賦值可能保守地使繫結快取失效；同值設定仍不發出通知。後續恢復仍遵守下述 Core RequiresReset 契約。

求值、初始化和建構的暫時標記在保護作用域內修改；巢狀求值與批次更新恢復進入前的層級。取消訂閱回呼失敗時保留重試能力，並阻止回呼遞迴取消自身。超限、逾時或取消後的恢復仍依賴 Core 的有界清理；若 Core 要求重設，宿主應重新建立受影響的 GUI 腳本工作階段及其繫結內容；GUI 的 finally 不提供業務狀態的交易回復。

原生屬性依原有順序分為九組同步，保留動態求值與例外恢復。控制項弱回呼進入腳本後再次檢查釋放狀態；目標已釋放時結束分派，避免重複進入主程式橋接。

ML 初始化捕獲的右側按外層作用域改寫，捕獲別名只在閉包本體內生效。屬性來源快照透過 Core 的解包介面讀取引用或唯讀捕獲儲存中的實際屬性物件，再比較身分與修訂；一般物件欄位維持直接讀取，不額外保留捕獲負載。回呼仍使用 FValueRoot 保留並透過 InvokeCallable 呼叫，業務介面不變。

原生純量屬性同步僅在未取得值時補查設定，避免有效值路徑上的重複查詢。快取與 getter 介面保持原樣。依序執行的同步群組重用純量狀態變數，屬性值範圍保持獨立。共用 Binding 先檢查遞迴狀態，再重用修訂快取；快取命中時省去額外的重新整理方法呼叫。

原生元素查找與樣式存在性查詢明確登記唯讀成員中繼資料，確保 Core 可透過 const 宿主控制代碼與按值捕獲呼叫這些查詢。修改操作保留原有中繼資料，GUI 業務介面不變。

## 原生產物維護

原生選項、公開配置或內嵌程式庫變動後，須一併重建 GUI 提供者、宿主、腳本程式庫及 SDK／套件候選。相同產品版本號不能證明實作相容。

## 後端預設值與內部分派

實作輔助型別與指令碼分派入口使用 `VS::Gui::Internal`，例如 `NativeAttributeCache<T>` 和 `NativeValueAttributeCache<T>`；這些屬於框架內部實作，不作為應用程式 API。控制項中以 `Vsgui` 開頭的成員仍是框架產生及繼承掛鉤的保留名稱，用於避免與應用程式成員衝突。

原始碼匯入偵測區分命名空間／註記使用與僅命名為 `Gui` 的一般變數、欄位、參數；修改偵測時同步處理器設定身分。原生繫結、產生的呼叫點及兩個編輯器擷取器須與 `VS::Gui::Internal` 限定名一致。`FContext` 在幾何變化後記錄 `bLayoutNotificationPending`，先發布完整幾何再回呼，僅在通知成功後清除旗標。失敗或尚未派發的通知保留待重試，成功通知無新變化時不重複。掛載掛鉤修改在版面配置前同步，版面配置掛鉤修改在下一次 `Update` 同步；輸入與繪製仍要求更新成功。分別驗證失敗／重試、同層通知派發及回呼可讀幾何。

## 同步實作

靜態快速路徑要求執行期型別完全相符、初始化完成且無自訂指令碼同步。未知衍生型別保留保守路徑。固定純量設定可暫停輪詢；設定替換、Setter、樣式變化、宿主修改及安裝繫結會恢復輪詢。繫結與 optional Getter 持續求值，包括空結果；弱觀察不能強持有元件。

產生的純量屬性保留未裝箱的區域、樣式及輸出快照。變更令牌僅在回呼成功後提交，失敗和重新登錄不得提交舊輸入。原生變更令牌減少未變狀態的重複查詢，但不能跳過動態 Getter 與自訂編解碼器。樣式保留解析成功的內建字面值，變數展開、資源解析、選擇器狀態和主題繼續動態更新。公共 Builder 方法隨型別產生，減少宣告操作而不改變語法。

原生同步期間，目前掛載元件的重複檢查共用回呼入口已驗證的作用域。成功與失敗後均須還原；其他元件和回呼外查詢保留原生檢查。維持屬性求值、optional 後備、穩定化及生命週期順序。

## Context 後備與 Runtime 選項路由

`Private/Bindings/VeroScriptGuiRuntimeBindings.cpp` 的預設 `__NativeContext` 建構橋在參數未指定時先查 `FScriptServices.DefaultBackend("gui")`，再查已解析的 `Application.DefaultBackend("gui")`。呼叫 `Factories.Gui` 前重新檢查空結果，僅此時使用 `rmlui`；保留明確 ID／auto／none。`FScriptServices.CreateContext` 繞過此應用工廠分支，仍由宿主負責。`VeroScriptRuntimeMain.cpp` 將 VM／JIT 選擇保留在執行設定和套件 `SelectedBackends` 中，傳入 GUI 命令適配器的值須排除服務鍵 `vm`、`jit`。分別檢查無回呼、空回呼結果、環境優先序、明確選擇，以及 GUI 容器攜帶 VM／JIT 選項的檢查／執行。

## 場景重用與同步邊界

樣式同步會重用未變更的場景結構和選擇器比對結果；一般幾何、文字與動畫輸出不會單獨使選擇器快取失效。資源解析、樣式值解碼和動態 Getter 仍依同步規則執行。只有經生成程式碼確認不需週期工作的控制項才停止輪詢；動態繫結或未知同步行為維持原有路徑。

`FMarkupRuntime::MatchesScene` 比較主題、控制項、父級、名稱範圍、所屬關係與樣式表後重用 `FContext::SceneStyles`。選擇器結果追蹤資格、焦點／模態／懸停／按下狀態及樣式輸入；動態資格 getter 禁止重用。提交套用前的輸入，使樣式輸出能使下一穩定化輪次失效；替換根或銷毀時清除場景狀態。`CanSuspendSynchronization` 對未知方法／建構、其他處理器產生的工作、自訂 codec 及缺少輔助方法的舊套件基底維持保守。私有產生名稱保留模組的正規連結身分。

## GUI 編譯設定

GUI 的 `CompilerOptions` 向腳本編譯擴充傳遞所選 VM；僅在沒有明確 VM 且 `bClearVmBackend` 為 false 時使用 `FallbackVmBackendFactory`／`FallbackVmBackendPath`，僅在擴充選項未提供時繼承 `ExecutionControl`。明確停用與呼叫者獨立控制維持優先。

<a id="gui-text-layout-maintenance"></a>

## 文字版面使用端約束

段落快取身分除邏輯文字及方向外，還須包含所選服務實例與不可變設定；僅後端 ID 不足以區分。樣式片段之間保留完整邏輯段落，先確定換行，再查詢該行的雙向排列；透過解析後的服務保留模組租約。繪製時保留原始 advance 與 offset，只在每個可見叢集的最後一個字形後增加一次字元間距，使繪製與行寬、游標位置一致。保留有限負字元間距，並將最終行寬限制為非負。剪貼簿內容與位移保持邏輯順序。公開整形、叢集及能力限制以 [Font 契約](../../../../../VeroScriptFont/Resources/Docs/User/zh-TW/index.md)為正本。

<!-- docs-navigation -->

---

[開發文件](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
