# JSON 參考

腳本的按值捕獲遵循 Core 的唯讀綁定規則。需要對捕獲的宿主值呼叫可變原生成員時，使用 `mutable` Lambda，或對可寫綁定進行參考捕獲。初始化捕獲與區域 static 沿用 Core 的一般語意。

VeroScriptJson.h 提供連結 VeroScript::Json 的獨立 C++17 資料 API，不需要 VM。產品與套件版本為 0.1.1。編譯使用 json 的腳本前須安裝匹配的 vs-json；腳本物件對映還需要模組編譯處理器，僅匯入繫結目錄不會註冊處理器。

六種 JSON 值採用不可變共享快照，保留原始數字文字、精確十進位比較及受檢轉換。修改僅在成功時提交。讀取可共享快照；修改同一包裝變數或 builder 須同步。Try* 成功清除錯誤、失敗保留輸出；建構與工廠仍可能擲出配置例外。錯誤文字可能為空，可讀 FError::Code 與無須配置的 Id()。工作檢查回呼例外為 Callback，sink 例外為 Sink，配置失敗為 Allocation。數量/索引為 int64；DOM 預設及硬深度上限皆為 256。as_double 可以捨入，精確轉換須明確要求。

大量建構使用 FBuilder，避免反覆複製容器快照。builder 複本共享狀態，TryFinish 關閉、TryReset 重用。FPointer（RFC 6901）、Patch（RFC 6902）、Merge Patch（RFC 7396）保留精確值；根刪除以 FDocument::bHasValue 表示。

FReader::Feed 傳回消費位元組數，須保留並重交未消費尾段，逐個讀取 EventReady，標記最後區塊後處理至 Done。非最終 `1` 接 `2 ` 形成數字 `12`。Visit 同步派發；false 在目前事件後暫停且不重送，例外進入 Failed，須 reset。visitor/work 僅於本次呼叫借用。FWriter::Write 以 bAccepted 表示接收，接收後只能 Resume，不重交事件。`0 + NeedOutput` 是正常背壓，`0 + Progress` 為錯誤。Finish 可暫停且完成後可重複呼叫；串流保留順序，排序輸出用 DOM TryWrite。

json::limits/parse_options/write_options 按值複製，修改取回的 limits 後以 set_limits 提交。限制包含 input_bytes/output_bytes/token_bytes/nodes/depth/number_bytes/exponent/work_units 及 setter，數量為非負 int64，提高 depth 也不能超過 256。duplicate_keys 為 0 Reject、1 KeepFirst、2 KeepLast、3 ReportAll（僅 reader/SAX）。另有 allow_leading_bom、pretty/sort_keys/indent（0–16）。入口為 parse_with_options、try_parse_with_options、write_with_options、make_reader_with_options、make_writer_with_options。

非擲錯函式包含 try_parse、try_write、try_write_with_options、try_number、`try_from_string`、`try_from_double`、try_compile_pointer、try_as_int64/uint64/double/double_exact/bool/string、try_resolve、try_patch、try_merge_patch。結果參考在最後的 error 參考之前，選項變體依公開宣告。一般資料錯誤傳回 false、保留結果並提交 json::error；取消或無法交付包裝物件仍使 native 呼叫失敗。錯誤存取為 code/message/message_id/has_source/byte_offset/line/column/instance_pointer/patch_operation。

array_builder/object_builder 提供 push/set/reserve/size/capacity/finish、reset_array/reset_object 及 try 變體。別名觀察相同狀態，finish 的 DOM 獨立。try_finish/size/capacity 的第一個參考是結果、最後是 error。DOM 另有 try_contains/try_keys。reader 狀態為 0 NeedInput、1 EventReady、2 Done、3 Failed，event() 擁有資料。writer 為 0 Active、1 NeedOutput、2 Done、3 Failed；write 回報接收，take 每次最多取走 64 KiB，再 resume。失敗後讀 error 並明確 reset。

Schema 採 Draft 2020-12、精確數值、動態參考與 unevaluated 組合，正規表示式為 Unicode ECMAScript。format/content 預設是註解，不暗中解碼。schema_options 提供 retrieval_uri、limits、max_resources，空 URI 為 urn:veroscript:json-schema:root。compile_schema_with_options(schema,options,resources) 接受 URI → Schema 物件。C++ 可在編譯期間借用同步 resolver，編譯結果擁有全部資源。不下載參考，也不以 cwd 解析。內建標準 meta-Schema 與八種詞彙資源。

編譯/驗證檢查工作量、取消、節點、文字與深度。URI 最多 64 KiB、正規表示式 4096 位元組、一次表示式執行保留配置 16 MiB；堆疊/步數保護也可回報 Limit。報告為 1–16384 項，含子錯誤，截斷不代表成功。schema_report 提供 valid/truncated/size/issue 與 JSON details。

另有 try_compile_schema、try_validate_schema、try_compile_schema_with_options(value,options,resources,ref schema,ref error)、try_validate_schema_with_limits(schema,value,max_issues,limits,ref report,ref error)、try_compile_pointer_with_limits、pointer.try_resolve[_with_limits]、document.try_get。根不存在時保留輸出並回傳 false。已編譯 Pointer 使用時重新檢查目前 token/depth/work 限額。

- [C++ 型別對映](native-mapping.md)
- [腳本物件對映](script-mapping.md)
- [封閉多型對映](polymorphic-mapping.md)
- [Schema 註解報告](schema-annotations.md)
- [Schema 格式與方言](schema-formats.md)
- [診斷與本地化](diagnostics.md)

```VeroScript
json::schema schema = json::compile_schema(json::parse("{\"type\":\"integer\",\"minimum\":0}"));
bool valid = schema.is_valid(json::from_number("1.0"));
json::schema_report report = schema.validate(json::from_string("bad"));
print(report.issue(0).get("keyword").as_string());
```

```cmake
find_package(VeroScriptJson 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Json)
```

`FSchemaOptions::MaxResources = 256`

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](README.md)
