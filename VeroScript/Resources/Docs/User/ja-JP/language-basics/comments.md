# コメント

コメントはソース ファイルにメモを追加します。 VeroScript は、単一行コメントとブロック コメントをサポートしています。

## 単一行のコメント

単一行のコメントは `//` で始まり、現在の行の終わりで終わります。

```VeroScript
// Explain the purpose of the variable below.
int retry_count = 3;

int value = 1; // A comment may follow a statement.
```

## コメントをブロックする

ブロック コメントは `/*` で始まり `*/` で終わります。複数行にまたがる場合があります。

```VeroScript
/*
	This note applies to the following code block.
*/
int result = 0;
```

コメント自体は実行文ではありません。閉じていないブロックコメントは字句エラーになります。ホストは全コメント、文書コメントのみ、またはコメントなしを選択でき、保持されたコメントと文書はソースメタデータから参照できます。有効化されたコンパイル拡張はその情報をコード生成に利用できます。注釈の保持はコメント保持とは独立しています。

## エディターツール向けのソース文書

Tooling のホバーは、ソース宣言の直前に連続して置かれたコメントを読み取ります。ソース定義されたプリプロセッサーマクロの補完項目にも直前のコメントを含められます。host 補完の文書は binding metadata から取得します。`//`（`///` を含む）または `/* ... */` ブロックを使い、コメントと宣言の間に空行を入れないでください。これは Tooling の規約です。コンパイラーにとってコメントは通常の字句コメントのままです。

`@param 名前 説明` はパラメーターを、`@return 説明` と `@returns 説明` は戻り値を説明します。ホバーではアクティブな言語カタログのラベルを使ってこれらのタグを表示します。未知のタグは通常のコメント本文として表示されます。マクロ補完の文書はコメントの記述をそのまま保持します。

```VeroScript
// Adds two integers.
// @param left First addend.
// @param right Second addend.
// @return The sum.
int Add(int left, int right)
{
	return left + right;
}
```

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/comments.md) · [fr-FR](../../fr-FR/language-basics/comments.md) · [ja-JP](comments.md) · [ko-KR](../../ko-KR/language-basics/comments.md) · [zh-CN](../../zh-CN/language-basics/comments.md) · [zh-TW](../../zh-TW/language-basics/comments.md)
