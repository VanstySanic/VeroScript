# Python on Android and iOS

Use a mobile embedding SDK matching the target CPython minor version. Android supports CPython 3.13/3.14 release builds with GIL, arm64-v8a/x86_64 and API 26+. iOS supports the same Python versions, iOS 15+, arm64 devices and arm64/x86_64 simulators. Desktop wheels cannot replace this SDK. CPython and its dependencies are supplied by the embedding application. These integrations remain experimental; verify your application on simulator/emulator and device before deployment.

On Android, deploy the matching slice’s `jniLibs/<abi>/` and the target CPython libraries. Use one compatible `libc++_shared.so` across native dependencies. Preserve `python/veroscript/`, including `stdlib/`, `localization/` and `py.typed`, in an extracted app-owned directory. Before the first import, set `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR` to the installed app’s absolute `ApplicationInfo.nativeLibraryDir`, and add the directory containing `veroscript/` to `sys.path`. The app must extract its JNI libraries (`useLegacyPackaging = true`); an APK asset name is not a native library path.

On iOS, embed and sign the matching `Python.xcframework` and `VeroScriptPython.xcframework`. Preserve `python/veroscript/` in the bundle, `_veroscript.fwork`, and the framework’s `.origin` marker. Add `<app-bundle>/python` to `sys.path` and disable bytecode writes to the read-only bundle. The target interpreter’s Apple framework loader resolves the prepared extension; do not substitute a desktop `.so` or move the resources without adjusting the origin.

Importing the facade loads its packaged catalogs; call `set_language(tag)` afterwards. Language selection is process-wide. Keep the packaged standard library beside the facade, and put optional bytecode caches in a real app-owned writable directory. Create, configure, execute and close each Session on one worker thread. Return results to the UI after execution; host callbacks must enforce their own permissions and cancellation. Mobile SDKs bundle no native JIT; request VM for predictable governed execution.

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

<a id="mobile-compiler-thread"></a>

For this mobile embedding profile, provide at least 8 MiB of native compiler stack. Configure `threading.stack_size` before creating application Python workers; it affects new Python threads, not an existing native/JNI/dispatch thread. If the host supplies the worker directly, configure its native stack there. Create, use and close each Session on the same worker; existing sessions are not automatically migrated. The example waits for its result and must run outside the UI thread. Its output is `["42"]`. Native stack size is independent of byte-buffer and VM execution quotas.

<!-- docs-navigation -->

---

[User documentation](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
