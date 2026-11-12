# Python ホストバインディング

ホストオブジェクトを所有する `Value` とそのサブタイプ検査は、作成元の `Session` を閉じた後も安全に使用できます。型の検索には Python クラスへの弱参照を使います。clone／equals／hash プロトコルを持つ値は、map/set のキーとしての利用を含め、プロトコルの呼び出しに必要なコールバックを最後の関連する値が解放されるまで保持します。これらのプロトコルを持たない値は、無関係なクラスやセッションのコールバックを保持しません。

`HostClass` の公開メソッドは `constructor(...)`、`method(...)`、
`static_method(...)`、`property(...)`、`field(...)` です。これらで VeroScript
から見えるメンバーを明示します。

ホストバインディングは Python の関数、クラス、シリアライザーを明示的な VeroScript 型契約として公開します。`HostParameter` は `type_name`、名前、`by_ref`、`const_ref`、ドキュメントを記述します。

```Python
from veroscript import HostParameter

session.register_function(
	"Add",
	add,
	parameters=(
		HostParameter("int", "left"),
		HostParameter("int", "right"),
	),
	return_type="int",
)
```

`register_function` と `host_function` の契約は実際の callback 署名と一致させます。`HostClass` はコンストラクター、インスタンス/静的メソッド、プロパティを宣言します。`construct`、`call_method`、`get_field`、`set_field` は登録済みメンバーだけを操作します。

`register_serializer`、`serialize_object`、`deserialize_object` はデータ境界を形成するため、型とバージョンを検証してください。`set_mutation_callback` のイベントは短時間で処理し、列挙中のオブジェクトやセッションを破棄しないでください。完了済みの変更は通知によって取り消せません。callback 例外は `CallbackError` に変換されます。

ホストのメンバー シグネチャは明示的に指定します。スカラー、登録済み
ホスト オブジェクト、`enum<Name>`、`array<int>` や
`map<string,PythonObject>` のようなネストしたコンテナーを使用できます。
スカラーでない callback 値は `Value` で表します。クラス メンバーは現在
値渡しのパラメーターを受け取り、自由関数では可変 `ref` と読み取り専用
`const_ref` も使用できます。

`field(...)` は既定でライブ getter と書き込み可能な setter を公開し、
`writable=False` で読み取り専用にできます。`register_struct(...)` の値型は
`protocol(...)` で clone を登録してください。map/set のキーにする場合は
equals と hash も必要です。`register_enum(...)` は `IntEnum` または `IntFlag` を
受け取ります。通常の `enum.Enum` は拒否されます。
`register_generic_class(...)` または `register_generic_struct(...)` で登録した
テンプレートは、利用するソースをロードする前に `instantiate(...)` で具体化します。
継承可能なホスト クラスでは `register_class(..., inheritable=True)` と
`protocol(subclass_factory=...)` を使用すると、スクリプトのサブクラスから管理対象
インスタンスを生成できます。

注釈名は既定で大文字と小文字を区別します。
`register_annotation("@GuiWidget", case_sensitive=False)` を使うと、プロセッサーの
購読とリフレクションでは注釈名の ASCII の大小文字を区別せず、ソースの表記は
保持します。引数名とメタデータキーは引き続き完全一致です。名前の規則が競合すると
`Error` が発生し、既存の登録は保持されます。この登録だけではコンパイル時の
プロセッサーはインストールされません。

`register_annotation(...)` は注釈の語彙と引数を、`register_type_alias(...)` は
具体型またはジェネリック型の別名を登録します。インスタンス呼び出しの
`set_instance_call_interceptor(...)` では `InterceptResult.UNHANDLED`、`InterceptResult.HANDLED`、`InterceptResult.FAILED` を
返せます。`context.proceed()` は最大 1 回で、callback の終了後は context を
保持できません。信頼できるホスト呼び出しには
`call_method_bypassing_interceptor(...)` を使用します。

登録済みのホスト基底型を使う signature には、登録済みで互換性のある subtype の
instance を渡せます。互換性は登録された関係に基づき、任意の Python class から
推測されません。

## callback の制限

登録した callback はセッションを閉じるまで保持されます。例外は `CallbackError` に変換され、native ABI を越えて伝播しません。script callback は VM worker thread で実行されることがあるため、共有 Python state を同期してください。worker callback から作成 thread に限定された `Session` や保持 callable のメソッドを呼び出してはいけません。generic host type は source の読み込み前に instantiate し、host struct にはコピーやキー利用に必要な protocol を登録してください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
