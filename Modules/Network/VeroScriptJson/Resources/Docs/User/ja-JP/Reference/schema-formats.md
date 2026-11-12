# Schema の format と方言

Draft 2020-12 の format は既定で注釈です。FSchemaOptions::bAssertFormats／schema_options.set_assert_formats(true) を指定すると全コンパイルリソースで検証し、未知 format はコンパイル失敗になります。string だけが対象で、不一致は通常の invalid、取消／割り当て／制限は未完了です。DNS、通信、送信、resource 読み取りや文字列変更は行いません。

19 種を実装します。date-time/date/time は RFC 3339 の構文、offset、数字のある小数秒、offset 換算後のうるう秒位置を検査し、外部の通達は参照しません。duration は付録 A の週、順序、中間成分を検査します。email/idn-email は SMTP quoting／escape／長さ／IP、国際形式では UTF-8 local part を扱います。hostname/idn-hostname は長さ、A-label/Punycode/NFC/IDNA context/bidi、非移行 UTS #46 を使い、末尾 root dot は受けません。ipv4/ipv6 は厳密構文で IPv4 の先頭ゼロ不可。uri/uri-reference は RFC 3986、iri/iri-reference は RFC 3987 の Unicode と private-use 位置、uuid はハイフン付き hex（大小可、URN 不可）、uri-template は RFC 6570、json-pointer/relative-json-pointer は fragment decode しない文字列形式、regex は Unicode ECMAScript です。

IDNA は Unicode 17 / python-idna 3.12 の C++ 組み込み表で Python 実行環境は不要です。mapping 入力 4096 byte、各 label 256 code point、正規化一時領域 64 KiB、超過は Limit です。DNS の A-label は各 63／全体 253 byte のままで取消も検査します。

format-assertion vocabulary を宣言する独自 meta-Schema を $schema で選べます。既知の optional vocabulary も完全な意味を持ち、未知 required は拒否、未知 optional keyword は注釈です。直接選択した meta-Schema の $vocabulary のみ適用し、$ref で継承せず、業務 Schema 自身の $vocabulary で方言を変更しません。埋め込み resource は自身の $schema がなければ継承、独立取得文書に $schema がなければモジュール既定 Draft 2020-12 を使います。自己／循環 meta-Schema も検証と予算の対象です。bAssertFormats は明示的 override、meta/format-assertion は内蔵です。注釈モードでは未知 format も許可します。

```VeroScript
json::schema_options options = json::schema_options();
options.set_assert_formats(true);
json::schema schema = json::compile_schema_with_options(
	json::parse("{\"format\":\"email\"}"), options, json::object());
bool valid = schema.is_valid(json::from_string("x@example.test"));
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/schema-formats.md) · [fr-FR](../../fr-FR/Reference/schema-formats.md) · [ja-JP](schema-formats.md) · [ko-KR](../../ko-KR/Reference/schema-formats.md) · [zh-CN](../../zh-CN/Reference/schema-formats.md) · [zh-TW](../../zh-TW/Reference/schema-formats.md)
