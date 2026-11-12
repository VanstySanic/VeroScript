# Python AOT

`veroscript aot --target=python` 会将通过编译检查的 VeroScript 程序转换为可读
的 Python 源码。生成文件包含必要的兼容运行时，运行时不加载 VeroScript VM、
`VeroScriptInterop` 或 `veroscript` 扩展。

## 运行时边界

生成程序需要用户提供 CPython 3.11–3.15。它不会打包 Python 解释器、Python
SDK、宿主绑定或 VeroScript 原生库。若要在 Python 应用中嵌入运行时，请使用
[Python SDK](index.md)。

生成的 worker 使用操作系统线程。脚本全局变量和静态字段从每个 worker 的独立
快照初始化；worker 之间的显式同步能力是 channel。任务 ID 只属于创建它的
worker，不能在其他 worker 中 join 或 cancel。

## 单文件输出

```powershell
veroscript aot --target=python app.vs -o app.py
python app.py
```

应用模块不在入口文件旁边时，可重复使用 `-I`/`--import-path`。只有生成成功后
才会替换输出文件。

## 源码 bundle

```powershell
veroscript aot --target=python app.vs --output-dir app_python --bundle-name app
python app_python
python -m app_python
```

bundle 包含 `__main__.py`、`__init__.py`、`_veroscript_runtime.py`、
`_veroscript_program.py` 和 `python-aot.manifest.json`。完整 import 图会在写入前
检查；含有无关文件的目录不会被替换。

所有文件先暂存，再分阶段替换普通目录；最终路径不可为符号链接或 Windows 目录联接。
提交期间会先移开原目录，输出路径可能短暂不存在；提交失败时尝试恢复原目录。

## 支持范围与明确限制

支持标量、字符串、容器、函数、namespace、import、enum、class、struct、interface、
转换、异常、`switch`、`algorithm`/`numeric`/`random` 以及标准并发 task wrapper。
`ref`/`out`、用户自定义开放 generic、重载集合、析构函数、多重具体基类、class
default、重复的私有扁平类型名、`goto`/label、`switch` fallthrough、宿主绑定、
源码 lambda 和局部 `static` 声明会被拒绝。未支持构造只产生诊断，不会留下部分输出。

函数值声明须显式使用 `fn(...) -> T` 类型，暂不支持函数值的 `auto` 推断。
支持命名空间限定的函数值。`Super` 接收者和可变参函数值会被拒绝；可变参函数的直接调用仍然支持。

`shared<T>` / `handle<T>` 包装类型、可调用对象形式的任务入口和执行式反射也会
被拒绝。可通过 Python SDK 使用这些运行时功能。

关闭时会关闭已有通道，并在同一等待期限内等待 worker 及其派生的各代任务结束。关闭期间新建的通道初始即为关闭状态。

生成程序使用 `VERO_JOIN_TIMEOUT_MS` 限制 worker 关闭时的等待时间，默认值为
30000 毫秒。worker 调用 `exit(n)` 时只会使该任务失败，不会终止父进程。

如果目标是独立、可检查的 Python 源码交付物，请使用 Python AOT；如果需要
虚拟机或 JIT、动态加载、宿主绑定、反射、热重载、保留调用对象或运行时包操作，
请使用 [Python SDK](index.md)。Python 前端、BindGen 和转换器是独立的源码工具链。

## 运行时内存支持

独立 AOT 当前不支持 `std::memory` 的查询、缓冲区、视图及相关类型。实际调用或使用这些类型时，生成器会报告明确错误；仅导入未使用的 `memory.vs` 或 `core.vs` 仍可生成。

[Runtime SDK](index.md)

独立 Python AOT 支持 `std::console` 文本输出、终端能力与尺寸查询、颜色、光标和行控制。输出重定向时返回无终端能力、零尺寸和控制失败；非法名称、模式或坐标仍会报错。未知控制台内置函数会被生成器明确拒绝。

Windows 颜色、样式重置、清屏、清行及光标控制使用原生控制台 API，无需开启 ANSI 模式。尺寸和坐标对应可见窗口；清屏不会删除窗口之外的历史内容。

## 属性源码导出

属性配置比较是受支持的只读 builtin 特例：`has_same_configuration(ref other)` 不建立可写别名。`try_get()` 可用，`try_get(ref output)` 仍不支持；各操作边界见 attribute 参考。

[std::attribute](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/attribute.md)

<a id="native-bundle-api"></a>

原生 SDK 包含 `VeroScriptPythonAot.h`；`FPythonAotSourceBundle` 提供 `Files`、`ManifestJson`，通过 `EmitPythonSourceBundleFromSource`／`EmitPythonSourceBundleFromFile` 生成，再由 `WritePythonSourceBundle` 暂存替换目录。`BuildPythonSourceBundle` 可打包已生成的 Python。Python 控制包装的 `veroscript_executable` 选择 CLI 可执行文件或 SDK 根／bin 目录；未指定时依次查找 `VEROSCRIPT_EXECUTABLE`、`VEROSCRIPT_SDK` 和 PATH。这个外部 CLI 包装与生成的独立程序是不同层次。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](aot.md) · [zh-TW](../zh-TW/aot.md)
