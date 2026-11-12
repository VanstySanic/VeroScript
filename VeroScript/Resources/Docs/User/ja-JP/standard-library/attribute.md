# 属性とメソッド関数値

`attribute.vs` · `callable.vs` · `core.vs`

`std::attribute<T>` は固定値または同期 getter を保持します。コピーごとの設定は独立し、キャプチャの所有権は通常の言語規則に従います。読み取りのたびに再評価します。

```VeroScript
import "attribute.vs";

int read_count()
{
	return 42;
}
void main()
{
	std::attribute<int> count = 10;
	count.bind(read_count);
	int snapshot = count;
	assert(snapshot == 42 && count() == 42);
	count.reset();
	assert(!count.try_get().has_value());
}
```

`get()`、`operator()`、`T` への変換はそれぞれ一度読み取ります。`try_get()` は optional を返し、`get_or(fallback)` の引数は先に評価されます。実行エラーは伝播します。未設定、空の reader 結果、失効した弱参照だけが「値なし」です。

`set(value)`、`bind(fn()->T)`、`bind_optional(fn()->optional<T>)`、`reset()` は設定全体を置き換えます。`is_set()` と `is_bound()` は getter を実行しません。

`try_get(ref output)` は既存変数へ一度読み取り、値が得られたかを返します。未設定属性または空の optional リーダーでは `output` を変更せず、getter の例外は伝播します。既存変数へ書き込む場合にこのオーバーロードを使います。optional を返す従来の `try_get()` も利用できます。

`has_same_configuration(ref other)` は getter を評価せず設定の同一性を比較します。コピー直後は同じ識別を共有し、片方の設定を置換すると値が等しくても異なる設定になります。バインド結果は設定を変えずに変化できるため、この比較で動的 getter を省略してはいけません。

`std::make_attribute_by_name<T>(target, "read")` は強参照の引数なし getter を作成します。`T` は明示し、対象型は推論または第 2 型引数で指定します。名前は大文字小文字を区別し、作成時に解決します。通常のアクセスと仮想呼び出し規則を維持します。

`std::make_weak_attribute<T>(weak_target, getter)` は読み取りごとに一度ロックし、getter は `fn(U)->T` です。`std::make_weak_attribute_by_name<T>(weak_target, "read")` は引数なしのメソッドを名前でバインドします。失効後も設定済みです。構造体には `shared<S>` または `handle<S>` を使います。

`std::bind_method<F>(target, "method")` は通常の型付き関数値を返します。完全に一致する public インスタンスメソッドが必要です。対象を強く保持し、仮想ディスパッチを維持します。結果は `attribute.bind`、`delegate.bind`、`multicast_delegate.add` に渡せます。削除には追加した関数値を保存して使います。最小のインポートは `callable.vs` です。

単独 AOT：C++ は固定値、getter、弱参照をサポートし、C# と Python は固定値、名前付き getter、定数の強メソッド名をサポートします。Python のソース Lambda、C#/Python の弱バインドには既存の制限があります。動的な名前、ホストメソッド名、曖昧なジェネリック選択は実行時が必要で、エクスポート時に拒否します。VM、Mixed、JIT はコア実装を共有します。

C++／C# ソース出力は `try_get()` と `try_get(ref output)` に対応します。Python は `try_get()` に対応し、書き込み可能な `ref` 出力は拒否します。読み取り専用の `has_same_configuration(ref other)` は 3 種類とも対応します。この特別な変換は Python に一般的な書き込み参照を追加しません。

[`attribute_demo.vs`](../../../../Scripts/examples/attribute_demo.vs) · [`attribute_functions.vs`](../../../../Scripts/examples/attribute_functions.vs) · [`attribute_methods.vs`](../../../../Scripts/examples/attribute_methods.vs) · [`attribute_weak.vs`](../../../../Scripts/examples/attribute_weak.vs)

標準ライブラリの宣言は同じモジュール ID に統一してください。パッケージが attribute を含めて再公開する場合、その宣言または共有依存を使用します。別のソース版 `core.vs` / `attribute.vs` を同時に取り込むと公開名が重複します。

複数のライブラリでは、`export import "attribute.vs";` から共通の `shared-stdlib.vslib` を先に作成します。各ライブラリはそのコンポーネントを import し、アプリケーションもソースの別コピーではなく同じコンポーネントとライブラリを import します。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/attribute.md) · [fr-FR](../../fr-FR/standard-library/attribute.md) · [ja-JP](attribute.md) · [ko-KR](../../ko-KR/standard-library/attribute.md) · [zh-CN](../../zh-CN/standard-library/attribute.md) · [zh-TW](../../zh-TW/standard-library/attribute.md)
