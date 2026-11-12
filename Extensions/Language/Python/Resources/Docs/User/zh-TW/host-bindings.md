# Python 宿主繫結

持有宿主物件的獨立 `Value` 及其子型別檢查，在建立它的 `Session` 關閉後仍可安全使用。型別查詢使用 Python 類別的弱參照。帶 clone／equals／hash 協定的值會保留呼叫這些協定所需的回呼狀態，因此仍可用作 map/set 鍵；最後一個相關值釋放後，該狀態也會釋放。不帶這些協定的值不會額外保留無關類別或工作階段回呼狀態。

`HostClass` 的公開方法為 `constructor(...)`、`method(...)`、
`static_method(...)`、`property(...)` 與 `field(...)`，用來明確描述暴露給
VeroScript 的成員。

宿主繫結將 Python 函式、類別和序列化器以明確的 VeroScript 型別契約公開給腳本。`HostParameter` 描述 `type_name`、參數名稱、`by_ref`、`const_ref` 和說明文字。

```Python
from veroscript import HostParameter

session.register_function(
	"Add",
	add,
	parameters=(
		HostParameter("int", "left"),
		HostParameter("int", "right"),
	),
	return_type="int",
)
```

`register_function` 和 `host_function` 的契約必須與實際回呼簽章一致。`HostClass` 可宣告建構函式、實例/靜態方法與屬性；`construct`、`call_method`、`get_field`、`set_field` 只操作已註冊成員。

`register_serializer`、`serialize_object` 和 `deserialize_object` 形成資料邊界，應驗證型別與版本。`set_mutation_callback` 接收 `MutationKind` 事件；回呼不應修改正在列舉的物件或銷毀工作階段。通知無法回復已完成的變更。回呼例外會轉換為 `CallbackError`。

## 宿主型別、中繼資料與攔截

宿主成員必須使用明確簽章。簽章可以使用純量、已註冊的宿主物件、
`enum<Name>` 以及 `array<int>`、`map<string,PythonObject>` 等巢狀容器；
非純量回呼值使用 `Value` 表示。宿主類別成員目前使用傳值參數；自由函式
另外支援可變 `ref` 與唯讀 `const_ref` 參數。

`field(...)` 預設提供即時 getter 與可寫 setter；傳入 `writable=False` 可設為唯讀。
`register_struct(...)` 用於註冊值語意宿主型別；如果 VeroScript 可能複製該值，
應透過 `protocol(...)` 提供 clone；將其用作 map/set 索引時還需要 equals 與 hash。
`register_enum(...)` 要求 Python 型別繼承 `IntEnum` 或 `IntFlag`；一般的
`enum.Enum` 會被拒絕。以
`register_generic_class(...)` 或 `register_generic_struct(...)` 註冊的泛型樣板，
必須在載入相依腳本前使用 `instantiate(...)` 完成具體化。
對於允許繼承的宿主類別，應在 `register_class(...)` 中傳入 `inheritable=True`，並透過
`protocol(subclass_factory=...)` 提供腳本子類別建立受管理實例所需的工廠。

註記名稱預設區分大小寫。使用
`register_annotation("@GuiWidget", case_sensitive=False)` 後，處理器訂閱與腳本反射
會忽略該註記名稱的 ASCII 大小寫，並保留原始碼中的拼寫。參數名稱與中繼資料索引鍵
仍採精確比對。名稱規則衝突會拋出 `Error`，既有註冊保持不變。此註冊不會安裝編譯期處理器。

`register_annotation(...)` 註冊註記詞彙及其參數，`register_type_alias(...)` 註冊
具體或泛型型別別名。`set_instance_call_interceptor(...)` 接收已解析的
`InterceptContext`：回傳 `InterceptResult.UNHANDLED` 使用正常實作，最多呼叫一次
`context.proceed()` 後回傳 `InterceptResult.HANDLED`，也可以回傳 `(InterceptResult.HANDLED, replacement)`；
`InterceptResult.FAILED` 表示呼叫失敗。回呼結束後 context 立即失效；可信任的宿主呼叫可以使用
`call_method_bypassing_interceptor(...)` 繞過攔截器。

如果簽章使用已註冊的宿主基底類別，可以傳入已註冊且相容的子型別實例。執行階段
依據已註冊的型別關係判斷相容性，不會從任意 Python 類別自動推斷。

## 回呼限制

工作階段會保留已註冊的回呼，直到工作階段關閉。回呼例外會轉換為 `CallbackError`，不會穿越原生 ABI。腳本回呼可能在 VM 工作執行緒執行；請同步共用的 Python 狀態。工作執行緒上的回呼不可呼叫限制於建立執行緒的 `Session` 或保留 callable 方法。載入來源前必須具體化泛型宿主型別；宿主 struct 用於複製或作為索引時，必須提供所需 protocol。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](host-bindings.md)
