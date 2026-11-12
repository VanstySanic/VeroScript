# 型のエイリアスと特殊な宣言

VeroScript は型エイリアスをサポートし、`= default` または `= delete` を使用してコンストラクター、デストラクター、メソッド、演算子、および自由関数の状態を明示的にします。

## 型のエイリアス

`using` を使用して、既存の型に別の名前を付けます。

```VeroScript
using Ints = array<int>;
using Scores = map<string, int>;

Ints values = Ints{1, 2, 3};
Scores scores = Scores();
```

`typedef` は型エイリアスでもサポートされています。

```VeroScript
typedef Names = array<string>;
Names names = Names();
```

型エイリアスは新しいランタイム型を作成しません。エイリアスはモジュール スコープまたはローカル ブロックで宣言できます。ローカル エイリアスはそのスコープ内でのみ表示されます。

## エイリアスのエクスポート

モジュールスコープでは、名前空間内も含めて `export using` または `export typedef` を使えます。どちらも `Alias = Type` の形式です。通常のエイリアスはモジュール内に、ローカルエイリアスはそのスコープ内に限定されます。インポート側では名前空間で修飾した公開エイリアスを使えます。`export import` は公開エイリアスを再公開し、非公開のものは公開しません。`using namespace` は自身のモジュールとスコープの名前検索にのみ影響し、`export using namespace` は拒否されます。

次のファイルを aliases.vs として保存します。

```VeroScript
namespace Measures
{
	export using Count = int;
	export typedef Label = string;
	using InternalCount = int;
}
```

```VeroScript
import "aliases.vs";

int main()
{
	Measures::Count count = 42;
	Measures::Label label = "total";
	print(count);
	return 0;
}
```

利用側を main.vs として保存し、`veroscript main.vs` を実行すると `42` が表示されます。`export import "aliases.vs";` を含むファサードをインポートしても利用できます。バイナリライブラリを配布するには `veroscript --emit-package aliases.vslib aliases.vs` を実行し、利用側のインポート先を `"aliases.vslib"` に変更します。

## デフォルトの宣言

コンストラクターまたはデストラクターは、`= default` を使用して、コンパイラーが生成した実装を要求できます。

```VeroScript
struct Token
{
	int value = 7;

	Token() = default;
	~Token() = default;
};
```

デフォルトの構築では、フィールドのデフォルトとタイプの通常のデフォルト ルールが結合されます。 `= default` を記述すると、生成された実装の意図が明示的になり、宣言が型インターフェイスで表示されたままになります。

## 削除された宣言

`= delete` を使用して、呼び出し形式を明示的に禁止しながら署名を保持します。

```VeroScript
struct Token
{
	Token() = default;
	Token(string text) = delete;

	int read() const
	{
		return 7;
	}
	int read(string mode) const = delete;
};
```

削除されたコンストラクター、メソッド、演算子、または関数を呼び出すと、コンパイル診断が生成されます。削除された宣言は、不要な暗黙的な呼び出しや不正なオーバーロードを拒否するのに役立ちます。空のランタイム関数は作成されません。

`= default` および `= delete` は宣言の一部であり、関数本体と組み合わせることはできません。仮想機能とインターフェイス スロットは、継承と実装のルールを満たしている必要があります。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/declarations.md) · [fr-FR](../../fr-FR/language-basics/declarations.md) · [ja-JP](declarations.md) · [ko-KR](../../ko-KR/language-basics/declarations.md) · [zh-CN](../../zh-CN/language-basics/declarations.md) · [zh-TW](../../zh-TW/language-basics/declarations.md)
