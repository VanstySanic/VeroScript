# 宿主綁定與綁定目錄

宿主綁定把 C++ 宿主中的函式、型別、成員和元資料暴露給 VeroScript。綁定目錄（binding catalog）是描述這層公開介面的 JSON 檔案，可由 CLI 校驗和格式化，也可以在檢查或執行腳本時載入。

綁定目錄是介面契約，不會取代實際的 C++ 註冊程式碼，也不會單獨提供執行階段實作。目錄中有某個函式或類型，只表示編譯器可以看到它；執行腳本時，宿主仍必須註冊與目錄一致的真實實作。

## 目錄檔案與版本

綁定目錄的頂層物件由以下欄位組成。匯入時各陣列欄位可以省略；宿主匯出目錄時會輸出這些陣列。 `version` 如果出現，必須是整數 `1`。

| 欄位 | 類型 | 說明 |
| --- | --- | --- |
| `version` | 整數 | 綁定目錄格式版本；目前為 `1`。 |
| `functions` | 陣列 | 可供腳本呼叫的宿主函式描述。 |
| `genericTypeTemplates` | 陣列 | 泛型宿主類型模板描述。 |
| `types` | 陣列 | 宿主類別、結構體、枚舉及其成員描述。 |
| `typeAliases` | 陣列 | 宿主類型別名描述。 |
| `annotations` | 陣列 | 註記詞彙表，供反射、工具和檔案使用。 |

一個完整的空目錄可以寫成：

```json
{
	"version": 1,
	"functions": [],
	"genericTypeTemplates": [],
	"types": [],
	"typeAliases": [],
	"annotations": []
}
```

檔案應儲存為 `.json`。程式碼區塊中的 `json` 只是檔案中的程式碼高亮標籤，並不表示 JSON 檔案可以改寫成 `.vs` 腳本。

## 函式描述

`functions` 中的每個物件至少需要 `name`、`returnType` 和 `paramTypes`：

| 欄位 | 類型 | 必要 | 說明 |
| --- | --- | --- | --- |
| `name` | 字串 | 是 | 腳本可見的函式名稱。 |
| `returnType` | 字串 | 是 | 腳本類型名，例如 `int`、`string`、`object`。 |
| `paramTypes` | 字串陣列 | 是 | 參數類型，順序必須與 C++ 註冊簽章一致。 |
| `documentation` | 字串 | 否 | 函式文件。 |
| `paramNames` | 字串陣列 | 否 | 參數名稱，依參數順序排列。 |
| `paramDocumentation` | 字串陣列 | 否 | 參數文件，依參數順序排列。 |
| `returnDocumentation` | 字串 | 否 | 傳回值文件。 |
| `annotations` | 物件陣列 | 否 | 函式上的註解實例。 |
| `paramAnnotations` | 物件陣列的陣列 | 否 | 參數註解；外層和 `paramTypes` 使用相同的參數位置。 |
| `returnHostTypeName` | 字串 | 否 | 傳回值為 `object`、`enum` 或 `function` 時對應的宿主類型名稱或簽章。 |
| `paramHostTypeNames` | 字串陣列 | 否 | 與 `paramTypes` 平行的宿主類型名稱。 |
| `returnElementTypeName` | 字串 | 否 | 一元容器傳回值的元素類型。 |
| `paramElementTypeNames` | 字串陣列 | 否 | 一元容器參數的元素類型。 |
| `returnKeyTypeName` | 字串 | 否 | 二元映射回傳值的鍵類型。 |
| `returnValueTypeName` | 字串 | 否 | 二元映射回傳值的值類型。 |
| `paramKeyTypeNames` | 字串陣列 | 否 | 二元映射參數的鍵類型。 |
| `paramValueTypeNames` | 字串陣列 | 否 | 二元映射參數的值類型。 |
| `paramFlags` | 整數陣列 | 否 | 參數傳遞方式；省略時等同於所有參數為值傳遞。 |
| `typeParams` | 字串陣列 | 否 | 泛型宿主函式的型別參數。 |
| `scriptReturnTypeName` | 字串 | 否 | 泛型呼叫在腳本側使用的回傳類型名。 |
| `genericHostObjectCast` | 布林值 | 否 | 是否啟用泛型宿主物件轉換。僅在對應的泛型註冊場景使用。 |

所有「平行陣列」都必須使用相同的參數位置：例如 `paramHostTypeNames[1]` 描述 `paramTypes[1]`，`paramFlags[1]` 也描述同一個參數。陣列長度或位置不一致時，應以 CLI 校驗器報告為準並修正目錄。

### 類型名稱與容器元數據

`returnType` 和 `paramTypes` 使用宿主綁定支援的型別名稱。常用名稱及別名如下：

| 類型類別 | 可用名稱 |
| --- | --- |
| 基本類型 | `void`、`bool`、`char`、`short`、`int`、`long`、`float`、`double`、`string`、`wstring`、`unsigned`、`unsigned int`、`unsigned char`、`unsigned short`、`unsigned long`、`int8`、`uint8`、`int16`、`uint16`、`int32`、`uint32`、`int64`、`uint64`、`enum`、`function`、`fn` |
| 宿主物件 | `object`、`HostObject` |
| 一元容器 | `array`、`DynamicArray`、`set`、`DynamicSet`、`multiset`、`MultiSet`、`ordered_multiset` 、`OrderedMultiSet`、`queue`、`stack`、`deque`、`channel`、`priority_queue`、`PriorityQueue` |
| 二元映射 | `map`、`DynamicMap`、`ordered_map`、`OrderedMap`、`multimap`、`MultiMap`、`ordered_multimap`、`OrderedMultiMap` |

容器的元素、鍵和值不直接寫入 `returnType` 或 `paramTypes`，而是寫入對應元資料：

| 類型 | 傳回值欄位 | 參數欄位 |
| --- | --- | --- |
| `array`、集合、佇列、堆疊、雙端佇列、`channel`、優先權佇列 | `returnElementTypeName` | `paramElementTypeNames` |
| `map`、`ordered_map`、`multimap`、`ordered_multimap` | `returnKeyTypeName`、`returnValueTypeName` | `paramKeyTypeNames`、`paramValueTypeNames` |
| `object`、`enum`、`function` | `returnHostTypeName` | `paramHostTypeNames` |

例如，一個傳回 `Widget` 宿主物件、接受 `array<int>` 和 `map<string,double>` 的函式，可以這樣描述相關類型：

```json
{
	"name": "inspect_widget",
	"returnType": "object",
	"paramTypes": ["array", "map"],
	"returnHostTypeName": "Widget",
	"paramElementTypeNames": ["int", ""],
	"paramKeyTypeNames": ["", "string"],
	"paramValueTypeNames": ["", "double"]
}
```

### 參數傳遞標誌

`paramFlags` 的每個元素是位元標誌：

| 值 | 意義 |
| --- | --- |
| `0` | 以值傳遞。 |
| `1`（`0x01`） | 非 `const` 引用。 |
| `3`（`0x01 | 0x02`） | `const` 引用。 |

其他位元組合無效；`const` 位元必須與引用位元同時出現。引用標誌仍必須與實際註冊函式的簽名一致，目錄不會把一個按值的 C++ 函式變成引用函式。

## 宿主類型與成員

`types` 中的物件至少需要 `name` 和 `kind`。 `kind` 只能是 `enum`、`struct` 或 `class`：

| 欄位 | 類型 | 說明 |
| --- | --- | --- |
| `name` | 字串 | 腳本可見的型別名稱。 |
| `kind` | 字串 | `enum`、`struct` 或 `class`。 |
| `documentation` | 字串 | 型別文件。 |
| `annotations` | 物件陣列 | 類型上的註解實例。 |
| `inheritable` | 布林值 | 類型是否允許派生。 |
| `scopedEnum` | 布林值 | 枚舉是否使用限定存取語意。 |
| `hasClone` | 布林值 | 是否提供複製行為。 |
| `hasEquals` | 布林值 | 是否提供相等比較。 |
| `hasHash` | 布林值 | 是否提供雜湊行為。 |
| `hasSubclass` | 布林值 | 是否提供宿主子類別建立支援。 |
| `genericTemplateName` | 字串 | 實例化自哪個泛型宿主模板。 |
| `genericTypeArguments` | 字串陣列 | 泛型模板的實際類型參數。 |
| `members` | 物件陣列 | 建構子、方法、屬性、欄位或枚舉成員。 |

成員物件至少需要 `kind`；普通成員通常還需要 `name`。支援的成員種類為：

| `kind` | 說明 |
| --- | --- |
| `constructor` | 建構子；名稱可以為空字串。 |
| `method` | 實例方法。 |
| `staticMethod` | 靜態方法。 |
| `propertyGetter` | 屬性讀取器。 |
| `propertySetter` | 屬性寫入器。 |
| `enumMember` | 枚舉成員。 |
| `field` | 實例欄位。 |
| `staticConstant` | 靜態常數。 |

成員可以使用函式描述中的檔案、註解、參數、傳回值和類型元資料欄位，並額外支援：

| 欄位 | 類型 | 說明 |
| --- | --- | --- |
| `enumValue` | 整數 | 枚舉值；必須是精確的 JSON 整數，且在安全的 JSON 數值範圍內。 |
| `enumValueInt64` | 字串 | 完整的有符號 64 位元枚舉值；需要表示大於 JSON 安全整數範圍的值時使用。 |
| `operator` | 布林值 | 是否為運算子成員。 |
| `constMethod` | 布林值 | 是否為 `const` 方法。 |
| `mutableThis` | 布林值 | 方法是否允許修改目前物件。 |
| `nativeName` | 字串 | 宿主側的原生名稱；腳本可見名稱由 `name` 決定。 |

成員的 `returnType`、`paramTypes`、`paramFlags` 與函式描述具有相同意義。枚舉成員應使用 `enumMember`；對於超出 JSON 精確整數範圍的值，不要把近似的 JSON number 寫入 `enumValue`，應寫入十進位字串 `enumValueInt64`。

## 泛型模板與型別別名

### `genericTypeTemplates`

每個泛型模板物件包含：

| 欄位 | 類型 | 必要 | 說明 |
| --- | --- | --- | --- |
| `name` | 字串 | 是 | 模板名稱。 |
| `arity` | 整數 `1`–`255` | 是 | 類型參數個數。 |
| `documentation` | 字串 | 否 | 範本檔案。 |

### `typeAliases`

每個別名物件包含：

| 欄位 | 類型 | 必要 | 說明 |
| --- | --- | --- | --- |
| `name` | 字串 | 是 | 別名名稱。 |
| `typeParams` | 字串陣列 | 是 | 別名的型別參數，可以為空陣列。 |
| `targetTypeName` | 字串 | 是 | 別名指向的目標類型名稱。 |
| `documentation` | 字串 | 否 | 別名文件。 |

例如：

```json
{
	"genericTypeTemplates": [
		{ "name": "Box", "arity": 1, "documentation": "A generic host box." }
	],
	"typeAliases": [
		{
			"name": "WidgetId",
			"typeParams": [],
			"targetTypeName": "int",
			"documentation": "The identifier used by the host."
		}
	]
}
```

## 註記詞彙表與註解實例

頂層 `annotations` 是註記詞彙表。每個條目支援：

| 欄位 | 類型 | 說明 |
| --- | --- | --- |
| `name` | 字串 | 註記名稱。 |
| `detail` | 字串 | 簡短類別或簽章說明。 |
| `documentation` | 字串 | 註解文件。 |
| `arguments` | 物件陣列 | 註記參數說明。 |
| `caseSensitive` | 布林值 | 預設為 `true`；`false` 表示此定義的名稱忽略 ASCII 字母大小寫。 |
| `targets` | 整數 | 目標位元遮罩，預設為 `131071`（所有支援的目標），有效值為 `1` 到 `131071`。 |

名稱保留原始碼中的拼寫；接受的拼寫範圍相交時，註冊會被拒絕。參數名稱與中繼資料鍵仍區分大小寫。每個參數描述還可包含同形的遞迴 `children` 陣列，用於描述巢狀參數，不是可執行的值。原生註冊使用 `FHostBindingRegistry::RegisterAnnotation(..., Targets, CaseSensitive)`。目錄中繼資料不會安裝處理器回呼。

詞彙表中的 `arguments` 物件支援 `name`、`detail` 和 `documentation`。詞彙表用於描述可用的註記名稱，不會自動把註解應用到函式或類型。

函式、類型和成員上的 `annotations` 是註解實例，格式為：

```json
{
	"name": "HOST_FUNCTION",
	"arguments": [
		{ "name": "Category", "value": "Examples|Math" },
		{ "name": "ReadOnly" }
	]
}
```

註記參數物件中的 `value` 可以省略。省略表示無值的標誌參數；寫成 `"value": ""` 則表示參數明確帶有空字串值。這兩個含義不同。 `paramAnnotations` 是參數註解實例的二維陣列，外層位置必須與 `paramTypes` 一致。

從 Resources 整合文件取得[繫結目錄範例](../../../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json)，將副本儲存至可寫工作目錄，命名為 `bindings.sample.json`。它示範自訂註解中繼資料及宿主型別，不應假定功能 SDK 歸檔包含該範例。下列命令從該工作目錄執行，並將 `app.vs` 替換為自己的腳本。

## 校驗、格式化與載入

CLI 提供目錄維護指令：

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

省略 `-o` 或 `--output` 時，`bindings format` 會重寫輸入目錄檔案；指定輸出路徑時，改為寫入該檔案。標準輸出只包含寫入確認，不是格式化後的 JSON；請從輸出檔案讀取結果。

`validate` 檢查 JSON 類型、必要欄位、可用枚舉值、平行陣列和數值範圍；`format` 以工具鏈的穩定格式輸出 JSON。格式化到新檔案時，建議先使用臨時路徑並檢查結果，再取代原始檔案。

檢查或執行腳本時，可以重複使用 `--binding-catalog`：

```powershell
veroscript --binding-catalog bindings.sample.json app.vs
```

載入的目錄提供型別可見性和元資料。透過 CLI 匯入的函式和成員描述用於編譯期目錄資訊，不能替代執行階段回呼；實際宿主未註冊對應實作時，腳本仍可能在執行階段失敗。

匯入器也會拒絕超出安全邊界的 JSON：檔案大小上限為 `128 MiB`，JSON 值數量上限為 `4,194,304`，巢狀深度上限為 `256`。這些限制用於防止目錄檔案在校驗階段消耗過多資源。

## 在 C++ 宿主中註冊

嵌入式宿主通常透過 `VeroScriptSession` 完成綁定。公開註冊邊界包括：

- `RegisterNative`：註冊簡單原生函式，或使用參數描述註冊結構化函式。
- `RegisterHostType`：註冊宿主類別、結構體或枚舉。
- `RegisterHostMember`：註冊建構子、方法、靜態成員、屬性或欄位。
- `RegisterHostOperator`：註冊宿主類型的運算子。
- `RegisterHostTypeClone`：提供宿主類型的複製行為。
- `SetHostTypeInheritable` 與 `SetHostEnumScoped`：設定類型和枚舉語意。
- `RegisterHostTypeSubclassFactory`：為可派生宿主類型註冊實例建立方式。
- `RegisterGenericHostTypeTemplate` 與 `InstantiateGenericHostType`：註冊並實例化泛型宿主類型範本。
- `GetBindingCatalog`：讀取目前會話可匯出的綁定目錄。

宿主註冊完成後，應匯出目錄並執行 CLI 校驗，使檔案、靜態檢查和執行階段註冊保持一致。靜態宿主成員通常透過 `Type.staticMethod(...)` 存取；宿主常數使用 `Type::Constant` 形式。更完整的引擎生命週期、`CheckSource`/`RunSource` 以及 `CheckFile`/`RunFile` 用法，請參閱[宿主整合](embedding.md)。

## 可為 null 的宿主物件參數

明確參數為 `HostObject` 的靜態宿主成員可以接收腳本的 `nullptr`。回呼會收到空的
宿主物件控制代碼，解除參考載重前必須先檢查。空的執行個體接收者仍然無效；此規則
只適用於靜態呼叫的明確參數。

## 相容性原則

綁定目錄屬於公開介面的一部分。新增函式或成員通常是相容擴充；刪除條目、改變參數類型、改變引用標誌、改變枚舉值、改變宿主類型名稱或改變成員可見性，都可能破壞腳本。升級目錄或宿主綁定時，應同時執行目錄校驗、腳本檢查和目標項目的整合測試。

## 宿主多載

<a id="host-overload-resolution"></a>

一般宿主實例方法、靜態方法與原生自由函式支援依正規化參數型別及參考限定多載。註冊時拒絕重複參數簽章及僅回傳型別不同的宣告。呼叫依實參型別、數值轉換及 variant 後備規則選擇；同等候選會報告歧義。具名宿主成員使用由簽章產生的穩定原生識別。建構函式及運算子保留既有協定。僅依名稱的 `FindFunction` 對多載集合回傳空值；使用 `GetFunctions()` 列舉各宣告及其獨立文件。繫結目錄匯入匯出會保留全部多載。現有單一簽章註冊繼續使用相同公開介面。

省略 `ref` 時保留原生介面既有的值輸入／控制代碼行為；變數儲存槽回寫仍需顯式 `ref`。一般實參優先選擇可用的值簽章。顯式參考會檢查儲存型別與可寫性，`const ref` 可接收 const 變數。編譯擴充會保留全部多載。VM、Mixed、JIT 與 Core AOT 使用核心選擇結果；獨立 C++ 原始碼匯出保留個別的 C 連結橋接契約，遇到不支援的具名宿主多載時會明確提示使用 Core AOT。

## 原生回呼中繼資料

IScriptCallInvoker::GetActiveModule() 僅於目前原生回呼期間提供借用的唯讀語意中繼資料；執行主程式未提供時可回傳 nullptr。回呼返回後不得保留該指標、借用的呼叫服務或中繼資料內部參照；保留值或程式擁有者也不會延長此回呼借用契約。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/bindings.md) · [fr-FR](../fr-FR/bindings.md) · [ja-JP](../ja-JP/bindings.md) · [ko-KR](../ko-KR/bindings.md) · [zh-CN](../zh-CN/bindings.md) · [zh-TW](bindings.md)
