# C# 前端

`VeroScriptCSharpFrontend` 是 C# BindGen 和 C# Transpiler 共用的语义前端。内部使用 Roslyn，
但公开模型归 VeroScript 所有，不把 Roslyn 对象暴露给消费者。

前端支持多文件、命名空间、类、结构体、接口、record、枚举、委托、字段、
属性、事件、构造函数、方法、运算符、特性、XML 文档、泛型形状、源位置以及
`ref`/`in`/`out` 参数和返回元数据。类型映射覆盖 C# 基础类型、一维数组、
`List<T>`、`Dictionary<TKey,TValue>`、`SortedDictionary<TKey,TValue>`、
`HashSet<T>`、`Queue<T>`、`Stack<T>`、声明的枚举和已知宿主类型。

nullable、指针、委托、`dynamic`、无法解析的类型和开放类型参数会显式失败，
不会静默丢失语义。本模块是构建期声明库；需要生成绑定目录和代码时使用
C# BindGen 生成；需要执行严格的 C# 到 VeroScript 转换和 source map 时使用
`VeroScriptCSharpTranspiler`。

## 标准容器的泛型参数数量

<a id="container-mapping-arity"></a>

标准容器映射要求声明的参数数量一致：`List<T>`、`HashSet<T>`、`Queue<T>` 和 `Stack<T>` 为一个，`Dictionary<TKey,TValue>` 和 `SortedDictionary<TKey,TValue>` 为两个。同名但参数数量不同的自定义类型不会被转为畸形的内置容器；它需要显式的已知宿主类型映射，否则以 `VSCF2008` 失败。

## 声明可访问性

属性与嵌套类型的可访问性分别判断。CLR 隐式基类与显式声明的继承分开处理，消费声明模型时不应暴露私有访问器，也不应将 `System.ValueType` 作为宿主基类。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](frontend.md) · [zh-TW](../zh-TW/frontend.md)
