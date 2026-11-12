# constexpr

`constexpr` はコンパイル時に決まるスカラー値を要求し、`const` を含意します。ローカル、モジュール、名前空間の変数と、明示的な `static constexpr` フィールドで使用できます。

自由関数、名前空間の関数、静的メソッドおよび対応するテンプレートを constexpr にできます。値渡しの引数と void 以外の戻り値は bool、固定幅整数、char、列挙型に限ります。初期化済みスカラー変数、代入、分岐、ループ、switch、制限付き再帰を使用できます。実行時引数による通常の呼び出しも可能です。

通常の if は未実行の分岐も検査し、if constexpr は選択した分岐だけを検査します。ネイティブ呼び出し、変更可能なグローバル状態、オブジェクト構築、参照、浮動小数点、文字列、コンテナー、例外、非同期タスクは使用できません。通常の関数引数から明示的な constexpr ローカル変数を初期化することはできません。

定数初期化、値テンプレート引数と既定値、固定配列長、整数／列挙型の case、列挙値は同じ厳密な評価器を使います。オーバーフロー、不正な変換、ゼロ除算、予算超過はエラーです。短絡評価では選択されない分岐の演算を実行しません。

エクスポートには通常の import を使います。コアのコンパイル／パッケージサービスで `.vslib` を作成すると、定義環境と非公開ヘルパーの依存関係が保持されます。元のプロジェクトなしで利用でき、読み込み時にコンパイルプロセッサーを再実行しません。

ホストは `FCompileOptions::ConstantEvaluationLimits` または直接コンパイラーの `SetConstantEvaluationLimits` を使います。既定値は作業量 1,000,000、呼び出し深度 64、式／ネイティブ深度 256、一時領域 16 MiB、キャッシュ 8 MiB です。制限は正数、キャッシュはゼロで無効になります。解析のキャンセルと期限は有効で、VM 実行制限とは別です。

リフレクションの記述子は `is_constexpr()`、`std::compiler` の宣言ビューは `is_constexpr` を公開します。問い合わせは評価を実行しません。Core AOT は同じバイトコードを使用し、独立ソース出力は検査済みの型付きリテラルと既存のテンプレート制限を使用します。

```VeroScript
constexpr int square(int value)
{
	return value * value;
}
constexpr int capacity = square(8);

int main()
{
	constexpr int count = capacity + 1;
	int input = 3;
	print(count);         // 65
	print(square(input)); // 9
	return 0;
}
```

[constexpr.vs](../../../../Scripts/examples/constexpr.vs)

独立ソース出力では既存の機能制限が維持されます。C++ には確定した配列長を渡します。C#／Python が未対応の固定長配列や、インスタンスごとに異なるジェネリック定数には Core AOT を使用します。関数のデフォルト引数には既存のリテラル／テンプレート値の制限が適用されます。

## if constexpr による分岐

条件には bool、整数、char、非 scoped 列挙型の定数を使用できます。scoped 列挙型には明示的な変換が必要です。constexpr 呼び出しとテンプレート値に対応し、通常の実行時引数は使用できません。テンプレートの各インスタンスが独立に分岐を選択します。両方の分岐に正しい構文が必要ですが、型検査、追加のインスタンス化、戻り経路と Lambda キャプチャの解析は選択した分岐だけに適用します。選択した分岐のブロックスコープは保持されます。通常の関数も同じ規則を使い、通常の if は両方を検査します。

```VeroScript
template<int N>
int sum()
{
	if constexpr (N == 0)
		return 0;
	else
		return N + sum<N - 1>();
}
```

constexpr 関数でも使用できますが、通常の引数は定数呼び出し中でも条件にはなりません。VM、JIT、Core AOT は同じ選択結果を使用します。インスタンス依存のジェネリックのソース出力には引き続き Core AOT を使用します。

ローカルの非 scoped 列挙型のメンバー名も定数条件に使用でき、通常の字句スコープの隠蔽規則に従います。遅延評価する const 初期化式は宣言時の環境を保持し、後続の同名宣言に影響されません。Lambda は外側の列挙定数を実行時ストレージのキャプチャなしで使用でき、テンプレート内のローカル列挙型は具体的なインスタンスごとに評価されます。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-reference/constexpr.md) · [fr-FR](../../fr-FR/language-reference/constexpr.md) · [ja-JP](constexpr.md) · [ko-KR](../../ko-KR/language-reference/constexpr.md) · [zh-CN](../../zh-CN/language-reference/constexpr.md) · [zh-TW](../../zh-TW/language-reference/constexpr.md)
