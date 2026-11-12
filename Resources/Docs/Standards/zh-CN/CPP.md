# C++ 代码规范

适用范围：[VeroScript 本项目开发与贡献](README.md)。

本文件是 C++ 的当前权威规范。除非明确写为“推荐”或“按需”，本文件中的规则均视为
必须遵守的格式和结构规则。

## 1. 缩进、作用域与花括号

- 一个逻辑层级使用一个 Tab 缩进；命名空间、类型、方法、条件、循环、lambda 和局部作用域
  都必须增加一级缩进。
- 左花括号必须独占一行，且与其所属声明或控制语句对齐：

  ```cpp
  void Process(const FRequest& Request)
  {
  	if (Request.bEnabled)
  	{
  		Execute(Request);
  	}
  }
  ```

- 不强制在左花括号后或右花括号前留空行；空行只用于分隔不同的逻辑分块。
- 右花括号与对应左花括号对齐。`else`、`else if`、`catch` 和 `do` 尾部的 `while` 必须另起一行，与对应右花括号对齐；子句的左花括号也独占一行：

  ```cpp
  if (bPrimary)
  {
  	HandlePrimary();
  }
  else
  {
  	HandleFallback();
  }
  ```

- 所有具有执行体的控制分支必须使用花括号和独立作用域，包括 `if`、`else`、`for`、
  `while`、`do`、范围 `for` 和每个 `switch case`。
- 唯一允许省略花括号的执行分支，是**分支体仅含一条直接 `return` 的单行卫语句**：

  ```cpp
  if (!bInitialized) return;
  ```

  此例外只适用于 `return;` 或 `return <表达式>;`，且不得与 `else` 链、清理、日志、赋值、
  宏条件或嵌套逻辑组合。`break`、`continue`、`throw`、`co_return` 以及任何包含多条语句的
  分支都不是例外，必须使用花括号作用域。

## 2. 类型整体布局

类型内容按以下顺序安排。没有内容的分块不写空分块。

1. 类型顶部按需放置 `friend` 声明、`using`、`typedef` 等关系或别名；
2. 随后放置类型需要的宏，例如 `DECLARE_XXX`；
3. 访问区域固定按 `public`、`protected`、`private` 的顺序出现；
4. 每个访问区域内，先放继承而来的 `override` 方法；
5. 再放内部类型；
6. 然后放本类型的方法；
7. 最后放成员变量。

建议骨架：

```cpp
class FExample : public IBase, protected ISecondary
{
	friend class FExampleFactory;
	using FBase = IBase;

	DECLARE_EXAMPLE_TYPE(FExample)

public:
	//~ Begin IBase Interface
	virtual void Initialize() override;
	virtual void Shutdown() override;
	//~ End IBase Interface

	enum class EState
	{
		Idle,
		Running,
	};

	struct FOptions
	{
		bool bEnabled = false;
	};

	void Start(const FOptions& Options);

protected:
	//~ Begin ISecondary Interface
	virtual bool CanStart() const override;
	//~ End ISecondary Interface

	void NotifyStarted();

private:
	void ResetState();

	EState State = EState::Idle;
	FOptions Options;
};
```

### 2.1 访问区域

- `public`、`protected`、`private` 必须按此顺序安排。
- 在一个类型中，同一访问级别应尽量只出现一个连续区域。只有语言、宏展开或清晰的语义边界
  确有必要时才重复访问说明符；重复后仍不得破坏总体顺序。
- 成员变量按其实际访问属性放在对应区域的最后，不把数据夹在方法或内部类型之间。

### 2.2 继承方法

- 在每个访问区域中，`override` 方法优先于本类型新定义的方法。
- 多个基类时，按类型继承列表从左到右的顺序分组；同一基类内按该基类接口声明顺序排列。
- 当一组继承方法不止一个、所属基类不直观，或与本类型方法相邻容易混淆时，使用配对标记：

  ```cpp
  //~ Begin ISerializable Interface
  virtual bool Serialize(FArchive& Archive) override;
  virtual bool Deserialize(FArchive& Archive) override;
  //~ End ISerializable Interface
  ```

- `Begin` 和 `End` 必须成对、名称完全一致，使用直接基类或接口名。只有一个明显的 `override`
  方法时按需省略该标记，不能为每一个函数机械添加。

### 2.3 内部类型

同一访问区域内的内部类型固定按以下优先级排列：

1. `enum`、`enum class`；
2. `struct`；
3. 其他类型声明，例如 `union`、局部别名、函数对象类型；
4. `class`。

- 内部类型各分块之间空一行；同类内部类型按依赖关系和名称形成易读顺序。
- 本应放在类型顶部的普通 `using`、`typedef` 不应无故留到内部类型区域。只有别名依赖前面的
  内部类型时，才放在“其他类型声明”位置。
- 若完整定义的依赖关系与此顺序冲突，优先使用前向声明解除依赖；确实必须打破顺序时，在
  紧邻位置说明完整类型依赖原因。

## 3. 头文件、前向声明与实现文件

### 3.1 头文件与按需实现文件

- 每个自有 C++ `.h` 文件的文件头固定为：两行 SPDX 版权与许可证声明后空一行，再写 `#pragma once`。`#pragma once`
  后空一行，再开始 include 或其他声明。

  ```cpp
  // SPDX-FileCopyrightText: <copyright-holder>
  // SPDX-License-Identifier: MIT

  #pragma once

  #include <cstdint>
  ```

- `.inl` 是被包含的实现片段，不等同于 `.h`。它同样必须以两行 SPDX 版权与许可证声明开始，声明后空一行，
  但默认**不**写 `#pragma once`，也不要求配套 `.cpp`；只有确认该文件是每个翻译单元仅能包含一次的
  头文件式内容时，才按 `.h` 的方式使用 `#pragma once`。

- `.h` 不强制配套同名 `.cpp`。只有实现或构建职责确实需要独立编译单元时，才按职责组织 `.cpp`；不为文件数量或目录对称创建空实现文件。
- 仅含声明、类型定义、常量、模板或 `inline` 定义的头文件可以独立存在。必须在头文件中可见的定义继续保留在头文件中，不因文件拆分移走。
- 确需 `.cpp` 时，将其放在同一模块的实现位置，并显式包含所实现的头文件。没有 `Public/`、`Private/` 分层时通常同目录；有分层时位于 `Private/` 的合理语义子目录，相对路径无需完全对应 `Public/`。
- 仅有实现职责的 `.cpp` 可以没有同名 `.h`。
- 清理既有空 `.cpp` 时，核对其实际构建、注册、显式实例化及平台职责，并同步更新构建引用。
- 第三方和自动生成文件保留其适用的文件组织方式。

### 3.2 声明与定义顺序

- `.h` 中声明了、且需要在 `.cpp` 实现的方法，`.cpp` 必须按 `.h` 中的声明顺序定义。
- 分组也必须保持一致：继承接口实现先后、本类型公开/保护/私有方法的相对顺序不得在 `.cpp`
  中打乱。
- `.cpp` 的匿名命名空间辅助函数、文件局部常量和仅实现需要的类型可以位于方法定义前；它们
  不改变头文件声明对应定义的相对顺序。
- 新增声明时，应同时确认 `.cpp` 的定义位置，而不是把实现统一追加到文件末尾。

### 3.3 前向声明与同头多类型声明

- 所有前向声明按以下优先级排列：`enum` → `struct` → 其他声明 → `class`。
- 前向声明的 `enum` 必须采用 C++ 合法的 opaque-enum 形式：`enum class EName;`，或显式指定
  底层类型的 `enum EName : std::uint8_t;`。不得声明没有底层类型的非作用域 `enum EName;`。
- 同一个头文件中可以定义多个顶层类型；在不受完整类型依赖约束时，也按：
  `enum` → `struct` → 其他声明 → `class`。
- “其他类型声明”包括 `union`、`using`/`typedef` 别名和函数类型别名。类模板按 `class` 排列，
  结构模板按 `struct` 排列；不能机械地把所有 `template` 声明归入“其他”。
- 如果完整类型依赖要求不同顺序，先尝试前向声明；无法避免时，依赖关系优先，并在相邻处
  留下简短说明。

### 3.4 成员变量默认值与布尔位域

- 成员变量需要初始值，且默认状态能在声明处直接、清晰地表达时，应在声明处给出默认值。
- 依赖构造参数、运行期状态或其他成员初始化结果的值，应在构造函数初始化列表中设置；不要为了遵循本条而写出误导性的固定默认值。
- 仅表示开关状态、适合压缩存储，且不需要取地址、引用、原子操作或稳定外部二进制布局的布尔成员，优先使用 1 位位域。例如：`uint8 bEnabled : 1;`（或 `std::uint8_t bEnabled : 1;`）。位宽写作 `: 1`；`uint8` 是存储类型，不是位宽。
- 本项目当前最低标准为 C++17。C++17 的位域不能使用类内默认成员初始化；位域需要非零或明确初始状态时，应由构造函数初始化列表初始化。升级到支持该语法的语言标准前，不要写 `uint8 bEnabled : 1 = true;`。

## 4. `switch` 与执行分支

- 每一个 `switch` 必须包含 `default`。
- `case` 和 `default` 与 `switch` 内容层级对齐；标签下的执行体花括号整体再缩进一级，花括号内的
  语句再缩进一级：

```cpp
switch (Kind)
{
case EKind::First:
	{
		HandleFirst();
		break;
	}
case EKind::Second:
	{
		HandleSecond();
		break;
	}
default:
	{
		HandleUnknown();
		break;
	}
}
```

- `case` 标签若唯一执行路径是直接结束当前方法的 `return` 或 `return <表达式>`，可以沿用第 1 节的
  单行卫语句例外；该标签下不得混入日志、清理、局部声明、条件或其他语句。除此以外，所有
  `case` 和 `default` 都必须使用上述独立花括号作用域。
- `default` 即使按设计不执行任何业务动作，也必须保留花括号作用域和显式 `break`。只有“不处理”的
  原因不从上下文显而易见时，才添加简短英文注释说明该选择。
- 不依赖隐式 fallthrough。需要贯穿时，使用项目允许的显式标记并说明原因。
- 除本规范第 1 节严格定义的直接 `return` 卫语句例外外，所有执行分支都使用花括号。

## 5. 预处理宏

- `#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、`#endif` 与它们包裹代码所在的**同一逻辑层级**
  缩进；预处理条件不额外增加一层缩进。
- 宏包裹的代码在移除宏后应保持原来的层级和可读性：

  ```cpp
  void Initialize()
  {
  	#if WITH_DIAGNOSTICS
  	RegisterDiagnostics();
  	#endif

  	StartRuntime();
  }
  ```

- 条件宏必须尽量覆盖完整的语义分块，不把单条声明或单个表达式切成难以阅读的碎片。
- 成对宏必须保持相同缩进，`#endif` 可按需添加简短的条件注释以帮助识别较长分块。
- 宏生成的声明仍按本文件的类型布局顺序放置。

## 6. 注释与区域

- 单行说明使用 `//`，放在所说明声明或分块的紧邻上方；短行尾说明只用于不会妨碍阅读的情况。
- 多行 API 语义、所有权、线程、失败条件或复杂不变量使用 `/** ... */` 文档注释。
- 不用注释重复名称明显表达的内容，例如 `// Gets the name` 后接 `GetName()`。
- 需要解释的成员变量优先说明单位、所有权、有效期、线程约束、缓存失效条件或与其他字段的关系。
- `//~ Begin <Name> Interface` / `//~ End <Name> Interface` 只用于继承接口分块；不能被滥用为
  普通方法区标题。
- 其他较大的分块可以使用简短说明注释，但优先通过本规范规定的固定布局让结构自解释。

## 7. 其他推荐规则

- 当前不规定 `.cpp` 或 `.h` 的 include 顺序或分组方式；遵循当前文件的可读性和构建约束即可。
  前向声明的布局仍遵循第 3.3 节。
- 使用 `nullptr` 表达空指针，使用显式转换表达可能损失信息的类型转换。
- 资源所有权、可能为空的对象和跨线程回调必须在类型或方法注释中清楚表达。
- 空函数、默认实现和刻意忽略参数应通过最短且可读的方式表达意图；不要为了满足形式而制造
  无意义局部变量或空作用域。

## 8. 待维护者补充

以下主题尚未形成硬性规则，后续由维护者决定后补充：命名细节、模板/概念、异常策略、智能指针
策略、测试框架、格式化工具和行宽。include 顺序当前明确不作规定。
