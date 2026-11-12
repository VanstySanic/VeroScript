# 腳本物件對映

json::converter<T> 在活動 VM 中轉換腳本物件並產生輸入/輸出 Schema。登記原生擴充後，將 bindings、processors、dependencies、Owners 一起放入模組環境，專案 owner 宣告 json 提供者相依後再編譯。單獨繫結目錄不會安裝處理器；原生 C++ 對映獨立。

write(ref value) 明確接受 const ref。write_with_options/read_with_options/try_write/try_read 及選項變體遇一般錯誤傳回 false、json::error、保留原輸出，成功清除錯誤。取消、逾時及不可恢復 VM 錯誤仍結束執行。

bool/整數/有限浮點/string 保持 JSON 型別和範圍，整數不經 double。列舉預設成員名稱，可用 JsonName。array/deque/queue/stack/priority_queue 使用 values() 順序，stack 由底至頂。set 拒絕重複，multiset 保留數量。string 鍵 map/ordered_map 為物件，其他 map 與全部 multimap 需要 key_value_pairs。公開 struct/class 執行個體欄位可轉換，class 可 null，json::value 保留原值。雜湊順序依容器契約；要求插入順序使用 ordered。特殊宿主型別/函式須明確轉換器。

JsonName(value) 改名、JsonIgnore 排除入出、JsonRequired 要求存在（null 由型別決定）、JsonSerializeOnly 禁入、JsonDeserializeOnly 不輸出。JsonProperty(name,setter) 明確加入無參數 const getter，無 setter 則只輸出。JsonFactory(type) 呼叫 create(value,context)，JsonConverter(type) 選配接器。包含可存取的繼承公開欄位，排除靜態/不可存取欄位，const 僅輸出。缺少非必填欄位維持正常建構預設值。重名/規則衝突失敗，未知欄位除 set_ignore_unknown(true) 外拒絕。

mapping_options 提供 ignore_unknown/numeric_enums/key_value_pairs/exact_floating 及 setter、set_limits(depth,nodes)，深度為 1–256，節點須正。設定按值複製，每次轉換有獨立 context。instance_pointer 使用 JSON Pointer 跳脫。class 循環拒絕，不同分支共享按值重複。解碼成功才提交新值，但工廠/屬性/轉換器外部副作用不回復。

配接器提供靜態 encode(const Target&,json::value&,mapping_context)、decode(json::value,Target&,mapping_context)、schema(mapping_context)。custom_converter<Target,Adapter> 還須 create(value,context) -> Target。無預設建構 struct 須工廠/create，class 可從 null 開始。context.fail/fail_range/adopt(error) 提交診斷，無診斷 false 成為 Callback。巢狀 converter::encode/decode/schema_nested 共用 context，自訂 Schema 須描述實際轉換。

schema() 產生輸入、schema_with_options(options,false) 產生輸出 Schema，方向分別決定必填/忽略/未知/禁入欄位、整數範圍、元素、列舉與可空性。遞迴使用 $defs，不執行建構子。schema_nested 快取按型別及方向區別，切換 set_schema_input 不會沿用錯誤方向定義。

自動產生針對模組/命名空間直接宣告的型別；區域/巢狀資料型別不產生模組輔助函式，其 JsonFactory/JsonConverter 回報範圍限制，請在可見合法範圍用 custom_converter。JsonPolymorphic 在非範本表類別自身產生靜態方法，故可區域/巢狀。仍遵守所有權、可見性、建構及存取規則。欄位/別名/基底/配接器在宣告模組解析，依 import/re-export、範本數量/預設及模組優先；型別/值參數（整數/bool/列舉）和限定 constexpr 保持。不同封閉型別的 Schema 快取獨立，static constexpr 欄位仍排除。

使用 JSON 處理器建置的 .vslib 保留產生方法；一般程式庫的可存取公開欄位可由使用端產生對映，但建構/存取規則不放寬。處理器操作數、產生原始碼位元組與工作預算仍適用。AOT/其他宿主須確認實際支援途徑，不能以 VM 覆蓋代表所有平台。

```cpp
Description.Bindings = Registry.GetBindings();
Description.CompileProcessing.Registry = Registry.GetProcessors();
Description.AvailableDependencies = Registry.GetDependencies();
Description.Owners = Registry.GetOwners();
```

```VeroScript
struct Record
{
public:
	@JsonName(value="record_id") @JsonRequired int64 id = 7;
	string name = "default";
	@JsonIgnore int transient = 0;
};

int main()
{
	Record source = Record();
	json::value document = json::converter<Record>::write(ref source);
	Record result = json::converter<Record>::read(document);
	json::schema shape = json::compile_schema(json::converter<Record>::schema());
	assert(result.id == 7 && shape.is_valid(document));
	return 0;
}
```

```cpp
static bool encode(const Target& input, json::value& output, json::mapping_context context);
static bool decode(json::value input, Target& output, json::mapping_context context);
static json::value schema(json::mapping_context context);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/script-mapping.md) · [fr-FR](../../fr-FR/Reference/script-mapping.md) · [ja-JP](../../ja-JP/Reference/script-mapping.md) · [ko-KR](../../ko-KR/Reference/script-mapping.md) · [zh-CN](../../zh-CN/Reference/script-mapping.md) · [zh-TW](script-mapping.md)
