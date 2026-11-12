# JSON リファレンス

スクリプトの値キャプチャは Core の読み取り専用バインディング規則に従います。キャプチャしたホスト値を変更するネイティブメソッドには、`mutable` Lambda または書き込み可能なバインディングの参照キャプチャを使用します。初期化キャプチャとローカル static は通常の Core の意味論に従います。

VeroScriptJson.h は VeroScript::Json にリンクする独立 C++17 データ API で、VM は不要です。製品／パッケージは 0.1.1 です。json を使うスクリプトをコンパイルする前に対応する vs-json を導入します。スクリプト型変換にはモジュールのコンパイルプロセッサーも必要で、バインドカタログだけでは登録されません。

6 種の JSON 値は不変の共有スナップショットで、元の数値文字列、正確な十進比較、検査付き変換を保持します。変更は成功時だけ確定します。読み取りは共有でき、同じ変数／builder の変更は同期します。Try* は成功時にエラーを消し、失敗時に出力を保ちます。コンストラクター／factory は割り当て例外を投げる場合があります。エラー文字列が空でも FError::Code と割り当て不要の Id() を確認できます。work callback の例外は Callback、sink は Sink、割り当て失敗は Allocation です。サイズ／添字は int64、DOM 深度の既定値／上限は 256 です。as_double は丸める場合があり、厳密変換は明示します。

大きな配列／object の構築は FBuilder を使い、スナップショットを毎回複製しないでください。builder のコピーは状態を共有し、TryFinish が閉じ、TryReset で再利用します。FPointer (RFC 6901)、Patch (RFC 6902)、Merge Patch (RFC 7396) は正確な値を保ち、根の削除は FDocument::bHasValue で表します。

FReader::Feed は消費バイト数を返し、未消費 suffix は保持して再送します。EventReady を一つずつ読み、最後のチャンクを示して Done まで処理します。非最終の `1` と続く `2 ` は数値 `12` です。Visit は同期的に通知し、false は現在イベントの後で停止して再通知せず、例外時は reset まで Failed になります。visitor/work は呼び出し中のみ借用します。FWriter::Write の bAccepted が真になったら同じイベントを再送せず Resume します。`0 + NeedOutput` は通常の backpressure、`0 + Progress` はエラーです。Finish は中断でき、完了後も繰り返せます。ソートには DOM の TryWrite を使います。

スクリプトの json::limits/parse_options/write_options は値コピーです。返された limits を変更したら set_limits で戻します。input_bytes/output_bytes/token_bytes/nodes/depth/number_bytes/exponent/work_units と setter を提供します。数量は非負 int64、深度を増やしても 256 を超えません。duplicate_keys は 0 Reject、1 KeepFirst、2 KeepLast、3 ReportAll（reader/SAX のみ）です。allow_leading_bom、pretty/sort_keys/indent（0–16）も設定できます。入口は parse_with_options、try_parse_with_options、write_with_options、make_reader_with_options、make_writer_with_options です。

非例外入口は try_parse、try_write、try_write_with_options、try_number、`try_from_string`、`try_from_double`、try_compile_pointer、try_as_int64/uint64/double/double_exact/bool/string、try_resolve、try_patch、try_merge_patch です。結果参照は最後の error 参照より前に置き、options 付きは公開宣言に従います。通常のデータエラーは false、結果保持、json::error を返しますが、取消や wrapper を返せない失敗は native 呼び出し自体が失敗します。code/message/message_id/has_source/byte_offset/line/column/instance_pointer/patch_operation を取得できます。

array_builder/object_builder は push/set/reserve/size/capacity/finish、reset_array/reset_object と try 形式を持ちます。alias は同じ状態を観測し、finish の DOM は独立します。try_finish/size/capacity は最初が結果参照、最後が error です。DOM にも try_contains/try_keys があります。reader は 0 NeedInput、1 EventReady、2 Done、3 Failed、event() はデータを所有します。writer は 0 Active、1 NeedOutput、2 Done、3 Failed、write は受理の有無、take は最大 64 KiB を取得し resume で続けます。失敗後は error を確認し明示的に reset します。

Schema は Draft 2020-12、厳密数値、動的参照、unevaluated と Unicode ECMAScript 正規表現を使用します。format/content は既定で注釈で、暗黙デコードはしません。schema_options は retrieval_uri、limits、max_resources を提供し、空 URI は urn:veroscript:json-schema:root です。compile_schema_with_options(schema,options,resources) は URI → Schema object を受けます。C++ resolver はコンパイル中のみ借用し、結果が全リソースを所有します。ダウンロードせず cwd に依存しません。標準 meta-Schema と 8 語彙のリソースを内蔵します。

コンパイル／検証は work、取消、node、text、深度を検査します。URI は 64 KiB、正規表現は 4096 バイト、正規表現一回の保持割り当ては 16 MiB、stack／step 保護も Limit を返します。エラーは子を含め 1–16384 件で、truncated は成功を意味しません。schema_report は valid/truncated/size/issue と JSON details を返します。

try_compile_schema、try_validate_schema、try_compile_schema_with_options(value,options,resources,ref schema,ref error)、try_validate_schema_with_limits(schema,value,max_issues,limits,ref report,ref error)、try_compile_pointer_with_limits、pointer.try_resolve[_with_limits]、document.try_get も提供します。根がなければ出力を保持して false。コンパイル済み Pointer も利用時の token／depth／work 制限を再検査します。

- [C++ 型マッピング](native-mapping.md)
- [スクリプトオブジェクトの変換](script-mapping.md)
- [閉じた多態マッピング](polymorphic-mapping.md)
- [Schema 注釈レポート](schema-annotations.md)
- [Schema の format と方言](schema-formats.md)
- [診断とローカライズ](diagnostics.md)

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

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
