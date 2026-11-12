# variant — 動態值盒

variant 是核心型別，不需 import。std::variant 和 variant 指向相同型別。它保存一個帶正規來源型別的值，或處於空狀態。

明確的 variant 目標允許具體值隱式裝箱。讀取必須顯式 cast<T> 或 try_cast<T>；variant_is<T> 精確檢查儲存型別，不進行轉換。`optional<T>` 和 `try_cast` 同樣由編譯器提供，無須匯入標準函式庫。空盒的 `cast<T>` 報錯；空盒或一般轉換不符的 `try_cast<T>` 傳回空 optional。複製失敗、取消和限額仍為錯誤。

複製 variant 共用不可變盒。腳本 struct 在裝箱和提取時按值快照；class 和容器保留原有參考語意。保留靜態宣告型別，包括基底型別和具型別的 null。後者仍為有值，與空盒不同；裸 nullptr 必須先存入明確型別的變數。

僅定義 variant 之間的相等比較：正規型別和載荷原有相等規則皆須符合。不提供動態算術、成員查找、呼叫、隱式拆箱、排序或雜湊。禁止 variant 鍵、channel 傳送及含 variant 的 worker 參數、結果、擷取或全域狀態；worker 內部區域使用允許。原生陣列及一般 array/map 值可以包含 variant。

```vero
import "core.vs";
variant empty;
variant answer = 42;
assert(!variant_has_value(empty));
assert(variant_is<int>(answer));
int exact = cast<int>(answer);
optional<double> converted = try_cast<double>(answer);
map<string, variant> properties = map<string, variant>();
properties["width"] = answer;
properties["text"] = "Save";
assert(cast<int>(properties["width"]) == exact);
```

VM 完整支援語意。Mixed／asmjit 對不支援的 variant 指令安全回退 VM。可攜位元組碼和套件保留宣告與指令。獨立 C++、C#、Python AOT 在產生輸出前明確拒絕 variant 相依。偵錯器提供唯讀 $present、$type、$value 子項；type<variant>().kind() 傳回 "variant"。

## SDK

請參閱[Interop SDK — variant](../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#exported-value-lifetime)。

匯出物件跨宿主呼叫存活，且方法或解構需要腳本全域狀態時，使用既有持久化工作階段模式：C++ SetHotReloadEnabled(true)、C# HotReloadEnabled 或 Python hot_reload_enabled=True。一般暫時執行在呼叫結束時釋放全域狀態。

```text
C:      vs_interop_value_set_variant_empty / vs_interop_value_set_variant
        vs_interop_variant_has_value / vs_interop_variant_get_type_name / vs_interop_variant_get
C#:     value.SetVariantEmpty(); session.CreateVariant(typeName, payload);
        value.VariantHasValue; value.VariantTypeName; value.GetVariantValue();
Python: Value.empty_variant(); session.variant(type_name, payload_value);
        value.set_variant_empty(); value.assign_variant(other_box);
        value.variant_has_value(); value.variant_type_name(); value.variant_get();
C++:    VeroScriptVariant.h, VeroScriptValueRoot.h
        session.MakeVariant(typeName, payload, output, error);
        session.RetainValue(output, root, error); root.Read(); root.Reset();
```

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/variant.md) · [fr-FR](../../fr-FR/standard-library/variant.md) · [ja-JP](../../ja-JP/standard-library/variant.md) · [ko-KR](../../ko-KR/standard-library/variant.md) · [zh-CN](../../zh-CN/standard-library/variant.md) · [zh-TW](variant.md)
