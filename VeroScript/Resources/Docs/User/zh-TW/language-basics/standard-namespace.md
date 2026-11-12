# `std` 標準命名空間

VeroScript 提供一個由語言保留的 `std` 命名空間，用於統一書寫內建容器、通用值類型、反射資訊類型和公開執行階段函式。新代碼應使用 `std::` 限定形式。模組工具採用依職責劃分的子命名空間；`std::contains`、`std::read_file` 這類舊式扁平寫法不再作為別名提供。

## 解析規則

`std::name` 和 `std.name` 都可以被辨識；公共代碼建議使用 `std::name`。這些名稱在編譯階段解析為現有的語言或標準函式庫宣告，不會建立第二套容器 ABI，也不會增加執行階段分派。格式化函式是編譯器提供的公開入口，其他原始碼標準函式庫函式仍遵循普通匯入規則。

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

語言基礎型別與執行階段直連函式仍保留未限定寫法。模組所屬 API 則刻意限制為命名空間形式；匯入模組後應使用 `std::algorithm::sort` 等名稱。這不是舊 `std` 名稱的相容轉發。

`std` 是保留命名空間，腳本不能為 `std` 或其子命名空間新增宣告。標準命名空間別名也不改變模組可見性：別名指向來源標準函式庫函式時，仍必須匯入提供該函式的模組。

## 類型別名

下表列出目前公開的類型別名。右側是它們對應的現有語言類型或反射類型。

| `std::` 寫入法 | 對應名稱 |
| --- | --- |
| `string`、`wstring` | `string`、`wstring` |
| `array`、`map`、`multimap` | 同名容器 |
| `ordered_map`、`ordered_multimap` | 同名有序映射 |
| `set`、`multiset`、`ordered_multiset` | 同名集合 |
| `queue`、`priority_queue`、`stack`、`deque` | 同名序列容器 |
| `channel`、`task`、`concurrent::task_group`、`concurrent::mutex`、`concurrent::counting_semaphore` | 頻道、並發任務、互斥量和信號量類型 |
| `pair` | `pair` |
| `optional`、`weak` | 同名可選值與弱引用型別 |
| `readonly` | 唯讀值包裝器 |
| `shared`, `handle` | `handle` |
| `delegate`、`multicast_delegate` | `delegate`、`multicast_delegate` |
| `type`、`field_info`、`method_info` | 反射資訊類型 |
| `module_info`、`function_info`、`global_info` | 模組、函式與全域量資訊類型 |
| `enum_member_info` | 枚舉成員資訊類型 |

類型別名只改變原始碼拼字。泛型參數仍以原始類型的規則檢查，例如 `std::map<string, int>` 仍是一個二元映射。

## 轉換寫法

`std::readonly<T>` 是 `readonly<T>` 的標準命名空間寫法。轉換內建函式仍位於全域作用域，應在沒有 `std::` 前綴的情況下使用 `cast<T>`、`try_cast<T>`、`const_cast<T>` 以及帶括號的 `(T)expression`。`std::cast<T>` 和 `std::try_cast<T>` 等形式不是文件規定的別名。轉換行為和失敗規則請參閱[型別轉換](../language-reference/type-conversions.md)。

```VeroScript
std::readonly<int> published = std::readonly<int>(41);
double precise = cast<double>(published.get());
optional<int> narrowed = try_cast<int>(precise);
```

## 函式別名

### 直接可用的執行階段函式

以下名稱對應內建函式或執行階段直接提供的函式：

| 類別 | 函式 |
| --- | --- |
| 輸出與格式化 | `print`、`eprint`、`printf`、`format`、`formatf`、`assert`、`std::console::{write, error, flush, clear, is_interactive}` |
| 長度與轉換 | `len`、`to_string`、`to_wstring`、`to_int`、`to_unsigned`、`to_int64`、`to_uint64`、`to_double`、`to_float` |
| 數值 | `abs`、`min`、`max` |
| 執行階段工具 | `range`、`exit` |

時間、主控台、執行緒與環境功能只提供依職責分組的名稱：`std::chrono::now`、
`std::chrono::steady_now`、`std::this_thread::sleep`、`std::environment::getenv` 與
`std::environment::setenv`。不保留對應的扁平 `std` 別名：

```VeroScript
int main()
{
	std::print("elapsed={}", std::chrono::now() > 0.0);
	string mode = std::environment::getenv("MODE");
	std::this_thread::sleep(1);
	return 0;
}
```

### 模組函式

下面這些名稱是標準庫模組公開函式的標準命名空間寫法。它們仍遵循普通 `import` 規則：

| 模組 | 函式 |
| --- | --- |
| `io.vs` | `std::io::{read_file, write_file, append_file, read_line, pause, read_token, read_string, read_int, read_double, read_bool}` |
| `io.vs` | `std::filesystem::{delete_file, exists, is_file, is_directory, remove_dir, list_dir, mkdir, cwd, chdir}` |
| `string.vs` | `std::strings::{substring, find, last_find, char_code, contains, starts_with, ends_with, trim_left, trim_right, trim, split, join, to_lower, to_upper, replace, repeat, remove_prefix, remove_suffix}` |
| `path.vs` | `std::filesystem::{basename, dirname, join_path, extension, change_extension}` |
| `concurrent.vs` | `std::concurrent::{channel_send, channel_recv, channel_close, send, recv, close, make_channel, try_send, send_for, try_recv, recv_for, is_closed, pending, capacity, spawn, spawn_task, join_task, cancel_task, cancel_requested, when_all, cancel_all, make_mutex, mutex_lock, mutex_try_lock, mutex_lock_for, mutex_unlock, make_semaphore, semaphore_acquire, semaphore_try_acquire, semaphore_acquire_for, semaphore_release, make_synchronized, synchronized_load, synchronized_store, synchronized_exchange, synchronized_fetch_add}` |
| `math.vs` | 扁平數值名稱：`std::{sqrt, sin, cos, tan, floor, ceil, round, trunc, pow, exp, log, log2, asin, acos, atan2, isfinite, isnan, pi, e, clamp, clamp_int}` |
| `algorithm.vs` | `std::algorithm::{sort, reverse, find_index, count, lower_bound, binary_search, fill, copy, contains, equal, is_sorted, min_index, max_index, rotate, shuffle}` |
| `numeric.vs` | `std::numeric::{accumulate, sum, average}` |
| `random.vs` | `std::random::{make_engine, seed, next_uint, next_bounded_uint, next_int, next_double, next_double_range, next_bool}` |

例如，`std::sqrt` 在上一個範例中可以使用，是因為範例先匯入了 `math.vs`。只寫 `std::sqrt` 不會自動載入標準函式庫模組；`std::format` 和 `std::formatf` 是格式化的編譯器入口，不需要透過 `string.vs` 才能解析。

## 統一擴充呼叫

可見自由函式可以把第一個參數寫成接收者：

```VeroScript
int grow(ref Counter self, int amount)
{
	/* ... */
}

counter.grow(2); // grow(ref counter, 2)
```

真實腳本成員、虛擬呼叫、宿主成員和內建成員始終優先。只有當它們都不匹配時，編譯器才會嘗試等價的自由函式呼叫。泛型推導、重載決議、匯入、套件可見性、預設參數、轉換和 `ref`/`const ref` 均重複使用普通函式規則。引用接收者會隱式綁定；若複製與引用形式同時可用，優先選擇相容的引用接收者重載。該轉換只發生在編譯期。

`concurrent.vs` 使用此機制提供通道方法：

```VeroScript
import "concurrent.vs";

channel<int> messages = channel<int>();
messages.send(42);
int value = messages.recv();
messages.close();
```

## 不能做什麼

- 不能宣告 `namespace std`，也不能把自訂類型或函式註冊到 `std` 名稱樹中。
- 不能把 `std::` 別名當作模組匯入；模組仍由 `import`、`export` 和套件規則控制。
- 不應依賴以 `__` 開頭的內部輔助函式。公開的 `std::` 寫法和對應的標準函式庫頁面才是穩定介面。

完整別名表由執行階段的標準庫註冊表維護。普通命名空間規則請參考[命名空間](namespaces.md)，模組簽名見[標準函式庫參考](../standard-library/index.md)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/standard-namespace.md) · [fr-FR](../../fr-FR/language-basics/standard-namespace.md) · [ja-JP](../../ja-JP/language-basics/standard-namespace.md) · [ko-KR](../../ko-KR/language-basics/standard-namespace.md) · [zh-CN](../../zh-CN/language-basics/standard-namespace.md) · [zh-TW](standard-namespace.md)
