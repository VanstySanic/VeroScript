# 07: モジュール、インポート、およびプリプロセッサ

プログラムが複数のファイルにまたがると、モジュールによって依存関係とパブリック API が整理されます。プリプロセッサはコンパイル前にソースを選択します。このチュートリアルはプライベート実装から始まり、他のファイルが必要とする関数のみをエクスポートします。

## ステップ 1: モジュールを作成する

`math_utils.vs` を作成します。

```VeroScript
export int square(int value)
{
	return value * value;
}

int hidden_value()
{
	return 99;
}
```

`export` は、`square` をモジュールのパブリック宣言の一部にします。 `hidden_value` はモジュールに対してプライベートであり、パブリック ライブラリ API ではありません。

`main.vs` を作成します。

```VeroScript
import "math_utils.vs";

int main()
{
	print(square(7));
	return 0;
}
```

同じディレクトリから両方のファイルを実行します。

```powershell
veroscript --check main.vs
veroscript main.vs
```

## ステップ 2: モジュールを再エクスポートする

`facade.vs` を作成します。

```VeroScript
export import "math_utils.vs";
```

エントリ ポイントはファサードのみをインポートできるようになりました。

```VeroScript
import "facade.vs";

int main()
{
	print(square(8));
	return 0;
}
```

再エクスポートすると、元のモジュールがエクスポートした宣言が引き続き公開されます。 `hidden_value` は公開されません。

## ステップ 3: コンパイル時条件を追加する

プリプロセッサはコンパイル時にソースを選択します。実行時の判断には通常の `if` ステートメントを使用します。

```VeroScript
#define MESSAGE "development"

#ifdef RELEASE
#define MESSAGE "release"
#endif

int main()
{
	print(MESSAGE);
	return 0;
}
```

コマンドラインで `RELEASE` を定義します。

```powershell
veroscript --check main.vs
veroscript -D RELEASE main.vs
```

マクロはコンパイル前に展開されます。マクロに関数と同じ名前を付けることは避け、ロジックが本格化する場合は実際の関数を使用してください。

## ステップ 4: 無名名前空間で宣言をまとめる

無名名前空間は `namespace { ... }` と記述します。追加の修飾を使わずに宣言をまとめられます。VeroScript では透明に扱われ、合成された名前空間を作成せず、C++ のような内部リンケージも提供しません。

```VeroScript
namespace
{
	int local_helper()
	{
		return 6;
	}
}

int main()
{
	print(local_helper());
	return 0;
}
```

名前付き名前空間の内部では、メンバーは外側の名前空間の修飾を保持します。

```VeroScript
namespace Diagnostics
{
	namespace
	{
		int code()
		{
			return 200;
		}
	}
}

int main()
{
	print(Diagnostics::code());
	return 0;
}
```

別のモジュールから参照する個々の宣言には `export` を付けてください。無名名前空間自体はエクスポートされず、それだけで宣言を非公開にするものでもありません。

## 演習

1. `cube` 関数をエクスポートし、メイン モジュールから呼び出します。
2. 文字列モジュールと数学モジュールの両方を再エクスポートするファサードを作成します。
3. デバッグ ビルドに追加情報を出力する `#if` ブランチを追加します。
4. 名前付き名前空間の内部にある無名名前空間へヘルパーを配置し、外側の名前空間による修飾を使って呼び出します。

[モジュール](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/modules.md)、[プリプロセッサ](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/preprocessor.md)、[名前空間](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/namespaces.md) を参照してください。配布サンプルは、[`examples/reexport_main.vs`](../../../../../VeroScript/Resources/Scripts/examples/reexport_main.vs) および [`examples/preprocessor.vs`](../../../../../VeroScript/Resources/Scripts/examples/preprocessor.vs) です。

次: [文字列、ファイル、パス](../standard-library/08-strings-files-and-paths.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/language/07-modules-and-preprocessor.md) · [fr-FR](../../fr-FR/language/07-modules-and-preprocessor.md) · [ja-JP](07-modules-and-preprocessor.md) · [ko-KR](../../ko-KR/language/07-modules-and-preprocessor.md) · [zh-CN](../../zh-CN/language/07-modules-and-preprocessor.md) · [zh-TW](../../zh-TW/language/07-modules-and-preprocessor.md)
