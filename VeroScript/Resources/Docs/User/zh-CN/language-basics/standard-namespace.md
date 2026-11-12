# `std` 标准命名空间

VeroScript 提供一个由语言保留的 `std` 命名空间，用于统一书写内置容器、通用值类型、反射信息类型和公开运行时函数。新代码应使用 `std::` 限定形式。模块工具采用按职责划分的子命名空间；`std::contains`、`std::read_file` 这类旧式扁平写法不再作为别名提供。

## 解析规则

`std::name` 和 `std.name` 都可以被识别；公共代码推荐使用 `std::name`。这些名称在编译阶段解析为现有的语言或标准库声明，不会创建第二套容器 ABI，也不会增加运行时分派。格式化函数是编译器提供的公开入口，其他源码标准库函数仍遵循普通导入规则。

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

语言基础类型与运行时直连函数仍保留未限定写法。模块所属 API 则有意限制为命名空间形式；导入模块后应使用 `std::algorithm::sort` 等名称。这不是旧 `std` 名称的兼容转发。

`std` 是保留命名空间，脚本不能向 `std` 或其子命名空间添加声明。标准命名空间别名也不改变模块可见性：别名指向源标准库函数时，仍然必须导入提供该函数的模块。

## 类型别名

下表列出当前公开的类型别名。右侧是它们对应的现有语言类型或反射类型。

| `std::` 写法 | 对应名称 |
| --- | --- |
| `string`、`wstring` | `string`、`wstring` |
| `array`、`map`、`multimap` | 同名容器 |
| `ordered_map`、`ordered_multimap` | 同名有序映射 |
| `set`、`multiset`、`ordered_multiset` | 同名集合 |
| `queue`、`priority_queue`、`stack`、`deque` | 同名序列容器 |
| `channel`、`task`、`concurrent::task_group`、`concurrent::mutex`、`concurrent::counting_semaphore` | 通道、并发任务、互斥量和信号量类型 |
| `pair` | `pair` |
| `optional`、`weak` | 同名可选值和弱引用类型 |
| `readonly` | 只读值包装器 |
| `shared`, `handle` | `handle` |
| `delegate`、`multicast_delegate` | `delegate`、`multicast_delegate` |
| `type`、`field_info`、`method_info` | 反射信息类型 |
| `module_info`、`function_info`、`global_info` | 模块、函数和全局量信息类型 |
| `enum_member_info` | 枚举成员信息类型 |

类型别名只改变源码拼写。泛型参数仍按原类型的规则检查，例如 `std::map<string, int>` 仍是一个二元映射。

## 转换写法

`std::readonly<T>` 是 `readonly<T>` 的标准命名空间写法。转换内置函数仍位于全局作用域，应在没有 `std::` 前缀的情况下使用 `cast<T>`、`try_cast<T>`、`const_cast<T>` 以及带括号的 `(T)expression`。`std::cast<T>` 和 `std::try_cast<T>` 等形式不是文档规定的别名。转换行为和失败规则请参阅[类型转换](../language-reference/type-conversions.md)。

```VeroScript
std::readonly<int> published = std::readonly<int>(41);
double precise = cast<double>(published.get());
optional<int> narrowed = try_cast<int>(precise);
```

## 函数别名

### 直接可用的运行时函数

以下名称对应内置函数或运行时直接提供的函数：

| 类别 | 函数 |
| --- | --- |
| 输出和格式化 | `print`、`eprint`、`printf`、`format`、`formatf`、`assert`、`std::console::{write, error, flush, clear, is_interactive}` |
| 长度和转换 | `len`、`to_string`、`to_wstring`、`to_int`、`to_unsigned`、`to_int64`、`to_uint64`、`to_double`、`to_float` |
| 数值 | `abs`、`min`、`max` |
| 运行时工具 | `range`、`exit` |

时间、控制台、线程与环境功能只提供按职责分组的名称：`std::chrono::now`、
`std::chrono::steady_now`、`std::this_thread::sleep`、`std::environment::getenv` 和
`std::environment::setenv`。不保留对应的扁平 `std` 别名：

```VeroScript
int main()
{
	std::print("elapsed={}", std::chrono::now() > 0.0);
	string mode = std::environment::getenv("MODE");
	std::this_thread::sleep(1);
	return 0;
}
```

### 模块函数

下面这些名称是标准库模块公开函数的标准命名空间写法。它们仍遵循普通 `import` 规则：

| 模块 | 函数 |
| --- | --- |
| `io.vs` | `std::io::{read_file, write_file, append_file, read_line, pause, read_token, read_string, read_int, read_double, read_bool}` |
| `io.vs` | `std::filesystem::{delete_file, exists, is_file, is_directory, remove_dir, list_dir, mkdir, cwd, chdir}` |
| `string.vs` | `std::strings::{substring, find, last_find, char_code, contains, starts_with, ends_with, trim_left, trim_right, trim, split, join, to_lower, to_upper, replace, repeat, remove_prefix, remove_suffix}` |
| `path.vs` | `std::filesystem::{basename, dirname, join_path, extension, change_extension}` |
| `concurrent.vs` | `std::concurrent::{channel_send, channel_recv, channel_close, send, recv, close, make_channel, try_send, send_for, try_recv, recv_for, is_closed, pending, capacity, spawn, spawn_task, join_task, cancel_task, cancel_requested, when_all, cancel_all, make_mutex, mutex_lock, mutex_try_lock, mutex_lock_for, mutex_unlock, make_semaphore, semaphore_acquire, semaphore_try_acquire, semaphore_acquire_for, semaphore_release, make_synchronized, synchronized_load, synchronized_store, synchronized_exchange, synchronized_fetch_add}` |
| `math.vs` | 扁平数值名称：`std::{sqrt, sin, cos, tan, floor, ceil, round, trunc, pow, exp, log, log2, asin, acos, atan2, isfinite, isnan, pi, e, clamp, clamp_int}` |
| `algorithm.vs` | `std::algorithm::{sort, reverse, find_index, count, lower_bound, binary_search, fill, copy, contains, equal, is_sorted, min_index, max_index, rotate, shuffle}` |
| `numeric.vs` | `std::numeric::{accumulate, sum, average}` |
| `random.vs` | `std::random::{make_engine, seed, next_uint, next_bounded_uint, next_int, next_double, next_double_range, next_bool}` |

例如，`std::sqrt` 在上一个示例中可以使用，是因为示例先导入了 `math.vs`。只写 `std::sqrt` 不会自动加载标准库模块；`std::format` 和 `std::formatf` 是格式化的编译器入口，不需要通过 `string.vs` 才能解析。

## 统一扩展调用

可见自由函数可以把第一个参数写成接收者：

```VeroScript
int grow(ref Counter self, int amount)
{
	/* ... */
}

counter.grow(2); // grow(ref counter, 2)
```

真实脚本成员、虚调用、宿主成员和内建成员始终优先。只有它们都不匹配时，编译器才尝试等价的自由函数调用。泛型推导、重载决议、导入、包可见性、默认参数、转换和 `ref`/`const ref` 均复用普通函数规则。引用接收者会隐式绑定；若复制与引用形式同时可用，优先选择兼容的引用接收者重载。该转换只发生在编译期。

`concurrent.vs` 使用这一机制提供通道方法：

```VeroScript
import "concurrent.vs";

channel<int> messages = channel<int>();
messages.send(42);
int value = messages.recv();
messages.close();
```

## 不能做什么

- 不能声明 `namespace std`，也不能把自定义类型或函数注册到 `std` 名称树中。
- 不能把 `std::` 别名当作模块导入；模块仍由 `import`、`export` 和包规则控制。
- 不应依赖以 `__` 开头的内部辅助函数。公开的 `std::` 写法和对应的标准库页面才是稳定接口。

完整别名表由运行时的标准库注册表维护。普通命名空间规则见[命名空间](namespaces.md)，模块签名见[标准库参考](../standard-library/index.md)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/standard-namespace.md) · [fr-FR](../../fr-FR/language-basics/standard-namespace.md) · [ja-JP](../../ja-JP/language-basics/standard-namespace.md) · [ko-KR](../../ko-KR/language-basics/standard-namespace.md) · [zh-CN](standard-namespace.md) · [zh-TW](../../zh-TW/language-basics/standard-namespace.md)
