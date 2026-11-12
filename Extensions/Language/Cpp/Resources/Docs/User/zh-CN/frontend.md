# C++ 前端

## 项目合并与表示边界

多文件合并不是 linker：它不会根据输入顺序选择定义，不会重命名内部链接实体，
也不会替任意 C++ 所有权或存储规则做解析。`std::shared_ptr<T>` 不会被直接
擦除为 `T`；共享所有权需要显式适配器。`std::size_t` 和 `std::ptrdiff_t`
按照目标 ABI 映射，指针及其指向对象的 cv 限定属于重载身份的一部分。

`VeroScriptCppFrontend` 将有意限定的 C++17 子集解析为稳定的公开 AST，供 [BindGen](bindgen.md) 和 [Transpiler](transpiler.md) 共享。无法支持的构造会产生诊断，绝不会静默近似。

公开 API 包括 `ParseSource`、`ParseFile`、`ParseFiles`、`MergeTranslationUnits`、`MapTypeToVeroScript`、`ResolveTypeAliases`、`EvaluateEnumValues`、`TryGetEnumValueRange`、`GetFunctionIdentity`、`FormatDiagnostic` 和 `JoinTokens`。AST 保留源码位置、命名空间、访问控制、限定名、构造函数、字段、函数、枚举、基类、引用种类、指针及指向对象的 cv 限定，以及 `constexpr`、`consteval`、`constinit`、`noexcept` 等语义修饰符。

`FParseOptions` 控制 include、`#pragma`、未知预处理指令的拒绝策略及函数体解析。`FTypeMapOptions` 明确指定已知宿主类型的原始指针、右值引用、已知宿主类型、枚举、类型参数及忽略 include 时所需别名的映射策略。应主动配置这些策略；允许解析某个声明并不代表它一定可以自动绑定或转译。

支持命名空间、`using Name = Type`、普通 `typedef Type Name`，以及带类型参数和末尾默认类型参数的类/函数/别名模板；还支持有作用域和无作用域枚举、类/结构体、访问说明符、字段、构造函数、方法、自由函数、全局变量、定宽整数、标准字符串以及拥有 VeroScript 对应类型的标准容器。普通和 `u8` 原始字符串会按完整分隔符扫描并规范化为 UTF-8 转义字面量；宽原始字符串会被拒绝。简单的同类型声明器列表会展开，混合指针/引用声明器列表仍不支持。命名的前向声明可以与定义合并。

类外的构造函数、析构函数、方法、运算符和传统静态数据成员定义可以与声明匹配，也可以放在项目的其他输入文件中。C++17 属性会在记录、枚举、枚举成员、字段、函数和参数上以结构形式保留。枚举初始化器支持可检查的整数及可移植单字符表达式，保留目标 ABI 的整数字面量宽度与符号性，遵循 C++ 整数提升，允许无符号回绕，并拒绝有符号溢出和非法移位量。最终值必须能由公开的 `int64` 表示且符合映射后的底层类型范围。负有符号数的移位以及短路 `&&`／`||` 表达式仍不支持。

未指定底层类型的无作用域枚举，引用先前成员时保留初始化表达式的类型；固定底层类型的枚举按该类型进行整数提升。依赖此差异的 MSVC 宿主必须启用 `/Zc:enumTypes`（VS 2022 17.4 或更高），或为这些枚举显式指定底层类型。该选项默认关闭，`/permissive-` 也不会启用它；SDK 不会修改宿主编译选项。

预处理器边界很小：支持 `#pragma once`、include、常规完整文件头保护，以及 `extern "C"` 声明或声明块；一般条件预处理和未知指令会被拒绝。多文件收集会合并兼容的外部声明，但会拒绝重复定义、重复输入路径、内部链接冲突和匿名命名空间。

非类型模板参数、参数包、特化、SFINAE、concept、依赖编译期表达式、union、C 数组、匿名命名空间、未知预处理指令以及无法表示的指针均不支持。类型、函数和成员 friend 声明会作为记录级声明保留，但 inline friend 定义会被拒绝。允许的原始指针策略最多可放行指向已知宿主类型的单个可变指针；标量指针、指向 const 的指针和多级指针仍被拒绝。

类型查找优先当前声明作用域，支持相对限定名与 `::` 绝对限定名。已声明的用户类型优先于 `string`、`vector` 等便利映射名；无法解析的限定类型会报错。

记录、枚举及已绑定的模板参数优先于外层别名。手动解析别名时，可使用 `ResolveTypeAliases` 的三参数重载，通过查找选项提供 `KnownHostTypes`、`KnownEnumTypes` 和 `KnownTypeParameters`。

空的 `FType::DeclarationScope` 表示全局作用域，别名替换时仍会保留。`FBaseType::Type` 保留基类的结构化类型、查找作用域和模板参数。`bQualifyNamedTypes` 可为源码生成请求绝对限定的用户类型名；默认仍返回绑定目录使用的规范名称。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](frontend.md) · [zh-TW](../zh-TW/frontend.md)
