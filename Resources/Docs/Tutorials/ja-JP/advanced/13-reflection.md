# 13: ランタイムリフレクション

このチュートリアルでは、VeroScript のリフレクション API を説明するための小さな型インスペクターを構築します。リフレクションは現在のスクリプト型システムのメタデータに加え、スクリプトで公開されたフィールドやメソッドを型指定して操作できます。ツール、エディター、シリアル化アダプターに役立ちますが、任意のメンバーを動的な文字列呼び出しインターフェイスに変えたり、アクセス制御を回避したりするものではありません。

## ステップ 1: メタデータを宣言に追加する

注釈は、型、フィールド、メソッド、自由関数、グローバル、列挙型メンバーに添付できます。

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

	int score;

	int add_score(int amount)
	{
		score += amount;
		return score;
	}
};
```

この言語は、アノテーション名、キー、および値を保存しますが、それらのアプリケーション固有の意味は解釈しません。ここで、`entity`、`data`、および `call` はユーザー定義のメタデータ規約であり、動作を自動的に生成するキーワードではありません。

次の手順は同じファイル内で続行されます。この `Record` 宣言を保持し、作業を進めるにつれて `main` を置き換えるか拡張します。

## ステップ 2: 型情報を取得する

`type<Record>` は、`Record` 自体を説明します。

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	print("name={}, qualified={}", info.name, info.qualified_name);
	print("kind={}, abstract={}", info.kind(), info.is_abstract());
	print("fields={}, methods={}", info.field_count(), info.method_count());
	return 0;
}
```

`name` は短縮名で、`qualified_name` は修飾名です。フィールドおよびメソッドの情報オブジェクトは、その修飾名と 0 から始まるインデックスを使用して構築されるため、最初に `type<T>` オブジェクトを保持するのが一般的です。

## ステップ 3: フィールドを列挙する

`field_info<Record>(info.qualified_name, i)` を使用して各フィールドを読み取ります。

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}, mutable={}",
			field.name(), field.declared_type(), field.runtime_type(),
			field.is_static(), field.is_mutable());

		for (int j = 0; j < field.metadata_count(); j++)
		{
			print("  metadata {}={}", field.metadata_key(j), field.metadata_value(j));
		}
	}

	return 0;
}
```

フィールド情報は、フィールドが現在の型で直接宣言されているか、およびコンテキスト依存の `mutable` 修飾子を使用しているかも報告します。`declared_type()` はソース宣言の表示に、`runtime_type()` はランタイムに記録された型の表示に使用できます。

## ステップ 4: メソッドとパラメータを列挙する

`method_info<T>` は、メソッド修飾子とパラメーター情報を提供します。

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, static={}, virtual={}, const={}",
			method.name(), method.return_type(), method.is_static(),
			method.is_virtual(), method.is_const());

		for (int j = 0; j < method.parameter_count(); j++)
		{
			print("  param {}: name={}, type={}, ref={}, const={}",
				j, method.parameter_name(j), method.parameter_type(j),
				method.parameter_is_ref(j), method.parameter_is_const(j));
		}
	}

	return 0;
}
```

`parameter_type(j)` は、`ref` または `const ref` を削除した後の基になる型を返します。ツールが完全なパラメーター セマンティクスを再構築する必要がある場合は、`parameter_is_ref(j)` と `parameter_is_const(j)` を個別に確認してください。

## ステップ 5: モジュールの表面を検査する

`module_info` は、エントリ モジュールから表示されるフリー関数、トップレベル グローバル、およびスクリプト タイプを列挙します。インデックスをそのカウントで制限して、`function_info(i)` と `global_info(i)` を構築します。

```VeroScript
module_info module = module_info();
print("module={}, globals={}, types={}",
	module.path(), module.global_count(), module.type_count());

for (int i = 0; i < module.function_count(); i++)
{
	function_info function_meta = function_info(i);
	print("{} -> {}", function_meta.name(), function_meta.return_type());
}

for (int i = 0; i < module.global_count(); i++)
{
	global_info global_meta = global_info(i);
	print("global {}: declared={}, const={}",
		global_meta.name(), global_meta.declared_type(), global_meta.is_const());
}
```

すべての宣言記述子は、`has_metadata(key)`、`metadata(key)`、および `has_annotation(name)` を提供します。 enum-member アノテーションの場合は、`enum_member_info<E>(type<E>().qualified_name, i)` を構築します。

たとえば、`RecordState` の最初のメンバーを読み取ります。

```VeroScript
type<RecordState> state_type = type<RecordState>();
enum_member_info<RecordState> state =
	enum_member_info<RecordState>(state_type.qualified_name, 0);

print("{}={}, annotated={}",
	state.name(), state.value(), state.has_annotation("display"));
```

## ステップ 6: カウントとエラーを処理する

リフレクション クエリは、空の可能性がある動的オブジェクトを返すのではなく、カウントとインデックスを使用します。インデックスを作成する前に、フィールド、メソッド、関数、グローバル、型、または列挙メンバーの数を確認してください。範囲外のインデックス、欠落している型情報、および適用できないクエリは実行時エラーです。

実行可能な型指定リフレクションは、公開されたスクリプトのインスタンス フィールドとインスタンスまたは静的メソッドに限られます。アクセス制御を回避したり、制限のない文字列ベースの検索や呼び出しを提供したりするものではありません。`__reflection_*` 形式の名前は内部ヘルパー シンボルであり、スクリプト API ではないため、直接使用しないでください。

## ステップ 7: 選択したメンバーの読み取り、書き込み、呼び出し

メタデータから descriptor を選択し、フィールド型または戻り値型を明示します。メソッド引数は位置で渡します。

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	int score_index = -1;
	int add_index = -1;
	int identity_index = -1;
	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> candidate = field_info<Record>(info.qualified_name, i);
		if (candidate.name() == "score")
		{
			score_index = i;
		}
	}
	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> candidate = method_info<Record>(info.qualified_name, i);
		if (candidate.name() == "add_score")
		{
			add_index = i;
		}
		if (candidate.name() == "identity")
		{
			identity_index = i;
		}
	}
	if (score_index < 0 || add_index < 0 || identity_index < 0) return 1;

	field_info<Record> score = field_info<Record>(info.qualified_name, score_index);
	method_info<Record> add = method_info<Record>(info.qualified_name, add_index);
	method_info<Record> identity = method_info<Record>(info.qualified_name, identity_index);
	Record record = Record();
	score.set<int>(record, 10);
	print("score={}", score.get<int>(record));
	print("add_score={}", add.invoke<int>(record, 5));
	print("identity={}", identity.invoke_static<int>(21));
	return 0;
}
```

`get<V>` と `set<V>` はインスタンス フィールドを操作し、`invoke<R>` はインスタンス メソッドを、`invoke_static<R>` は静的メソッドを呼び出します。選択したメンバーの値型または戻り値型は `V` または `R` と完全に一致する必要があります。対応しないシグネチャと backend ごとの制限は完全なリファレンスを参照してください。

## 演習

1. 静的メソッドを `Record` に追加し、`is_static()` で見つけます。
2. `Record` の直接の基底クラスを `base_count()` および `base_name(i)` で表示します。
3. `RecordState` のメンバーを `enum_member_count()`、`enum_member_name(i)`、および `enum_member_value(i)` とともに表示します。
4. `@data` メタデータを含むフィールドのみを出力するツールを作成します。

完全なルールについては、[Reflection](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/reflection.md) を参照してください。検査用の例は [`examples/reflection.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection.vs)、実行可能な操作の例は [`examples/reflection_executable.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection_executable.vs) です。ホスト インターフェイスの説明とスクリプト リフレクションは別のレイヤーです。ホスト カタログについては、[バインディング カタログ](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/bindings.md) で説明されています。

これでメインのチュートリアル パスが完了します。必要に応じて、[ツールチェーン リファレンス](../../../User/ja-JP/toolchain/index.md) または [標準ライブラリ リファレンス](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/index.md) を参照してください。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/advanced/13-reflection.md) · [fr-FR](../../fr-FR/advanced/13-reflection.md) · [ja-JP](13-reflection.md) · [ko-KR](../../ko-KR/advanced/13-reflection.md) · [zh-CN](../../zh-CN/advanced/13-reflection.md) · [zh-TW](../../zh-TW/advanced/13-reflection.md)
