# 17：可选值、弱引用和共享结构体句柄

本教程使用 VeroScript 编译器提供的工具类型，构建一个小型状态共享示例。你将学习什么时候使用 `optional<T>`、`weak<T>`、`handle<T>` 和 `pair<T, U>`，以及它们在值语义和所有权规则上的区别。

`shared<T>` 是 `handle<T>` 的推荐写法；两者是完全相同的类型，在容器、泛型函数和 `weak<T>` 中也可混用。`std::shared<T>` 与 `std::handle<T>` 使用同一类型身份。显式调用 `shared<T>()` 会创建非空包装对象；赋值 `nullptr` 会清空句柄。结构体载荷在构造时复制到包装对象中，之后复制共享句柄会保留同一对象身份。`weak<shared<T>>` 观察这个包装对象；`lock()` 返回同一对象的强句柄，失效后返回 `nullptr`，访问 `.value` 前应检查锁定结果。`shared` 是保留的内置类型别名，不是语法关键字。按照现有软泛型规则，用户声明同名且泛型参数数量匹配的 `handle<T>` 时，两种写法均指向该用户类型。

## 目标

完成本教程后，你应当能够：

- 不使用哨兵值，表示可能不存在的结果；
- 在不延长对象生命周期的情况下观察一个类；
- 通过类句柄共享结构体载荷；
- 区分映射条目的值快照和可写视图。

完整参考请参阅[值工具类型](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/value-utility-types.md)。每个示例执行前都应先使用 `--check` 检查。

## 第一步：表示可选结果

空结果使用 `optional<T>()`，包含值的结果使用 `optional<T>(value)`。调用 `get()` 前先检查 `has_value()`：

```VeroScript
optional<int> find_score(bool available)
{
	if (available)
	{
		return optional<int>(95);
	}
	return optional<int>();
}

int main()
{
	optional<int> missing = find_score(false);
	print(missing.value_or(0));

	optional<int> found = find_score(true);
	if (found.has_value())
	{
		print(found.get());
	}

	return 0;
}
```

只需要默认值时，`value_or` 很方便；明确检查状态后，可以使用 `get()`。对空 optional 调用 `get()` 会通过 `assert` 失败。`try_cast<T>` 也使用同样的结果模式表示可能失败的转换。

## 第二步：不持有对象地观察类

类变量是强句柄。`weak<T>` 值只观察对象，不会延长对象的生命周期：

```VeroScript
class Session
{
public:
	int id = 0;
};

int main()
{
	Session owner = Session();
	owner.id = 42;

	weak<Session> watcher = weak<Session>(owner);
	if (!watcher.expired())
	{
		Session locked = watcher.lock();
		print(locked.id);
	}

	owner = nullptr;
	print(watcher.expired());
	return 0;
}
```

对象仍然存在时，`lock()` 返回强句柄；否则返回空句柄。访问成员前，必须检查 `expired()`，或检查 `lock()` 的结果。复制 weak 值会创建另一个观察，重置一个副本不会重置另一个副本。

## 第三步：共享结构体载荷

结构体赋值通常会复制值。如果程序的多个部分必须修改同一个结构体载荷，可以将它放进 `handle<T>`：

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void move_right(handle<Point> point)
{
	point.value.x += 1;
}

int main()
{
	shared<Point> first = shared<Point>(Point());
	handle<Point> second = first;

	first.value.x = 10;
	move_right(second);
	print(first.value.x);
	print(second.value.x);

	return 0;
}
```

将一个 `handle<T>` 赋值给另一个句柄时，会复制类句柄，因此两个变量看到的是同一个 `value`。而 `handle<T>(source)` 会把载荷复制到新的共享对象中。赋值 `nullptr` 会清空句柄。完整示例见 [examples/handle_struct.vs](../../../../../VeroScript/Resources/Scripts/examples/handle_struct.vs)。

## 第四步：选择 `pair` 视图

映射 API 使用 `pair<K, V>`。`entries()` 的结果是值快照数组，因此修改快照不会修改映射。使用 `pair<K, V>&` 直接遍历映射时，可以取得可写的条目视图：

```VeroScript
int main()
{
	ordered_map<string, int> scores = ordered_map<string, int>();
	scores["alice"] = 95;
	scores["bob"] = 88;

	for (pair<string, int> snapshot : scores.entries())
	{
		print("{} -> {}", snapshot.first, snapshot.second);
	}

	for (pair<string, int>& entry : scores)
	{
		entry.second += 1;
	}

	for (auto& [key, value] : scores)
	{
		print("{} -> {}", key, value);
	}

	return 0;
}
```

需要独立条目值集合时使用 `m.entries()`；需要将修改写回条目时，直接遍历映射。完整示例见 [examples/pair_map.vs](../../../../../VeroScript/Resources/Scripts/examples/pair_map.vs)。

## 练习

1. 修改 `find_score`，让它返回 `optional<string>` 消息，而不是整数。
2. 添加第二个 `weak<Session>` 观察者，并验证重置一个观察者不会重置另一个。
3. 为 `handle<Point>` 示例添加沿 `y` 轴移动的函数，并通过第二个句柄调用。
4. 添加 `map<string, Point>`，通过 `pair<string, Point>&` 更新嵌套字段。

## 需要记住的内容

- `optional<T>` 表示空值或有值的状态，是值类型。
- `weak<T>` 观察类，但不会延长类的生命周期。
- `handle<T>` 共享一个包含公共 `value` 字段的类对象，载荷保存在该字段中。
- `entries()` 返回的 `pair<K, V>` 是值快照；直接遍历映射得到的 pair 引用会写回映射。

下一篇：[使用 std 标准命名空间](14-standard-namespace.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/advanced/17-value-utility-types.md) · [fr-FR](../../fr-FR/advanced/17-value-utility-types.md) · [ja-JP](../../ja-JP/advanced/17-value-utility-types.md) · [ko-KR](../../ko-KR/advanced/17-value-utility-types.md) · [zh-CN](17-value-utility-types.md) · [zh-TW](../../zh-TW/advanced/17-value-utility-types.md)
