# C++ 型別對映

包含 VeroScriptJsonMapping.h 並連結 VeroScript::Json，不需要 Core。以 TObjectMapping<T> 明確登記欄位/屬性，再呼叫 TryEncode/TryDecode，僅成功時取代輸出。解碼先備妥轉換，再執行正常建構與賦值；無預設建構子可提供 shared_ptr<T> 工廠。建構、屬性及轉換器的外部副作用不回復。

名稱允許合法 UTF-8 與 NUL，路徑採 RFC 6901 跳脫。未知欄位預設拒絕，除非 bIgnoreUnknownFields。FFieldOptions 控制 required/ignore/serialize/deserialize；僅輸出欄位出現在輸入會失敗，ignore 則略過。重複或方向矛盾於登記時失敗。基底欄位須明確加入，靜態與屬性不自動列舉，僅執行已登記 Property。

bool、至多 64 位元整數、有限浮點與擁有的 UTF-8 字串保持 JSON 型別，整數不經 double。bExactFloatingPoint 拒絕捨入，預設允許受檢捨入。vector/list/deque/固定陣列對映為陣列且檢查固定長度。set 拒絕重複，multiset 保留，順序依容器契約。唯一 string 鍵 map 預設為物件，其他 map/multimap 須 bMapAsPairs。optional/shared_ptr/unique_ptr 接受 null，普通值不接受。RegisterEnum 預設登記名稱，bEnumsAsNumbers 使用整數但仍拒絕未登記值。未支援型別不以空物件代替。

RegisterConverter<T> 提供型別化 encode/decode/Schema。遞迴 FMappingContext 共用路徑、預算與取消。每次呼叫取得不變登錄快照；登錄複本共享登記狀態，但替換不影響活動呼叫。Owner 須涵蓋程式碼、解構與 RTTI。傳回及巢狀 shared_ptr 含複本/別名保有本次轉換器集合，登錄或外層結果釋放後仍有效；按值移出或使用別的控制區塊須自行保有 Owner。首次型別鍵的 RTTI Owner 保持至登錄及快照釋放。回呼/工廠先於 Owner、在鎖外解構，插入失敗亦同。

目前路徑上的循環回報 json.cycle，不同分支共用則重複按值輸出，不恢復圖形身分。TryMappingSchema<T>(Input/Output,...) 共用欄位方向、$defs/$ref、整數範圍、長度與列舉。自訂 Schema 缺失會失敗。建構不變量、自訂比較、鍵值對鍵唯一性與精確浮點仍由解碼器檢查。FMappingOptions::Limits 及借用 FWork 限制緊湊 JSON 位元組、深度、節點、文字與數字，保留首份錯誤/路徑；取消不提交輸出。

```cpp
#include <VeroScriptJsonMapping.h>
using namespace VeroScript::Json;

struct Settings
{
	std::string Name;
	int Port = 8080;
};
FMappingRegistry registry;
FError error;
FFieldOptions required;
required.bRequired = true;
bool registered = TObjectMapping<Settings>("app::Settings")
	.Field("name", &Settings::Name, required)
	.Field("port", &Settings::Port)
	.Register(registry, error);

FJsonValue input;
std::shared_ptr<Settings> settings;
bool decoded = registered && TryParse("{\"name\":\"Vero\"}", input, error)
	&& TryDecode(input, settings, error, registry);
// Successful decoding preserves the constructed default port, 8080.
```

```cpp
FJsonValue input_schema, output_schema;
bool ok = TryMappingSchema<Settings>(EMappingDirection::Input, input_schema, error, registry)
	&& TryMappingSchema<Settings>(EMappingDirection::Output, output_schema, error, registry);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/native-mapping.md) · [fr-FR](../../fr-FR/Reference/native-mapping.md) · [ja-JP](../../ja-JP/Reference/native-mapping.md) · [ko-KR](../../ko-KR/Reference/native-mapping.md) · [zh-CN](../../zh-CN/Reference/native-mapping.md) · [zh-TW](native-mapping.md)
