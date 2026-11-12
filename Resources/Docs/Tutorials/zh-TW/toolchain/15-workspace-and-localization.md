# 15：設定專案選項與診斷本地化

使用 CLI 選項設定匯入路徑、前置處理定義和指令碼參數。這些工具鏈設定不會改變 VeroScript 的語言語意。

## 準備教學檔案

使用相符 Runtime 及其附帶的 VM、本地化資源。建立可寫教學目錄，在其中準備 `main.vs` 與 `src`／`shared` 子目錄，儲存以下三份檔案。所有指令從該目錄執行。搜尋路徑與巨集定義只作用於本次呼叫；下方每條檢查或執行 `main.vs` 的指令都重複提供相同選項。

### main.vs

```VeroScript
import "score.vs";

int main(string[] args)
{
#ifdef FEATURE_SCORE
	print("score={}, argc={}", scoreValue(), len(args));
#else
	print("feature disabled");
#endif
	return 0;
}
```

### src/score.vs

```VeroScript
import "settings.vs";

export int scoreValue()
{
	return baseValue() + 2;
}
```

### shared/settings.vs

```VeroScript
export int baseValue()
{
	return 40;
}
```

啟用 `FEATURE_SCORE=1` 並傳入 `demo 42` 時，預期印出 `score=42, argc=2` 並返回零。移除巨集定義則選擇 `feature disabled` 分支。檢查成功時返回零，不呼叫 `main`。

## 第一步：設定編譯選項

透過 `-I` 指定匯入路徑，透過 `-D` 指定前置處理定義；多個值可以重複傳入。CLI 直接讀取這些命令列選項。

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

## 第二步：選擇執行後端

本課使用 Runtime 隨附的 VM，無須選用 JIT 套件。下列命令將 `demo` 和 `42` 傳給 `main.vs`，保留腳本原本的輸出及結束結果。其他策略和模組環境選擇規則見 [CLI 後端參考](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md#cli-backend-policy)。

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

## 第三步：執行前檢查

執行前先檢查進入點：

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

`main(string[] args)` 接收腳本路徑之後的參數；腳本路徑本身不在 `args` 中。 CLI 的處理程序退出碼由 `int main`、`void main` 或 `exit(n)` 的最終結果決定。

## 第四步：選擇診斷語言

CLI 可以使用 `--lang` 臨時選擇語言：

```powershell
veroscript --lang zh-TW --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript --lang en-US --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

也可以在 PowerShell 會話中使用 `VERO_LANG`：

環境變數範例在檢查後恢復原本的 `VERO_LANG`。

```powershell
$tutorialPreviousLanguage = $env:VERO_LANG
try
{
	$env:VERO_LANG = "zh-TW"
	veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
	$env:VERO_LANG = $tutorialPreviousLanguage
}
```

語言標籤從已部署的目錄中發現。缺少的翻譯會回退至英文；明確傳入不支援的 `--lang` 值會回報參數錯誤。
解析器的宣告診斷（包括無效的 `static class` 和 `static struct` 宣告）在 CLI 與 LSP 中使用相同的穩定 ID 和目錄。工具邏輯應使用診斷 ID；翻譯後的訊息只用於顯示。

## 第五步：為專案提供在地化目錄

將發行套件的完整 `localization/` 目錄樹複製到專案中，保留完整的 `en-US` 英文基準。下圖展示語言目錄檔案；複製得到的其他目錄檔案也應保留。

```text
localization/
  en-US/
    language.json
    ...
  zh-TW/
    language.json
    ...
```

透過 `VERO_LOCALIZATION_DIR` 明確選擇專案目錄樹：執行檔旁的目錄優先於工作目錄的自動探索。下列範例在命令結束後還原原有設定。

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang zh-TW --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

`en-US` 定義完整基準。其他帶子標籤的正規 BCP 47 目錄只能包含英文目錄樹中已有的檔案和訊息 ID；缺少的翻譯檔案或鍵回退到英文。檔案必須是字串值組成的平面 UTF-8 JSON 物件，不能有重複鍵或尾隨內容。新增語言時，將其正規目錄和 JSON 檔案加入已選取的目錄樹。

## 練習

1. 在一次執行指令移除 `-D FEATURE_SCORE=1`，觀察 `feature disabled`；恢復巨集定義並傳入 `demo 42`，確認 `score=42, argc=2`。
2. 把入口參數改為三個值，在 `main(string[] args)` 中逐一列印。
3. 選取專案目錄樹後，刪除 `zh-TW/language.json` 中的 `diag.expected_expression`，執行下方命令，確認診斷以英文顯示「expected expression」。

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang zh-TW -e 'int main() { return +; }'
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

[CLI 參考](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md) · [Localization](../../../../../VeroScript/Resources/Docs/User/zh-TW/localization.md)

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/toolchain/15-workspace-and-localization.md) · [fr-FR](../../fr-FR/toolchain/15-workspace-and-localization.md) · [ja-JP](../../ja-JP/toolchain/15-workspace-and-localization.md) · [ko-KR](../../ko-KR/toolchain/15-workspace-and-localization.md) · [zh-CN](../../zh-CN/toolchain/15-workspace-and-localization.md) · [zh-TW](15-workspace-and-localization.md)
