# Python SDK

独立 Python 生成、源码 bundle 及其限制请参阅 [Python AOT](aot.md)。

`VeroScriptPython` 是面向 CPython 的 VeroScript Interop ABI 适配器。Python 应用可以用它加载和执行脚本、调用脚本函数、注册类型化回调和类、读取元数据，并处理包和值，而不需要依赖私有 C++ 运行时类型。

SDK 支持 VM、mixed 和 JIT 后端、结构化诊断、类型化标量和容器、宿主函数与类、源代码覆盖、热重载、JIT 策略与统计、反射、包操作、保留调用对象、外部序列化、变更事件以及本地化。

## 安装与运行时边界

```text
python -m pip install veroscript
```

扩展要求原生 Interop ABI 完全匹配：当前为 `0x00000101`（0.1.1）。不匹配时，导入 `veroscript` 会抛出 `ImportError`。请成套使用 wheel 自带的原生库，不要替换为其他发行版本的 Interop 库。成功导入后，`veroscript.ABI_VERSION` 表示 ABI，`veroscript.RUNTIME_VERSION` 表示运行时版本。

Wheel 包含 Python 扩展、匹配的 Interop 库、运行时目录、类型信息和标准库文件；Python 运行时由宿主应用提供。原生扩展与 CPython 的小版本和进程架构相关，安装时必须匹配当前解释器。包中包含 `py.typed` 和 `_veroscript.pyi`，可供静态类型检查器使用。

Python 回调重新取得 GIL；回调异常会转换为 `CallbackError`，不会穿过 C ABI 展开。宿主对象最终释放时也会在持有 GIL 的条件下进行。

发行 wheel 支持启用 GIL 的标准 CPython 3.11–3.15，不支持调试版或自由线程解释器。请选择与操作系统、进程架构及 CPython 小版本匹配的 wheel。Linux wheel 的兼容性受其原生构建环境限制，不能假定其他 Linux 发行版上的 wheel 一定适用于当前系统。

Python SDK 压缩包或安装器中的原生模块只匹配该包采用的 CPython 小版本。使用其他受支持小版本时，请在对应解释器的环境中安装匹配的 wheel。

## 最小执行

```Python
from veroscript import Backend, Session

source = """
int main(string[] args)
{
	print("arguments={}", len(args));
	return 0;
}
"""

with Session(backend=Backend.MIXED, script_args=("one", "two")) as session:
	result = session.load_source(source, "main.vs")
	if not result.success:
		raise RuntimeError(result.diagnostics)
	result = session.run()
	if not result.success:
		raise RuntimeError(result.diagnostics)
```

## 页面索引

- [会话与执行](session.md)：配置、加载、调用、结果、后端、JIT、热重载和线程规则。
- [值与容器](values.md)：标量、类型化容器、通道、所有权和保留调用对象。
- [宿主绑定](host-bindings.md)：类型化函数、装饰器、Python 类、回调和变更处理。
- [包、反射与本地化](packages.md)：包、元数据、反射、序列化、引用枚举和语言目录。
- [Python 前端](frontend.md)：静态分析类型化 Python 并生成声明 AST。
- [Python BindGen](bindgen.md)：生成 Binding Catalog 和项目构件。
- [Python 转换器](transpiler.md)：将类型化 Python 转换为经验证的 VeroScript。

语言参考仍是 VeroScript 语法和语义的权威来源。本模块只说明 Python 边界，不把 Python 语法当作 VeroScript 语法。

[Android/iOS 中使用 Python](mobile.md)

## 受管理内存

宿主接口：`session.memory` / `current_memory()` / `set_memory_limits`。所有权、配额、视图失效与租借规则参阅 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)。

- [宿主中的受管理内存](memory.md)

## variant

参阅[Interop SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#exported-value-lifetime)。

```python
from veroscript import Session, Value
with Session() as session:
	payload = Value(42, kind="int32")
	box = session.variant("int", payload)
	assert box.variant_has_value()
	assert box.variant_type_name() == "int"
	extracted = box.variant_get()
	assert extracted.value == 42
```

使用 `Session.load_application(absolute_path, options=...)` 加载应用，检查结果后再调用 `run()`。宿主选项中的路径必须为绝对路径。环境、签名及信任配置遵循 [Application 加载约定](../../../../../../../VeroScriptApplication/Resources/Docs/User/zh-CN/index.md)。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
