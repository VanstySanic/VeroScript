# Interop value、callback、package

## value 與 container

ABI 公開 fixed-width scalar、string、enum、host object、array、map、ordered map、set、queue、stack、deque、channel。container enumeration callback 收到 borrowed value。型別必須符合 VeroScript 契約：元素值的型別驗證傳回 `TYPE_MISMATCH`，執行失敗可能傳回 `ENGINE_ERROR`，不執行隱含轉換。multiset 計數直接累加出現次數陣列的長度，不複製元素或配置遍歷快照。

物件欄位依據來源值所屬模組的中繼資料，接受可指派給所宣告基底類別或介面的衍生腳本物件。例如，繼承 `Base` 並實作 `IRead` 的 `Child` 可放入 `array<Base>` 或 `queue<IRead>`。容器型別宣告仍保持不變性；這不會將 `array<Child>` 轉換為 `array<Base>`，也不增加數值轉換。

map/set 的鍵型別宣告在容器為空時也會驗證，包括 multimap/multiset。不支援的內建型別（`bool`、`float`、`double`、8 位元無符號和 16 位元整數、容器、函式及參考）傳回 `INVALID_ARGUMENT`，不取代原值。基礎鍵型別為 `char`、`int`、`unsigned int`、`int64`、`uint64`、`string`、`wstring` 及其拼寫別名。命名 enum/host 型別（包括具現化的 host 型別）由使用該值的工作階段解析。

channel capacity 在 first use 前 configure。send、receive、try、timed、count、capacity、closed、close 有獨立 status behavior。closing 會 wake blocked operation；API 有提供時 error text 以 owned UTF-8 buffer 回傳。

## 回呼內容

native function、generic host-type factory、instance-call interceptor、mutation callback、external serializer/deserializer 經 context setter/callback result value report failure。callback exception 必須在經 C return 前 convert。user data、host-object payload、managed root 必須存活到每個可能 callback entry。

無效的 ApplyConfig 描述元在套用前拒絕。核心套用設定時發生配置記憶體或內部錯誤，可能留下部分已套用設定；依賴這些設定前，應重新套用完整設定。C 取消控制代碼在成功和失敗路徑均指向工作階段實際安裝的控制。Linux 記憶體配置故障迴歸涵蓋啟用／停用控制、取消一致性及重試復原。

external serialization 也能藉 ABI visitor contract enumerate reference。reference kind、visitor order、borrowed-value lifetime 是 machine-facing data；visitor 不可 retain borrowed value 或 reentrant mutate owning session。map-entry/set-element visitor 同樣遵守 borrowed-view rule，propagate callback failure，不能把 partial traversal 變成 success。

reentrant destruction/session clearing 不是 general callback feature。若 implementation reject，ABI 必須回傳 engine error 並保留 owning session，而非 partial clear。

## package 與 reflection

package API read/destroy package handle，公開 kind/name/version、metadata、canonical signed byte、load/reload/build operation。reflection export loaded type/field/method/global/enum/interface 的 JSON metadata。JSON field name/type name 是 machine contract，不可 localize。

`vs_interop_session_build_package_file` 以帶 `message_id` `diag.failed_to_build_package` 的 structured diagnostic report build failure。第一個 diagnostic argument 是 underlying build reason；consumer 應針對 selected locale 呼叫 `vs_interop_session_format_diagnostic`，不可將 `message` 當作 diagnostic contract 的替代。

## 通道

channel value 用 `vs_interop_value_create_channel` 或 `vs_interop_value_create_bounded_channel` 建立。zero bounded capacity 表示 unbounded channel。 `vs_interop_value_channel_send` block 到 accepted，`vs_interop_value_channel_try_send` 為 non-blocking，`vs_interop_value_channel_send_for` 套用 timeout。receive family 有對應 blocking/non-blocking/timed operation。count、capacity、closed-state、close 是 separate ABI call。closing 會 wake waiter；host 必須將 closed result 視為 terminal，且不可在 callback scope 後 reuse borrowed value。

## 泛型主控端型別內容

`vs_interop_generic_type_context_*` function 公開 generic host-type factory 的 template argument count/argument。factory 以同一 borrowed context 提供 result type、member、protocol declaration、error。context accessor/setter 只在 factory callback 中 valid。callback 必須在 return 前完成 registration；context 不是 retained builder object。

## call 與 interception

`vs_interop_intercept_context_*` 公開 receiver、resolved target、argument、`proceed`、callback error channel。`proceed` 是 already-resolved call 的 interceptor continuation。需要後續 inbound dispatch 的 host 可使用 bypass entry interceptor 的 session call API；method body 中 nested call 仍然 interceptable。

`vs_interop_call_context_*` 提供借用參數和可變參數存取、`host_this`、程式世代及錯誤通道。函式與 callable 呼叫助手遵循工作階段的重入和生命週期規則。保留 callable 的介面為 `vs_interop_session_retain_callable`、`vs_interop_callable_destroy`、`vs_interop_callable_is_valid` 和 `vs_interop_callable_invoke`。僅限擁有者執行緒呼叫與弱 `this` 擷取屬於生命週期策略，不提供跨執行緒同步。

## 外部序列化內容

serializer context 公開已註冊型別、mutation version 和借用欄位。serializer 透過 `vs_interop_external_serialize_context_set_bytes` 複製輸出位元組；替換失敗保留先前已接受的 payload。deserializer 讀取位元組和 encoded version，再將按名稱指定的欄位賦值加入暫存交易。兩者透過 error setter 與 callback result 回報失敗。reference enumeration 是獨立遍歷，不延長借用值的生命週期。adapter 必須複製在 callback 後仍需保留的位元組和字串。

## Annotation 名稱比對策略

`vs_interop_session_register_annotation` 註冊的 annotation 名稱預設區分大小寫。將 `name_flags` 設為 `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE`，可對該定義明確啟用 ASCII 字母忽略大小寫。參數名稱與 metadata 鍵仍精確比對，反射保留原始碼中的拼寫。精確比對與忽略大小寫的註冊發生重疊時，操作會被拒絕且不改變目錄。`struct_size` 必須設為 `sizeof(vs_interop_annotation_desc)`；不完整的描述符與未知旗標會被拒絕。宿主應使用配套的目前 Interop 標頭檔及執行階段重新建置。

## variant

請參閱[Interop SDK — variant](../../User/zh-TW/index.md#exported-value-lifetime)。

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

明確的 variant 目標允許具體值隱式裝箱。讀取必須顯式 cast<T> 或 try_cast<T>；variant_is<T> 精確檢查儲存型別，不進行轉換。try_cast 使用的 optional<T> 需要 core.vs。空盒不能強制提取；一般轉換不符傳回空 optional。複製失敗、取消和限額仍為錯誤。
## 工作階段歸屬與回呼快照

匯出至 C 的可變模組容器即使目前為空，也繫結來源工作階段。工作階段保持開啟時，共用的 C 別名和複本會保留底層圖；釋放一個控制代碼不會銷毀仍被其他控制代碼保留的物件。只有最後一個保留參考釋放後，才可在所屬工作階段的安全點回收。獨立純量、宿主值和列舉值不因此增加工作階段約束。

由 C 建立、可容納模組相關圖的可變容器，首次傳入工作階段時即繫結該工作階段，即使目前為空。工作執行緒只能將獨立值匯出至擁有的 C 控制代碼；含 `variant` 或函式槽的容器即使為空，也不能如此匯出，也不能將這類外部 C 儲存體交給工作執行緒修改。

依賴模組的物件與容器圖在複製後仍歸屬原工作階段。呼叫、欄位、全域、掛鉤與序列化介面會在存取中繼資料或修改狀態前，以 `ENGINE_ERROR` 拒絕其他工作階段的圖；獨立純量與宿主值維持正常傳遞語意。借用的原生呼叫與反序列化內容在同步進入另一工作階段時仍保留原工作階段歸屬，有效期間仍限於原回呼返回之前。

替換模組的初始化提交前，不能將依賴模組的腳本結果持久保存在擁有的 C 值控制代碼中。賦值和複製會傳回 `ENGINE_ERROR`；被拒絕的賦值保留目標擁有控制代碼的原值。回呼的借用傳回槽仍可將結果傳回同一次腳本呼叫，包括 `vs_interop_intercept_context_proceed(context, out_return)`。暫存結果仍在所屬工作階段的安全點回收。

`vs_interop_value_reset` 對借用的回呼參數和傳回槽傳回 `INVALID_ARGUMENT`。重設擁有控制代碼，或將其值替換為獨立純量，會解除原工作階段歸屬，使該控制代碼可獨立使用。

map/set visitor 可以重設或銷毀來源擁有控制代碼。列舉會持有已註冊的圖，直到所有快照檢視都離開作用域；借用的項目檢視僅在目前 visitor 呼叫期間有效，需要保留時應複製。工作階段執行與設定的重入限制仍然適用。

套用完整設定會保留透過專用 setter 設定的數值、記憶體與標準函式庫限額。外部記憶體註冊僅在輸出成功發布後接管 owner，任何失敗都由呼叫端繼續負責該 owner。執行例外會將有效結果輸出初始化為失敗，記憶體配置失敗會保留 `OUT_OF_MEMORY`。

值替換或重設提交後，舊值清理仍可能失敗。已發布的值維持不變，清理錯誤會留在原工作階段的錯誤通道，不會將已提交的發布追溯誤報為失敗。

C 函式值和保留的 callable 沿用 Core 閉包語義：複本共用同一可變擷取環境，分別執行閉包建立運算式會得到獨立環境，參考擷取可在來源呼叫框架結束後繼續存活。區域 static 儲存體歸屬執行中程式狀態的宣告位置，在相同狀態的呼叫或閉包執行個體之間共用。需要在 C 呼叫間保留狀態時，使用 `vs_interop_session_set_hot_reload_enabled`；一般的新執行會初始化並清理各自的狀態。其私有槽不會作為一般全域變數反映匯出。這些語言功能透過既有 C API 使用所選 VM/JIT。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/values-and-callbacks.md) · [fr-FR](../fr-FR/values-and-callbacks.md) · [ja-JP](../ja-JP/values-and-callbacks.md) · [ko-KR](../ko-KR/values-and-callbacks.md) · [zh-CN](../zh-CN/values-and-callbacks.md) · [zh-TW](values-and-callbacks.md)
