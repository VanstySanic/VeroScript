# C++ 前端、BindGen 与 Transpiler

## 前端 AST

`VeroScriptCppFrontend` 将有界 C++17 子集解析为稳定模型：token、源位置、属性、类型、参数、模板、record、enum、变量、函数、普通 `typedef`/`using` 别名、命名空间和函数体。可表示时保留限定名、C 链接、存储/语义限定符、构造/析构、friend 和同文件类外定义。

`FParseOptions` 控制 include/pragma、未知预处理指令和函数体解析；`FTypeMapOptions` 控制原始宿主指针、右值引用、已知宿主/enum 类型、类型参数和项目别名。它们是显式选项，不是隐藏全局策略。

收集器跨输入合并兼容声明和前向声明，并可将一份输入中的常规类外成员定义与另一份输入的完整 record 配对。它拒绝重复定义、重复输入路径、内部链接冲突、匿名命名空间、不支持模板和无法保持所有权或控制流含义的构造。

## Binding Catalog 生成

BindGen 发射机器可读的 Binding Catalog JSON，也可为每个输入发射 API 引用和项目清单。合并后的目录是权威输入；API 引用仅供审阅，绝不能编译。仅当目录模式可表示时，才保留结构化属性、别名、enum 身份、重载信息、成员标志和文档。

项目生成先解析全部声明，为同名输入基名赋予确定名称，并以单一事务替换目录、引用与清单。

## 源代码 Transpiler

Transpiler 仅降级拥有直接 VeroScript 含义的构造。它发射 VeroScript 源和声明级源码映射；项目模式为每份输入发射一个模块和映射及清单。默认以真实 VeroScript 编译器编译生成源。无法表示的所有权、RTTI、指针访问、高级模板、不支持的 `std` 设施和属性必须明确失败。

成员 `operator()` 与 `operator[]` 保留重载和引用参数。源代码转换以 `T&` / `const T&` 输出引用签名，包括引用返回；运算符实参按核心规则隐式绑定引用。BindGen 保留运算符与引用参数标志，但当前 Binding Catalog 无法表示原生引用返回的存储和生命周期，因此对此明确拒绝。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/frontend-and-tools.md) · [fr-FR](../fr-FR/frontend-and-tools.md) · [ja-JP](../ja-JP/frontend-and-tools.md) · [ko-KR](../ko-KR/frontend-and-tools.md) · [zh-CN](frontend-and-tools.md) · [zh-TW](../zh-TW/frontend-and-tools.md)
