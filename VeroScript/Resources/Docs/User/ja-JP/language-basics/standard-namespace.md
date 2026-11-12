# `std` 標準名前空間

VeroScript は、組み込みコンテナ、ユーティリティ値タイプ、リフレクション タイプ、およびパブリック ランタイム関数用に、言語で予約された `std` 名前空間を提供します。新しいコードでは、修飾された `std::` スペルを使用する必要があります。モジュール ユーティリティは責任ベースの子名前空間を使用します。 `std::contains` や `std::read_file` などの廃止されたフラット形式はエイリアスではありません。

## 解決ルール

`std::name` と `std.name` の両方が受け入れられます。パブリック コードでは `std::name` を使用する必要があります。コンパイラはこれらの名前を既存の言語または標準ライブラリの宣言に解決するため、エイリアスによって 2 番目のコンテナ ABI が作成されたり、ランタイム ディスパッチが追加されたりすることはありません。フォーマット関数は、コンパイラが提供するパブリックなエントリ ポイントです。他のソース標準ライブラリ関数は引き続き通常のインポート ルールに従います。

```VeroScript
import "math.vs";

int main()
{
	std::array<int> values = {2, 3, 5};
	std::optional<int> selected = std::optional<int>(values[0]);

	std::print("selected={}, root={}", selected.get(), std::sqrt(81.0));
	return 0;
}
```

言語の基本型とランタイム直結関数は、修飾されていない形式も維持します。モジュール所有の API は意図的に名前空間限定であり、モジュールをインポートしてから `std::algorithm::sort` のように呼び出します。これは古い `std` 名の互換性転送層ではありません。

`std` は予約されています。スクリプトは、`std` またはその子名前空間の 1 つに宣言を追加できません。名前空間のエイリアスでもモジュールの可視性は変わりません。ソースの標準ライブラリ関数へのエイリアスでは、それを提供するモジュールをインポートする必要があります。

## 型のエイリアス

次の表に、パブリック型の別名を示します。右側は、既存の言語またはリフレクション タイプのアイデンティティです。

| `std::` のスペル |正規名 |
| --- | --- |
| `string`、`wstring` | `string`、`wstring` |
| `array`、`map`、`multimap` |同じ名前のコンテナ |
| `ordered_map`、`ordered_multimap` |同じ名前の順序付きマップ |
| `set`、`multiset`、`ordered_multiset` |同じ名前のセット |
| `queue`、`priority_queue`、`stack`、`deque` |同じ名前のシーケンス コンテナー |
| `channel`、`task`、`concurrent::task_group`、`concurrent::mutex`、`concurrent::counting_semaphore` |チャネル、同時タスク、mutex、セマフォの種類 |
| `pair` | `pair` |
| `optional`、`weak` |同じ名前のオプション型と弱い型 |
| `readonly` | 読み取り専用の値ラッパー |
| `shared`, `handle` | `handle` |
| `delegate`、`multicast_delegate` | `delegate`、`multicast_delegate` |
| `type`、`field_info`、`method_info` |反射情報の種類 |
| `module_info`、`function_info`、`global_info` |モジュール、関数、およびグローバル情報の種類 |
| `enum_member_info` | Enum メンバー情報の型 |

エイリアスはソースのスペルのみを変更します。ジェネリック引数は引き続き元の型ルールを使用してチェックされるため、`std::map<string, int>` は 2 パラメーター マップのままです。

## 変換の表記

`std::readonly<T>` は `readonly<T>` の標準名前空間表記です。変換組み込みはグローバルのままなので、`std::` 接頭辞なしで `cast<T>`、`try_cast<T>`、`const_cast<T>`、および括弧付きの `(T)expression` を使用します。`std::cast<T>` や `std::try_cast<T>` のような形式は、文書化されたエイリアスではありません。動作と失敗規則については、[型変換](../language-reference/type-conversions.md)を参照してください。

```VeroScript
std::readonly<int> published = std::readonly<int>(41);
double precise = cast<double>(published.get());
optional<int> narrowed = try_cast<int>(precise);
```

## 関数のエイリアス

### 直接ランタイム関数

これらの名前は、ランタイムによって直接提供される組み込みまたは関数を指します。

|カテゴリー |機能 |
| --- | --- |
|出力とフォーマット | `print`、`eprint`、`printf`、`format`、`formatf`、`assert`、`std::console::{write, error, flush, clear, is_interactive}` |
|長さと変換 | `len`、`to_string`、`to_wstring`、`to_int`、`to_unsigned`、`to_int64`、`to_uint64`、`to_double`、`to_float` |
|数値 | `abs`、`min`、`max` |
|ランタイムユーティリティ | `range`、`exit` |

時間、コンソール、スレッド、および環境関数は、責任ベースの名前
(`std::chrono::now`、`std::chrono::steady_now`、`std::this_thread::sleep`、
`std::environment::getenv`、および `std::environment::setenv`) のみを使用します。
対応するフラット `std` エイリアスは保持されません。

```VeroScript
int main()
{
	std::print("elapsed={}", std::chrono::now() > 0.0);
	string mode = std::environment::getenv("MODE");
	std::this_thread::sleep(1);
	return 0;
}
```

### モジュールの機能

次の名前は、エクスポートされた標準ライブラリ関数の標準名前空間の綴りです。これらは依然として通常の `import` ルールに従います。

|モジュール |機能 |
| --- | --- |
| `io.vs` | `std::io::{read_file, write_file, append_file, read_line, pause, read_token, read_string, read_int, read_double, read_bool}` |
| `io.vs` | `std::filesystem::{delete_file, exists, is_file, is_directory, remove_dir, list_dir, mkdir, cwd, chdir}` |
| `string.vs` | `std::strings::{substring, find, last_find, char_code, contains, starts_with, ends_with, trim_left, trim_right, trim, split, join, to_lower, to_upper, replace, repeat, remove_prefix, remove_suffix}` |
| `path.vs` | `std::filesystem::{basename, dirname, join_path, extension, change_extension}` |
| `concurrent.vs` | `std::concurrent::{channel_send, channel_recv, channel_close, send, recv, close, make_channel, try_send, send_for, try_recv, recv_for, is_closed, pending, capacity, spawn, spawn_task, join_task, cancel_task, cancel_requested, when_all, cancel_all, make_mutex, mutex_lock, mutex_try_lock, mutex_lock_for, mutex_unlock, make_semaphore, semaphore_acquire, semaphore_try_acquire, semaphore_acquire_for, semaphore_release, make_synchronized, synchronized_load, synchronized_store, synchronized_exchange, synchronized_fetch_add}` |
| `math.vs` |フラットな数値名: `std::{sqrt, sin, cos, tan, floor, ceil, round, trunc, pow, exp, log, log2, asin, acos, atan2, isfinite, isnan, pi, e, clamp, clamp_int}` |
| `algorithm.vs` | `std::algorithm::{sort, reverse, find_index, count, lower_bound, binary_search, fill, copy, contains, equal, is_sorted, min_index, max_index, rotate, shuffle}` |
| `numeric.vs` | `std::numeric::{accumulate, sum, average}` |
| `random.vs` | `std::random::{make_engine, seed, next_uint, next_bounded_uint, next_int, next_double, next_double_range, next_bool}` |

たとえば、`std::sqrt` は `math.vs` をインポートするため、前のサンプルで使用できます。 `std::sqrt` を単独で記述すると、標準ライブラリ モジュールはロードされません。 `std::format` および `std::formatf` はコンパイラ提供の書式設定エントリ ポイントであり、解決するためだけに `string.vs` は必要ありません。

## 均一内線通話

可視の自由関数は、最初の引数をレシーバーとして呼び出すことができます。

```VeroScript
int grow(ref Counter self, int amount)
{
	/* ... */
}

counter.grow(2); // grow(ref counter, 2)
```

実際のスクリプト、仮想、ホスト、および組み込みのメンバーが常に勝ちます。一致する実際のメンバーがない場合にのみ、コンパイラーは同等のフリー関数呼び出しを試みます。汎用推論、オーバーロードの解決、インポート、パッケージの可視性、デフォルト、変換、および `ref`/`const ref` ルールは、通常の呼び出しと共有されます。参照レシーバーは暗黙的にバインドします。両方の形式が存在する場合、レシーバーをコピーするよりも、互換性のある参照レシーバーのオーバーロードが優先されます。この低下はコンパイル時のみです。

`concurrent.vs` はチャネルに次のメカニズムを使用します。

```VeroScript
import "concurrent.vs";

channel<int> messages = channel<int>();
messages.send(42);
int value = messages.recv();
messages.close();
```

## 境界

- スクリプトでは、`namespace std` を宣言したり、`std` 名前ツリーの下にユーザー タイプや関数を登録したりすることはできません。
- `std::` エイリアスはモジュール インポートではありません。モジュールは、`import`、`export`、およびパッケージ可視性ルールによって引き続き制御されます。
- `__` で始まる名前に依存しないでください。文書化された `std::` スペルと標準ライブラリ ページは、安定したパブリック インターフェイスです。

完全な別名テーブルは、ランタイム標準ライブラリ レジストリによって維持されます。通常の名前空間ルールについては [名前空間](namespaces.md) を、モジュール署名については [標準ライブラリ リファレンス](../standard-library/index.md) を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/standard-namespace.md) · [fr-FR](../../fr-FR/language-basics/standard-namespace.md) · [ja-JP](standard-namespace.md) · [ko-KR](../../ko-KR/language-basics/standard-namespace.md) · [zh-CN](../../zh-CN/language-basics/standard-namespace.md) · [zh-TW](../../zh-TW/language-basics/standard-namespace.md)
