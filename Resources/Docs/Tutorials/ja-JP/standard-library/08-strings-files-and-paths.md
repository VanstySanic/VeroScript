# 08: 文字列、ファイル、パス

このチュートリアルでは、テキスト ファイルを読み取り、その内容を消去し、結果を別のファイルに書き込むという小さなタスクを完了します。文字列、I/O、およびパス モジュールを一緒に使用します。

## ステップ 1: 作業ディレクトリを準備する

スクリプトの横に `input.txt` を作成します。

```text
  VeroScript
  is practical
```

`clean_text.vs` を作成します。

```VeroScript
import "io.vs";
import "path.vs";
import "string.vs";

int main()
{
	string input_path = "input.txt";
	string output_path = std::filesystem::join_path("output", "clean.txt");

	if (!std::filesystem::is_file(input_path))
	{
		print("missing: " + input_path);
		return 1;
	}

	std::filesystem::mkdir(std::filesystem::dirname(output_path));
	string content = std::io::read_file(input_path);
	string cleaned = std::strings::trim(content);
	std::io::write_file(output_path, cleaned + "\n");

	print("wrote " + output_path);
	return 0;
}
```

`io.vs` はファイルとディレクトリを処理し、`path.vs` はパスを結合し、`string.vs` はテキストをクリーンアップします。これらの役割を分離しておくと、パス ロジックを手書きしたり、文字列ヘルパーを繰り返したりすることがなくなります。

## ステップ 2: 実行して結果を検査する

```powershell
veroscript --check clean_text.vs
veroscript clean_text.vs
Get-Content output/clean.txt
```

出力ファイルの先頭と末尾にある ASCII 空白が削除されます。`trim` は端のバイト `0x09`～`0x0D` と空白 (`0x20`) だけを取り除き、内部および非 ASCII バイトは保持します。`is_file` はシンボリックリンクをたどり、入力が通常ファイルを指すかどうかを返します。`read_file` および `write_file` は現在の作業ディレクトリからの相対パスを解決するため、ターミナル コマンドが実行されている場所に注意してください。

## ステップ 3: 個々の行を処理する

各行を個別に処理する必要がある場合は、テキストを分割して結合します。

```VeroScript
import "io.vs";
import "string.vs";

int main()
{
	string content = std::io::read_file("input.txt");
	array<string> lines = std::strings::split(content, "\n");
	array<string> cleaned = array<string>();

	for (string line : lines)
	{
		if (std::strings::trim(line) != "")
		{
			cleaned.push(std::strings::trim(line));
		}
	}

	std::io::write_file(
		"clean-lines.txt",
		std::strings::join(cleaned, "\n") + "\n");
	return 0;
}
```

Windows テキストでは `\r\n` が使用される場合があります。入力が複数のプラットフォームから来る可能性がある場合は、1 つの行末規則を想定するのではなく、明示的に `\r` を処理します。

## ステップ 4: 結果をフォーマットする

ファイルを処理した後、読みやすい概要が役立つことがよくあります。 `format` は中括弧フィールドを使用し、文字列を返します。 `print` の複数引数形式は同じ方言を使用します。

```VeroScript
import "string.vs";

int main()
{
	string path = "output/clean.txt";
	int line_count = 2;
	string summary = format("wrote {}, lines={}", path, line_count);
	print(summary);
	printf("status=%s%%\n", "100");
	return 0;
}
```

printf スタイルの変換が必要な場合は、`formatf` または `printf` を使用します。 `%d` または `%.2f` 構文を `format` 文字列に含めないでください。フィールド、幅、精度、変換については、[文字列言語の動作](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/strings.md#%E3%83%95%E3%82%A9%E3%83%BC%E3%83%9E%E3%83%83%E3%83%88) を参照してください。

## 演習

1. 出力拡張子を `std::filesystem::change_extension` から `.clean.txt` に変更します。
2. 入力ファイルの `std::filesystem::basename`、`dirname`、および `extension` を出力します。
3. 出力が存在する場合は、`std::io::append_file` の処理レコードを追加します。
4. `format` を使用して概要を生成し、`formatf` を使用して小数点以下 2 桁の期間を出力してみます。
5. `output/clean.txt` を削除し、`std::filesystem::is_directory` で `output` を確認してから、空のディレクトリを `std::filesystem::remove_dir` で削除します。

[`examples/path_helpers.vs`](../../../../../VeroScript/Resources/Scripts/examples/path_helpers.vs) を比較します。完全な API は [I/O](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/io.md)、[Paths](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/path.md)、および [Strings](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/string.md) にあります。

次へ: [同時実行性とイベント](09-concurrency-and-events.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/standard-library/08-strings-files-and-paths.md) · [fr-FR](../../fr-FR/standard-library/08-strings-files-and-paths.md) · [ja-JP](08-strings-files-and-paths.md) · [ko-KR](../../ko-KR/standard-library/08-strings-files-and-paths.md) · [zh-CN](../../zh-CN/standard-library/08-strings-files-and-paths.md) · [zh-TW](../../zh-TW/standard-library/08-strings-files-and-paths.md)
