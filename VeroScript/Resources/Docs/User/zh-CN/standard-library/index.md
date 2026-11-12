# 标准库参考

VeroScript 标准库以模块形式提供常用的字符串、数学、文件、路径、并发和事件能力。脚本可以导入 `core.vs` 使用聚合的运行时模块，也可以只导入需要的模块。

调用模块所属函数时，使用完整公开命名空间，例如 `std::strings::join` 或 `std::concurrent::spawn`；未限定的全局写法会被拒绝。`array<T>`、`print` 等基础类型和直接提供的函数仍可使用原有规范名称。命名空间别名不能替代所需的模块导入。 [std](../language-basics/standard-namespace.md).

- [受管理内存](memory.md)：`std::memory`

- [属性与方法函数值](attribute.md)：`std::attribute<T>` · `std::bind_method<F>`

## 模块目录

- [内置函数](builtins.md)：无需导入的输出、转换、数值与环境入口；输入接口见 io.vs。
- [核心入口](core.md)：重新导出该页列出的运行时模块；compiler.vs 需要显式导入。
- [字符串](string.md)：查找、截取、拆分、连接、修剪、大小写、替换和格式化。
- [数学](math.md)：三角函数、平方根、取整和范围限制。
- [算法](algorithm.md)：泛型数组排序、反转、查找与计数。
- [数值聚合](numeric.md)：累加、求和与平均值。
- [随机数](random.md)：使用显式独立状态的确定性伪随机流。
- [输入输出](io.md)：文件、目录、标准输入和工作目录。
- [路径](path.md)：文件名、目录名、扩展名和路径拼接。
- [并发](concurrent.md)：通道、隔离任务、任务等待和协作取消。
- [事件](events.md)：单播委托和多播委托。

## 导入约定

完整导入：

```VeroScript
import "core.vs";
```

按需导入：

```VeroScript
import "string.vs";
import "path.vs";
```

标准库函数通常是公开导出的声明。函数的参数和返回类型以对应模块中的实现为准；涉及文件、线程或宿主环境的操作还可能受到运行时权限和宿主配置影响。

- [应用国际化 — std::i18n](i18n.md): `std::i18n`

编译扩展显式导入 `compiler.vs`。`core.vs` 不重导出此模块，普通运行时代码仅导入它不会获得编译权限。

[编译扩展](compiler.md)


- [标准库基础](foundation.md)

- [variant — 动态值盒](variant.md)
<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/index.md) · [fr-FR](../../fr-FR/standard-library/index.md) · [ja-JP](../../ja-JP/standard-library/index.md) · [ko-KR](../../ko-KR/standard-library/index.md) · [zh-CN](index.md) · [zh-TW](../../zh-TW/standard-library/index.md)
