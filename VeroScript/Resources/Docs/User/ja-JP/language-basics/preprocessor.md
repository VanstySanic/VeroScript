# プリプロセッサ

プリプロセッサは、字句解析および意味解析の前に実行されます。条件付きコンパイル、オブジェクトのようなマクロ、関数のようなマクロ、文字列化、トークンの貼り付け、およびモジュールのインクルードをサポートします。

## オブジェクトのようなマクロ

オブジェクトのようなマクロは、名前を事前定義されたトークン シーケンスに置き換えます。

```VeroScript
#define VERSION 2
#define MAX_COUNT 64

int version = VERSION;
int capacity = MAX_COUNT;
```

マクロ展開は、コンパイラが型チェックを実行する前に行われます。マクロには、それ自体の VeroScript 静的型はありません。展開された式も、通常の構文と型の規則を満たしている必要があります。

## 関数のようなマクロ

関数のようなマクロは引数を受け入れます。展開によって優先順位が変わらないように、複合式では引数を括弧で囲みます。

```VeroScript
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int value = MAX(3, 8);
```

単純なトークン置換にはマクロを使用します。型チェック、オーバーロード、またはデバッグ動作が重要な場合は、関数またはジェネリック関数を優先します。

## 文字列化とトークンの貼り付け

関数のようなマクロ内で、`#` は引数をテキストに変換し、`##` は隣接するトークンを 1 つのトークンに貼り付けます。

```VeroScript
#define STR(x) #x
#define CAT(a, b) a##b

int VALUE = 9;
string name = STR(hello);
int result = CAT(VAL, UE);
```

文字列化とトークンの貼り付けはマクロの展開中に行われます。貼り付けた結果は、有効な VeroScript トークンである必要があります。

## 可変個引数マクロ

関数のようなマクロは、`...` を使用して追加の引数を受け入れ、`__VA_ARGS__` を通じて展開できます。

```VeroScript
#define LOG(fmt, ...) print(fmt, ##__VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

現在の実装では、可変長引数が指定されていない場合に余分なコンマを削除する `##__VA_ARGS__` がサポートされています。また、`__VA_OPT__(tokens)` もサポートしています。括弧内のトークンは、可変長引数が空でない場合に出力され、それ以外の場合は省略されます。

```VeroScript
#define LOG(fmt, ...) print(fmt __VA_OPT__(,) __VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

`__VA_OPT__` は、可変個引数マクロの置換リスト内でのみ有効であり、その後にバランスのとれた括弧のペアが続く必要があります。

## 動的組み込みマクロ

コンパイラは、現在のソース モジュール コンテキストから生成された 2 つのマクロも提供します。

|マクロ |拡張 |
| --- | --- |
| `__FILE__` |現在のモジュールの正規化された論理パス。パスのないソース テキストの場合は `<memory>`。 |
| `__LINE__` | `1` で始まるマクロ呼び出しを含む物理ソース行。 |

これらは、通常のグローバル マクロではなく、各モジュールの前処理コンテキストで計算されます。

```VeroScript
#define REPORT(name) print("{}:{} {}", __FILE__, __LINE__, name)

int main()
{
	REPORT("ready");
	return 0;
}
```

`__FILE__`、`__LINE__`、`__VA_ARGS__`、および `__VA_OPT__` は予約名です。スクリプト、コマンドラインの初期定義、ホストの事前定義マクロ、または関数マクロ パラメーターはそれらを再定義できません。

## 条件付きコンパイル

`#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、および `#endif` を使用して、後のコンパイルに参加するソースを選択します。

```VeroScript
#define VERSION 2

#if VERSION >= 2
int feature_level = 2;
#else
int feature_level = 1;
#endif
```

ソースで使用できるディレクティブは `#include`、`#define`、`#define_export`、`#undef`、`#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、`#endif`、`#error` です。条件では `defined(NAME)` と `defined NAME` の両形式でマクロの存在を確認できます。先にマクロ展開を行い、未定義のまま残った識別子はゼロとして扱います。

`#if` 式では 64 ビット整数定数（10 進、C 形式の先頭ゼロによる 8 進、16 進）、括弧、単項 `+ - ! ~`、算術 `* / % + -`、シフト、比較、等値、ビット `& ^ |`、短絡 `&& ||`、`?:` を使用できます。スクリプト リテラルの 2 進接頭辞と桁区切りは使用しません。実際に評価される分岐のゼロ除算とオーバーフローはエラーですが、短絡された分岐は評価されません。

有効な分岐の `#error message` は前処理を停止し、前後の空白を除いたメッセージを診断に保持します。無効な条件分岐では無視されます。メッセージはマクロ展開されません。`#warning`、`#line`、`#pragma`（`#pragma once` を含む）は未対応で、ディレクティブ名を含むエラーになります。

コマンドライン ツールは `-D` または `--define` で単純なオブジェクト形式マクロを提供できます。ソース ファイルでは、前述の関数形式および可変長マクロも定義できます。

```powershell
veroscript -D ENABLE_LOGGING=1 script.vs
```

## モジュールを含める

`#include` は `export import` の別名です。公開宣言と公開マクロをインポートして再エクスポートし、テキスト挿入は行いません。

```VeroScript
#include "math.vs"

int main()
{
	print(std::clamp_int(15, 0, 10));
}
```

モジュールの依存関係を表現し、再エクスポートする場合は、`import` および `export import` を使用します。 `#include` を C++ スタイルのテキストインクルードメカニズムとして扱わないでください。

## 公開マクロ

<a id="exported-macro-contract"></a>

`#define_export` は `#define` と同じ構文で、オブジェクト形式、関数形式、可変引数を使用できます。通常の定義は非公開です。明示的な `import` はパス文字列の直後から対象の最終公開マクロを使用可能にし、`export import` は再公開も行います。`#include` は公開宣言と公開マクロを含む `export import` の別名です。`.vslib` ライブラリにも適用されます。ホストの暗黙的インポートは、利用側ソースの先頭から確定済みの公開マクロも提供しますが、再公開は行いません。提供モジュールとその明示的な依存先で先にインターフェースを確定し、提供モジュール間のマクロ依存には通常の `import` を使います。非公開マクロは漏出せず、競合には通常のインポート規則を適用します。例えば `veroscript --implicit-import settings.vs main.vs` を使うと、`main.vs` にインポートを書かずに公開マクロを利用できます。

```VeroScript
// settings.vs
#define PRIVATE_VALUE 7
#define_export VALUE 21
#define_export DOUBLE(x) ((x) * 2)

```

```VeroScript
// main.vs
import "settings.vs";
int main()
{
	print(DOUBLE(VALUE));
	return 0;
}
```

置換内容は呼び出し側の現在のマクロ環境で展開され、非公開の補助マクロを捕捉しません。必要な補助マクロも明示的に公開してください。`__FILE__` と `__LINE__` は呼び出し位置です。同名で元の定義が異なる場合、内容が同じでも競合します。インポートした定義を置き換える前に `#undef` してください。同じ元定義の再インポートは冪等で、`#undef` 後には定義を復元します。通常の `#define` による再定義は非公開になり、`#undef` は公開インターフェースから削除します。

公開マクロ定義のない循環依存は引き続き利用できます。循環ソースの依存閉包に非アクティブなものを含め `#define_export` がある場合、公開インターフェースを確定できずロードを拒否します。ソースのインポート循環を解消してください。ライブラリの条件付き公開はビルド時に確定し、アプリケーションパッケージは公開マクロを保持しません。

<a id="preprocess-resource-limits"></a>

## プリプロセスのリソース制限

既定では、中間置換テキストを含む各ファイルの展開出力を 32 MiB に制限します。一回のコンパイルは全ファイルで 268,435,456 内部単位の作業予算を共有します。いずれかの上限を超えるとプリプロセスエラーになり、コンパイルを拒否します。この単位は VM 命令数でも実行時間の保証でもありません。ネイティブホストは `FPreprocessContext::Budget` に `FPreprocessBudget` を渡せます。ゼロは上限ゼロであり、無制限ではありません。コンパイル拡張の実行予算とは別です。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/preprocessor.md) · [fr-FR](../../fr-FR/language-basics/preprocessor.md) · [ja-JP](preprocessor.md) · [ko-KR](../../ko-KR/language-basics/preprocessor.md) · [zh-CN](../../zh-CN/language-basics/preprocessor.md) · [zh-TW](../../zh-TW/language-basics/preprocessor.md)
