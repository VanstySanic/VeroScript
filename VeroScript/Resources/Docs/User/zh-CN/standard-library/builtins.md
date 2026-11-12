# 内置函数

参阅[标准库基础](foundation.md#foundation-imports)。

内置函数与[标准库模块](index.md)的边界如下：内置函数适合表达运行时最基础的操作；更高层的字符串、文件、路径、并发和事件功能应优先使用对应的标准库模块。

## 输出

| 函数 | 说明 |
| --- | --- |
| `print(value)` | 将一个值写入标准输出。传入格式字符串和额外参数时，使用花括号格式化；`{{` 和 `}}` 分别表示字面量花括号。 |
| `printf(format, ...)` | 使用 printf 风格转换说明格式化并写入标准输出；它与 `print` 使用不同的格式语法。 |
| `eprint(value)` | 将一个值写入错误输出流。 |

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

需要将格式化结果保存在变量中时，使用标准库中的 `format` 或 `formatf`。花括号字段、printf 转换符、宽度、精度和错误边界见[字符串语言行为](../language-basics/strings.md#%E6%A0%BC%E5%BC%8F%E5%8C%96)与[字符串模块](string.md)。

## 控制台流

`std::console` 函数由运行时提供，不需要导入模块。它们只提供命名空间形式，
以明确表达输出流和交互式终端语义：

| 函数 | 说明 |
| --- | --- |
| `std::console::write(value)` | 将一个值写入标准输出，不追加换行。 |
| `std::console::error(value)` | 将一个值写入错误流，不追加换行。 |
| `std::console::flush()` | 刷新等待中的标准输出。 |
| `std::console::clear()` | 在宿主支持时清除交互式输出，并返回是否执行了清除。 |
| `std::console::is_interactive()` | 返回标准输入和标准输出是否都处于交互模式。 |

需要在下一次换行或将控制权交给宿主工具前显示进度片段时，调用
`std::console::flush()`。调试适配器会把刷新后的标准输出和错误输出作为不同的输出事件转发。

### 终端控制

| API | |
| --- | --- |
| `supports_color()`, `supports_cursor()`, `supports_clear()`, `is_output_terminal()` | 分别查询颜色、光标、清屏／清行能力及输出是否为终端。 |
| `set_color(string foreground, string background)`, `reset_style()` | 设置前景／背景色；复位样式。 |
| `clear_line(string mode)` | 清除光标之后、之前或整行，包含当前字符位置；不移动光标。 |
| `move_cursor(int column, int row)`, `set_cursor_visible(bool visible)` | 移动到从 0 开始的列／行；显示或隐藏光标。 |
| `width()`, `height()` | 可见窗口列数／行数；未知时返回 0。 |

控制函数返回 `bool`，不支持或执行失败时返回 `false`。重定向输出不会自动写入终端控制码；控制输出不要求输入也具有交互能力。`clear()` 清除可见窗口并将光标移到窗口原点，不删除历史滚动内容。坐标必须为 `0..32766` 的整数，超出已知窗口尺寸时返回 `false`。错误参数类型、颜色名称及清行模式在普通文本输出时也会报错。修改操作受控制台输出权限约束，查询为只读操作。

颜色名称为 `black`、`red`、`green`、`yellow`、`blue`、`magenta`、`cyan`、`white`，以及对应的 `bright_` 亮色和 `default`。前景与背景都需要指定。执行边界退出时会恢复已修改的样式和光标显示状态，包括运行错误、取消及指令预算耗尽；输出本身失败时尽力恢复。POSIX／自定义宿主恢复默认样式及可见光标；Windows VM 与三种 AOT 恢复原生初始状态。

嵌入式日志／UI 文本宿主默认不提供终端控制。C++ 宿主可以实现 `QueryConsole`／`ExecuteConsole`；C ABI、C# 和 Python 会话选项提供配套查询／控制回调。可选精确文本回调立即收到 `write()` 的文本，不额外添加换行。回调需要存活到会话销毁结束，并自行同步可能并发的工作线程调用。移动端和浏览器文本捕获默认保持普通文本。可运行样例 `console_demo.vs`。

```VeroScript
if (std::console::supports_color())
{
	std::console::set_color("bright_green", "default");
}
std::console::write("Ready\n");
std::console::flush();
std::console::reset_style();
```

## 类型转换

| 函数 | 说明 |
| --- | --- |
| `to_int(value)` | 转换为 `int`。浮点数向零截断；超出范围或为非有限值时报告错误。 |
| `to_double(value)` | 转换为 `double`。 |
| `to_float(value)` | 转换为 `float`；结果按 binary32 保存。 |
| `to_unsigned(value)` | 转换为 `unsigned int`；负数、超出范围和非法值会报告错误。浮点数向零截断。 |
| `to_int64(value)` | 转换为 `int64`，并进行范围检查。 |
| `to_uint64(value)` | 转换为 `uint64`，并进行范围检查。 |
| `to_string(value)` | 将值转换为字符串。 |
| `to_wstring(value)` | 将字符串值显式转换为宽字符串；参数必须是 `string` 或 `wstring`。 |

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

转换函数不会把任意文本自动解析为数字。需要读取数字输入时，应使用 `std::io::read_int` 或 `std::io::read_double`；需要解析文件或协议文本时，应在脚本中明确实现解析规则。

## 数值和集合

| 函数 | 说明 |
| --- | --- |
| `abs(value)` | 返回数值的绝对值。 |
| `min(a, b)` | 返回两个数值中的较小值。 |
| `max(a, b)` | 返回两个数值中的较大值。 |
| `len(value)` | 返回字符串、数组或标准容器的元素数量。多重集合返回所有元素出现次数之和。 |
| `range(int64 count)` | 接收 `int64` 数量，返回包含 `0` 到 `count - 1` 的 `array<int>`。负数数量产生空数组；数量大于 `1000000` 时产生运行时错误。 |

```VeroScript
int main()
{
	array<int> values = range(4);
	print("size={}", len(values));
	print("low={}, high={}, distance={}", min(-2, 5), max(-2, 5), abs(-9));
	return 0;
}
```

`len(value)` 返回非负 `int64`（`0` 到 `9223372036854775807`）。空值、不支持的类型或不可表示的长度会产生运行时错误。已有容器的大小、容量及元素数量查询也返回 `int64`。长度和线性索引宜使用 `int64`；赋给 `int` 时执行普通数值范围检查，超出其范围则失败。类型变宽不会取消原生分配或后端容器限制。

## 环境、时间和流程

| 函数 | 说明 |
| --- | --- |
| `std::environment::getenv(name)` | 读取宿主进程环境变量；变量不存在时返回空字符串。名称不能为空。 |
| `std::environment::setenv(name, value)` | setenv(name, "") 设置存在的空值；删除使用 unsetenv，删除不存在项也成功。try_getenv 区分缺失与空值。名称非空且不能含 = 或 NUL，值不能含 NUL。Windows 环境名称沿系统规则不区分大小写，POSIX 区分大小写。 |
| `std::chrono::now()` | 返回基于系统时钟的 Unix 时间起点以来的秒数，类型为 `double`。 |
| `std::chrono::steady_now()` | 返回单调时钟的秒数。测量耗时时应比较两次读取的差值，不要把它当作墙上时钟时间戳。 |
| `std::this_thread::sleep(milliseconds)` | 让当前脚本线程休眠指定的毫秒数。参数必须为非负整数。 |
| `assert(condition[, message])` | 接收一个 `bool` 条件和可选的 `string` 消息。条件为 `false` 时报告运行时错误；条件为 `true` 时不产生输出。 |
| `exit(code)` | 请求脚本以指定的整数退出码结束执行。 |

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

`assert` 接收一个或两个参数。第一个参数必须是 `bool`；如果提供第二个参数，则必须是 `string`。断言失败时，非空消息会包含在诊断中；空消息使用默认断言诊断。断言失败是运行时错误而不是脚本异常，因此不会被 `catch (...)` 或类型处理器捕获。

环境变量属于宿主进程状态。脚本修改环境变量后，是否影响宿主程序的后续逻辑取决于宿主平台和集成方式。

## 标准输入

| 函数 | 说明 |
| --- | --- |
| `std::io::read_token()` | 从标准输入读取下一个以空白分隔的令牌。 |
| `std::io::read_string()` | 读取下一个字符串令牌。 |
| `std::io::read_int()` | 读取并解析下一个整数令牌。 |
| `std::io::read_double()` | 读取并解析下一个浮点数令牌。 |
| `std::io::read_bool()` | 读取 `true` 或 `false` 并解析为布尔值。 |

输入到达文件末尾或令牌格式不正确时，读取函数会报告诊断。它们适合简单的命令行输入；更复杂的输入协议应由宿主程序或标准库文件接口负责。

## 实现内部名称

以 `__` 开头的名称（例如 `__read_file`、`__format`）专供编译器、运行时和标准库实现使用。普通脚本模块不能直接调用它们；请使用[标准库参考](index.md)中记录的公开函数。

- [标准库基础](foundation.md)

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/builtins.md) · [fr-FR](../../fr-FR/standard-library/builtins.md) · [ja-JP](../../ja-JP/standard-library/builtins.md) · [ko-KR](../../ko-KR/standard-library/builtins.md) · [zh-CN](builtins.md) · [zh-TW](../../zh-TW/standard-library/builtins.md)
