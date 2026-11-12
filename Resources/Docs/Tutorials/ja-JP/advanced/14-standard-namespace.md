# 14: `std` 標準名前空間を使用する

このチュートリアルでは、修飾されていないモジュール宣言を使用する小さなスクリプトを、推奨されるパブリック `std` スペルにリファクタリングします。 `std::` は、標準名のソースレベルのエイリアスです。 2 番目のコンテナー タイプを作成したり、実行時の動作を変更したりすることはありません。

## ステップ 1: スコア スクリプトを準備する

`scores.vs` を作成します。

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	array<int> scores = {81, 92, 88};
	optional<int> selected = optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	print(format("selected={}, root={}", selected.get(), root));
	return 0;
}
```

まず確認して実行します。

```powershell
veroscript --check scores.vs
veroscript scores.vs
```

最初のバージョンでは、モジュール所有の数学関数をすでに修飾しています。モジュール所有の標準ライブラリ関数は公開 `std` 名前空間からのみ利用でき、グローバルに残るのは小さなコア組み込み API だけです。

## ステップ 2: `std::` に移行する

アルゴリズムではなく、名前修飾のみを変更します。

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	std::array<int> scores = {81, 92, 88};
	std::optional<int> selected = std::optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	std::print(std::format("selected={}, root={}", selected.get(), root));
	std::print(std::strings::contains("scores", "core"));
	return 0;
}
```

同じチェックおよび実行コマンドを再度実行します。 `std::array<int>` と `array<int>` は同じ言語タイプを指し、`std::print` と `print` は同じランタイム関数を指します。 `std::` はコンパイル中に解決され、ランタイム ディスパッチは追加されません。

数学、入出力、ファイルシステム、文字列、パス、並行処理、時間、スレッド、環境、および型付き入力のエントリポイントは名前空間専用です。非修飾の標準ライブラリ名や `std::now` などのフラットな互換名は提供されません。ユーザーコードでは、同じ非修飾名を持つ無関係な関数を引き続き定義できます。

## ステップ 3: インポート境界を理解する

標準名前空間のエイリアスはモジュールのインポートではありません。ソース標準ライブラリ関数には、それを提供するモジュールが引き続き必要です。

- `math.vs` は、`sqrt`、`sin`、`cos`、`tan`、`floor`、`ceil`、`round`、`trunc`、`pow`、`exp`、`log`、`log2`、`asin`、`acos`、`atan2`、`isfinite`、`isnan`、`pi`、`e`、`clamp`、および `clamp_int` を提供します。
- `string.vs` は、`std::strings` を通じて `last_find`、`repeat`、`remove_prefix`、`remove_suffix` を含む文字列クエリを提供します。
- `io.vs` は、`std::io` を通じてストリーム操作を提供し、`std::filesystem` を通じてファイルシステム操作を提供します。
- `path.vs` は、`std::filesystem` にパス操作を追加します。 `concurrent.vs` は `std::concurrent` を使用します。

ランタイムのコンソール関数にはモジュールのインポートは必要ありません。
改行なしの出力には `std::console::write` と `std::console::error`、保留中の出力を
公開するには `std::console::flush()`、対話型ターミナルの検出には
`std::console::is_interactive()` を使用します。

たとえば、`import "math.vs";` を削除しても、`std::sqrt` が数学モジュールを自動的にロードすることはありません。最初にモジュールをインポートし、次に修飾名を使用して、依存関係が明示的なままになるようにします。

## ステップ 4: コンテナーと値のタイプを移行する

標準の名前空間には、マップ、セット、キュー、チャネル、タスク、およびリフレクション情報タイプも含まれます。

```VeroScript
int main()
{
	std::map<string, int> totals = {"math": 2, "io": 1};

	std::print("math={}, io={}", totals["math"], totals["io"]);
	return 0;
}
```

新しいコードでは、グループ化された `std` スペルを一貫して使用します。実装宣言はモジュール内で修飾されていないため、名前空間層はすべてのバックエンドを同じシンボル ID にマップできます。

## ステップ 5: 値の保護と変換を明示する

現在の例では、`std::readonly<T>` エイリアスとグローバルな変換組み込みも組み合わせています。`std::` はすべての組み込みに付けるわけではないため、`cast<T>`、`try_cast<T>`、`const_cast<T>` は修飾なしで使用します。

```VeroScript
std::readonly<int> published_score = std::readonly<int>(selected.get());
double exact_score = cast<double>(scores[1]);
std::optional<int> narrowed = try_cast<int>(exact_score);

const std::array<int> explicitly_unlocked = {13};
// 基になるストレージが書き込み可能だと確認できる場合だけ、この const 解除を使用します。
const_cast<std::array<int>>(explicitly_unlocked).push(published_score.get());
```

`cast<T>` は変換を要求し、`try_cast<T>` は呼び出し側が検査できる `optional<T>` を返します。`const_cast<T>` は値を変換したり実行時型チェックを行ったりせず、コンパイル時の const アクセスだけを解除するため、意図的かつ制御された用途に限定してください。完全な規則については、[型変換](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-reference/type-conversions.md)を参照してください。

## ルート修飾名

先頭の `::` はグローバル/ルート スコープから検索を開始します。現在の名前空間、ローカル エイリアス、`using namespace` のフォールバックを迂回します。

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

ルートの宣言を意図する場合は `::Name` を使います。`std::readonly<T>` は `readonly<T>` ラッパーの標準名前空間表記でもあり、別の実行時型ではありません。


## 演習

1. この例を `std::unordered_map` に置き換えないでください。完全なエイリアス テーブルを参照し、代わりにどのサポートされているコンテナが適切であるかを説明してください。
2. `std::set<int>` を追加し、それを使用して重複スコアを削除します。
3. `string.vs` をインポートし、`std::strings::contains` または `std::strings::starts_with` を呼び出します。

完全な [`std` 標準名前空間](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/standard-namespace.md) テーブルを参照してください。実行可能な例は [`examples/standard_namespace.vs`](../../../../../VeroScript/Resources/Scripts/examples/standard_namespace.vs) です。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/advanced/14-standard-namespace.md) · [fr-FR](../../fr-FR/advanced/14-standard-namespace.md) · [ja-JP](14-standard-namespace.md) · [ko-KR](../../ko-KR/advanced/14-standard-namespace.md) · [zh-CN](../../zh-CN/advanced/14-standard-namespace.md) · [zh-TW](../../zh-TW/advanced/14-standard-namespace.md)
