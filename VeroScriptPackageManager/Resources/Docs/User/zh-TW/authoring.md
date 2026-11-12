# 撰寫分發套件

`pack` 優先讀取 `veroscript-package.json`；否則讀取 `veroscript-project.json`，從 `packageManager.distribution` 取得散發中繼資料（物件或專案相對 JSON 檔名）。專案打包會在封存內產生獨立的 `veroscript-package.json`，排除來源專案設定。檔案篩選須包含產生的套件清單與所有宣告入口。

## 資訊清單與相依性

ZIP 根放置 veroscript-package.json。名稱為 2–64 位小寫字母/數字及單連字號，排除裝置保留名。版本為 major.minor.patch，可帶預發行，不含建置中繼資料。要求支援精確版本或空白分隔比較式的交集。執行時還驗證路徑、目標、身分及完整相依圖，不能僅憑 Schema 判定可用。

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

相依宣告 runtime/compile/development 用途。--development 選擇開發相依；選用相依需 `packages.<name>.enabled=true`。預發行需明確要求或 --prerelease。同一環境的正規名稱僅對應一個版本與來源。部署只選適用 runtime 根宣告，間接安裝不賦予其他根宣告直接存取權。

## 資源、輸入與顯示文字

entries.modules 公開匯入名稱，resources 宣告具名資源群組。套件路徑須為可攜相對路徑，不含越界、連結或裝置別名。缺少選用資源不同於收據所屬檔案被刪除/修改，完整性錯誤不能當成一般缺失。

本地化根含正規 BCP 47 目錄及 en-US JSON 基準。譯文可缺少基準鍵，但不能新增、跨檔重複或改變 formatId。一般字串是常值，括號不自動格式化；格式訊息明確使用 pattern 與 formatId=vero.named。標準鍵含 package.displayName、package.description、install.before/install.after、`inputs.<name>.description`，取得 package.name 與 package.version。此目錄不改變指令碼/編譯器診斷。

```json
{
	"package.displayName": "Demo Math",
	"install.after": {
		"pattern": "{package.name} {package.version}",
		"formatId": "vero.named"
	}
}
```

輸入保留 string、boolean、integer、enum 型別。一般輸入用 `--input=name=<JSON>`，敏感字串用 --secret-env=name=ENV_NAME。秘密僅保留於此次操作，不用於宣告式範本。`<package>@<version>.<input>` 優先於 `<package>.<input>` 與裸名；CLI 預設非互動。

## 安裝處理與限額

宣告式 copy/template 僅寫新的 generated 區域。指令碼掛鉤需 --allow-hooks 及匯出的非泛型 void entry(std::package::install_context)，只使用允許資源及產生區寫入；檔案/環境/主控台/工作/位址/時鐘服務及套件原生註冊不可用。取消及 VM 預算為協作機制，不是作業系統沙箱。

預設掛鉤 1,000,000 指令、2,000 毫秒，單次資源文字服務 16 MiB，掛鉤緩衝區 64 MiB，顯示文字 65,536 位元組；不限制整個堆積。掛鉤取得型別化輸入及獨立顯示快照。自訂可信處理器是具版本及實作身分的宿主服務。設定影響的產物在變更設定後須重裝。uninstall --skip-hooks 是明確恢復選項並回報警告。

掛鉤編譯沿用核心的 `constexpr`、`if constexpr`、泛型及原始碼／`.vslib` 匯入。每次編譯共用一份常量求值預算：預設 1,000,000 工作量、64 層呼叫、256 層運算式、16 MiB 求值記憶體和 8 MiB 快取。宿主以 `FLimits.HookConstantEvaluationLimits` 設定；僅 `MaxCacheBytes` 可設為零以停用快取。預檢與執行前編譯、解除安裝掛鉤使用相同策略。編譯遵守整個套件操作的期限及取消檢查；2,000 毫秒 VM 逾時獨立用於執行階段。數值及緩衝區限額仍屬執行階段策略。

## 存放庫與發行

專案設定與鎖定檔均未指定官方來源時，CLI 預設使用 https://veroscript.org/api。--registry-api 替換此備用位址；packageManager.registries.official 設定專案來源。嵌入宿主仍明確設定 FClientOptions.OfficialApi 或專案來源。HTTPS 驗證憑證，--allow-http 僅供明確開發使用。--credential-env 讀取 Authorization 標頭的值，例如 Bearer YOUR_TOKEN，不含欄位名稱 Authorization:。省略 --credential-scope 時，認證資訊僅限 --registry-api 或內建官方 API；只有位於該來源與路徑範圍內的 URL 才會收到認證資訊。專案 API 或鏡像位於此範圍之外時，應明確設定 --credential-scope。認證資訊不得寫入鎖定檔或原始碼清單。

設定的鏡像需要 trustedMirror=true；--mirror 僅為本次操作授權官方來源鏡像。allowOriginFallback 預設為 false。重新導向跨越來源或範圍時會移除敏感標頭，且不會降級 HTTPS。

## 追加發佈其他平台

使用 publish 建立版本，或向同一版本追加其他平台的製品。所有輸入封存檔必須具有相同的套件名稱、版本、相依性與衝突條件。資訊清單中的目標不能彼此重疊，也不能與已發佈製品重疊；不同的作業系統或架構限制可以區分平台，僅最低版本或能力要求不同不足以區分目標。可攜製品與所有目標重疊。

已發佈製品的 ID、位元組與中繼資料維持不變；完全相同的製品會直接重用，不必再次上傳。修改既有目標的內容需要使用新的套件版本，僅改封存檔名不會產生新目標。網路結果不確定時，請保留原封存檔與發佈快取，使用同一組檔案重試；用戶端會核對登錄後再確認成功。

每次命令中同一封存檔只提供一次；即使檔名不同，位元組完全相同的兩個輸入也會被拒絕。

```text
veroscript install example-math@0.1.1
veroscript publish ./library.vspkg
veroscript publish ./gui-0.1.1-windows-x64.vspkg ./gui-0.1.1-linux-x64.vspkg
veroscript publish ./gui-0.1.1-macos-arm64.vspkg
veroscript install example-math@0.1.1 --registry-api=https://your-host/api/v1
```

[套件結構定義](../../../Schemas/veroscript-package.schema.json) · [本地化結構定義](../../../Schemas/veroscript-package-localization.schema.json) · [資訊清單範例](../../../Examples/Local/library/veroscript-package.json)

## 原生項目與 CLI 套件

`entries.native` 必須提供 `id`、`path`、`linkage` 與 `roles`。選用的 `componentVersion` 用於核對原生描述元版本；分析 bindings/compiler 項目時，必須同時提供它與 `catalog`。角色支援 `bindings`、`compiler`、`provider`、`cli`，其集合須與程式庫查詢描述元相符。原生套件需要相符的目標身分，包括 OS、架構、環境、Core 建置及 `abis.native-extension`；使用對應 `veroscript env --json` 提供的值，不應只由套件版本推測。

`cli` 項目向 CLI 宿主服務登錄命令及選項。套件在選定環境安裝並啟用後，直接使用登錄名稱，不須指定程式庫選擇器；停用套件後不再探索其命令。分析模式及未提供 CLI 服務的宿主會跳過僅有 CLI 角色的項目。組合的 bindings/compiler 角色仍須滿足分析目錄及元件版本要求。安裝過程照常驗證不可變產物與載入器中繼資料。

[命令使用說明](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md)

VeroScript 官方安裝分發包統一使用 `vs-` 前綴，例如 `vs-asmjit`；其 JIT 後端 ID 仍為 `asmjit`。官方倉庫為授權的官方發布者保留此前綴。本機或私人來源中的相同前綴不自動代表官方身分，仍須核對來源與產物。

原生項目 ID 由小寫 ASCII 字母及數字組成，以單一連字號分段，不得以連字號開頭或結尾。Schema 允許 2–64 個字元；原生描述符的 64 位元組包含 NUL 結尾，因此實際原生模組最多使用 63 個字元。宣告 ID 必須與描述符及預期 ID 一致。`entries.backends[].module` 參照已宣告的原生提供者 ID，不是主機服務 ID。

## 官方後端與 GUI 發行套件

JIT 與支援腳本的基礎模組發行物，除含產品目錄的可攜 ZIP/tar 外，也提供可安裝的 `.vspkg`。只有根目錄含 `veroscript-package.json` 的封存檔才能安裝。預設安裝至目前專案；`--path` 接受相對或絕對專案路徑，`--root` 選擇 Runtime/SDK 環境。`vs-asmjit` 的 JIT 後端 ID 仍為 `asmjit`。

```text
veroscript install ./vs-asmjit-0.1.1-win-x64.vspkg --path ./MyProject
veroscript --path ./MyProject --jit-backend asmjit --backend jit ./MyProject/main.vs
```

GUI 適配器使用既有宿主服務 `veroscript.gui.tools`。請使用相符且啟用 GUI 的 CLI／Runtime，或明確組裝 GuiScript 的宿主。安裝不會將 GUI 內嵌至純 Core。SDL3、RmlUi、Stb 和 FontBackend 仍是分別安裝與選擇的提供者。離線安裝須透過專案相依設定或命令引數明確提供相依封存檔；相鄰檔案不會自動成為相依來源。

## 後端項目與保留選擇值

`entries.backends[]` 必須包含 `service` 和 `id`，遵循套件 ID 語法（2–64 個小寫 ASCII 字母／數字，以單個連字號分段）。`auto`、`none` 是保留選擇值，不能作為後端 ID。兩種項目形式互斥：`module` 參照已宣告且具有 `provider` 角色的原生項目，不得再寫 `path`、`contract`；或用 `path` 加 `contract` 宣告專用 VM／JIT 程式庫，不得寫 `module`，且 `service` 與 `contract` 必須同為 `vm` 或同為 `jit`。仍須通過原生目標與 ABI 檢查。專案 `services` 中省略 `backend` 繼承自動選擇，明確空值或 `none` 停用，`auto` 要求自動候選。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md) · [en-US](../en-US/authoring.md) · [fr-FR](../fr-FR/authoring.md) · [ja-JP](../ja-JP/authoring.md) · [ko-KR](../ko-KR/authoring.md) · [zh-CN](../zh-CN/authoring.md) · [zh-TW](authoring.md)
