# I/Oモジュール

モジュールファイル: `io.vs`

次のようにインポートします。

```VeroScript
import "io.vs";
```

## ファイルとディレクトリ

|機能 |説明 |
| --- | --- |
| `std::io::read_file(string path)` |完全なテキスト ファイルを読み取ります。 |
| `std::io::write_file(string path, string content)` |テキストファイルを上書きします。 |
| `std::io::append_file(string path, string content)` |テキストが追加され、不足している場合はファイルが作成されます。 |
| `std::io::read_bytes(string path)` | ファイル全体を所有バイトバッファーへ読みます。 |
| `std::io::read_bytes(string path, int64 max_bytes)` | 明示した最大バイト数で読み、超過はエラーになります。 |
| `std::io::write_bytes(string path, region source)` | バッファーまたはビューのバイトで上書きします。 |
| `std::io::append_bytes(string path, region source)` | バッファーまたはビューのバイトを追記します。 |
| `std::filesystem::exists(string path)` |パスが存在するかどうかをテストします。 |
| `std::filesystem::delete_file(string path)` |ファイルを削除します。 |
| `std::filesystem::is_file(string path)` | 通常ファイルかどうかを調べます。 |
| `std::filesystem::is_directory(string path)` | ディレクトリかどうかを調べます。 |
| `std::filesystem::remove_dir(string path)` | 空のディレクトリを一つ削除します。 |
| `std::filesystem::list_dir(string path)` |ディレクトリエントリ名の配列を返します。 |
| `std::filesystem::mkdir(string path)` |ディレクトリとその親を作成します。 |
| `std::filesystem::cwd()` |現在の作業ディレクトリを返します。 |
| `std::filesystem::chdir(string path)` |現在の作業ディレクトリを変更します。 |

`std::filesystem::exists` はパスが存在しないことを確認した場合にだけ `false` を返し、検査エラーは報告します。`std::filesystem::delete_file` はファイルとシンボリックリンクを削除できますが、実際のディレクトリは拒否します。`std::filesystem::chdir` はホストプロセス全体の作業ディレクトリを変更し、他のセッションにも影響します。環境書き込みとプロセスディレクトリ変更の両方の権限が必要です。C++ ホストでは `FScriptCapabilities::bAllowProcessDirectoryChange`、C・C#・Python ホストではスクリプト権限ビット `PROCESS_DIRECTORY_CHANGE` を使用します。

アクセス可能なファイル システムの範囲は、ホストの権限と現在の作業ディレクトリによって異なります。読み取り、書き込み、削除、ディレクトリ作成、またはディレクトリ変更の失敗は実行時に報告されます。

`std::filesystem::is_file` と `std::filesystem::is_directory` はシンボリックリンクをたどり、存在しないパスでは `false` を返します。ファイル読み取り権限が必要で、その他の検査エラーは報告されます。`std::filesystem::remove_dir` は削除権限が必要です。存在しないパスでは `false`、ファイル・空でないディレクトリ・ディレクトリへのシンボリックリンクではエラーになります。再帰削除はしません。

## 標準入力

|機能 |説明 |
| --- | --- |
| `std::io::read_line()` |末尾の改行なしで 1 行を読み取ります。 EOF で空の文字列を返します。 |
| `std::io::try_read_line()` | 通常の EOF の場合だけ空の optional を返します。 |
| `std::io::try_read_token()` | 次のトークン、または通常の EOF で空の optional を返します。 |
| `std::io::pause()` | 1 つの入力行を待ちます。非対話型ホストでは EOF ですぐに戻ります。 |
| `std::io::read_token()` |次の空白で区切られたトークンを読み取ります。 |
| `std::io::read_string()` |文字列トークンを読み取ります。 |
| `std::io::read_int()` |整数トークンを読み取ります。 |
| `std::io::read_double()` |浮動小数点トークンを読み取ります。 |
| `std::io::read_bool()` |ブールトークンを読み取ります。 |

トークン入力関数は、空白で区切られた値を消費します。完全な行が重要な場合は `std::io::read_line` を使用し、値を個別に読み取る必要がある場合は型指定された `read_*` 関数を使用します。


- [標準ライブラリ基盤](foundation.md)

`region` は buffer、view、readonly_view の各オーバーロードを表します。ファイル読み込みは空も含むディレクトリーを拒否します。バイト I/O 上限、明示的な最大値 0、EOF の規約は [基礎リファレンス](foundation.md#io-contract)を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/io.md) · [fr-FR](../../fr-FR/standard-library/io.md) · [ja-JP](io.md) · [ko-KR](../../ko-KR/standard-library/io.md) · [zh-CN](../../zh-CN/standard-library/io.md) · [zh-TW](../../zh-TW/standard-library/io.md)
