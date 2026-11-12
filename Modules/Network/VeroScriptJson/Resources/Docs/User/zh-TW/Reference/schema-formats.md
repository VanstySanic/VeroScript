# Schema 格式與方言

Draft 2020-12 的 format 預設為註解。設定 FSchemaOptions::bAssertFormats 或 schema_options.set_assert_formats(true) 會在全部編譯資源啟用斷言，未知格式則編譯失敗。只作用於字串；不符是正常無效，取消/配置/限制是驗證未完成。不進行 DNS、網路、投遞、資源讀取或修改字串。

支援 19 種：date-time/date/time 檢查 RFC 3339 語法、偏移、含數字小數秒及偏移換算後閏秒位置，不查公告；duration 檢查附錄 A 的週、順序和中間分量。email/idn-email 檢查 SMTP 引號/跳脫/長度/IP，國際形式支援 UTF-8 本地部分。hostname/idn-hostname 檢查長度、A-label/Punycode/NFC/IDNA 上下文與雙向規則，使用非過渡 UTS #46，不接受尾端根點。ipv4/ipv6 採嚴格語法，IPv4 不允許前導零。uri/uri-reference 採 RFC 3986；iri/iri-reference 採 RFC 3987 Unicode 及私用位置限制；uuid 為大小寫皆可的帶連字號 hex、不接受 URN；uri-template 採 RFC 6570；json-pointer/relative-json-pointer 為字串形式、不解碼 fragment；regex 採 Unicode ECMAScript。

IDNA 使用 Unicode 17 / python-idna 3.12 產生的 C++ 表，不需要 Python 執行期。對映輸入最多 4096 位元組，每標籤 256 碼點，正規化暫存 64 KiB；超限回報 Limit。DNS A-label 仍每標籤 63、總 253 位元組，繼續檢查取消。

自訂 meta-Schema 可宣告 format-assertion 詞彙，由 $schema 選用。已知選用詞彙有完整語意，未知必需詞彙拒絕，未知選用關鍵字為註解。只採直接指定 meta-Schema 的 $vocabulary，$ref 不繼承，業務 Schema 自身 $vocabulary 不切換方言。內嵌資源無自己的 $schema 則繼承；獨立檢索文檔缺少 $schema 使用模組 Draft 2020-12。自參考/循環 meta-Schema 仍受驗證與預算限制。bAssertFormats 為明確覆寫，內建 meta/format-assertion；預設註解模式允許未知格式。

```VeroScript
json::schema_options options = json::schema_options();
options.set_assert_formats(true);
json::schema schema = json::compile_schema_with_options(
	json::parse("{\"format\":\"email\"}"), options, json::object());
bool valid = schema.is_valid(json::from_string("x@example.test"));
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/schema-formats.md) · [fr-FR](../../fr-FR/Reference/schema-formats.md) · [ja-JP](../../ja-JP/Reference/schema-formats.md) · [ko-KR](../../ko-KR/Reference/schema-formats.md) · [zh-CN](../../zh-CN/Reference/schema-formats.md) · [zh-TW](schema-formats.md)
