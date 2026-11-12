# 18：选择和使用 C++ 工具链模块

本教程完成一个原生集成流程：根据任务选择嵌入、绑定生成、C++ 到 VeroScript 的转换或 AOT，并运行最小的正确流程。

前提：匹配的 `veroscript`、Cpp BindGen 和 Transpiler 工具可从可执行文件搜索路径调用，且工作目录可写。下文路径表示你提供的输入：准备包含受支持声明的 `Include/Types.hpp`、`Include/Api.hpp`，源码定义 `Source/Types.cpp`、`Source/Game.cpp` 及有效的 `script.vs`，或将所有路径替换为已有文件。命令从该工作目录执行。预期得到合并的 `bindings.json`、API 参考与清单，带源映射的 VeroScript 模块，以及 AOT 步骤的 `generated.cpp`；生成这些文件不会注册运行时宿主回调。

## 第一步：选择边界

先阅读 [C++ SDK 文档](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/index.md)：运行长期会话和调用脚本使用 `VeroScriptCpp`；从 C++ 声明生成绑定目录使用 `VeroScriptCppBindGen`；把受支持的 C++ 源码转换为 VeroScript 模块使用 `VeroScriptCppTranspiler`；把 VeroScript 转为本机构建所需的 C++17 源码使用 `VeroScriptCppAot` 或 `veroscript aot`。

BindGen 描述宿主 API，Transpiler 转换源码行为，AOT 把 VeroScript 转换为 C++；三者方向不同，不能互相替代。

## 第二步：生成并校验绑定目录

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

绑定目录供编译器和工具使用，API 参考供人阅读；它不会替你注册回调。宿主完成对应注册后，校验目录并检查脚本：

```powershell
veroscript bindings validate build/bindings.json
veroscript --check script.vs --binding-catalog build/bindings.json
```

多个输入应使用项目模式，在生成输出前合并声明：

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp --output-dir build/generated --manifest build/generated/transpile.manifest.json
```

遇到不支持的声明时，应编写明确的宿主包装，而不是修改目录来掩盖指针或所有权问题。

## 第三步：转换源码并生成 AOT

```powershell
VeroScriptCppTranspiler Source/Types.cpp `
  -o build/Types.vs `
  --source-map build/Types.vs.map.json

veroscript aot script.vs -o build/generated.cpp
```

多个输入应使用项目模式，并把 manifest 和源映射与生成源码一起保存。AOT 输出是 C++17 源码而不是可执行文件；请在目标项目中验证运行时链接、回调生命周期和宿主状态同步。

需要确定性的多文件 C++ 产物时，使用 `FCppAotSourceBundle` API，并将入口源码、生成片段及清单一起保存。这些文件是目标 C++ 项目的构建输入，不是已经构建的可执行文件。

生成的顶层全局变量和类静态字段仍按原生工作线程隔离。AOT 会在 `main` 之前按声明顺序
初始化每个工作线程的全局访问器。不要把全局变量当作隐式的跨线程共享机制；需要共享时，
应使用通道或同步的宿主状态。

当成员 Lambda 使用接收者时，AOT 会保留源类型的语义：类接收者仍然是存活的保留
对象，结构体接收者则是值快照。嵌套 Lambda 会保留已经捕获的接收者，无捕获 Lambda
不能使用 `this`。C++ AOT 还会拒绝在析构函数体中捕获类接收者，因为此时接收者已经
进入析构过程。

## 练习

1. 将一个目录项与对应的 C++ 注册代码进行对照。
2. 用项目模式转换两个 C++ 输入并查看 manifest。
3. 为使用 channel 的脚本生成 AOT，并写下需要同步的宿主状态。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/cpp/01-cpp-toolchain.md) · [fr-FR](../../fr-FR/cpp/01-cpp-toolchain.md) · [ja-JP](../../ja-JP/cpp/01-cpp-toolchain.md) · [ko-KR](../../ko-KR/cpp/01-cpp-toolchain.md) · [zh-CN](01-cpp-toolchain.md) · [zh-TW](../../zh-TW/cpp/01-cpp-toolchain.md)
