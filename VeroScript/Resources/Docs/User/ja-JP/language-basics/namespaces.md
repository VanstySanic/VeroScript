# 名前空間

ネームスペースは名前を整理し、スクリプトの個別の領域間の衝突を防ぎます。名前空間には、変数、関数、構造体、クラス、インターフェイス、列挙型、およびその他の宣言を含めることができます。

## 名前の宣言と修飾

`namespace` で名前空間を宣言し、`::` でそのメンバーにアクセスします。

```VeroScript
namespace Util
{
	int Seed = 10;

	int next_seed()
	{
		Seed = Seed + 1;
		return Seed;
	}
}

int current = Util::Seed;
int next = Util::next_seed();
```

修飾名により、所有する名前空間が明示的になります。型メンバー、列挙型メンバー、静的メンバーにも `::` を優先します。

```VeroScript
enum class State
{
	Ready,
	Running
};

State state = State::Running;
```

## グローバル修飾

先頭の `::` はグローバル/ルート スコープから名前検索を開始します。

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

`::GlobalValue` は現在の名前空間、ローカル型エイリアス、`using namespace` のフォールバックを迂回します。ルートの型や名前空間にも使用でき、`using namespace ::Name` も有効です。

## 無名名前空間

`namespace { ... }` は無名名前空間を宣言します。VeroScript では透明な宣言グループとして扱われ、合成された名前空間を作成せず、C++ のような翻訳単位の内部リンケージも提供しません。メンバーは周囲の名前空間に残ります。

グローバル スコープでは、無名名前空間のメンバーを追加の修飾なしで使用できます。

```VeroScript
namespace
{
	int transparent_helper()
	{
		return 40;
	}
}

int main()
{
	print(transparent_helper() + 2);
	return 0;
}
```

名前付き名前空間の内部でも同じ規則が適用されます。メンバーは外側の名前空間の修飾を保持します。

```VeroScript
namespace Outer
{
	namespace
	{
		int nested_helper()
		{
			return 9;
		}
	}
}

int main()
{
	print(Outer::nested_helper());
	return 0;
}
```

無名名前空間はエクスポートやモジュールの可視性を変更しません。別のモジュールから使用する宣言には個別に `export` を付けてください。ソースの整理に透明なグループが必要な場合に無名名前空間を使用します。

## 標準の名前空間

組み込みコンテナ、ユーティリティ値型、およびパブリック ランタイム関数には、予約された `std` 名前空間で推奨される表記があります。

```VeroScript
import "string.vs";

std::array<int> values = {1, 2, 3};
std::optional<int> selected = std::optional<int>(values[0]);
std::print(std::format("selected: {}", selected.get()));
```

`array`、`optional`、`print`、`format` などの既存のグローバル スペル
互換性エイリアスは残ります。 `std::` はコンパイル時に解決され、何も追加されません。
VM または JIT ディスパッチ。 `std::sqrt` などのソース ライブラリ関数には引き続き必要があります
通常のモジュールのインポート。修飾は `import`/`export` をバイパスしません。
グローバル `std` 名前空間は予約されており、スクリプトで拡張することはできません。

ネームスペースは名前を整理しますが、エクスポート ステータスは自動的に変更されません。宣言がそのモジュールの外部に表示されるかどうかは、引き続きモジュールの `export` ルールによって制御されます。

完全な型と関数の別名テーブルについては、[`std` 名前空間](standard-namespace.md) を参照してください。

## ネームスペースの使用

`using namespace` は、名前空間の直接メンバーを、それを含むスコープで使用できるようにします。

```VeroScript
namespace MathTools
{
	int base = 2;
}

using namespace MathTools;

int value = base + 3;
```

ディレクティブは、そのディレクティブを含むスコープとそれに続くコードに影響します。名前を再エクスポートしたり、兄弟の名前空間に影響を与えたりすることはありません。名前が衝突する可能性がある場合は、完全修飾を維持してください。

```VeroScript
int value = MathTools::base + 3;
```

安定した名前境界には名前空間を使用します。別のソース ファイルから宣言を取り込むことが目的の場合は、モジュール `import` を使用します。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/namespaces.md) · [fr-FR](../../fr-FR/language-basics/namespaces.md) · [ja-JP](namespaces.md) · [ko-KR](../../ko-KR/language-basics/namespaces.md) · [zh-CN](../../zh-CN/language-basics/namespaces.md) · [zh-TW](../../zh-TW/language-basics/namespaces.md)
