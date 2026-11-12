# 枚举

枚举使用一组有名称的整数成员表示有限的取值集合。VeroScript 支持普通枚举和作用域枚举（`enum class`），并允许指定底层整数类型。

## 声明枚举

普通枚举的成员默认从 `0` 开始，后续成员依次递增：

```VeroScript
enum State
{
	Stopped,
	Running,
	Paused
};
```

也可以为成员指定整数值。没有显式值的成员会在前一个成员的基础上递增：

```VeroScript
enum ErrorCode
{
	None = 0,
	NotFound = 404,
	Timeout = 408
};
```

枚举成员的值必须是底层类型能够表示的整数常量表达式。支持受检运算、已声明的前序枚举成员及合格的 [constexpr 调用](../language-reference/constexpr.md)；前向引用、循环和溢出会报错。

## 作用域枚举

使用 `enum class` 可以将成员限制在枚举类型的作用域内：

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u
};

RunState state = RunState::Running;
```

作用域枚举的成员不会以裸名称出现在外层作用域中，因此更适合用于成员名称可能重复、或需要明确区分不同枚举类型的场景。访问成员时，文档和新代码推荐使用 `Type::Member` 形式。

普通枚举的成员可以直接使用：

```VeroScript
State state = Running;
```

## 底层整数类型

如果没有指定底层类型，枚举默认使用 `int`。需要明确控制存储范围时，可以使用固定宽度整数类型，例如 `int8`、`uint8`、`int16`、`uint16`、`int32`、`uint32` 和 `int64`：

```VeroScript
enum class ByteState : uint8
{
	Empty = 0u,
	Full = 1u
};

enum class SignedLimit : int16
{
	Minimum = -32768,
	Maximum = 32767
};
```

无符号底层类型不能表示负数。显式指定底层类型后，成员值必须落在该类型可以表示的范围内。

## 使用枚举值

枚举值可以参与比较，也可以作为容器的键或元素：

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u,
	Paused = 2u
};

map<RunState, string> labels = map<RunState, string>();
labels[RunState::Running] = "running";

set<RunState> states = set<RunState>();
states.add(RunState::Paused);
```

枚举类型适合表达有限且稳定的状态集合。对于 `enum class`，应保留其枚举类型语义，不要把它当作可以与任意整数自由混用的普通整数。

## 导出枚举

枚举可以使用 `export` 导出，供其他模块通过 `import` 使用：

```VeroScript
export enum class Operation : uint8
{
	Add = 1u,
	Multiply = 2u
};
```

导入模块后，使用完整的枚举类型名称访问作用域枚举成员：

```VeroScript
Operation operation = Operation::Add;
```

嵌套类型、局部声明、访问规则和分段泛型实参请参见[类型作用域](scope.md)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/enums.md) · [fr-FR](../../fr-FR/language-basics/enums.md) · [ja-JP](../../ja-JP/language-basics/enums.md) · [ko-KR](../../ko-KR/language-basics/enums.md) · [zh-CN](enums.md) · [zh-TW](../../zh-TW/language-basics/enums.md)
