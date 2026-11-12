# Python の値、コンテナー、ライフサイクル

## 型付きコンテナーと参照

公開ファクトリは `Value.array`、`Value.map`、`Value.set`、`Value.queue`、
`Value.priority_queue`、`Value.stack`、`Value.deque`、`Value.channel` です。
要素型またはキーと値の型を明示するため、空のコンテナーでも型が曖昧に
なりません。通常の map と set は一意性を持ち、multi-map と multi-set は
対応する runtime または host binding を使用します。`to_python()` が返すのは
対応する場合の snapshot であり、snapshot の変更は script のコンテナーへ
書き戻されません。

`HostParameter(..., by_ref=True)` は `Value` として渡される可変参照を、
`const_ref=True` は読み取り専用参照を表します。両方を同時に指定できず、
失敗した呼び出しや `exit` の後には write-back されません。

## channel の公開 API

正確な操作名は `send`、`try_send`、`send_for`、`receive`、`try_receive`、
`receive_for`、`close_channel` です。`try_send` と `send_for` は bool を返し、
非ブロッキングまたはタイムアウト付きの受信は値または `None` を返します。
`count`、`channel_capacity`、`channel_closed` で状態を確認できます。

`Value` は Interop ABI が管理するスクリプト値で、独立したネイティブハンドルを所有します。セッション操作と保持された callable には、セッションの寿命による制約もあります。ホストオブジェクトを所有する値とそのプロトコルは、作成元のセッションを閉じた後も使用できます。[ホストバインディング](host-bindings.md)を参照してください。

整数、浮動小数点、bool、文字列、ワイド文字列、enum、配列、sequence、map、set、channel に型付きアクセスがあります。コンテナーの Python 表現はスナップショットです。

`Session.retain_callable()` は `RetainedCallable` を返します。

```Python
from veroscript import Session

with Session() as session:
	session.load_source("""
int add(int left, int right) { return left + right; }
function(int, int)->int make_callback() { return add; }
void main() {}
""", "callback.vs")
	function_value = session.call("make_callback")
	with session.retain_callable(function_value) as callback:
		result = callback(20, 22)
		assert result == 42
```

保持 callable は作成スレッドに属し、リロードやセッション終了後に無効になることがあります。`is_valid` を確認し、利用後は閉じてください。map と set の反復、および `keys()`、`values()`、`items()` は所有するスナップショットを返し、保持できます。スカラーは通常の Python 値に変換され、その他の要素は独立した所有 `Value` ハンドルになります。通常の map と set の順序は未規定で、`ordered=True` は最初の挿入順を保持します。読み取り操作から要素へ直接書き戻すことはできず、変更には VeroScript 関数またはホストコールバックを使います。型、スレッド、callback の問題はそれぞれ `TypeMismatchError`、`ThreadError`、`CallbackError` になります。

`Value(None)` は VeroScript の `null` 値を作成します。nullable でない型を
宣言した値へ `None` を代入すると `TypeError` になります。
`Value.priority_queue("int", [3, 1, 2])` で priority queue を作成できます。
通常の `map` と `set` は重複を保持しません。

`kind`、`type_name`、`value` は現在の表現を示します。`clone()` は呼び出し側が所有する別の `Value` handle を作成します。runtime の clone 規則に従い、deep copy は保証しません。各 `Value` は native handle を所有し、Python object が破棄されると解放されます。`Value` に公開 `close()` メソッドはありません。セッション操作が返した値は呼び出し側が所有します。セッションと保持 callable は context manager で閉じてください。

ブロッキング `receive()` は GIL を解放し、他の Python thread が実行を続けられるようにします。channel の要素型は runtime の channel 契約に従う必要があります。

所有する Python ホスト値は、参照サイクルがある場合や元のセッションを閉じた後でも、存続中は clone・等価比較・hash のコールバックを保持します。作成スレッドでのセッションの自動回収も、存続中のプロトコルを保護します。確実な解放にはセッションのコンテキストマネージャーを使い、最後の値を別スレッドに渡す前に作成スレッドでセッションを閉じてください。

コンテナーは `len`、反復、添字、`get`、`contains` で読み取れます。`Session.construct` はオブジェクト値を作り、`get_field`、`set_field`、`call_method` は登録したメンバーにアクセスします。`weak_this=True` は保持メソッドのレシーバーを生存させないため、その失効を処理してください。`closed` はラッパーの終了、`is_valid` は対象の有効性を示します。上の例の結果は `42` です。

`count`、`channel_capacity`、`channel_closed` は読み取り専用プロパティで、括弧を付けません。次の完全な例は出力せず正常終了します。

```Python
from veroscript import Value

channel = Value.channel("int", 1)
assert channel.channel_capacity == 1 and not channel.channel_closed
assert channel.try_send(42)
assert channel.count == 1
assert channel.receive() == 42
channel.close_channel()
assert channel.channel_closed
```

## 正確な数値

`RuntimeCapabilities.NUMERICS` は Core の bigint/rational 対応を示します。
これらは `kind == "host_object"` の `Value` として保持され、
`NumericKind(value.numeric_kind)` で `BIGINT`、`RATIONAL`、`NONE` を確認できます。
スクリプトとホストコールバック間でも `Value` を使用し、Python の整数や浮動小数点数へ
暗黙に変換しません。

`session.numerics.bigint(text)`、`rational(text)`、
`parse(text, NumericKind.BIGINT)` は明示的な文字列を解析し、`to_text(value)` は
正規化された文字列を返します。`convert(value, type_name, exact_float=False)` は
指定した固定幅整数、`float`、`double` 型を保持する `Value` を返します。
範囲外の値や `exact_float=True` で正確に表現できない変換はエラーになります。

`Session.set_numeric_limits` のキーワード引数は `max_value_bits`、`max_input_bytes`、
`max_output_bytes`、`max_scratch_bytes`、`max_work_units` です。ゼロは Core の有限な
既定値を選択します。プロセス全体のメモリ使用量を制限するものではありません。
セッション操作は作成スレッド上でアイドル状態のときに行います。ホストコールバックでは
`current_numerics()` で有効なコンテキストを借用します。そのメソッドはコールバック外では
失敗します。ネイティブ数値操作は GIL を解放します。単独で動作する Python AOT 出力は
bigint/rational に対応していません。

## モジュールに依存する値の所有権

[相互運用 SDK — モジュールに依存する値の所有権](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#value-domain-confinement) を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](../zh-TW/values.md)
