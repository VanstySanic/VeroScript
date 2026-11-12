# 屬性與方法函式值

`attribute.vs` · `callable.vs` · `core.vs`

`std::attribute<T>` 保存固定值或同步 getter。副本具有獨立設定；函式捕獲沿用語言原有的所有權。每次讀取重新求值，不自動快取。

```VeroScript
import "attribute.vs";

int read_count()
{
	return 42;
}
void main()
{
	std::attribute<int> count = 10;
	count.bind(read_count);
	int snapshot = count;
	assert(snapshot == 42 && count() == 42);
	count.reset();
	assert(!count.try_get().has_value());
}
```

`get()`、`operator()` 和轉換到 `T` 都讀取一次。`try_get()` 回傳 optional；`get_or(fallback)` 的備用引數提前求值。未綁定函式與執行錯誤正常傳播；只有未設定、可選 reader 回傳空、弱目標過期才表示無值。

`set(value)`、`bind(fn()->T)`、`bind_optional(fn()->optional<T>)` 和 `reset()` 整體替換設定。`is_set()`、`is_bound()` 只檢查設定，不執行 getter。

`try_get(ref output)` 向既有變數讀取一次，傳回是否取得值。屬性未設定或 optional reader 傳回空值時，`output` 保持不變；getter 例外仍會傳播。需要寫入既有變數時可使用此多載，傳回 optional 的原有 `try_get()` 仍可使用。

`has_same_configuration(ref other)` 不執行 getter，僅比較設定身分。複製後起初共用該身分；取代任一設定後，即使值相等，設定仍不同。繫結結果可能在設定不變時改變，因此不能藉此略過動態 getter。

`std::make_attribute_by_name<T>(target, "read")` 建立強物件的無參數 getter。`T` 明確指定，目標型別自動推導，也可作為第二個型別引數指定。名稱區分大小寫，建立時解析，呼叫保留正常存取與虛擬分派規則。

`std::make_weak_attribute<T>(weak_target, getter)` 每次讀取鎖定目標一次，getter 簽名為 `fn(U)->T`。`std::make_weak_attribute_by_name<T>(weak_target, "read")` 依名稱綁定無參數方法。弱目標過期後仍處於已設定狀態。結構使用 `shared<S>` 或 `handle<S>` 包裝。

`std::bind_method<F>(target, "method")` 回傳普通的型別化函式值。目標必須具有簽名完全相符的 public 執行個體方法，保留虛擬分派並強持有目標。結果可傳給 `attribute.bind`、`delegate.bind` 或 `multicast_delegate.add`；移除時使用實際加入的函式值。只需要此工廠時可匯入 `callable.vs`。

獨立 AOT：C++ 支援固定值、getter 和弱目標；C#、Python 支援固定值、具名 getter 和常量強方法名稱。Python 原始碼 Lambda、C#/Python 弱綁定維持原有的不支援界線。動態名稱、宿主名稱綁定及有歧義的泛型選擇需要執行階段，匯出時會明確拒絕。VM、Mixed、JIT 共用核心實作。

C++ 與 C# 原始碼匯出支援 `try_get()` 及 `try_get(ref output)`。Python 支援 `try_get()`，但拒絕可寫入的 `ref` 輸出。三者皆支援唯讀 `has_same_configuration(ref other)`；此特殊轉換不會為 Python 增加一般可寫入參照支援。

[`attribute_demo.vs`](../../../../Scripts/examples/attribute_demo.vs) · [`attribute_functions.vs`](../../../../Scripts/examples/attribute_functions.vs) · [`attribute_methods.vs`](../../../../Scripts/examples/attribute_methods.vs) · [`attribute_weak.vs`](../../../../Scripts/examples/attribute_weak.vs)

同一程式中的標準庫應維持一個模組身分。程式庫套件已包含並重新匯出 attribute 時，請使用套件的宣告或統一的共用相依項目；另行匯入來源版 `core.vs`／`attribute.vs` 會造成同名匯出衝突。

多個程式庫套件共用時，先用 `export import "attribute.vs";` 建立一份 `shared-stdlib.vslib`，各庫匯入該元件；應用匯入各庫和相同元件，避免額外匯入原始碼副本。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/attribute.md) · [fr-FR](../../fr-FR/standard-library/attribute.md) · [ja-JP](../../ja-JP/standard-library/attribute.md) · [ko-KR](../../ko-KR/standard-library/attribute.md) · [zh-CN](../../zh-CN/standard-library/attribute.md) · [zh-TW](attribute.md)
