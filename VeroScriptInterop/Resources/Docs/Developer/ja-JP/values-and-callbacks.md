# Interop value、callback、package

## value と container

ABI は fixed-width scalar、string、enum、host object、array、map、ordered map、set、queue、stack、deque、channel を公開します。container enumeration callback は borrowed value を受けます。型は VeroScript の契約に一致する必要があります。要素の値の型検証は `TYPE_MISMATCH`、実行の失敗は `ENGINE_ERROR` を返す場合があります。暗黙の変換は行いません。multiset の要素数は出現配列の長さを合計し、要素のコピーや snapshot の割り当てを行いません。

オブジェクトの要素には、元の値のモジュールメタデータに基づき、宣言された基底クラスまたはインターフェースに代入可能な派生スクリプトオブジェクトを使用できます。`Base` を継承し `IRead` を実装する `Child` は、`array<Base>` や `queue<IRead>` に挿入できます。コンテナの型宣言は不変であり、`array<Child>` から `array<Base>` への変換や数値変換は追加されません。

map/set のキー型宣言は、空のコンテナや multimap/multiset でも検証されます。未対応の組み込み型（`bool`、`float`、`double`、8 ビット符号なし整数、16 ビット整数、コンテナ、関数、参照）は `INVALID_ARGUMENT` を返し、以前の値を置き換えません。プリミティブキーは `char`、`int`、`unsigned int`、`int64`、`uint64`、`string`、`wstring` とその別名です。名前付き enum/host 型（具体化した host 型を含む）は利用するセッションで解決されます。

channel capacity は first use 前に configure します。send、receive、try、timed、count、capacity、closed、close は別 status behavior です。closing は blocked operation を wake し、API が提供する場合 error text は owned UTF-8 buffer で返ります。

## コールバックコンテキスト

native function、generic host-type factory、instance-call interceptor、mutation callback、external serializer/deserializer は context setter/callback result value で failure を report します。callback exception は C を通って return する前に convert します。user data、host-object payload、managed root は全 callback entry の間生存します。

無効な ApplyConfig 記述子は適用前に拒否されます。Core での適用中に割り当てや内部エラーが発生すると、設定の一部が適用済みになる場合があります。設定に依存する前に完全な構成を再適用してください。C のキャンセルハンドルは成功時も失敗時もセッションに実際に設定された制御を参照します。Linux の割り当て失敗回帰は、制御の有効化／無効化、キャンセルの整合性、再試行による復旧を検証します。

external serialization は ABI visitor contract で reference enumerate もできます。reference kind、visitor order、borrowed-value lifetime は machine-facing data です。visitor は borrowed value を retain せず owning session を reentrant に mutate しません。map-entry/set-element visitor も borrowed-view rule に従い、partial traversal を success に変えず callback failure を propagate します。

reentrant destruction/session clearing は一般 callback feature ではありません。implementation が reject する場合 ABI は engine error を返し owning session を partial clear せず保存します。

## package と reflection

package API は package handle の read/destroy、kind/name/version、metadata、canonical signed byte、load/reload/build operation を公開します。reflection は loaded type/field/method/global/enum/interface の JSON metadata を export します。JSON field name/type name は machine contract で localize しません。

`vs_interop_session_build_package_file` は `message_id` `diag.failed_to_build_package` の structured diagnostic で build failure を report します。最初の diagnostic argument は underlying build reason です。consumer は selected locale に `vs_interop_session_format_diagnostic` を使い、`message` を diagnostic contract の代替として扱いません。

## チャネル

channel value は `vs_interop_value_create_channel` または `vs_interop_value_create_bounded_channel` で作成します。zero bounded capacity は unbounded channel です。`vs_interop_value_channel_send` は accepted まで block、`vs_interop_value_channel_try_send` は non-blocking、`vs_interop_value_channel_send_for` は timeout 付きです。receive family は対応する blocking/non-blocking/timed operation です。count、capacity、closed-state、close は別 ABI call です。closing は waiter を wake します。host は closed result を terminal として扱い callback scope 後に borrowed value を reuse しません。

## ジェネリックホスト型のコンテキスト

`vs_interop_generic_type_context_*` は generic host-type factory の template argument count/argument を公開します。factory は result type、member、protocol declaration、error を同じ borrowed context に与えます。context accessor/setter は factory callback 中だけ valid です。callback は return 前に registration を完了し、context は retained builder object ではありません。

## call と interception

`vs_interop_intercept_context_*` は receiver、resolved target、argument、`proceed`、callback error channel を公開します。`proceed` は既に resolved の call に対する interceptor continuation です。後の inbound dispatch が必要な host は entry interceptor を bypass する session call API を使えます。method body の nested call は interceptable のままです。

`vs_interop_call_context_*` は借用引数と変更可能な引数へのアクセス、`host_this`、プログラムの世代、エラーチャネルを提供します。関数と callable の呼び出しはセッションの再入と寿命の規則に従います。callable を保持するインターフェースは `vs_interop_session_retain_callable`、`vs_interop_callable_destroy`、`vs_interop_callable_is_valid`、`vs_interop_callable_invoke` です。所有スレッドだけの呼び出しと弱い `this` のキャプチャは寿命の方針であり、スレッド間のアクセスを同期しません。

## 外部シリアライズのコンテキスト

serializer context は登録された型、mutation version、借用フィールドを公開します。serializer は `vs_interop_external_serialize_context_set_bytes` で出力バイトをコピーし、置換に失敗した場合は以前の payload を保持します。deserializer はバイト列と encoded version を読み、名前付きフィールドの代入を一時 transaction に追加します。両者は error setter と callback result で失敗を報告します。reference enumeration は別の traversal で借用値の寿命を延長しません。adapter は callback 後も必要な byte/string をコピーします。

## 注釈名の照合規則

`vs_interop_session_register_annotation` で登録する注釈名は、既定では大文字と小文字を区別します。その定義の ASCII 大文字と小文字を区別しない場合は、`name_flags` に `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE` を指定します。引数名とメタデータのキーは引き続き完全一致で比較され、リフレクションはソースの表記を保持します。名前が競合する登録はカタログを変更せずに拒否されます。`struct_size` は `sizeof(vs_interop_annotation_desc)` に設定してください。不完全な記述子と未知のフラグは拒否されます。対応する現在の Interop ヘッダーとランタイムを使ってホストを再ビルドしてください。

## variant

[相互運用 SDK — variant](../../User/ja-JP/index.md#exported-value-lifetime) を参照してください。

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

variant が代入先と確定している場合は暗黙にボックス化できます。読み取りには cast<T> または try_cast<T> が必要です。variant_is<T> は変換せず正確な型を判定します。try_cast の optional<T> には core.vs が必要です。空の強制抽出は失敗し、通常の変換不一致は空の optional を返します。複製失敗、キャンセル、制限超過はエラーです。
## セッションへの帰属とコールバックのスナップショット

C に公開した可変モジュールコンテナーは、現在空であっても元のセッションに帰属します。セッションが開いている間、共有する C のエイリアスやクローンが元のグラフを保持するため、一つのハンドルを解放しても別のハンドルが保持するオブジェクトは破棄されません。最後の保持参照が解放された後、所有セッションの安全点でクリーンアップの対象になります。独立したスカラー、ホスト値、列挙値にセッション制約を追加するものではありません。

C で作成した可変コンテナーがモジュール依存のグラフを保持できる場合、空であっても初めて渡したセッションに帰属します。ワーカーから所有 C ハンドルへ公開できるのは独立した値だけです。`variant` または関数の格納スロットを持つコンテナーは空でも公開できず、そのような外部 C ストレージをワーカーへ渡して変更させることもできません。

モジュールに依存するオブジェクトとコンテナーのグラフは、コピー後も元のセッションに帰属します。呼び出し、フィールド、グローバル、フック、シリアライザーは、別セッションのグラフをメタデータ参照や変更の前に `ENGINE_ERROR` で拒否します。独立したスカラーとホスト値の転送規則は従来どおりです。借用したネイティブ呼び出しと逆シリアル化のコンテキストは、別セッションで同期処理を行う間も元のセッションを保持します。有効期間は元のコールバックが戻るまでです。

置換モジュールの初期化が確定するまでは、モジュールに依存するスクリプトの結果を所有 C 値ハンドルに保持できません。代入やクローンは `ENGINE_ERROR` を返し、拒否された代入は代入先の所有ハンドルの既存値を保持します。コールバックの借用戻り値スロットを使って、同じスクリプト呼び出しに結果を返すことはできます。`vs_interop_intercept_context_proceed(context, out_return)` もこの用途に対応します。一時的な結果は、所有セッションの安全点で引き続きクリーンアップされます。

`vs_interop_value_reset` は、コールバックの借用引数と借用戻り値スロットを `INVALID_ARGUMENT` で拒否します。所有ハンドルをリセットするか、その値を独立したスカラーに置き換えると、以前のセッションへの帰属が解除され、独立して使用できます。

map/set の visitor は元の所有ハンドルをリセットまたは破棄できます。列挙はスナップショットの全ビューが解放されるまで登録済みグラフを保持します。借用エントリービューは各 visitor 呼び出しの間だけ有効です。保持するにはクローンしてください。セッション実行と設定の再入制限は引き続き適用されます。

完全な設定の適用でも、専用 setter で設定した数値、メモリー、標準ライブラリーの上限は保持されます。外部メモリーの所有権は出力の公開に成功した後にのみ移ります。失敗した場合は呼び出し側が所有権を保持します。実行例外では有効な結果出力を失敗に初期化し、割り当て失敗は `OUT_OF_MEMORY` を保持します。

値の置換やリセットを確定した後で、古い値のクリーンアップが失敗する場合があります。公開済みの値は保持され、エラーは元のセッションのエラーチャネルに残ります。確定済みの公開を後から失敗として扱うことはありません。

C の関数値と保持した callable は Core のクロージャ規則に従います。複製は可変キャプチャ環境を共有し、別々に生成したクロージャは独立した環境を持ち、参照キャプチャは元のフレーム終了後も有効です。ローカル static は実行中のプログラム状態の宣言箇所に属し、その状態内の呼び出しやクロージャ間で共有されます。C 呼び出し間で状態を保持するには `vs_interop_session_set_hot_reload_enabled` を使用します。通常の新規実行では、実行ごとに状態を初期化して解放します。内部スロットは通常のグローバル変数としてリフレクションに公開されません。既存の C API で選択した VM/JIT がこれらの機能を処理します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/values-and-callbacks.md) · [fr-FR](../fr-FR/values-and-callbacks.md) · [ja-JP](values-and-callbacks.md) · [ko-KR](../ko-KR/values-and-callbacks.md) · [zh-CN](../zh-CN/values-and-callbacks.md) · [zh-TW](../zh-TW/values-and-callbacks.md)
