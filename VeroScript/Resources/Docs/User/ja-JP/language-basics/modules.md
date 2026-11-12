# モジュール

モジュールは、ソース ファイルの境界に沿って宣言を編成し、可視性を制御します。モジュールは通常、`.vs` ファイルで表され、`import` でインポートされ、`export` で他のモジュールで使用できるようになります。

## モジュールのインポート

`import` を使用して、ソース モジュールまたはライブラリ パッケージをインポートします。

```VeroScript
import "string.vs";

int main()
{
	print(std::strings::join(std::strings::split("a,b,c", ","), "-"));
}
```

モジュールが複数回インポートされると、そのモジュール ID によって重複したロードが抑制されます。モジュールをインポートしても、そのプライベート宣言がすべて公開されるわけではありません。外部モジュールで使用できるのは、エクスポートされた宣言のみです。

## 宣言のエクスポート

宣言の前に `export` を配置して、インポートするモジュールで使用できるようにします。

```VeroScript
export struct Box<T>
{
	T value;

	Box(T initial)
	{
		value = initial;
	}

	T get() const
	{
		return value;
	}
};

int hidden_value()
{
	return 99;
}
```

`Box` はモジュールのパブリック API の一部として利用できますが、`hidden_value` はエクスポートされないためプライベートです。関数、変数、構造体、クラス、インターフェイス、列挙型、および汎用宣言をエクスポートできます。

## モジュールの再エクスポート

`export import` はモジュールをインポートし、そのモジュールのエクスポートされた宣言を現在のモジュールを通じて利用できるようにします。

```VeroScript
export import "string.vs";
```

別のモジュールはファサードをインポートし、それによって再エクスポートされた宣言を使用できます。

```VeroScript
import "string_facade.vs";

string text = std::strings::join(std::strings::split("a,b", ","), "-");
```

再エクスポートしても、プライベート宣言はパブリック宣言に変換されず、ソース テキストもコピーされません。

## モジュールと名前空間

モジュールはソースファイルの依存関係と可視性を処理します。名前空間は、モジュール内の名前を整理します。モジュールは複数の名前空間を宣言でき、別のモジュールの名前空間からパブリック宣言をインポートできます。

## ライブラリ パッケージのインポート

ソース モジュールとバイナリ ライブラリ パッケージは、同じ `import` 構文を使用します。

```VeroScript
import "DemoLibrary.vslib";
```

ライブラリ パッケージは、ビルド時にエクスポートされた宣言のみを公開できます。パッケージの作成、確認、配布については、[パッケージと配布](../../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/packages.md) を参照してください。

## 公開マクロ

[プリプロセッサ — 公開マクロ](preprocessor.md#exported-macro-contract) を参照してください。

入れ子の型、ローカル宣言、アクセス規則、各階層のジェネリック引数については、[型のスコープ](scope.md)を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/modules.md) · [fr-FR](../../fr-FR/language-basics/modules.md) · [ja-JP](modules.md) · [ko-KR](../../ko-KR/language-basics/modules.md) · [zh-CN](../../zh-CN/language-basics/modules.md) · [zh-TW](../../zh-TW/language-basics/modules.md)
