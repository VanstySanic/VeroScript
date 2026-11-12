# 值工具类型

VeroScript 提供了一组由编译器提供的泛型类型，用于表示可能不存在的值、对类对象的非所有权观察、共享结构体载荷以及映射条目。这些类型无需导入即可使用。[std 标准命名空间](standard-namespace.md)中说明的 `std::` 写法，是指向同一类型的兼容别名。

## `optional<T>`

`optional<T>` 是一种值类型，可以不包含值，也可以包含一个 `T` 类型的值。它的状态是显式的；默认构造的 optional 为空。

### 构造和方法

| 形式 | 含义 |
| --- | --- |
| `optional<T>()` | 构造一个空 optional。 |
| `optional<T>(value)` | 构造一个包含 `value` 的 engaged optional。 |
| `has_value() const` | 返回是否存在值。 |
| `get() const` | 返回存储的值。为空时调用会通过 `assert` 失败。 |
| `value_or(fallback) const` | 有值时返回存储值，否则返回 `fallback`。 |
| `reset()` | 将 optional 置为空。 |

```VeroScript
optional<int> score = optional<int>();
print(score.has_value());
print(score.value_or(0));

score = optional<int>(95);
if (score.has_value())
{
	print(score.get());
}

score.reset();
```

optional 保留 `T` 的值语义。对于结构体载荷，复制一个有值的 optional 会复制结构体；对于类载荷，复制的是类句柄，因此会保留共享身份。`reset()` 还会释放其中保存的类句柄（如果有）。

对于可能失败的转换，`try_cast<T>` 会返回 `optional<T>`。调用 `get()` 前应先检查 `has_value()`；完整规则请参阅[类型转换](../language-reference/type-conversions.md)。

## `weak<T>`

`weak<T>` 是对类对象的非所有权观察。`T` 必须是脚本类、接口或受支持的宿主类类型；基本类型和结构体不能作为目标。weak 值不会延长目标的生命周期。

### 弱引用方法

| 形式 | 含义 |
| --- | --- |
| `weak<T>()` | 构造一个空的弱引用。 |
| `weak<T>(target)` | 观察 `target` 中的类句柄。 |
| `lock() const` | 目标仍然存在时返回强 `T` 句柄，否则返回空句柄。 |
| `expired() const` | 返回目标是否已经无法锁定。 |
| `reset()` | 清除此弱引用。 |

```VeroScript
class Session
{
public:
	int id = 0;
};

Session owner = Session();
weak<Session> observer = weak<Session>(owner);

Session locked = observer.lock();
if (locked != nullptr)
{
	print(locked.id);
}
```

复制 weak 值会创建另一个观察同一目标的弱引用。重置其中一个副本不会重置其他副本。当所有强类句柄都消失后，`expired()` 会变为 `true`，`lock()` 会返回空句柄。访问成员前必须检查锁定结果。

## `shared<T>` / `handle<T>`

`handle<T>` 是编译器提供的单类型参数泛型类，包含一个名为 `value` 的公共字段。当结构体值需要放在共享的类身份之后时，可以使用它。默认构造函数会创建句柄，并在 `T` 为值类型时默认构造 `value`；如果 `T` 是类，载荷则遵循类句柄语义，默认值为空句柄。`handle<T>(value)` 会将载荷复制到新的共享对象中。

`shared<T>` 是 `handle<T>` 的推荐写法；两者是完全相同的类型，在容器、泛型函数和 `weak<T>` 中也可混用。`std::shared<T>` 与 `std::handle<T>` 使用同一类型身份。显式调用 `shared<T>()` 会创建非空包装对象；赋值 `nullptr` 会清空句柄。结构体载荷在构造时复制到包装对象中，之后复制共享句柄会保留同一对象身份。`weak<shared<T>>` 观察这个包装对象；`lock()` 返回同一对象的强句柄，失效后返回 `nullptr`，访问 `.value` 前应检查锁定结果。`shared` 是保留的内置类型别名，不是语法关键字。按照现有软泛型规则，用户声明同名且泛型参数数量匹配的 `handle<T>` 时，两种写法均指向该用户类型。

带一个载荷参数时，`auto h = shared(value)` 可以推断 `T`；无参数构造需要显式目标类型或类型实参。

源码导出有后端限制：C++ 导出支持共享包装和弱引用；C# 导出支持共享包装，但结构体载荷或开放泛型载荷的构造需要显式传入值，例如 `shared<Point>(Point())`，并且暂不支持 `weak<T>`。独立 Python 源码导出暂不支持共享包装和弱引用；Python 运行时绑定通过核心支持两者。完整的生命周期示例可在 VM、Mixed 和 JIT 后端运行。

### 构造和共享

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void bump_x(handle<Point> point)
{
	point.value.x += 1;
}

shared<Point> first = shared<Point>(Point());
handle<Point> second = first;
first.value.x = 10;
bump_x(second);
print(first.value.x);
```

将 `first` 赋值给 `second` 会复制类句柄，因此两个变量观察并修改同一个 `value`。赋值 `nullptr` 会清空句柄。当 `T` 为结构体时，构造 `handle<T>(source)` 会创建新的载荷副本；之后修改 `source` 不会影响这个副本。

`handle<T>` 是共享包装器，不是通用的类替代品。将 `h.value` 中的结构体转换为接口时，会装箱一个值副本。如果接口需要暴露共享的可变身份，应使用实现该接口的类。

完整的可运行示例见 [examples/handle_struct.vs](../../../../Scripts/examples/handle_struct.vs)。

## `pair<T, U>`

`pair<T, U>` 是编译器提供的泛型结构体，包含两个公共字段：

| 字段 | 类型 |
| --- | --- |
| `first` | `T` |
| `second` | `U` |

映射 API 使用 `pair` 表示键和值组成的条目。`m.entries()` 返回值类型的 `pair<K, V>` 快照数组：

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (pair<string, int> entry : scores.entries())
{
	print("{} -> {}", entry.first, entry.second);
}
```

直接遍历映射本身时，可以取得可写的条目视图。`pair<K, V>&` 会将修改写回映射，而 `pair<K, V>` 是副本：

```VeroScript
for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}

for (auto& [key, value] : scores)
{
	value += 1;
}
```

不要把 `entries()` 返回的 `pair` 当作原始映射条目的引用。完整的范围遍历规则请参阅[引用](references.md)和[容器](containers.md)。可运行的映射示例见 [examples/pair_map.vs](../../../../Scripts/examples/pair_map.vs)。

## 名称和兼容性

`optional`、`readonly`、`weak` 以及反射信息类型是严格保留的内置类型名，脚本不能重新声明。

`pair<T, U>` 和 `handle<T>` 是软泛型内置类型。只有在不存在同名且泛型参数数量相同的用户声明时，编译器才会提供双参数的 `pair` 和单参数的 `handle`。非泛型的 `pair` 或 `handle`，以及参数数量不同的声明，可以与对应的软泛型形式共存。

标准命名空间别名为 `std::optional`、`std::weak`、`std::shared`, `std::handle` 和 `std::pair`。这些别名不会创建不同的类型。

## 相关参考

- [数据类型](data-types.md)：内置类型、容器和只读包装器。
- [泛型](generics.md)：用户定义的泛型类型和软内置类型。
- [类型转换](../language-reference/type-conversions.md)：`cast`、`try_cast`、`const_cast` 以及转换结果。
- [容器](containers.md)：映射操作和范围遍历。

脚本对象开始析构时，其弱引用生命周期即结束：析构期间及之后，lock() 返回空，expired() 返回 true。析构函数中让 this 逸出也不会使对象复活，后续字段访问和方法调用会被拒绝。宿主对象的弱引用继续遵循宿主生命周期契约。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/value-utility-types.md) · [fr-FR](../../fr-FR/language-basics/value-utility-types.md) · [ja-JP](../../ja-JP/language-basics/value-utility-types.md) · [ko-KR](../../ko-KR/language-basics/value-utility-types.md) · [zh-CN](value-utility-types.md) · [zh-TW](../../zh-TW/language-basics/value-utility-types.md)
