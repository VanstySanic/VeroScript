# 01: 最初のスクリプト

このチュートリアルの目標は 1 つあります。それは、スクリプトを作成して実行し、出力行を確認することです。

## 何を学ぶか

- 最小限の VeroScript プログラムに含まれるもの。
- スクリプトの実行方法。
- スクリプトを実行せずにチェックする方法。
- `main` および `print` の機能。

## ステップ 1: ファイルを作成する

作業ディレクトリに `hello.vs` を作成します。

```VeroScript
int main()
{
	print("hello, VeroScript");
	return 0;
}
```

重要な部分が 2 つあります。`main` はスクリプトのエントリ ポイントであり、`int` はプロセスの終了ステータスを返します。 `print` は文字列を標準出力に書き込みます。 `void main` は引き続きサポートされますが、完全なプログラムは通常 `int main` を使用し、成功すると明示的に `0` を返します。

## ステップ 2: 確認して実行する

`hello.vs` を含むディレクトリから次のコマンドを実行します。

```powershell
veroscript --check hello.vs
veroscript hello.vs
```

出力は次のようになります。

```text
hello, VeroScript
```

`--check` は、`main` を実行せずにファイルをコンパイルし、問題を報告します。最初に開発中に使用し、チェックが成功した後にスクリプトを実行します。

## ステップ 3: 出力を有用なものにする

スクリプトを次のように変更します。

```VeroScript
int main()
{
	string name = "Ada";
	print("hello, " + name);
	return 0;
}
```

次の内容が表示されるはずです。

```text
hello, Ada
```

`string name` は文字列変数を宣言します。 `+` 演算子は 2 つの文字列を連結します。

## 何か問題が発生した場合

- `veroscript` が見つからない場合は、CLI がインストールされており、`PATH` にあることを確認します。
- `hello.vs` が見つからない場合は、端末の現在のディレクトリを確認するか、絶対パスを渡します。
- `veroscript --lang en-US --check hello.vs` を実行して英語の診断を要求します。

## 演習

1. 名前を自分のものに置き換えます。
2. `string language = "VeroScript";` を追加し、両方の変数を使用して文を出力します。
3. セミコロンを削除し、`--check` を実行し、診断を読み取り、ファイルを修正します。

次へ: [引数の確認、実行、読み取り](02-cli-and-arguments.md)。

完全な最小限の例は [`examples/hello.vs`](../../../../../VeroScript/Resources/Scripts/examples/hello.vs) です。構文については、[基本構文](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/basic-syntax.md) を参照してください。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/getting-started/01-first-script.md) · [fr-FR](../../fr-FR/getting-started/01-first-script.md) · [ja-JP](01-first-script.md) · [ko-KR](../../ko-KR/getting-started/01-first-script.md) · [zh-CN](../../zh-CN/getting-started/01-first-script.md) · [zh-TW](../../zh-TW/getting-started/01-first-script.md)
