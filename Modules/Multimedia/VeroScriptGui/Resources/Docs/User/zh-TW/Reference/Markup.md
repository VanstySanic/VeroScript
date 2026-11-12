# VSWML 與 VSWCSS

> 本頁介紹 `FMarkupProgram` 原生載入 API。一般 VS 匯入、註解 Schema、Builder、自訂屬性／事件及型別化工廠請見[框架指南](Framework.md)。下文原生介接器的限制僅適用於該載入 API。

使用 `VeroScriptGuiMarkup.h` 中的公開 API，將標記載入為帶型別化屬性及路由事件的 `FWidget` 樹，再交給既有的 `FContext`。支援文字、字型、圖片、自訂繪製和基本控制項。原生載入器預設只登記 `Widget`；使用帶 Schema 的 VS 編譯路徑可取得內建控制項及其型別化屬性／事件，也可明確提供原生型別工廠。標記格式不要求特定的配置或圖形後端。

## 載入

使用 `ReadMarkupFile` 取得入口，設定 `FMarkupCompileOptions.Read`，再呼叫 `FMarkupProgram::Compile`、`Create`，將根節點交給既有 `FContext`。錯誤使用 `FMarkupDiagnostic::Format`。內部 `src` 相對於宣告檔案解析，不受工作目錄影響；自訂 `FMarkupReader` 可使用不透明的模組／資源識別。`GetDependencies` 提供直接讀取來源的不可變快照，遞迴 VS 匯入由 Core 管理。編譯不需視窗或 GPU；每次 `Create` 都有獨立節點與腳本狀態，執行個體可在已編譯程式釋放後繼續存活。重新載入與編譯須明確要求，不提供自動狀態移轉。

```cpp
#include "VeroScriptGuiMarkup.h"
#include <utility>

bool LoadMarkupUi(const std::string& Entry,
				  std::shared_ptr<VeroScript::Gui::FWidget>& OutRoot,
				  std::string& Error)
{
	using namespace VeroScript;
	Gui::FMarkupSource Source;
	Gui::FMarkupDiagnostic Diagnostic;
	if (!Gui::ReadMarkupFile({}, Entry, Source, Error))
	{
		return false;
	}
	Gui::FMarkupCompileOptions Options;
	Options.Read = Gui::ReadMarkupFile;
	auto Program = Gui::FMarkupProgram::Compile(Source, Options, Diagnostic);
	if (!Program)
	{
		Error = Diagnostic.Format();
		return false;
	}
	auto Root = Program->Create({}, Diagnostic);
	if (!Root)
	{
		Error = Diagnostic.Format();
		return false;
	}
	OutRoot = std::move(Root);
	Error.clear();
	return true;
}
```

用 `SetRoot` 將回傳的根節點掛接到既有 `FContext`，並在 `Update` 成功後繪製。載入函式本身不建立視窗、裝置或後端。

## 語法與作用域

UTF-8、單根、區分大小寫的 XML 風格語法，支援引號屬性、註解、CDATA 及 XML／數字實體。bind: 和 on: 內建，不宣告 xmlns。任意已登記控制項可作根，所有 Widget 都可有子節點。

根可含多個 import／style 和至多一個 script；style 使用 src 或內嵌內容。普通根是執行個體；component 必須有 type 和一個 template，可指定 mark、base、id。type 的短名和 mark 都可作標籤；base 支援已匯入 ML 模板／原生工廠，禁止遞迴。

屬性原名／Mark 都可用；相同屬性的重複來源報錯。ML 的 id／class 使用小寫，值區分大小寫。模板有私有 ID、樣式、腳本作用域，Find(componentRoot, id) 查詢內部；使用處追加子節點保留其宣告作用域。原生工廠內部樹同樣私有；之後任意 AddChild 不會自動登記 ML 作用域。

普通屬性設定固定值；`bind:gap="{ Count + 1 }"` 或命名方法安裝 getter，`on:pointer-down="{ Count = Count + 1; }"` 或命名方法安裝處理器。未知控制項／屬性／事件、原始文字及不支援語法會診斷。

## 樣式與腳本

選擇器支援型別／Mark、*、class、id、複合、後代、>、逗號，以及 root／hover／active／focus／focus-within／disabled／checked／read-only 狀態。優先序是預設→主題→模板→使用處→局部固定值／getter；同作用域按特異度和來源順序。SetTheme、SetClasses、SetId 影響後續 Update。Set 取代 getter，Clear 恢復目前樣式／預設；錯誤不靜默降級。 作用域優先序先於跨作用域的特異度比較。`FMarkupProgram::SetTheme` 設定或移除執行個體執行階段的主題。CSS 只接受允許樣式設定的屬性；基底屬性提供 getter 繫結，具體控制項的 GuiProperty 結構描述則區分一般值與可繫結的 Attribute 屬性。

長度支援數字、px／dp、百分比、auto；padding 支援一到四個非負像素值。色彩由 sRGB 轉線性 RGBA。自訂變數繼承父節點已計算值，支援 var 和 fallback，循環／缺失／型別錯誤保留來源。變數繼承限同一 ML 執行環境，字型與文字屬性依下文規則繼承，其他普通屬性不採 HTML 繼承。不接受 !important、@ 規則、兄弟／屬性選擇器、CSS 動畫及任意 RCSS。狀態於 Update 更新，主機須轉送 Platform 取消、失焦及重設事件。

靜態 ML／CSS 僅需 Gui。啟用 `VEROSCRIPT_GUI_SCRIPTING=ON`、連結 `VeroScript::GuiScript`，配置 FMarkupScriptOptions.Engine，再將 MakeMarkupScriptFactory 傳入 Create。需要相同版本 C++ Runtime SDK／Core。內嵌 script 是每個執行個體的類別成員；一般頁面也可用 `<script src="logic.vs" type="App::Logic" />`。完整樹建立後才建構腳本，再安裝繫結；此時未完成掛載及首次佈局。 頁面取得獨立狀態物件；元件狀態繼承 `VS::Gui::Widget`，並參照既有原生元件根節點。建構函式可查詢已初始化的 `elements`，但掛載及配置尚未完成。

此原生 API 也可由宿主在 `FMarkupProgram::Create` 的第三個引數傳入 `IMarkupScriptContext`，取代 script 區塊。兩種上下文來源不能並存；元件使用自身的上下文。

內建 self／context／elements，事件另有 sender／event／arg0／arg1。外部 context 保持正常 VS 存取權限和區域變數遮蔽。elements.Find 可空，Require 缺失報錯。命名方法需 @GuiMethod，可選 Mark；GUI 註解名及其選項名忽略 ASCII 大小寫；Core 註解名預設仍區分大小寫。反射 `find_arguments(name)` 始終按已儲存的參數名精確比對，Mark 值也區分大小寫。路由方法支援無參數、Event、Widget sender 或兩者，回傳 void。Event 保留輸入快照及弱控制項參照；三種傳播／預設動作控制只在同步派發期間有效。

標準庫 attribute.vs、std::attribute.get／set 及 Core 本地化／格式化可透過正常 VS 呼叫使用；Text 和可編輯控制項均已提供；不會將 ML 字串隱含解釋為本地化 Key，應將文字繫結至正常的 Core 本地化／格式化呼叫。已發佈 ModuleEnvironment 綁定目錄不會被修改，Gui 以合併目錄接入。操作在所屬執行緒；若最後狀態參照於其他執行緒釋放，關閉前呼叫 CollectMarkupScripts。

原生工廠必須傳回在所有者執行緒新建的控制項及子節點；已初始化或已歸屬 ML 執行個體的節點會被拒絕。綁定運算式／程式區塊保留 `self`、`context`、`elements`、`sender`、`event`、`arg0`、`arg1`；區域變數、lambda 參數、foreach／catch 變數及區域型別別名不能重新宣告這些名稱。

已提供基本、集合、導覽控制項及模態對話框。啟用 GUI 的 CLI 提供 `gui-check`、`gui-compile` 與 `gui-preview`；完整命令、結構描述及型別化工廠見[框架指南](Framework.md)。[計數器標記](../../../../Examples/Markup/counter.vswml)、[元件標記](../../../../Examples/Markup/card.vswml)和[共用樣式](../../../../Examples/Markup/layout.vswcss)隨 Resources 整合文件分發，不應假定功能 SDK 歸檔內含有範例目錄。

## 文字、字型、圖片與繪製

Text 是 UTF-8 字面文字，可使用字串 attribute 綁定；不解析為 RML，不支援 ML 正文或混合行內標記。字型、文字樣式、方向和語言預設繼承父級。Direction 指定段落基礎方向 auto/ltr/rtl；auto 使用首個強方向字元，沒有時使用 LTR。Language 只影響塑形。RmlUi 支援混合方向段落，仍保存邏輯 UTF-8 文字與碼點選區。ML 的 `Direction="auto" Language="ar"` 與 CSS 的 `direction: auto; language: ar;` 使用相同屬性；left/right 對齊仍表示實體方向。

請參閱[GUI 框架契約 — 文字、字型、圖片與繪製](Framework.md#image-resources)。

預設內容配置依序放置內建圖片、文字及子控制項；修改屬性或重建控制項樹皆保持此順序。

Image、BackgroundImage、MaskImage 的 Getter 對直接值及綁定值統一回傳已解析的資源識別／URL，轉交其他模組時不會改變資源來源。

請參閱[GUI 框架契約 — 文字、字型、圖片與繪製](Framework.md#compositor-properties)。

[Framework](Framework.md)

| 屬性 | CSS 名稱 | 可用於樣式 |
| --- | --- | --- |
| Text, Image | text, image | 否 |
| FontFamily, FontSize, FontWeight, FontStyle | font-family, font-size, font-weight, font-style | 是 |
| TextColor, TextAlign, WhiteSpace, TextOverflow | color, text-align, white-space, text-overflow | 是 |
| LineHeight, LetterSpacing, Direction, Language | line-height, letter-spacing, direction, language | 是 |
| CornerRadius, ClipContent | border-radius, clip-content | 是 |
| BorderWidth, BorderColor | border-width, border-color | 是 |
| Cursor | cursor | 是 |
| BackgroundImage, MaskImage, BoxShadow | background-image, mask-image, box-shadow | 是 |
| Transform, Filter, BackdropFilter | transform, filter, backdrop-filter | 是 |

請參閱[GUI 框架契約 — 文字、字型、圖片與繪製](Framework.md#native-resource-services)。

Cursor：`VS::Gui::Cursor`；ML 使用 `Cursor="pointer"`，CSS 使用 `cursor: pointer`。此屬性可繼承並支援 Attribute 綁定。`Auto` 採用控制項預設行為，`None` 隱藏游標。指標服務與視窗邊界通知請參閱 [Framework](Framework.md)。

自訂屬性以 `--` 開頭，`var(--name, fallback)` 使用繼承值或可巢狀的後備值。以下 gap 為 8 pixels，移除 `--space` 後使用 4 pixels 的後備值：

```css
:root
{
	--space: 8px;
	gap: var(--space, 4px);
}
```

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/Markup.md) · [fr-FR](../../fr-FR/Reference/Markup.md) · [ja-JP](../../ja-JP/Reference/Markup.md) · [ko-KR](../../ko-KR/Reference/Markup.md) · [zh-CN](../../zh-CN/Reference/Markup.md) · [zh-TW](Markup.md)
