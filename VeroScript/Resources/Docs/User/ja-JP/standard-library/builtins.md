# 組み込み

[標準ライブラリ基盤](foundation.md#foundation-imports) を参照してください。

ビルトインと [標準ライブラリ モジュール](index.md) の間の境界は意図的です。最小のランタイム プリミティブにはビルトインを使用し、より高レベルの文字列、ファイル、パス、同時実行性、およびイベント機能には対応するモジュールを優先します。

## 出力

|機能 |説明 |
| --- | --- |
| `print(value)` | 1 つの値を標準出力に書き込みます。書式文字列と追加の引数では、中括弧の書式設定が使用されます。 `{{` および `}}` はリテラル中括弧を発行します。 |
| `printf(format, ...)` | printf スタイルの変換でフォーマットされた出力を書き込みます。形式構文は `print` とは異なります。 |
| `eprint(value)` | 1 つの値をエラー ストリームに書き込みます。 |

```VeroScript
int main()
{
	int score = 97;
	print("score={}", score);
	printf("score=%03d", score);
	eprint("diagnostic");
	return 0;
}
```

フォーマットされた結果を変数に格納する必要がある場合は、標準ライブラリの `format` 関数または `formatf` 関数を使用します。フィールド、変換、幅、精度、エラー境界については、[文字列言語の動作](../language-basics/strings.md#%E3%83%95%E3%82%A9%E3%83%BC%E3%83%9E%E3%83%83%E3%83%88) および [文字列モジュール](string.md) を参照してください。

## コンソール ストリーム

`std::console` 関数はランタイムが提供するため、インポートは必要ありません。
ストリームと対話型ターミナルの動作を明確にするため、これらは名前空間専用です。

| 機能 | 説明 |
| --- | --- |
| `std::console::write(value)` | 改行を追加せずに標準出力へ値を書き込みます。 |
| `std::console::error(value)` | 改行を追加せずにエラー ストリームへ値を書き込みます。 |
| `std::console::flush()` | 保留中の標準出力をフラッシュします。 |
| `std::console::clear()` | ホストが対応している場合に対話型出力を消去し、実行されたかどうかを返します。 |
| `std::console::is_interactive()` | 標準入力と標準出力の両方が対話型かどうかを返します。 |

改行の前やホスト ツールへ制御を返す前に進捗を表示する場合は
`std::console::flush()` を使用します。デバッグ アダプターはフラッシュされた標準
出力とエラー出力を別々の出力イベントとして転送します。

### 端末制御

| API | |
| --- | --- |
| `supports_color()`, `supports_cursor()`, `supports_clear()`, `is_output_terminal()` | 色、カーソル、画面／行消去、出力端末の機能を個別に確認します。 |
| `set_color(string foreground, string background)`, `reset_style()` | 前景／背景色の設定とスタイルのリセット。 |
| `clear_line(string mode)` | カーソル以降、以前、または行全体を消去。現在位置を含み、カーソルは移動しません。 |
| `move_cursor(int column, int row)`, `set_cursor_visible(bool visible)` | 0 始まりの列／行へ移動。カーソルの表示／非表示。 |
| `width()`, `height()` | 表示領域の列数／行数。不明なら 0。 |

制御関数は `bool` を返し、未対応または失敗なら `false` です。リダイレクト先には制御コードを自動出力しません。入力の対話性は不要です。`clear()` は表示画面だけを消去し、原点へ移動します。履歴は消去しません。座標は `0..32766` の整数で、既知の画面外は `false`。型、色名、行モードの誤りはエラーです。変更にはコンソール出力権限が必要です。色名は `black`、`red`、`green`、`yellow`、`blue`、`magenta`、`cyan`、`white` と `bright_` 系列、`default`。

終了・エラー・キャンセル時にスタイルと表示を可能な限り復元します。POSIX／カスタムホストは既定スタイルと表示状態に、Windows VM と各 AOT は元のネイティブ状態に復元します。埋め込みホストは既定で未対応。C++、C ABI、C#、Python の照会／制御コールバックと改行しないテキストコールバックで接続できます。ワーカースレッドへの同期はホストが行います。モバイル／ブラウザー出力はプレーンテキストです。例：`console_demo.vs`。

```VeroScript
if (std::console::supports_color())
{
	std::console::set_color("bright_green", "default");
}
std::console::write("Ready\n");
std::console::flush();
std::console::reset_style();
```

## コンバージョン数

|機能 |説明 |
| --- | --- |
| `to_int(value)` | `int` に変換されます。浮動小数点値はゼロに向かって切り捨てられます。範囲外および非有限の値はエラーです。 |
| `to_double(value)` | `double` に変換されます。 |
| `to_float(value)` | `float` に変換します。結果は binary32 として保存されます。 |
| `to_unsigned(value)` | `unsigned int` に変換され、負の値、範囲外の値、無効な値が拒否されます。浮動小数点値はゼロに向かって切り捨てられます。 |
| `to_int64(value)` |範囲チェックを使用して `int64` に変換します。 |
| `to_uint64(value)` |範囲チェックを使用して `uint64` に変換します。 |
| `to_string(value)` |値を文字列に変換します。 |
| `to_wstring(value)` | `string` または `wstring` 値をワイド文字列に明示的に変換します。 |

```VeroScript
int main()
{
	int count = to_int(12.75);
	double ratio = to_double(3);
	unsigned int flags = to_unsigned(true);
	string text = to_string(count);
	wstring wide = to_wstring(text);
	print("count={}, ratio={}, flags={}", count, ratio, flags);
	print(wide);
	return 0;
}
```

変換関数は、任意のテキストを数値として解析しません。数値入力には `std::io::read_int` または `std::io::read_double` を使用します。ファイルまたはプロトコル テキストの場合は、スクリプト内で解析ルールを明示的に定義します。

## 数値ヘルパーとコレクションヘルパー

|機能 |説明 |
| --- | --- |
| `abs(value)` |数値の絶対値を返します。 |
| `min(a, b)` | 2 つの数値のうち小さい方を返します。 |
| `max(a, b)` | 2 つの数値のうち大きい方を返します。 |
| `len(value)` |文字列、配列、または標準コンテナの要素数を返します。マルチセットの場合は、出現の合計数を返します。 |
| `range(int64 count)` | `int64` の個数を受け取り、`0` から `count - 1` を含む `array<int>` を返します。負の個数では空の配列を返し、`1000000` を超えると実行時エラーです。 |

```VeroScript
int main()
{
	array<int> values = range(4);
	print("size={}", len(values));
	print("low={}, high={}, distance={}", min(-2, 5), max(-2, 5), abs(-9));
	return 0;
}
```

`len(value)` は非負の `int64`（`0` から `9223372036854775807`）を返します。null、未対応の型、表現できない長さは実行時エラーです。既存のコンテナーのサイズ、容量、要素数の問い合わせも `int64` を返します。長さと線形インデックスには `int64` を使ってください。`int` への代入は通常の範囲検査付き数値変換を行い、値を表現できない場合は失敗します。型を広げてもネイティブの確保上限やバックエンドのコンテナー制限はなくなりません。

## 環境、時間、制御フロー

|機能 |説明 |
| --- | --- |
| `std::environment::getenv(name)` |ホストプロセス環境変数を読み取ります。変数が欠落している場合は空の文字列が返されます。名前を空にすることはできません。 |
| `std::environment::setenv(name, value)` | setenv(name, "") は存在する空値を設定します。削除には unsetenv を使い、未存在の削除も成功します。try_getenv は未存在と空値を区別します。名前は空・=・NUL を拒否し、値は NUL を拒否します。名前の大小文字規則は Windows/POSIX に従います。 |
| `std::chrono::now()` |システム クロックに従って、Unix エポックからの秒数を `double` として返します。 |
| `std::chrono::steady_now()` | 単調時計の値を秒単位で返します。経過時間には読み取り値の差を使用し、通常の時刻として扱わないでください。 |
| `std::this_thread::sleep(milliseconds)` |現在のスクリプト スレッドを指定されたミリ秒間一時停止します。引数は負ではない必要があります。 |
| `assert(condition[, message])` | `bool` 条件と、任意の `string` メッセージを受け取ります。条件が `false` の場合は実行時エラーを報告し、`true` の場合は出力しません。 |
| `exit(code)` |指定された整数の終了コードでスクリプトの終了を要求します。 |

```VeroScript
int main()
{
	std::environment::setenv("VERO_MODE", "docs");
	print(std::environment::getenv("VERO_MODE"));

	double started = std::chrono::steady_now();
	std::this_thread::sleep(1);
	assert(std::chrono::steady_now() >= started);
	exit(0);
	return 0;
}
```

`assert` の引数は 1 個または 2 個です。第 1 引数は `bool`、指定する場合の第 2 引数は `string` でなければなりません。失敗時は空でないメッセージが診断に含まれ、空のメッセージでは既定のアサーション診断が使われます。失敗はスクリプト例外ではなくランタイムエラーなので、`catch (...)` や型付きハンドラーでは捕捉できません。

環境変数はホスト プロセスに属します。変更が後のホスト ロジックに影響するかどうかは、ホスト プラットフォームと埋め込みモードによって異なります。

## 標準入力

|機能 |説明 |
| --- | --- |
| `std::io::read_token()` |標準入力から空白で区切られた次のトークンを読み取ります。 |
| `std::io::read_string()` |次の文字列トークンを読み取ります。 |
| `std::io::read_int()` |次の整数トークンを読み取り、解析します。 |
| `std::io::read_double()` |次の浮動小数点トークンを読み取り、解析します。 |
| `std::io::read_bool()` | `true` または `false` を読み取り、解析します。 |

入力の終わりまたは無効なトークンにより診断が生成されます。これらの関数は、単純なコマンドライン入力を目的としています。より複雑な入力プロトコルは、ホストまたはファイル指向の標準ライブラリ API に属します。

## 実装専用の名前

`__` で始まる名前（例: `__read_file`、`__format`）は、コンパイラ、ランタイム、標準ライブラリ実装専用です。通常のスクリプトモジュールからは呼び出せません。[標準ライブラリリファレンス](index.md)に記載された公開関数を使用してください。

- [標準ライブラリ基盤](foundation.md)

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/builtins.md) · [fr-FR](../../fr-FR/standard-library/builtins.md) · [ja-JP](builtins.md) · [ko-KR](../../ko-KR/standard-library/builtins.md) · [zh-CN](../../zh-CN/standard-library/builtins.md) · [zh-TW](../../zh-TW/standard-library/builtins.md)
