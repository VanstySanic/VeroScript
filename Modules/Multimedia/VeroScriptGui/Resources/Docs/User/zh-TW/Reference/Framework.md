# GUI 框架契約

## 範圍

原生 API 使用 VeroScript::Gui，指令碼使用 VS::Gui。框架提供元件樹、宣告私有 id 範圍、生命週期、型別化屬性、Attribute、事件、Context/Application、主題、展示動畫與 i18n 畫面更新快照。已提供基礎、集合和導覽控制項，以及模態對話框。選用編輯器配套由獨立指南說明。

## 編譯與建構

宿主先在目錄登記 RegisterScriptBindings，再於載入前呼叫 ConfigureGuiScriptEngine。已發布模組環境的權威目錄亦須包含相同 GUI 繫結。變更設定時複製環境，不原地修改已發布內容。

宿主若透過多次獨立的 `VeroScriptSession::CallFunction` 保留 GUI 全域狀態，應在初始化前設定 `session.SetHotReloadEnabled(true)`。否則每次入口會建立新的模組全域狀態。同一指令碼入口內執行 `Application.Run/Tick` 已共享該入口狀態。

支援 GUI 的 CLI 會在一般編譯或 `--check` 需要 GUI 功能時自動設定指令碼框架相依項，現有 GUI 程式不需新增開關。明確的 `gui-*` 工作和原生 `ConfigureGuiScriptEngine` 仍隱含匯入框架。

一般變數、欄位或參數僅命名為 `Gui` 不會匯入框架。命名空間參照、GUI 註解，以及巨集和無法檢查的匯入仍依既有規則處理。

@GuiWidget、@GuiProperty、@GuiMethod、@GuiEvent 共用可選的 Mark。GuiWidget 亦接受 Id，GuiProperty 的 Style 旗標允許 CSS 設定。註解名稱與選項不區分大小寫；值與 ML/CSS 名稱保持區分。原名與 Mark 都可使用。一般屬性只能設定值，只有 std::attribute<T> 能安裝 Getter 繫結。

此 ASCII 不區分大小寫規則適用於 GUI 註記及其參數。Core 預設仍區分註記名稱大小寫，GUI 處理器識別選項名稱時忽略 ASCII 大小寫。反射的 `find_arguments(name)` 始終按儲存的參數拼寫精確查詢，GUI 註記也不例外。

Type::Create().Property(value)[{ children }] 建立的 Builder 可直接隱含轉換為元件，不需要最後的 Build。完成設定後再進行完整初始化。建構函式準備內部狀態，屬性變更掛鉤同步有效值至內部元素。GetX 讀取基礎值，動畫另外產生展示值。

初始化依子到父執行且每個實例成功一次；掛載依父到子，卸載依子到父。掛載掛鉤的狀態修改在配置前同步，配置掛鉤的修改於下次 `Update` 同步。配置通知失敗時，失敗及尚未執行的通知會於後續更新重試；配置未變時不重複已成功的通知。配置回呼可讀取完整的目前幾何，輸入及繪製則需要更新成功。

## ML 與 CSS

.vswml 可使用任意元件作為頁面根，或用帶 template 的 component 定義型別。component 的 type/base/mark 產生真正 VS 型別，可與 VS/ML 交叉繼承及組合。一般頁面有獨立的私有狀態。import/script/style 由 Gui 處理，不直接等同 RML/RCSS。

一般屬性是固定值；bind:gap="{ Count }" 安裝型別化 Getter；on:pointer-down="Increase" 或大括號陳述式區塊安裝事件。bind/on 是內建前綴，不需要 xmlns。內嵌指令碼使用頁面或元件本身，外部指令碼使用宣告的業務物件。可存取的外部成員允許原名或 context 修飾，區域變數保持詞彙範圍。運算式是 VS，不替一般欄位建立隱藏變更監視。

自訂事件依精確參數、傳回型別與 delegate/multicast_delegate 種類處理。原生輸入使用擷取、目標、冒泡路由。停止傳播、立即停止與取消預設動作是三種不同操作。ML 回呼預設使用弱參照，明確使用 Core 強繫結時保留原有所有權。

每項宣告有私有 id 範圍，使用處加入的子元素不會變成模板私有部件。elements.Require("part") 在宣告範圍查詢。相對資源依所屬 ML/CSS/VS 模組解析，不依行程目前目錄。 `elements.Find("part")` 找不到元件時回傳 `nullptr`；`elements.Require("part")` 則斷言查詢成功。

VSWCSS 支援型別/Mark、*、class/id、複合、後代/子代、狀態與受限制的 var。未支援語法會產生診斷。優先順序為型別預設、主題、本地樣式、本地設定/繫結。Setter 替換 Getter，ClearX 不會恢復以前的 Getter。Style 不自動賦予自訂型別動畫映射。

## 執行與發行

Context 用於嵌入宿主，Application 組合 Platform/Graphics/Gui；同一事件來源只有一個泵送者。MakeRegisteredServiceFactories 使用契約相符的原生提供者並保持模組所有權。介面不關閉借用的裝置與繪製目標。 GUI 提供者回傳 `IBackendService`。

Application 將提供者的清理例外轉換為錯誤，並保留未完成釋放的資源與事件泵使用權。顯式 Close 失敗後可重試；最終參照釋放時的清理失敗，透過所屬執行緒的 CollectRetired 重試。

SetLocalization 連接 Core i18n context；GetLocalizationSnapshot 回傳畫面更新開始時固定的快照，語言等變更下一輪生效。型別化訊息與參數由 Core 保存，Gui 不另行實作 ICU。

`BakeGuiApplication` 產生保留 GUI 中繼資料的內部 Core 應用映像；直接使用映像的原生主機採用 `ReadPackage`、`RegisterGuiArtifactBindings` 與 Session 的 `LoadPackage`。`gui-compile` 命令再透過 Application 服務將映像包裝為標準 ZIP `.vsapp` 容器，由 `FApplication`／Session 的 `LoadApplication` 載入。容器編譯與載入需要 Application 支援；原始碼檢查與預覽不依賴此容器服務。

`CompileGuiSource`／`CompileGuiFile` 使用完整編譯器，不執行使用者初始化器、不建立視窗或裝置。原生主機直接載入映像時，在 Session 的 `LoadPackage` 前呼叫 `RegisterGuiArtifactBindings`。資源清單保留移除原始碼／偵錯資訊後的呼叫者相對查詢；已烘焙的指令碼與樣式不需原始檔，外部資產仍需提供者。

相符且啟用 GUI 的 Runtime 提供 `gui-check INPUT`、`gui-compile INPUT --output app.vsapp` 和 `gui-preview INPUT [--frames N] [-- SCRIPT_ARGS]`。檢查與編譯不執行初始化，也不建立 UI 服務。`--frames N` 僅限制自動產生的 ML 預覽迴圈；VS 入口自行控制執行期限，會拒絕此選項。

模組參數 `--platform-backend`、`--graphics-backend`、`--gui-backend` 位於命令或指令碼前，亦適用於一般 VS 應用程式。指令碼明確設定優先於本次呼叫／環境設定；空值表示繼承，`auto` 由工廠選擇可用提供者，`none` 停用服務。停用視窗、GUI 或啟用繪製時所需的圖形服務會報錯。原生宿主未為該服務提供預設選擇時，仍以 sdl3／sdl3／rmlui 作為一般回退。執行後端 `--backend` 繼續用於 VM／JIT。

CreateCommandLineService 供宿主使用同一任務。借用命令描述期間保留 Owner。SetEngineOptions/SetRuntimeServices/RegisterBindings/ConfigureEngine/UseArtifact 共用此次設定。GuiToolsModule 只轉送宿主服務，不重複連結 Core；宿主已登記時直接共用。

原生 `FMarkupProgram` API 仍可使用原生型別設定與選用的 `IMarkupScriptContext`。VS 型別匯出、Builder 生成及自訂屬性／事件應使用完整編譯器路徑。請參閱[框架範例](../../../../Examples/Framework/main.vs)。Windows/Linux 驗證不保證 macOS、Android、iOS 或瀏覽器執行。

宿主可透過 `FGuiCompilationConfiguration` 宣告必須提供的業務上下文及值 codec。字面值／樣式解析器是可見的普通 VS 函式，接收 `string` 並回傳宣告的值型別；不可變組態參與編譯身分。

共用原始碼範例：[main.vs](../../../../Examples/Framework/main.vs)、[page.vswml](../../../../Examples/Framework/page.vswml)、[page.vswcss](../../../../Examples/Framework/page.vswcss)。check/preview 亦接受已編譯的 GUI .vsapp。

## 文字、字型、圖片與繪製

文字使用 FreeType 2.13.3 與 HarfBuzz 10.4.0，內嵌未修改的 Lato Latin 字型，名稱為 `Vero Sans`。其他文字需由應用透過 Context.LoadFont 載入適合的字型及回退字型。六參數版本提供字重、斜體、回退和集合索引。

Text 是 UTF-8 字面文字，可使用字串 attribute 綁定；不解析為 RML，不支援 ML 正文或混合行內標記。字型、文字樣式、方向和語言預設繼承父級。Direction 指定段落基礎方向 auto/ltr/rtl；auto 使用每段首個強方向字元，沒有時使用 LTR。Language 只影響塑形，不覆蓋 auto。RmlUi 先解析 Unicode 雙向執行段，再塑形並逐顯示行重排；文字仍保存邏輯 UTF-8 順序，left/right 對齊仍表示實體方向。

<a id="image-resources"></a>

`Image` 載入內容影像。`BackgroundImage` 接受 `url(...)` 及線性、放射、圓錐漸層和重複漸層。相對影像從其 VS／ML／CSS 來源檔解析，延遲屬性 getter 亦保留此來源。原生主程式設定 `FResourceReader`，並可選擇設定 `FImageDecoder`；解碼像素使用非預乘 sRGB RGBA8。支援格式取決於所選 [Image 服務](../../../../../../VeroScriptImage/Resources/Docs/User/zh-TW/index.md)，`image/stb` 的格式請見 [Stb](../../../../../../Backend/VeroScriptStb/Resources/Docs/User/zh-TW/index.md)。SVG 與動畫需要適用的主程式解碼器／播放器。

預設內容配置依序放置內建圖片、文字及子控制項；修改屬性或重建控制項樹皆保持此順序。

Image、BackgroundImage、MaskImage 的 Getter 對直接值及綁定值統一回傳已解析的資源識別／URL，轉交其他模組時不會改變資源來源。

<a id="compositor-properties"></a>

`CornerRadius`、`ClipContent`、`Transform`、`Filter`、`BackdropFilter`、`MaskImage` 和 `BoxShadow` 使用同一屬性、繫結及樣式路徑。支援的合成操作與資源限制由所選後端決定；RmlUi 提供者請見 [RmlUi 合成與資源](../../../../../../Backend/VeroScriptRmlUi/Resources/Docs/User/zh-TW/index.md#composition-resources)。

OnPaint(PaintContext canvas) 在背景之後、子控件之前，以內容區座標繪製。VS 提供 FillRect、DrawLine、DrawImage、PushClip/PopClip、GetWidth/GetHeight；原生 `FPaintContext` 另有網格與變換。裁剪座標固定，變換影響後續幾何。畫布限回調內有效，繪製時不可修改 GUI，裁剪須配對。

BakeGuiApplication 可使用 FGuiAssetOptions；gui-compile 的 --asset 可重複指定圖片／字型。清單以工作目錄解析，內部引用仍相對模組。SDK 包含第三方字型與解碼授權聲明。GUI 契約 0.1.1，產品版本 0.1.1。

```cpp
@GuiWidget
export class CanvasView : public VS::Gui::Widget
{
protected:
	void OnPaint(VS::Gui::PaintContext canvas) override
	{
		canvas.FillRect(8, 8, 80, 24, VS::Gui::Color(0, 0.4, 1, 1));
		canvas.DrawLine(8, 40, 88, 40, 2, VS::Gui::Color(1, 1, 1, 1));
	}
};
// String attribute getters may call std::i18n using the frame's localization snapshot.
VS::Gui::Widget title = VS::Gui::Widget::Create().Text("Hello, VeroScript!").FontSize(24);
```

```xml
<Widget>
    <style>
        .title { font-size: 24px; color: #ffffff; }
        .surface { border-radius: 12px; clip-content: true;
                   background-image: linear-gradient(90deg, #173b70, #438bd0); }
    </style>
    <Widget class="title" Text="Hello, VeroScript!" />
    <Widget class="surface" Width="240px" Height="80px" />
    <Widget Image="assets/logo.png" />
</Widget>
```

<a id="native-resource-services"></a>

VeroScriptImage 與 VeroScriptFont 定義通用資源介面。VeroScriptStb 提供 `image/stb`；VeroScriptFontBackend 提供 `font/font-backend` 及獨立的 `text-layout/font-backend` 服務。原生 GUI 主程式透過 `FBackendOptions.Images` / `Fonts` / `TextLayouts` 注入圖片與字型。請選用支援所選 Font 與文字版面契約的主程式和配套 SDK，並安裝所需服務、後端套件及授權文件。

`ApplicationOptions.FontBackend` 與 `TextLayoutBackend` 分別選擇服務。空值繼承宿主／CLI 的選擇（`--font-backend`、`--text-layout-backend`）；`auto` 由工廠選擇提供者，`none` 停用該選用服務。原生宿主未為該服務提供預設選擇時，以 `font-backend` 作為一般回退。 原生主機提供 `FApplicationFactories.Font`／`TextLayout` 或 `Fonts`／`TextLayouts` 實體。使用與 GUI `0.1.1`、Font `0.1.2`、text-layout `0.1.0` 相容的提供者；上下文存續期間保留字型／版面服務，替換共用設定前關閉所有上下文。`FTextLayout::Create` 的首參傳入所選版面服務。

## 基礎控制項

ProgressBar、Slider 和 ScrollBar 在計算 GetNormalizedValue 前，先將 GetActualValue 限制到最新設定的邊界；範圍變更尚未同步時也遵循此規則。無效範圍仍於同步時拒絕。

`VS::Gui` 提供 Text、Image、Border、Spacer、Separator、Stack、Overlay、Button、CheckBox、ToggleSwitch、ProgressBar、Slider、ScrollBar、ScrollView、TextBox、TextArea。全部保留子控制項並共用 Builder、Attribute、事件和 ML／CSS。Border 使用 BorderWidth／BorderColor；編輯文字使用 Value。

Clicked、CheckedChanged、ValueChanged、Scrolled、TextChanged／Submitted 回報使用者操作。未繫結屬性接受變更；已繫結屬性保留 Getter，事件提供候選值。Setter 與一般重新整理不發送這些事件；IsXBound() 查詢繫結。繼承事件保留 CheckBox／TextBox 的 sender 型別。Minimum／Maximum／Step 歸一數值，Step=0 為連續；GetActualValue 讀取實際值。

支援滑鼠、觸控、Space／Enter、Tab／Shift+Tab、方向鍵、Home／End、PageUp／PageDown；PreventDefault 阻止預設行為。樣式增加 :checked／:read-only。ScrollView 支援滾輪、觸控及鍵盤；ScrollX/Y 為請求偏移，GetActualScrollX/Y、GetScrollWidth/Height 為最近布局結果。

編輯支援邏輯碼點選區、剪貼簿、撤銷／重做、唯讀、預留文字與 IME。Select 終點不含在選區；邏輯選區可對應多個視覺區段。箭頭、指標命中與 IME 光標共用顯示行布局，Backspace/Delete 保留邏輯刪除順序；復原、焦點切換及重建保留活動端點與雙向邊界光標歸屬。文字上限 1 MiB UTF-8，每個歷史最多 128 份／8 MiB。Application 接入平台，嵌入宿主提供 FTextInputServices；剪貼簿錯誤回復編輯。實體 IME、可存取性、完整字素導覽及其他平台仍須驗證或實作。GUI 契約 0.1.1 須一起重建，產品 0.1.1。範例：[ML](../../../../Examples/Controls/main.vs)、[頁面](../../../../Examples/Controls/page.vswml)、[樣式](../../../../Examples/Controls/page.vswcss)、[程式碼](../../../../Examples/Controls/code.vs)。

Checked、Value、範圍／步長、ReadOnly 及捲動偏移由程式碼或 ML 設定，樣式僅設定外觀與方向。TextBox 移除未繫結單行文字的換行／定位字元，GetActualValue 讀取實際文字。

步長無法整除區間時，最小值和最大值仍可到達。滑桿／捲軸的觸控拖曳優先於祖先捲動容器。Select 使用已提交文字的碼點索引；未聚焦時保留選取範圍，聚焦後還原，有效選取會結束預編輯。程式替換文字或切換編輯器種類會清空復原／重做記錄，重建控制項樹不改變這些規則。完整控制項程式庫增加原始碼編譯成本；發行可使用 gui-compile 烘焙，避免啟動時編譯原始碼。

ScrollWidth/Height 表示不含 Padding 的內容範圍，減去內容視口尺寸後與後端實際捲動範圍一致。宿主驅動腳本 GUI 時，每幀進入一次腳本工作階段，在該入口中呼叫 Context.Update 或 Application.Tick/Run。FScriptServices.Invoke 是逐一宿主回呼的後備介面，會增加工作階段進入成本。純原生 Context 不經過此腳本橋接。

捲動請求超出目前內容範圍時，滾輪、鍵盤和觸控操作從限制後的偏移開始計算；下一次配置前的連續事件仍會累積。移除正在拖選文字的滑鼠或觸控裝置會結束該拖選，移除其他裝置不會取消目前按壓。 內層容器無法繼續捲動時，滾輪交給可捲動的祖先；零增量不產生修改事件。移除裝置時，即使宿主文字輸入服務回報錯誤或擲出例外，也會釋放該裝置的捕獲並回報錯誤。

## 新增佈局、輸入與浮層

Grid 使用等寬列，Columns 為 int64（1–4096），Gap 控制行列間距；行高依最高子項，Collapsed 不佔格。子項寬度由 Grid 決定，不提供跨格或 CSS Grid 軌道。Wrap 預設橫向換行；縱向換列使用 Layout=Vertical 並限制 Height。Splitter 最多兩個普通子項（浮層不計），Value 為扣除 DividerSize 後第一區域比例。Minimum/Maximum 必須在 [0,1]，Step 預設 0.01，DividerSize 預設 6；Vertical 切換上下分割。拖曳與鍵盤發出 ValueChanged(Splitter,double)，ReadOnly 禁止修改。

RadioGroup.SelectedKey 管理最近組內非空且唯一的 RadioButton.Key；巢狀組獨立。未匹配時不選取。RadioButton 提供 IsChecked/Select/Focus 與 Selected(RadioButton)；組發出 SelectionChanged(RadioGroup,string)。方向鍵循環選擇，Space/Enter 與指標可啟用。綁定保留 Getter 並回報候選值，程式設定不發出使用者事件。

NumberBox 使用 double Value/Minimum/Maximum/Step（預設 0/0/100/1），提供 ReadOnly、Placeholder、Focus、Select、GetActualValue、ValueChanged/Submitted(NumberBox,double)。未完成文字保留到 Enter 或移轉焦點，再檢查有限十進位／指數、範圍與 Step；無效時恢復有效值。Up/Down 按 Step（0 時為 1），Escape 放棄草稿。使用不受地區影響的小數點；本地化展示另用核心格式化。PasswordBox 繼承 TextBox，保留選取、IME 與事件簽名，僅遮罩顯示，Getter 仍回傳實際文字。

Popup 為非模態浮層，錨定邏輯父項，脫離一般佈局與祖先裁剪並顯示於其上，保留邏輯事件路由。OffsetX/Y 調整，視口限制位置，必要時翻到上方；尺寸與外觀沿用樣式。Open 預設 false；Escape 或外部按下（DismissOnOutside=true）發出 OpenChanged(Popup,bool)，外部按下被消耗。綁定 Open 保留來源，焦點顯式設定；關閉可恢復先前可用焦點。IME 優先處理 Escape。ToolTip 整棵子樹不可互動；AutoOpen=true 時依父項懸停與 Delay（0.5 秒）開關，離開或重設滑鼠即隱藏；false 時使用 Open。IsActuallyOpen 讀最近狀態，Modal=true 啟用模態焦點限制，Dialog 預設使用此設定。

Columns、DividerSize、Vertical、OffsetX/Y、Delay 可寫入 CSS，其餘資料及綁定政策由程式／ML 設定。所有類型共用既有建構器、屬性與事件鏈路。GUI 契約 **0.1.1** 需一起重建，後端宣告 bExtendedControls；產品仍 0.1.1。[入口](../../../../Examples/AdditionalControls/main.vs)、[ML](../../../../Examples/AdditionalControls/page.vswml)、[CSS](../../../../Examples/AdditionalControls/page.vswcss)。

Image／Text 自帶內容也依等寬格子排在子控件前。Splitter 預設高度為 120 個介面單位。

容器小於 DividerSize 時，分隔條限制在可用分割範圍內。RadioGroup 回呼能讀取完整同步的選取狀態；回呼改選其他項目時，不會發出原請求的過期 Selected 事件。NumberBox 在視窗失焦或輸入狀態重設時也會提交草稿。提交和宿主輸入清理同時失敗時，保留原始提交錯誤；宿主恢復後由後續 Update 重試清理。數值最多接受一個前導正負號。

更換 FTextInputServices 時，若停止舊服務失敗，會保留舊服務與邏輯焦點以供重試。數值提交回呼可能關閉上下文或移除目標，因此焦點切換會在回呼後重新檢查上下文及目標的有效性。

## 焦點可見性與回呼錯誤

完成佈局後，Tab／Shift+Tab 和 Focus() 會依最近佈局，以最小位移將目標捲入祖先 ScrollView 的可見區；巢狀容器由內向外處理，超大控制項不會在兩側反覆跳動。浮層不會捲動其邏輯祖先。僅在偏移需要變更時發送 Scrolled；繫結的捲動屬性保留 Getter，由業務透過事件接受候選偏移。幾何於下一次 Update 生效。滑鼠／觸控按下造成的焦點不會捲動容器，避免目標在指標下移走。

直接 Activate／Focus 與輸入派發使用相同的回呼例外處理：標準及其他例外皆轉為 gui.callback_exception 錯誤，不越過回傳錯誤的介面。回呼前已提交的數值與業務副作用不會回復；呼叫者應處理錯誤後再重試。

框架保存滑鼠最近的視窗座標，在佈局完成後重新命中；懸停樣式和自動 ToolTip 於後續 Update 刷新，不合成指標輸入事件。取消、裝置移除、輸入重設、失焦及關閉會清理對應記錄，避免滑鼠靜止時仍懸停於已移走的控制項。

## 滑鼠游標與擷取

Cursor 是可繼承、支援樣式的 VS::Gui::Cursor 屬性；鏈式建構與 SetCursor 接受值或 std::attribute<VS::Gui::Cursor>，ML 使用 Cursor="pointer"，CSS 使用 cursor: pointer。ClearCursor() 恢復繼承／樣式，顯式 Auto 恢復控制項的預設行為。

Auto 為按鈕、核取方塊、切換、單選、滑桿與捲軸使用手形，為文字／數值／密碼輸入使用文字游標，僅在 Splitter 分隔線使用對應方向的調整游標。普通區域與停用控制項預設為箭頭；可用 :disabled { cursor: not-allowed; } 覆寫。內容沿用互動祖先的自動行為，浮層不沿用錨點按鈕的自動游標。

值為 Auto、Default、Text、Wait、Crosshair、Progress、ResizeNWSE、ResizeNESW、ResizeEW、ResizeNS、Move、NotAllowed、Pointer、None；CSS 對應 auto、default、text、wait、crosshair、progress、nwse-resize、nesw-resize、ew-resize、ns-resize、move、not-allowed、pointer、none。None 隱藏游標，Showcase 圖表展示十字游標。

Application 依 Platform 能力連接系統游標與視窗滑鼠擷取。嵌入宿主使用 `FContext::SetPointerServices(FPointerServices, Error)`；回呼在所屬執行緒執行，不可修改 GUI。沒有可選服務仍能排版與路由。游標快取避免重複提交，收到滑鼠輸入才啟用；觸控不要求滑鼠擷取。

拖曳離開視窗仍維持游標與擷取；放開、取消、裝置移除、失焦、重設或關閉會清理狀態。排版與樣式變動會刷新靜止游標。MouseEntered／MouseLeft 是視窗通知，GUI 根事件為 mouse-entered／mouse-left；離開清除懸停與 ToolTip，不取消拖曳或鍵盤焦點，外部宿主也須轉送。

服務錯誤與例外轉成錯誤結果。取得擷取失敗會回復邏輯狀態；游標失敗可由 Update 重試，更換服務與 Close 保留未完成的清理。解除服務恢復可見預設游標並釋放所持擷取。GUI 契約 **0.1.1**，產品仍為 **0.1.1**；提供者與使用者一起重建。

擷取同步先於游標求值執行，游標出錯不會阻止釋放系統滑鼠擷取。宿主只提供 SetMouseCapture 時，該服務不會額外求值 Cursor。公開指標操作會保持內容存活至宿主回呼返回，包括宿主在回呼中釋放其最後一個參考的情況。

原生視窗已經關閉時，Application 將停用文字輸入、釋放擷取及還原預設可見游標視為已完成的清理；啟用輸入或設定其他游標仍會傳回錯誤。

Application.Run 將 WaitMilliseconds 作為閒置影格的最長間隔（預設 16 毫秒），等待前扣除上一影格更新和繪製耗時；已有事件仍會提前喚醒。Tick 不增加等待，由宿主負責排程。未設定的原生屬性跳過指令碼輪詢，已設定的 attribute 仍依原有同步規則求值；直接賦值或 Clear 會解除對應綁定。

`Run(beforeUpdate)` 與 `Tick(beforeUpdate)` 會在 GUI 同步前呼叫回呼，通常位於輸入派發後。根控制項尚無配置時，輸入派發前的預先配置更新也會呼叫回呼，因此首次單步可能呼叫兩次。動畫或模型更新可使用 Run 多載並沿用事件等待預算；Tick 仍是不等待的單步更新。例外正常傳播，清理時仍會更新根檢視參考。

繫結 Getter（包括回傳空 optional 的讀取器）仍在每個必要同步輪次求值。直接 Setter 和 Clear 替換或移除區域繫結。變更通知僅在掛鉤成功後提交，失敗的掛鉤可在後續更新中重試；自訂值型別保留正常的語言複製行為。

GUI 繼承宿主執行設定。選擇執行後端時，將 `--backend=vm` 或 `--jit-backend="/path/to/Jit" --backend=jit` 放在 `gui-preview` 之前。C++ 宿主設定 FEngineConfig.ExecutionBackend 和 JitBackendFactory（LoadJitBackend 的結果）。沒有 JIT 工廠的 mixed、偵錯、保留狀態的初始化與有界清理使用 VM。持久工作階段的一般函式可依宿主設定使用 JIT。載入原始碼前應檢查 `VeroScriptSession::ApplyConfig` 的回傳值並傳遞拒絕原因。

正式發行套件為 `vs-gui`、`vs-rmlui`，產品版本均為 `0.1.1`。Gui 依賴 Platform／Graphics／Image／Font，RmlUi 依賴 Gui；後端按需選擇。原生 SDK 使用 `find_package(VeroScriptGui)`；腳本 SDK 加上 `COMPONENTS Bindings`，並提供匹配的 Core／C++ Runtime SDK。保留 `VeroScriptGuiFramework` 相容入口。

## 指令碼與主程式契約

屬性與事件的已封閉值型別保留核心的型別及範本值參數，包括 bool、固定寬度整數與列舉。產生的原始碼保留整數的準確型別與範圍，轉換內建容器、參照及函式簽章中的巢狀型別，並保持固定長度陣列的維度順序。GuiWidget 要求在全域或命名空間中宣告非靜態的封閉類別，不接受區域或巢狀控制項類別。GuiMethod 與屬性變更鉤子也必須是非範本執行個體方法，包括不能宣告範本值參數。

CompileGuiSource/File 保留 FCompileOptions.ConstantEvaluationLimits，由核心執行 constexpr 求值。動態 attribute 繼續在正常的 GUI 同步輪次取樣。

Context.Post 在執行或捨棄前保留已投遞的回呼及其擷取值；擷取物件可能繼續存活至延遲清理完成。Context.SetFocus 同步傳送控制項通知。

TextArea 保留邏輯 CR、LF、CRLF、NEL（U+0085）、行分隔符（U+2028）及段落分隔符（U+2029）；每個序列只顯示一次硬換行，Backspace/Delete 將 CRLF 整體刪除。碼點選區與剪貼簿仍計算、保留原始序列。單行控制項過濾這些分隔符；Tab 保持既有過濾行為，預留文字空白僅在顯示時正規化，不改變文字值。

光標以 leading/trailing 歸屬區分方向邊界的視覺位置。水平導覽依段落基礎方向跨越軟折行，從相應視覺邊緣進入下一行，也適用整行只有反向執行段的情況；不保證與所有系統編輯器相同。完整字素導覽與連字內部精確光標仍為獨立能力。

宿主文字輸入服務回呼可以查詢 GUI 狀態；回呼返回前，Context 和已掛載控制項的修改、服務替換及 Close 會被拒絕。

核心公開 ABI 或位元組碼內部配置變更後，應使用相符的 Core／C++ Runtime SDK 重新建構 GuiScript 與原生提供者，並重新產生對應配置的位元組碼套件。產品版本同為 0.1.1 不能單獨說明二進位相容性。

## 通知式繫結與批次更新

`Observable<T>.Get()/Set()` 儲存可通知的資料。`Set` 使用 `==` 比較，相等時不通知；修改可變物件內部內容後可呼叫繼承的 `Notify()`。`ChangeSignal.Notify()` 也能代表其他服務維護的輸入。單一來源使用 `AsBinding()`；組合計算使用 `Binding<T>(getter, inputs)`，明確傳入 signal 陣列。一般指令碼欄位不會被自動追蹤。 重複呼叫 AsBinding 會透過弱快取重用仍存活的繫結，不形成來源與繫結的持有循環。

通知 Getter 必須唯讀並宣告所有會變動的輸入。結果依輸入修訂快取，包含 optional 無值結果；共用同一 Binding 的控制項共用計算結果。`Binding<T>::FromAttribute(attribute, inputs)` 明確採用通知契約。未知相依、時間和 weak 物件失效仍須適當通知；直接使用的 `std::attribute<T>` 保留輪詢與回退行為。

重複通知由原生髒佇列合併，每次發布只喚醒各受影響控制項一次。`BindingUpdates.Batch(work)` 支援巢狀，於下一次安全更新處理 GUI；資料本身立即改變，例外不回復既有賦值。批次內不能執行 Context.Update、Application.Tick 或 Application.Run。控制項建構保留原有初始化與父子生命週期順序。

佇列在所有影格服務擷取快照後發布，由既有控制項同步讀取相依屬性；結果相等時不執行變更掛鉤，也不額外標記原生屬性失效。影格快照必須在擷取後通知，不可觀察的逐影格資料繼續使用一般動態 attribute。程式更新不發出使用者輸入事件；SetX/ClearX 解除舊訂閱，IsXBound 保留 attribute 語義。Dispose 或最終釋放會解除訂閱，卸載保留設定。Getter 失敗保留已提交值並沿用生命週期重試。FromAttribute 擷取傳入設定；之後取代原 attribute 設定不會取代此 Binding。 FromAttribute 保留回退層輪詢，讓預設 attribute 與原生樣式可獨立變動；傳入 Getter 的結果仍依通知修訂快取。

VS、產生的 Builder 與 .vswml 的 VS 區塊共用 setter。既有 `bind:` 保留輪詢語義，.vswcss 維持樣式層。通知和佇列處理在 GUI 所屬執行緒執行。

```cpp
auto amount = VS::Gui::Observable<double>(0.25);
VS::Gui::ProgressBar bar = VS::Gui::ProgressBar::Create().Value(amount.AsBinding());

// Setters need a mutable value capture; read-only getters do not.
VS::Gui::BindingUpdates::Batch([amount]() mutable -> void
{
	amount.Set(0.4);
	amount.Set(0.6);
});
// The next Context.Update/Application.Tick commits 0.6 once.

array<VS::Gui::ChangeSignal> inputs = {amount};
VS::Gui::Binding<string> caption = VS::Gui::Binding<string>([amount]() -> string
{
	return to_string(amount.Get());
}, inputs);
VS::Gui::Text label = VS::Gui::Text::Create().Text(caption);
```

## 集合、導覽與對話框

ListView／ListItem、TreeView／TreeItem 使用字串 SelectedKey 單選，提供 GetActualSelectedKey、FindItem、Select、Activate 及 SelectionChanged／ItemActivated。Key 省略時依序使用 Id、順序編號；變動資料應明確指定穩定唯一的 Key。空或未匹配的模型不選取項目。Select 可請求既有項目，Activate 另要求可互動。方向鍵、Home／End 導覽，Enter／Space 啟用。樹支援 Left／Right、Expanded attribute、IsActuallyExpanded 與 ExpandedChanged；摺疊保留子元件和其 Visibility。

TableView 使用 TableColumn／TableRow；列有 Key、正像素寬 ColumnWidth、Sortable，每行單元格數與列數一致。SetColumns、SetRows、GetCellText／SetCellText、SortBy 供程式使用；排序為穩定 UTF-8 文字順序，保留行物件和選取，不推測數字或區域排序。AutoSort 預設 true；事件含 ColumnClicked、Sorted、SelectionChanged、ItemActivated。目前為單選元件表格，資料來源可選定高列虛擬化，不提供試算表編輯；手寫元件模式應批次 SetChildren／SetRows。

ComboBox 接收 ListItem，提供 SelectedKey、Open、Placeholder、DropDownHeight、SetItems、Select、GetList、RequestOpen 和對應事件；顯示 Text 由選項 Text／Placeholder 管理。TabView 接收帶 Key／Title 的 TabItem，未繫結時預選首個可用頁；Select／SelectionChanged 切換頁面。未啟用頁仍掛載，內容獨立抑制，不覆寫 Visibility。表頭支援 Left／Right／Home／End。

Menu／MenuItem 支援巢狀子選單、Command、Checkable／Checked、Invoked、OpenMenu／CloseMenu／Invoke。子選單在下方／右側定位；Escape 和方向鍵導覽，一次外部指標按下關閉完整活動選單鏈並消耗該次按下。作業系統原生選單／快捷鍵由宿主處理。

Popup 預設非模態，Placement=below／right／center，MatchAnchorWidth 可選，DismissOnEscape 控制 Escape。GetDismissReason 為 escape／outside／空字串。Dialog 預設 Modal=true、置中、DismissOnOutside=false；Show／Close(result) 和 Closed(Dialog,string) 使用現有迴圈。它攔截背景輸入、限制 Tab 焦點並恢復先前有效焦點，不建立巢狀迴圈；Open 繫結保留來源，後續同步恢復模型值。

Builder 用 OnXxx，實例用 AddXxx。組合元件的 SetChildren 指向使用者內容；GetChildren／父級查詢保留實際樹，GetContentChildren 讀取內容子項。結構變更在 getter／更新鉤子之外執行，或透過 Context.Post。Grid 仍為等寬；表格獨立列寬用 TableColumn。參見[範例](../../../../Examples/CompositeControls/main.vs)、[ML](../../../../Examples/CompositeControls/page.vswml)、[CSS](../../../../Examples/CompositeControls/page.vswcss)。

`MenuItem.IsActuallyChecked()` 讀取目前勾選候選狀態；連續請求依候選值切換，下次同步恢復已繫結的 Checked 模型。Dialog 的 Closed 使用 OpenChanged 回呼前的結果快照，回呼重新開啟也不覆寫該次關閉結果。

資料清單和表格支援自動定高虛擬化：`ListView.SetVirtualItems(labels,rowHeight=28,overscan=2)`、`TableView.SetVirtualRows(rows,rowHeight=28,overscan=2)` 只建立及重用可視區域附近的列。`SetVirtualItemText(index,text)`、`SetCellText(key,column,text)` 可修改離屏資料。自訂列使用 `SetVirtualSource(keys,rowHeight,factory,overscan=2)`；工廠接收 `(string key, CollectionItem recycled)`，回傳該列或尚未掛載的新列，表格要求 TableRow。重用時必須重設舊鍵對應的狀態。`RefreshVirtualItems()` 重新綁定可見列；`SetVirtualOrder(keys)` 以完整鍵序列調整順序。內建虛擬表格保留穩定文字排序；自訂來源自行回應 ColumnClicked 並提供順序。

`GetItemCount()` 計算邏輯列數；`GetItems()`、`GetRealizedItemCount()`、`FindItem()` 只反映已實例化的列。選取保留邏輯鍵，`ScrollToItem(key)` 可顯示離屏列；擁有焦點、按下狀態或指標捕獲的列暫不回收。實例化在 Context.Update 的安全延後階段執行，版面配置階段才發現的變更於下次更新處理。列採固定邊框盒高度並裁切內容，自訂內距須符合此高度。鍵須非空且唯一，overscan 為 0..256，總高度上限為 16,777,216 邏輯像素。工廠或 getter 中不可替換、排序來源。工廠錯誤向上傳播，後續更新可重試；視圖負責釋放回傳的列。原有手寫子元件、SetItems、SetRows 保留原生命週期；暫不包含變高列與樹虛擬化。

首次版面配置前呼叫 ScrollToItem，會在實際視口可用後校正定位；隨後明確設定的捲動位置優先。

## Context 後端預設值

使用標準應用工廠橋接的 `VS::Gui::Context("", width, height)` 時，建構參數中的明確後端優先，其次為本次呼叫選擇，再其次為應用／環境工廠預設值；兩層均未提供選擇時，GUI 後備為 `rmlui`。宿主後端查詢回呼傳回空字串表示該層未指定，並不停用服務；明確的 `auto` 或 `none` 不會被 `rmlui` 替換。環境設定中明確儲存的空後端值仍依公共規則正規化為 `none`。自訂宿主 Context 工廠自行負責後端名稱處理。

<a id="context-callback-reentrancy"></a>

## Context 回呼限制

取代主題、框架服務、指標服務或文字輸入服務時，不應在其觸發的回呼中修改 Context 設定；同一 Context 的遞迴設定會被拒絕。透過 `Post` 將後續工作安排到擁有者執行緒。

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/Framework.md) · [fr-FR](../../fr-FR/Reference/Framework.md) · [ja-JP](../../ja-JP/Reference/Framework.md) · [ko-KR](../../ko-KR/Reference/Framework.md) · [zh-CN](../../zh-CN/Reference/Framework.md) · [zh-TW](Framework.md)
