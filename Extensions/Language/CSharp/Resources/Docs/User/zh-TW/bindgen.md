# C# BindGen

`VeroScriptCSharpBindGen` 使用 C# 語意模型，產生 Binding Catalog v1 中繼資料。也可按需產生供 `VeroScriptCSharp` 使用的無反射註冊程式碼、API 參考及供產生的 C# 程式使用的 AOT 橋接程式。

## 選擇輸出

CLI 必須指定 `--catalog`。`--registration`、`--aot-bridge`、`--api-reference` 和 `--api-reference-dir` 選擇附加輸出；僅產生中繼資料時，省略註冊與橋接選項。Catalog 描述繫結契約，不會安裝可執行回呼。註冊程式碼與 `VeroScript.CSharp` 一起編譯；AOT 橋接程式與原始受控宣告及產生的 C# 程式一起編譯，透過直接呼叫運作，不依賴反射或 `VeroScriptInterop`。

程式庫 API 的預設值不同：`CSharpBindGenOptions.EmitRegistrationSource` 與 `EmitApiReference` 預設為 `true`，`EmitAotBridgeSource` 預設為 `false`。只需要中繼資料、不產生可執行配接器時，須明確設定 `EmitRegistrationSource=false` 和 `EmitAotBridgeSource=false`。附加配接器的限制不應自動當成對應 Catalog 中繼資料的限制。

## 共同輸入規則

預設考慮公開類別、結構、列舉及公開成員。`--require-attribute` 只選擇標記 `[VeroScriptBinding]` 的型別，`[VeroScriptIgnore]` 排除宣告。泛型宿主型別或成員、async、指標、nullable 或委派型別、選擇性或 `params` 參數、運算子、參考傳回與欄位、索引子、required 成員和無法表達的多載會被拒絕。抽象類別建構函式不匯出。附加輸出也各有獨立限制。

### 容器索引鍵

map 與 ordered-map 的 key，以及 set element，可以是 `sbyte`、`int`、`uint`、`long`、
`ulong`、`string` 或已宣告的 enum。`sbyte` 會映射為 VeroScript 的 signed 8-bit `char`；
其他 key shape 會在 Binding Catalog 到達 runtime importer 前安全失敗。

## 即時註冊程式碼的限制

註冊程式碼只支援單層 array/list、map、set、queue 和 stack，元素為純量、字串或已宣告列舉；map 索引鍵與 set 元素也須符合上述索引鍵規則。不支援巢狀容器或宿主物件元素。multi-map、multi-set、priority queue 和 deque 請使用 SDK 值 API 或明確宿主包裝器。

即時註冊對涉及容器的靜態欄位、屬性和方法傳回 `VSCB3004`；具有公開執行個體欄位、屬性或非建構方法的結構傳回 `VSCB3001`，即使這些成員唯讀也相同。這些是即時註冊產生器的限制；僅產生中繼資料與產生 AOT 橋接程式分別判斷。AOT 橋接程式對結構的公開執行個體方法、可寫執行個體欄位和一般公開 setter 傳回 `VSCB3006`，因為無法保留裝箱接收者的回寫語意，請參閱 [C# AOT](aot.md)。

## 單檔命令

```powershell
veroscript-csharp-bindgen HostApi.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference generated/Bindings.vs.txt
```

## 專案命令與輸出

```powershell
veroscript-csharp-bindgen Models/A.cs Models/B.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference-dir generated/api `
  --manifest generated/bindings.manifest.json
```

專案模式合併 partial 宣告、分配穩定參考名稱，在取代前統一準備全部選定輸出。產生失敗時保留既有輸出，寫入失敗時嘗試還原。`--reference` 可重複；`--language-version` 選擇 C# 語法版本，預設為 `latest`。`--api-reference` 與 `--api-reference-dir` 互斥。若要同時輸出 API 參考和 `--manifest`，使用 `--api-reference-dir`；單獨指定 `--manifest` 不要求輸出 API 參考。

## 執行邊界

即時註冊回呼可能在多個 VM 工作者上並行執行，共用宿主狀態必須同步。AOT 橋接程式直接呼叫受控宣告。請一起使用同次產生的 Catalog、橋接程式、原始宣告和程式。

## 產生容器的執行邊界

Binding Catalog metadata 可以描述 ordered map。live registration 對 `SortedDictionary<TKey, TValue>` 回報 `VSCB3002`，產生的 AOT bridge 回報 `VSCB3003`；執行整合請使用明確的 host wrapper。live container marshalling 建立 managed snapshot，只有 `ref`/`out` 參數在回呼成功後回寫；按值傳入容器的修改不會更新 script container。AOT bridge 直接傳遞支援的 managed container 並保留 object identity。

## 連結輸出路徑

Windows 的 `\\?\`、`\\.\` 和磁碟區 GUID 路徑別名會依最終檔案系統位置比較。這些別名同樣不能略過驗證；相異的輸出路徑仍受支援。

BindGen 與 Transpiler CLI 在暫存輸出前解析既有的目錄與檔案連結。symbolic link 或 Windows junction 路徑不能覆寫輸入原始碼，也不能重複指向同一輸出；連結目錄內相異的輸出路徑仍受支援。

抽象主機類別保留受支援的方法及工廠傳回的執行個體；其建構函式不會匯出至 Catalog、即時註冊或 AOT bridge。相依於外層泛型參數的巢狀型別會回報 `VSCB1001`，即使透過屬性單獨選取該型別也不會匯出無效程式碼。

AOT 的 `ref`、`out`、`in` 和 `ref readonly` 參數在基本型別及受支援容器的表示一致時直接共用原儲存位置，保留參數間的參考別名。字串、字元、列舉或主機物件等需要表示轉換的參考參數會回報 `VSCB3007`；可設定 `EmitRegistrationSource=false` 及 `EmitAotBridgeSource=false` 產生 Catalog，並提供明確的 partial bridge。

## 輸出檔名衝突

BindGen 的 API 參考檔名在所有主機上都會忽略大小寫。同名主幹會加上穩定的路徑雜湊，仍有衝突時再加上數字後綴。名稱按輸入路徑順序分配，因此重新排列輸入清單不會改變名稱。請透過傳回的 API 參考記錄或資訊清單定位產生的 `.api.vs.txt` 檔案。

<a id="binding-marker-attributes"></a>

篩選特性的定義由輸入宣告提供，SDK 不會注入這些型別。程式庫選項預設使用 `BindingAttributeName="VeroScriptBindingAttribute"` 和 `IgnoreAttributeName="VeroScriptIgnoreAttribute"`，也可設定等價名稱。產生的即時註冊型別預設為 `VeroScript.GeneratedBindings.VeroScriptCSharpBindings`，由 `RegistrationNamespace` 和 `RegistrationClassName` 調整；指定名稱不會啟用已關閉的註冊輸出。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](bindgen.md)
