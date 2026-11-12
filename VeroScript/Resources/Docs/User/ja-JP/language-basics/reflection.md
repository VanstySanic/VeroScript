# リフレクション

VeroScript はモジュール、型、メンバー、宣言メタデータを調べるランタイム リフレクション API を提供します。型付き操作により、公開されたスクリプトのインスタンス フィールドにアクセスし、スクリプトのクラスまたはインターフェイスで宣言された公開メソッドを呼び出せます。アクセス制御は変更されません。

## 反射オブジェクト

この言語は、次の種類の公開情報を提供します。

- `type<T>` は、タイプ `T` 自体を記述します。
- `field_info<T>` は、`T` の 1 つのフィールドを記述します。
- `method_info<T>` は、`T` の 1 つのメソッドを説明します。
- `enum_member_info<T>` は、列挙型 `T` の 1 つの列挙子を記述します。
- `module_info` は、現在のモジュール ビューを説明します。
- `function_info` は、可視の自由関数 1 つについて説明します。
- `global_info` は、1 つの表示可能なトップレベル グローバルを記述します。

これらの型はコンパイラによって提供されるため、インポートは必要ありません。クエリでは、修飾された所有者名とゼロから始まるインデックスが使用されます。無効なインデックスは実行時エラーとして報告されます。

## タイプ情報

`type<T>` のパブリック メンバーは次のとおりです。

|メンバー |目的 |
| --- | --- |
| `name` |短いタイプ名。 |
| `qualified_name` |修飾された型名。`field_info<T>` および `method_info<T>` の構築に適しています。 |
| `kind()` |タイプのカテゴリを返します。 |
| `is_abstract()` |型が抽象型かどうかを報告します。 |
| `base_count()` / `base_name(i)` |直接基本型の数と名前を問い合わせます。 |
| `field_count()` |反射可能なフィールドの数を返します。 |
| `method_count()` |リフレクト可能なメソッドの数を返します。 |
| `metadata_count()`、`metadata_key(i)`、`metadata_value(i)` |型宣言のメタデータを照会します。 |
| `has_metadata(key)`、`metadata(key)` |メタデータを手動で列挙せずに検索します。 |
| `has_annotation(name)` |注釈名をテストします。 `name` と `@name` の両方が受け入れられます。 |
| `enum_member_count()`、`enum_member_name(i)`、`enum_member_value(i)` | enum メンバーをクエリします。 |
| `enum_underlying_type()` |基礎となる列挙型を返します。 |
| `is_scoped_enum()` |列挙型がスコープ指定されているかどうかを報告します。 |

Enum 固有のクエリは、enum 型に対してのみ意味があります。使用する前に `kind()` を確認してください。

## フィールド情報

`field_info<T>(owner, index)` を使用してフィールド クエリを作成します。

```VeroScript
type<Record> info = type<Record>();
field_info<Record> field = field_info<Record>(info.qualified_name, 0);

print(field.name());
print(field.declared_type());
print(field.runtime_type());
print(field.is_static());
print(field.is_mutable());
```

フィールド情報は以下を提供します。

|メンバー |目的 |
| --- | --- |
| `name()` |フィールド名。 |
| `declared_type()` |ソースで宣言されているフィールド タイプ。 |
| `runtime_type()` |ランタイムによって記録されるタイプ。 |
| `is_static()` |フィールドが静的かどうかを報告します。 |
| `is_mutable()` | フィールドがコンテキストの `mutable` 修飾子を使用するかどうかを示します。 |
| `is_declared_on_type()` |フィールドがクエリされた型で直接宣言されているかどうかを報告します。 |
| `metadata_count()`、`metadata_key(i)`、`metadata_value(i)` |フィールド宣言のメタデータを照会します。 |
| `has_metadata(key)`、`metadata(key)`、`has_annotation(name)` |フィールドのメタデータと注釈を名前で検索します。 |

## メソッド情報

`method_info<T>(owner, index)` を使用してメソッド クエリを構築します。

```VeroScript
method_info<Record> method = method_info<Record>(info.qualified_name, 0);

print(method.name());
print(method.return_type());
print(method.parameter_count());
print(method.parameter_type(0));
print(method.parameter_is_ref(0));
print(method.parameter_is_const(0));
```

メソッド情報は以下を提供します。

|メンバー |目的 |
| --- | --- |
| `name()` |メソッド名。 |
| `return_type()` |戻り値の型。 |
| `parameter_count()` |パラメータの数。 |
| `parameter_type(i)` |パラメータ `i` の基本タイプ。 |
| `parameter_name(i)` |パラメータの名前 `i`。 |
| `parameter_is_ref(i)` |パラメータ `i` が参照であるかどうかを報告します。 |
| `parameter_is_const(i)` |パラメータ `i` が const であるか読み取り専用参照であるかを報告します。 |
| `is_static()` |メソッドが静的かどうかを報告します。 |
| `is_virtual()` |メソッドが仮想かどうかを報告します。 |
| `is_abstract()` |メソッドが抽象かどうかを報告します。 |
| `is_const()` |メンバーメソッドがconstかどうかを報告します。 |
| `is_deleted()` |メソッドが削除されたかどうかを `= delete` で報告します。 |
| `is_constructor()` / `is_destructor()` |メソッドがコンストラクターであるかデストラクターであるかを報告します。 |
| `metadata_count()`、`metadata_key(i)`、`metadata_value(i)` |メソッド宣言のメタデータを照会します。 |
| `has_metadata(key)`、`metadata(key)`、`has_annotation(name)` |メソッドのメタデータとアノテーションを名前で検索します。 |

`parameter_type(i)` は、`ref` または `const ref` を削除した後の基になる型を返します。完全なパラメーター セマンティクスを再構築するには、`parameter_is_ref(i)` および `parameter_is_const(i)` も検査します。

## 実行可能なリフレクション

`set<V>` は、対象オブジェクトが書き込み可能でも、`const` として宣言されたフィールドを書き換えられません。対象が `const` の場合は `mutable` フィールドだけを書き換えられます。`get<V>` による読み取りは可能です。

記述子を選んだ後、`field_info<T>.get<V>(対象)`、`set<V>(対象, 値)`、`method_info<T>.invoke<R>(対象, 引数...)`、`invoke_static<R>(引数...)` を使用できます。`V` と `R` は明示し、メンバーの実行時型と厳密に一致させます。`R` には `void` も指定できます。引数は左から一度だけ評価され、固定シグネチャを呼び出し前に検証します。オーバーロードの選択には `parameter_count()` と `parameter_type(i)` を使います。

公開されたスクリプトのインスタンス フィールドと、スクリプトのクラスまたはインターフェイスで宣言された公開メソッドを実行できます。抽象インターフェイス メソッドはレシーバーの実装に解決され、公開された既定メソッドと静的インターフェイス メソッドも呼び出せます。仮想呼び出しは動的ディスパッチされ、構造体のフィールド書き込みは元のオブジェクトを更新します。無効・偽造・失効・別プログラム・アクセス不可の記述子は実行時エラーになります。非公開メンバー、ホスト メンバー、静的フィールド、コンストラクター、デストラクター、削除済みメソッドは照会のみ可能です。C++、Python、C# AOT は現在、この実行操作をコンパイル時に拒否します。古い開発ビルドのバイトコード パッケージとの互換性はサポートしません。現在のコンパイラーでソースから再構築してください。[完全な例](../../../../Scripts/examples/reflection_executable.vs)も参照してください。

`invoke` と `invoke_static` は参照引数と参照戻り値をサポートします。引数は `&value` または `ref value` と明示し、型を厳密に一致させます。書き込み可能な参照は読み取り専用ストレージに束縛できません。戻り値には `invoke<int&>(receiver, ...)` または `invoke<const int&>(receiver, ...)` を指定し、静的呼び出しでも同じ型を使用します。戻り値は別名と const を保持し、値として読む場合は参照先の値を取得します。通常の呼び出しと同じ管理された参照セルを使い、コピーによる書き戻しは行いません。全引数を検証してからメソッドに入り、例外前の変更は通常の呼び出し規則に従います。

参照戻り値を別の参照引数として渡す場合は、先にローカル参照へ束縛し、`&alias` または `ref alias` を渡してください。

オブジェクト引数は呼び出し位置の静的な型でも検証されます。有効なアップキャストと null ハンドルは許可されますが、暗黙のダウンキャストはメソッド本体の実行前に拒否されます。ダウンキャストが必要な場合は、先に明示的な `cast<T>` を使用してください。`set<V>` はソースの型を `V` に代入できるかコンパイル時に検証します。`void` 式は引数にできません。

`set<V>` は通常の代入と同じ検査付き変換を値に適用するため、`set<int64>` で `int` の値を `int64` フィールドへ書き込めます。数値のオーバーフローはフィールドの変更前に拒否されます。`V` は引き続きフィールドの宣言型と厳密に一致させてください。

値渡しの構造体と固定長配列の引数は、その引数を評価した時点でコピーされ、後続の引数による元データの変更の影響を受けません。参照戻り値で値変数を初期化する場合、値引数として渡す場合、または値戻り値を生成する場合も値がコピーされます。クラスとインターフェースのハンドルは通常の呼び出しの意味を維持します。これらのコンパイラー修正を適用するには、古いバイトコードパッケージをソースから再構築してください。

## モジュール、フリー関数、グローバル、および enum メンバーの情報

`module_info()` は、エントリ モジュールから見える宣言を公開します。 `function_count()`、`global_count()`、および `type_count()` を境界として使用し、`function_info(i)` または `global_info(i)` を構築します。 `type_name(i)` は、表示されている各スクリプト タイプ名を返します。インポートされたプライベート宣言は公開されません。エントリモジュールのプライベート宣言とエクスポートされたインポートされた宣言は、通常のインポート/エクスポート ルールに従って引き続き表示されます。

`function_info` は、名前、安定したリンケージ名、モジュール パス、戻り値とパラメーターの署名、`ref`/`const` フラグ、削除/エクスポートされたフラグ、および宣言メタデータを報告します。 `global_info` は、名前、リンケージ名、モジュール パス、宣言/実行時タイプ、const/エクスポート フラグ、および宣言メタデータを報告します。静的データ メンバーはフィールドのままであり、`global_info` では重複しません。

列挙型 `E` の場合、`enum_member_info<E>(type<E>().qualified_name, i)` を構築して、列挙子の名前、完全な `int64` 値、および宣言メタデータを検査します。これは、`type<E>` ですでに利用可能なコンパクトな列挙クエリを補完します。

## 宣言メタデータ

宣言には注釈を付けることができます。この言語は、注釈の名前、キー、および値を保存しますが、それらにアプリケーション固有の意味を割り当てません。

```VeroScript
@entity(sample)
struct BaseRecord
{
	int id;
};

struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}
};
```

ホスト、エディタ、またはその他のツールは、`metadata_count()`、`metadata_key(i)`、および `metadata_value(i)` を介してこの情報を列挙するか、`has_metadata(key)`、`metadata(key)`、および `has_annotation(name)` を使用して直接検索できます。キーが欠落している場合は、`metadata(key)` から空の文字列が返されます。空の値と欠落しているキーを区別する必要がある場合は、`has_metadata(key)` を使用します。この例では、`@entity`、`@data`、および `@call` はクエリ可能なメタデータ名であり、組み込みのビジネス動作ではありません。スクリプトまたはホスト統合で独自の規則を定義できます。

ホスト バインディング カタログ内の注釈とランタイム スクリプト リフレクションは、2 つの別個のレイヤーです。バインディング カタログはホスト API を記述し、リフレクション情報タイプは現在コンパイルされたスクリプト モジュールをクエリします。

## 完全な例

次の例では、継承、フィールド、メソッド、静的メソッド、トップレベルのグローバル、列挙型メンバー、および宣言メタデータをクエリします。

```VeroScript
@setting(primary)
export const int ReflectionVersion = 1;

@utility(sample)
export int echo_value(const int& value)
{
	return value;
}

enum class RecordState
{
	@display(primary)
	Ready,
	Archived
};

struct BaseRecord
{
	int id;

	int read_id() const
	{
		return id;
	}
};

@entity(sample)
struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}

	static int identity(int value)
	{
		return value;
	}
};

int main()
{
	type<Record> info = type<Record>();
	print("{} ({})", info.name, info.kind());
	print("bases={}, fields={}, methods={}",
		info.base_count(), info.field_count(), info.method_count());

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}",
			field.name(), field.declared_type(), field.runtime_type(), field.is_static());
	}

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, params={}, static={}, virtual={}",
			method.name(), method.return_type(), method.parameter_count(),
			method.is_static(), method.is_virtual());
	}

	module_info module = module_info();
	print("module={}, functions={}, globals={}, types={}",
		module.path(), module.function_count(), module.global_count(), module.type_count());

	for (int i = 0; i < module.function_count(); i++)
	{
		function_info function_meta = function_info(i);
		print("function {}: returns={}, exported={}",
			function_meta.name(), function_meta.return_type(), function_meta.is_exported());
	}

	for (int i = 0; i < module.global_count(); i++)
	{
		global_info global_meta = global_info(i);
		print("global {}: declared={}, runtime={}, const={}",
			global_meta.name(), global_meta.declared_type(),
			global_meta.runtime_type(), global_meta.is_const());
	}

	type<RecordState> state_type = type<RecordState>();
	enum_member_info<RecordState> state =
		enum_member_info<RecordState>(state_type.qualified_name, 0);
	print("enum member {}={} annotated={}",
		state.name(), state.value(), state.has_annotation("display"));
	return 0;
}
```

実行可能なバージョンについては、[サンプル索引](../examples.md) のリフレクション エントリを参照してください。リフレクション API は、エントリポイントの戻り値の型とは独立しています。この例では、`int main` を使用し、明示的なプロセス ステータス コードを返します。

## 境界とエラー

- リフレクションは情報の照会と上記の型付き実行操作に対応します。任意のメンバーへの無制限な動的アクセスは提供しません。
- `field_count()`、`method_count()`、および enum-member の数は、現在のランタイム型情報を示します。インデックスを作成する前に境界を確認してください。
- 型情報の欠落、無効なインデックス、および適用できないクエリは実行時エラーとなります。無効なインデックスを空の結果として扱わないでください。

基礎となる型の規則については、[データ型](data-types.md)、[構造体](structs.md)、[クラス](classes.md)、および [列挙型](enums.md) を参照してください。ホスト側インターフェイスの説明は、[バインディング カタログ](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/bindings.md) で説明されています。

## 構造化されたリフレクション

<a id="reflection-metadata-contract"></a>

`type<T>`、field_info、method_info、module_info、function_info、global_info、enum_member_info は annotation_count/annotation/annotations、comment_count/comment/comments、documentation、comments_available、comments_retention、declaration、source_metadata を公開します。添字の前に件数を確認してください。annotation_info と annotation_argument_info は argument_count/argument/arguments を持ち、型付き値アクセサーはスカラーの種類と一致させます。definition() は登録済みの説明を返し、未登録の場合は is_valid() で確認します。

保持方式はホストが Full、Documentation、NoComments から選びます。Unavailable はソース情報がない状態です。NoComments でも注釈と別途登録された文書は残ります。メタデータは不変スナップショットを保持し、ホットリロードで旧オブジェクトの参照先が変わりません。アクセス制御を回避したり非公開メンバーを呼び出せるようにはしません。

[注釈とソースメタデータ](annotations.md)

## 入れ子型のリフレクション

`display_name()`、`is_nested()`、`is_local()`、`declaring_type_name()`、`declaring_function_name()`、`access()` で所属を調べます。型メンバーでない場合のアクセス種別は `none` です。`qualified_name` は具体的な実行時識別子を維持し、表示名は識別キーとして使用しません。

`nested_type_count()`、`nested_type_name(i)`、`nested_type_parameter_count(i)`、`nested_type_declaration(i)` はアクセス可能な直接の子型宣言を列挙し、未具体化のテンプレートも含みます。パラメーター数は外側から取り込んだ分を除きます。エイリアスは別の実行時型を作りません。クエリ生成時のアクセス文脈を保持し、ローカル名や非公開名をグローバル検索へ公開しません。

## テンプレートメタデータ

`module_info.template_declaration_count()` は具体化せず開いた宣言を列挙します。`template_declaration_id/name/kind/owner` は宣言インデックスを受け取ります。`template_parameter_count`、`template_own_parameter_count`、`template_enclosing_parameter_count` で環境を区別します。`template_parameter_id/name/kind/value_type/default/is_enclosing` は `(宣言インデックス, パラメーターインデックス)` を受け取り、結合リストは外側を先に並べます。

`type<T>`、`method_info<T>`、`function_info` は `template_declaration_id()`、`template_argument_count()` とインデックス付き `template_argument_encoding/name/kind/value_type` を公開します。外側には `template_enclosing_argument_count/encoding/name` を使います。通常の宣言は空 ID とゼロ件を返します。符号化は整数の幅、符号、列挙型の識別を保持し、表示名は識別キーではありません。照会は実行メソッドを生成せず、従来の型パラメーター数は意味を維持します。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/reflection.md) · [fr-FR](../../fr-FR/language-basics/reflection.md) · [ja-JP](reflection.md) · [ko-KR](../../ko-KR/language-basics/reflection.md) · [zh-CN](../../zh-CN/language-basics/reflection.md) · [zh-TW](../../zh-TW/language-basics/reflection.md)
