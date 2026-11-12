# 语言基本特性

本目录按主题介绍 VeroScript 的基本语言规则。每篇文档说明一个相对独立的语法主题，并使用最小示例说明行为；完整的 API 和运行时机制不在本目录中展开。

## 源文件、模块和作用域

- [基本语法](basic-syntax.md)：源文件结构、语句、代码块、标识符和关键字。
- [注释](comments.md)：单行注释和块注释。
- [作用域](scope.md)：模块、命名空间、函数和代码块的作用域规则。
- [命名空间](namespaces.md)：命名空间声明、限定名称和 `using namespace`。
- [`std` 标准命名空间](standard-namespace.md)：内置类型、运行时函数及其兼容别名的完整列表。
- [模块](modules.md)：模块导入、导出、重新导出和库包导入。
- [预处理器](preprocessor.md)：宏、条件编译、字符串化、标记拼接和模块包含。

## 值和类型

- [字面量](literals.md)：整数、浮点数、字符串、宽字符串、布尔值和空值。
- [数据类型](data-types.md)：内置类型、固定宽度整数、容器类型和类型推导。
- [值工具类型](value-utility-types.md)：optional、weak、共享结构体句柄和映射条目 pair。
- [变量](variables.md)：变量声明、初始化、赋值和引用现有值。
- [常量](constants.md)：`const` 变量和常量成员。
- [类型别名与特殊声明](declarations.md)：`using`、`typedef`、`= default` 和 `= delete`。
- [结构体](structs.md)：值类型、字段、方法、构造和结构体复制语义。
- [类](classes.md)：共享身份、成员访问、构造、继承和虚方法。
- [接口](interfaces.md)：方法契约、默认实现、接口继承和类型转换。
- [枚举](enums.md)：枚举成员、作用域、底层整数类型和枚举值使用。
- [数组](arrays.md)：动态数组、固定长度原生数组、索引和范围遍历。
- [容器](containers.md)：映射、集合、队列、栈、双端队列和通道。
- [字符串](strings.md)：`string`、`wstring`、字符串运算和标准库辅助函数。
- [引用](references.md)：引用参数、只读引用以及引用范围遍历。
- [反射](reflection.md)：`type<T>`、字段和方法信息、枚举信息以及声明元数据。
- [静态成员](static-members.md)：静态工具类型、类型级字段、静态方法和泛型类型中的独立静态存储。

## 表达式和运算

- [运算符](operators.md)：算术、比较、逻辑、位运算、赋值和运算符重载。

## 泛型、函数和控制流

- [泛型](generics.md)：泛型类型、泛型函数、默认类型参数和 `where` 约束。
- [函数](functions.md)：函数声明、参数、返回值、默认参数和重载。
- [Lambda 和函数值](lambdas.md)：函数类型、Lambda 表达式、捕获和引用参数。
- [控制流](control-flow.md)：条件、循环、`switch`、跳转和返回。

[注解与源码元数据](annotations.md)

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/index.md) · [fr-FR](../../fr-FR/language-basics/index.md) · [ja-JP](../../ja-JP/language-basics/index.md) · [ko-KR](../../ko-KR/language-basics/index.md) · [zh-CN](index.md) · [zh-TW](../../zh-TW/language-basics/index.md)
