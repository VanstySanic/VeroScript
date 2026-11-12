# Android/iOS 中使用 Python

使用与目标 CPython 次版本匹配的移动嵌入 SDK。Android 支持带 GIL 的 CPython 3.13/3.14 release、arm64-v8a/x86_64、API 26+；iOS 支持相同 Python 版本、iOS 15+、arm64 真机及 arm64/x86_64 模拟器。桌面 wheel 不能替代移动 SDK。应用负责提供 CPython 及其依赖。这些接入仍处于实验阶段，投入应用前须在模拟器／模拟环境及真机验证。

Android 部署对应 slice 的 `jniLibs/<abi>/` 和目标 CPython 库，各原生依赖使用一个兼容的 `libc++_shared.so`。把 `python/veroscript/` 连同 `stdlib/`、`localization/`、`py.typed` 解包到应用自有目录。首次 import 前，将 `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR` 设为已安装应用的绝对 `ApplicationInfo.nativeLibraryDir`，并把包含 `veroscript/` 的目录加入 `sys.path`。应用必须配置 JNI 库提取（`useLegacyPackaging = true`）；APK asset 名称不是原生库的文件路径。

iOS 将匹配的 `Python.xcframework` 与 `VeroScriptPython.xcframework` 嵌入并签名。保留 Bundle 中的 `python/veroscript/`、`_veroscript.fwork` 和 framework 的 `.origin` 标记。把 `<app-bundle>/python` 加入 `sys.path`，禁止向只读 Bundle 写入 bytecode。目标 CPython 的 Apple framework loader 负责加载已准备的扩展；不能换成桌面 `.so`，移动资源时必须同步 origin。

import facade 时会加载包内目录，随后调用 `set_language(tag)`；语言选择为进程级。标准库保留在 facade 旁，可选字节码缓存放在应用自有、真实可写目录。同一工作线程创建、配置、运行并关闭 Session，执行结束后再把结果交给 UI。宿主回调仍须自行执行权限和取消策略。移动 SDK 不包含原生 JIT；有执行治理需求时显式使用 VM。

```python
import threading
from concurrent.futures import ThreadPoolExecutor
from veroscript import Backend, ScriptCapabilities, Session, set_language

threading.stack_size(8 * 1024 * 1024)

def run_script():
	set_language("zh-CN")
	output = []
	with Session(backend=Backend.VM, print_callback=output.append,
		max_instructions=100_000, timeout_milliseconds=5_000,
		max_call_depth=256, enable_execution_control=True,
		capability_policy_enabled=True,
		allowed_script_capabilities=ScriptCapabilities.CONSOLE_OUTPUT) as session:
		session.set_memory_limits(16 * 1024 * 1024, 32 * 1024 * 1024)
		loaded = session.load_source('import "core.vs"; void main() { print(42); }')
		if not loaded.success:
			raise RuntimeError(loaded.diagnostics)
		result = session.run()
		if not result.success:
			raise RuntimeError(result.diagnostics)
	return output

with ThreadPoolExecutor(max_workers=1) as worker:
	output = worker.submit(run_script).result()
```

[Session](session.md) · [Localization](packages.md)

源代码编译须在原生栈至少为 8 MiB 的工作线程执行；在同一线程创建、使用并关闭 Session。应在启动应用 Python 线程前配置工作线程栈。绑定保留线程所有权，不会跨线程迁移已创建的 Session 或回调。

<a id="mobile-compiler-thread"></a>

此移动嵌入配置须提供至少 8 MiB 原生编译栈。在创建应用 Python 工作线程前设置 `threading.stack_size`；它只影响新建 Python 线程，不改变既有原生／JNI／dispatch 线程。宿主直接提供工作线程时，应在那里配置原生栈。每个 Session 在同一工作线程创建、使用和关闭，既有 Session 不会自动迁移。示例会等待结果，须在非 UI 线程运行；输出为 `["42"]`。原生栈大小与字节缓冲区、VM 执行配额分别配置。

<!-- docs-navigation -->

---

[用户文档](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](mobile.md) · [zh-TW](../zh-TW/mobile.md)
