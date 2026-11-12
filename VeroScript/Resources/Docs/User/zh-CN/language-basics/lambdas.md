# Lambda 和函数值

Lambda 是可以保存、传递和调用的函数值。函数类型使用 `function` 表示，`fn` 是它的简写形式。

## 函数类型

函数类型由参数列表和返回类型组成。推荐在箭头两侧保留空格，以便在长声明中保持可读性：

```VeroScript
function(int) -> int increment = function(int value) -> int => value + 1;
function() -> string get_name = function() -> string => "VeroScript";
function(int&, int) -> void mutate = function(int& value, int amount) -> void
{
	value += amount;
};
```

函数值可以作为参数、返回值或局部变量：

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

function(int) -> int double_value = function(int x) -> int => x * 2;
int result = apply(double_value, 21);
```

`fn(int) -> int` 与 `function(int) -> int` 等价。公共文档和较长的声明通常使用完整的 `function` 拼写。

## Lambda 表达式

Lambda 可以使用表达式体，也可以使用代码块体：

```VeroScript
function(int) -> int increment = function(int x) -> int => x + 1;

function(int) -> int square = function(int x) -> int
{
	return x * x;
};
```

调用函数值的圆括号语法与调用普通函数相同：

```VeroScript
int a = increment(41);
int b = square(5);
```

当函数类型包含引用参数时，调用点必须显式写出引用标记。公共示例推荐使用 `T&` 和 `&expression`；`ref T` 与 `ref expression` 仍是兼容写法：

```VeroScript
void add_to(int& value, int amount)
{
	value = value + amount;
}

function(int&, int) -> void mutate = add_to;
int value = 40;
mutate(&value, 2);
```

## 按值捕获

Lambda 可以捕获外层作用域中的局部变量。普通 Lambda 和 `[=]` 使用按值捕获；显式列表只捕获列出的名称：

```VeroScript
int base = 40;

function(int) -> int add = function(int x) -> int => x + base;
function(int) -> int add_explicit = [base](int x) -> int => x + base;
```

按值捕获在创建闭包时初始化，默认是只读绑定。需要修改自有捕获时，在参数列表后写 `mutable`，修改会保留到下一次调用；复制函数值仍共享同一闭包环境，多次执行创建表达式得到独立环境。普通 const 来源不会因 mutable 变为可写。

VM、Mixed、JIT 和 Core 支持型 AOT 使用同一运行时服务。独立 C++／C# 源码导出支持其值捕获和标量局部 static；引用初始化捕获、复杂对象 static 及跨作用域的 static 引用需要 Core 路径。Python 源码导出仍不支持 Lambda 或局部 static。

独立 C++ 导出中，拥有捕获或接收对象的 Lambda 若返回引用，也需要 Core AOT：原生引用无法在函数值释放后继续保有闭包存储。

空捕获列表 `[]` 不捕获外层局部变量，但全局名称仍然可以通过普通名称查找访问：

```VeroScript
function(int) -> int increment = [](int x) -> int => x + 1;
```

## 按引用捕获

VeroScript 支持按引用捕获。`[&]` 将使用到的外层局部变量按引用捕获，`[&name]` 只按引用捕获指定变量；默认捕获也可以与显式按值捕获组合：

```VeroScript
int running = 40;
function() -> int next = [&]() -> int
{
	running++;
	return running;
};

print(next());
print(running);

int step = 2;
function() -> int advance = [=, &running]() -> int
{
	running += step;
	return running;
};
```

可用的捕获形式包括：

- `[&]`：使用到的外层局部变量按引用捕获。
- `[&name]`：`name` 按引用捕获。
- `[=]` 或普通 `function(...)` Lambda：使用到的外层局部变量按值捕获。
- `[=, &name]`：默认按值捕获，但 `name` 按引用捕获。
- `[&, name]`：默认按引用捕获，但 `name` 按值捕获。
- `[]`：不捕获外层局部变量。

按引用捕获的变量会被提升到安全的共享存储中。即使闭包在创建它的函数返回后仍然存在，也不会继续引用已经失效的栈槽；外层变量、同一变量的别的闭包以及当前闭包看到的是同一个可变状态。若外层绑定本身是常量，引用捕获会保留其只读性质。

显式捕获列表中的名称必须来自允许的外层绑定，不能重复声明默认捕获已经涵盖的同一种捕获方式。空捕获列表不能访问外层局部变量；全局变量和对象成员仍按各自的名称查找与句柄语义处理。

## 成员函数返回 Lambda

成员函数可以返回捕获当前对象状态的函数值：

```VeroScript
class Counter
{
public:
	int value;

	Counter(int initial)
	{
		value = initial;
	}

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

Counter counter = Counter(42);
function() -> int get = counter.getter();
int current = get();
```

这里的 `value` 是对象成员，不是已经离开作用域的普通局部变量。闭包仍然遵循类句柄和对象生命周期的语义；按引用捕获只改变捕获绑定的访问方式，不会把任意对象自动复制成值对象。

如果 Lambda 直接使用接收者本身，接收者会遵循其外层类型的语义。类接收者会
作为共享句柄保留，因此闭包可以观察之后发生的字段修改；结构体接收者会按值
捕获为快照，因此之后对原结构体的修改不会影响已捕获的接收者。嵌套 Lambda
也会按照同一规则保留或复制已经捕获的接收者。无捕获 Lambda 不能使用 `this`。

## 与引用语义的关系

引用参数的调用规则、引用返回值以及可安全返回的引用来源见[引用和引用语义](references.md)。按引用捕获属于闭包的捕获规则，不能用普通的按值捕获来替代。

完整示例见[示例索引](../examples.md)中的 Lambda 示例。

## 初始化捕获、mutable 与局部 static

使用 `auto` 时，应显式写出 Lambda 返回类型，例如 `() mutable -> void { ... }`。若已有 `function() -> void` 等函数类型上下文，Lambda 可以省略返回类型箭头。这沿用现有上下文推导规则；`mutable` 本身不会从函数体推导返回类型。

局部静态常量的 `static const int value = 42;` 与 `const static int value = 42;` 等价，推荐统一使用 `static const`。

```VeroScript
function() -> int make_counter()
{
	return [count = 0]() mutable -> int { return ++count; };
}

int remember_first(int seed)
{
	static int value = seed;
	return ++value;
}

void capture_example()
{
	auto next = make_counter();
	auto alias = next;
	assert(next() == 1 && alias() == 2);
	int total = 0;
	auto advance = [&target = total, step = 2]() -> int { target += step;
		return target; };
	assert(advance() == 2 && total == 2);
	assert(remember_first(10) == 11 && remember_first(99) == 12);
}
```

初始化捕获的右侧在外层作用域从左到右求值一次；新名字只在正文可见。`[&name = expression]` 要求可以安全保留的左值，并保留来源的 const。全局、类型静态字段与局部 static 通过普通名称查找直接访问，快照它们应使用初始化捕获。

局部 static 在第一次执行到声明时初始化，后续调用共享同一值；同一 Lambda 表达式的不同闭包共享其函数 static。初始化失败并完成清理后可以重试，递归初始化报错。不同程序状态、Session 和 worker 隔离；清空状态会重新开始。静态引用变量不支持。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/lambdas.md) · [fr-FR](../../fr-FR/language-basics/lambdas.md) · [ja-JP](../../ja-JP/language-basics/lambdas.md) · [ko-KR](../../ko-KR/language-basics/lambdas.md) · [zh-CN](lambdas.md) · [zh-TW](../../zh-TW/language-basics/lambdas.md)
