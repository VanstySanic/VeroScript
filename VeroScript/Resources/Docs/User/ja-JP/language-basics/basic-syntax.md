# 基本的な構文

このページでは、ステートメント、ブロック、識別子、キーワードなど、VeroScript ソース ファイルの基本構造について説明します。

## ソースファイル

VeroScript ソース ファイルでは、`.vs` 拡張子が使用されます。ソース ファイルには、グローバル変数、関数、構造体、クラス、インターフェイス、列挙型、名前空間、およびモジュール宣言を含めることができます。

アプリケーション スクリプトは通常、`main` エントリ ポイントを提供します。ライブラリ モジュールは、エントリ関数を定義せずに型と関数をエクスポートできます。 `main` は `int` または `void` を返す場合があります。プロセス終了コードを明示する必要がある場合は、`int` をお勧めします。

```VeroScript
int global_value = 1;

int add(int left, int right)
{
	return left + right;
}

int main()
{
	print(add(global_value, 2));
	return 0;
}
```

## 推奨されるソース スタイル

ドキュメントの VeroScript スニペットでは、インデント レベルごとに Tab 文字、独立した行に左中括弧、1 行に 1 つの通常のステートメント、および `class`、`struct`、`interface`、および `enum` 宣言の後に末尾のセミコロンを使用します。この言語は末尾のセミコロンのないいくつかの簡潔な形式を受け入れますが、ドキュメント コードでは完全な形式を優先する必要があります。スタンドアロンのサンプル ファイルは、examples README に記載されたフォーマッター スタイルに従います。

## ステートメントとセミコロン

通常のステートメントはセミコロンで終わります。

```VeroScript
int value = 1;
value = value + 1;
print(value);
```

関数宣言、型宣言、および制御フロー ステートメントでは、複合ステートメントに中かっこを使用します。通常、空白や改行によってステートメントの意味が変わることはありません。

```VeroScript
if (value > 0)
{
	print("positive");
}
```

## 識別子

識別子は、変数、関数、型、フィールド、名前空間、およびモジュール メンバーに名前を付けます。識別子には ASCII 文字、数字、アンダースコアを含めることができますが、数字で始めることはできません。

```VeroScript
int item_count = 3;
string display_name = "VeroScript";
```

識別子では大文字と小文字が区別されます。 `item_count`、`ItemCount`、および `itemCount` は異なる名前です。

## キーワード

キーワードには言語定義の意味があり、通常の識別子としては使用できません。キーワード セットには次のものが含まれます。

```text
void bool char short int long float double string wstring
int8 uint8 int16 uint16 int32 uint32 int64 uint64
unsigned auto channel array map multimap set multiset
queue priority_queue stack ordered_map ordered_multimap deque ordered_multiset
fn function
if else while do for switch case default return break continue fallthrough goto
try catch finally throw struct class interface enum
import export using typedef namespace friend
new delete this const ref static explicit
public protected private virtual override final
where operator template in
true false nullptr
```

キーワード セットは言語文法の一部です。新しいキーワードを使用すると、有効な識別子が使用できなくなる可能性があるため、ライブラリ コードではキーワードをパブリック名として使用しないようにする必要があります。

`do` は `do ... while` 用の予約語になりました。以前 `do` を識別子として使っていたソースは名前を変更してください。

## 同型の宣言グループと生文字列

トップレベル、名前空間、ローカルの宣言では、その位置で有効な `const` / `ref` 修飾子と先頭の完全な型が同じ場合に複数の変数を導入できます。初期化子は左から右へ評価され、名前も同じ順で可視になります。

```VeroScript
const int first = 1, second = first + 1;
for (int index = 0, end = 3; index < end; index++)
{
	print(index);
}
```

各項目には固有の初期化子と通常の宣言チェックが適用されます。特に `auto first = 1, second = 2;` は意図的に拒否されます。名前ごとの配列接尾辞で異なる形状を宣言することもできません。共有する配列長は型の前置部分に書いてください。

UTF-8 の生文字列は固定形式 `r"..."` を使います。バックスラッシュと改行をエスケープ処理せずに保持します。

```VeroScript
string path = r"C:\tools\new";
string text = r"first line
second line";
```

次に現れる最初の `"` がリテラルを閉じるため、生テキストには引用符を含められません。ワイド生文字列、カスタム区切り、文字列補間はありません。

## 意図的な構文境界

VeroScript は単独の空文 `;` を拒否します。意図的に空の本体には `{}` を使用してください。一般的なコンマ式、`f(value: 1)` のような名前付き引数、`auto` 戻り値推論、構造化束縛、`sizeof` や `alignof` などの C++ レイアウト演算子には対応しません。宣言グループは同型の宣言にだけ使用し、明示的な戻り値型と言語またはコンテナー API を使用してください。

## parser の安全上限

式、単項式、プリプロセッサ `#if` 式のネストは最大 256 レベルです。statement の
ネストは 32 レベル、expression chain は 256 link までです。上限を超えると
`expression nested too deeply`、`statement nested too deeply`、`#if expression
nested too deeply`、`expression chain is too long` などの diagnostic が出ます。
自動生成コードが深くなりすぎる場合は、式や control-flow block を分割してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/basic-syntax.md) · [fr-FR](../../fr-FR/language-basics/basic-syntax.md) · [ja-JP](basic-syntax.md) · [ko-KR](../../ko-KR/language-basics/basic-syntax.md) · [zh-CN](../../zh-CN/language-basics/basic-syntax.md) · [zh-TW](../../zh-TW/language-basics/basic-syntax.md)
