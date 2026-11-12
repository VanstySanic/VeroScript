# 02: 引数の確認、実行、読み取り

スクリプトが別の入力を受け入れるとすぐに、ソース内のデータのハードコーディングは不便になります。このチュートリアルでは、コマンド ライン引数を読み取り、`--check` を使用してプログラムを実行する前に検証します。

## ステップ 1: 引数を受け入れる

`argv_echo.vs` を作成します。

```VeroScript
int main(string[] args)
{
	print("argc=" + to_string(len(args)));
	for (int i = 0; i < len(args); i = i + 1)
	{
		print(args[i]);
	}
	return 0;
}
```

`main(string[] args)` は、スクリプト パスの後に引数を受け取ります。スクリプト ファイル名は `args` には含まれていないため、`len(args)` がスクリプトに渡される値の数になります。

## ステップ 2: 引数を渡す

```powershell
veroscript --check argv_echo.vs
veroscript argv_echo.vs red blue
```

出力は次のようになります。

```text
argc=2
red
blue
```

引数は文字列です。`to_int` などの数値変換関数は数値または真偽値を受け取り、文字列の数値解析は行いません。引数を数値として使う場合は、受け付ける文字列形式を定義して検証してから計算してください。[変換関数](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/builtins.md)も参照してください。

## ステップ 3: 挨拶ツールを構築する

次のプログラムを `argv_echo.vs` と同じディレクトリに `greet.vs` として保存します。

```VeroScript
int main(string[] args)
{
	string name = "world";
	if (len(args) > 0)
	{
		name = args[0];
	}

	print("hello, " + name);
	return 0;
}
```

プログラムを検査してから、引数なしと引数ありで実行します。

```powershell
veroscript --check greet.vs
veroscript greet.vs
veroscript greet.vs VeroScript
```

引数なしの実行では既定の名前を使い、引数ありの実行では最初の引数を名前として使います。

## ステップ 4: 実行バックエンドを選択する

通常の実行では、デフォルトで混合バックエンドが使用されます。実行モードを比較するときにバックエンドを明示的に選択できます。

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm argv_echo.vs one two
veroscript --jit-backend="/path/to/Jit" --backend=jit argv_echo.vs one two
```

VM のコマンドは Runtime 同梱のインタープリターで実行します。JIT のコマンドを試す前に `/path/to/Jit` を互換バックエンドへ置き換えてください。ロードに失敗するとスクリプトを実行する前に終了します。成功時は、どちらも前と同じく `one` と `two` を `argv_echo.vs` に渡します。プロジェクトの既定値、自動選択、JIT の無効化は [CLI バックエンドの参照](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md#cli-backend-policy)を確認してください。

デバッグには VM デバッグ パスが使用されます。ここでは、ワークフローを覚えておいてください。開発中に `--check` を使用し、実行を比較または制限する理由がある場合にのみバックエンドを選択します。

## 演習

1. 引数が指定されていない場合は、`usage: greet.vs <name>` を出力し、ゼロ以外のコードを返します。
2. 2 番目の引数が存在することを確認し、`len` でその長さを表示します。
3. `--lang en-US` と `--lang ja-JP` の診断を比較します。

次: [値、条件、およびループ](../language/03-values-and-control-flow.md)。

完全な例は [`examples/argv_echo.vs`](../../../../../VeroScript/Resources/Scripts/examples/argv_echo.vs) です。オプションについては、[CLI リファレンス](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md) を参照してください。

性能比較では入力とバックエンドを揃え、通常実行でウォームアップを行ってください。デバッグ、保持された初期化、有界クリーンアップは VM を使い、永続セッションの条件を満たす通常関数は選択した JIT を使えます。バイトコードキャッシュは起動時のコンパイル処理を減らします。セッション JIT キャッシュと制限は [C++ 組み込みガイド](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/embedding.md#threads-and-hot-reload)を参照してください。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/getting-started/02-cli-and-arguments.md) · [fr-FR](../../fr-FR/getting-started/02-cli-and-arguments.md) · [ja-JP](02-cli-and-arguments.md) · [ko-KR](../../ko-KR/getting-started/02-cli-and-arguments.md) · [zh-CN](../../zh-CN/getting-started/02-cli-and-arguments.md) · [zh-TW](../../zh-TW/getting-started/02-cli-and-arguments.md)
