# 12：宿主綁定與腳本整合

VeroScript 的一個重要使用場景是嵌入 C++ 宿主。宿主負責建立引擎或會話、註冊函式和類型；腳本負責呼叫穩定的公開能力。本篇先用綁定目錄理解邊界，再說明實際整合時的工作順序。

## 第一步：認識綁定目錄

綁定目錄以 JSON 描述宿主 API。從本文檔資源或獨立 Resources 發行取得 [bindings.sample.json](../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json)，以同名檔案複製到可寫教學目錄。SDK 歸檔不附範例。以下指令均從該目錄執行：

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

綁定目錄包含 `version`、`functions`、`genericTypeTemplates`、`types`、`typeAliases` 和 `annotations` 等頂層欄位。它描述介面，不會代替 C++ 註冊程式碼，也不會單獨產生執行階段實作。

先把目錄看成「編譯器看到的介面清單」：函式至少描述 `name`、`returnType` 和 `paramTypes`；宿主物件和容器還需要對應的宿主類型、元素類型或鍵值類型元資料。引用參數使用與 `paramTypes` 平行的 `paramFlags`，參數註解使用平行的 `paramAnnotations`。這些欄位必須和 C++ 註冊簽章保持一致。

例如，範例綁定目錄中的 `host_add` 函式條目使用兩個 `int` 參數，並為第一個參數附加了註解：

```json
{
	"name": "host_add",
	"returnType": "int",
	"paramTypes": [
		"int",
		"int"
	],
	"paramFlags": [
		0,
		0
	],
	"paramAnnotations": [
		[
			{
				"name": "HOST_PARAM",
				"arguments": [
					{
						"name": "DisplayName",
						"value": "Left"
					}
				]
			}
		],
		[]
	]
}
```

不要把目錄中的元資料陣列理解成執行階段實作；它們只描述介面和工具可見資訊。完整欄位表、支援的型別名稱、泛型範本、型別別名和 JSON 限制請見[宿主綁定參考](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/bindings.md)。

## 第二步：在腳本中使用宿主能力

宿主註冊完成後，腳本可以像使用普通宣告一樣呼叫公開函式或類型。例如，假設宿主註冊了 `HostLog`：

```VeroScript
int main()
{
	HostLog("script started");
	return 0;
}
```

假設宿主註冊了帶有靜態方法的類型，腳本通常透過類型名存取：

```VeroScript
int main()
{
	int value = HostMath.clamp(120, 0, 100);
	print(value);
	return 0;
}
```

上面的名字只是展示呼叫形狀；實際可呼叫名稱必須以宿主註冊內容和綁定目錄為準。

## 第三步：依序整合

在 C++ 宿主中，建議依照下面的順序推進：

執行前，透過 `FEngineConfig::VmBackendFactory`／`VmBackendPath` 或 Session 選擇 API 設定相符 VM。僅連結 C++ SDK 不會選取直譯器，指令碼編譯處理器也需要 VM。所需 SDK 目標與設定步驟見 [VeroScriptBytecodeVm](../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/zh-TW/index.md)。

1. 建立 `VeroScriptEngine` 或長期存在的 `VeroScriptSession`。
2. 註冊原生函式、宿主類型、成員和運算子。
3. 為需要工具支援的公開介面補充檔案和參數資訊。
4. 匯出或載入綁定目錄，並執行 `bindings validate`。
5. 先呼叫 `CheckSource`/`CheckFile`，經過後再呼叫 `RunSource`/`RunFile`。
6. 為腳本檢查和執行失敗保留診斷訊息，不要只回傳一個布林值。

長期執行的編輯器、遊戲或宿主工具通常使用 `VeroScriptSession` 管理模組、綁定、執行狀態和熱重載；一次性執行可以使用 `VeroScriptEngine` 的原始程式碼或檔案入口。

## 第四步：控制介面穩定性

把綁定當作版本化 API：新增函式通常是相容擴展，但刪除成員、改變參數類型、改變枚舉值或改變所有權語意都可能破壞腳本。發佈宿主版本時，同時檢查：

- 綁定目錄是否通過校驗。
- 腳本是否通過 `--check`。
- 宿主註冊表與目錄是否一致。
- 類別、結構體和控制代碼的生命週期是否有明確約定。

如果註冊的靜態宿主成員有明確的 `HostObject` 參數，也可以用 `nullptr` 測試可為 null
的邊界。回呼會收到空的宿主物件控制代碼，解除參考載重前必須先檢查；空的執行個體
接收者仍然無效。

## 練習

1. 為一個宿主函式設計一個穩定的名稱、參數和回傳類型。
2. 給綁定函式增加簡短檔案和參數說明。
3. 修改一個綁定條目後，分別執行目錄校驗和腳本檢查。

宿主整合的完整邊界見[宿主整合](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/embedding.md)，綁定目錄見[宿主綁定](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/bindings.md)。


下一篇學習如何在腳本執行階段檢查類型和宣告資訊：[執行階段反射](13-reflection.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/advanced/12-host-bindings.md) · [fr-FR](../../fr-FR/advanced/12-host-bindings.md) · [ja-JP](../../ja-JP/advanced/12-host-bindings.md) · [ko-KR](../../ko-KR/advanced/12-host-bindings.md) · [zh-CN](../../zh-CN/advanced/12-host-bindings.md) · [zh-TW](12-host-bindings.md)
