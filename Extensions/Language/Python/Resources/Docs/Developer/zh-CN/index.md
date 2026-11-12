# VeroScript Python 技术文档


本目录面向维护者和绑定作者，描述源码与二进制契约，不是 Python 用户教程。Python/VeroScript 语法、API 与模块名、选项、JSON 键和诊断均按源码原样书写。

## 模块归属

- `VeroScriptPython`：稳定 C Interop ABI 之上的 CPython 适配器，拥有会话、类型值、容器、包、回调、反射、保留可调用对象、序列化和 Python 支持的宿主类。
- `VeroScriptPythonAot`：发射独立 Python 源或多文件源码包；输出不加载 VM、Interop 共享库或 CPython 扩展。
- `VeroScriptPythonFrontend`：静态解析受支持的带类型 Python 3.11 声明子集，不导入或执行输入模块。
- `VeroScriptPythonBindGen`：将前端声明转为 Binding Catalog 元数据和每输入 API 引用。
- `VeroScriptPythonTranspiler`：将受支持带类型 Python 子集降级为 VeroScript，并由真实编译器验证。

## 静态工具边界

Frontend、BindGen 和 Transpiler 是静态工具，绝不导入、执行或利用输入模块的运行时副作用。不支持的动态特性、无类型签名、无法保持的任意精度语义、async、反射和继承必须明确失败，不能近似。项目模式先解析完整模块图；相对包布局、模块名、导入、源码映射、API 引用和清单条目相互一致，任何解析、降级、导入解析或最终编译失败后均不替换输出。

## 运行时与生成代码边界

CPython 适配器在允许时释放 GIL，Python 回调时重新取得 GIL；创建线程限制、可调用对象所有权、回调重入和共享 Python 状态同步是公共契约。worker 回调不得调用创建线程受限的会话操作。独立 AOT 运行时仅为文档规定的可执行子集保持 VeroScript 整数、容器、异常、模块和并发语义；生成 worker 采用 OS 线程，隔离脚本全局/静态字段，并保留 channel 身份作为显式同步共享机制。

## Python 对象与产物细节

主要运行时对象是 `Session`、`Value`、`Package`、保留 callable 句柄、`HostParameter` 和宿主类/函数注册对象。会话须在创建线程显式关闭；空/窄容器使用显式元素类型，遍历及 `keys()`/`values()`/`items()` 返回拥有快照。宿主/变更回调各有异常与线程规则，回调拥有状态必须自行同步。

AOT Bundle 有清单和兼容运行时文件，文件先暂存，再分阶段替换空的普通目录或具有有效 VeroScript Python AOT 清单且无外来内容的目录；解释器 `__pycache__` 是唯一容忍的额外生成物。最终路径不可为链接或 Windows 目录联接；提交期间输出路径可能短暂不存在，失败时尝试恢复。生成程序选择用户 Python 解释器，不发现或打包 Python SDK。

## 打包与版本

wheel/源码分发须携带匹配的原生扩展、Interop 库、Python 源、类型信息、标准库和本地化资源；`VeroScriptVersion.h` 是语义版本依据。前端项目模式每输入生成一个 AST 产物及项目清单；BindGen 生成合并目录、每输入 API 引用和 `bindgen.manifest.json`；Transpiler 保留相对包布局，解析项目相对导入，发射源码映射，仅在完整图验证后写入 `transpile.manifest.json`。

## 详细页面

- [Python 运行时与静态工具](runtime-and-tools.md)
- [Python AOT 与打包](aot-and-packaging.md)
- [Python 工具链与包契约](toolchain-and-packaging.md)

## 依据

以 Python 源包、原生适配器、前端 AST、生成器/转译器测试、CMake、包元数据和分发验证工具为准。支持子集、线程规则、生成产物、包布局或 ABI 映射变更时必须更新本页。


[Android/iOS 中使用 Python](mobile.md)

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
