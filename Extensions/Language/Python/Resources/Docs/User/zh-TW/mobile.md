# Android/iOS 中使用 Python

使用與目標 CPython 次版本相符的行動嵌入 SDK。Android 支援帶 GIL 的 CPython 3.13/3.14 release、arm64-v8a/x86_64、API 26+；iOS 支援相同 Python 版本、iOS 15+、arm64 真機及 arm64/x86_64 模擬器。桌面 wheel 不能替代行動 SDK。應用程式負責提供 CPython 及相依程式庫。這些接入仍處於實驗階段，投入應用前須在模擬器／模擬環境及真機驗證。

Android 部署對應 slice 的 `jniLibs/<abi>/` 和目標 CPython 程式庫，各原生相依項目使用一個相容的 `libc++_shared.so`。將 `python/veroscript/` 連同 `stdlib/`、`localization/`、`py.typed` 解包至應用程式自有目錄。首次 import 前，將 `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR` 設為已安裝應用程式的絕對 `ApplicationInfo.nativeLibraryDir`，並將包含 `veroscript/` 的目錄加入 `sys.path`。應用程式必須設定 JNI 程式庫提取（`useLegacyPackaging = true`）；APK asset 名稱不是原生程式庫的檔案路徑。

iOS 將相符的 `Python.xcframework` 與 `VeroScriptPython.xcframework` 嵌入並簽署。保留 Bundle 中的 `python/veroscript/`、`_veroscript.fwork` 和 framework 的 `.origin` 標記。將 `<app-bundle>/python` 加入 `sys.path`，禁止向唯讀 Bundle 寫入 bytecode。目標 CPython 的 Apple framework loader 負責載入已準備的擴充功能；不能換成桌面 `.so`，移動資源時必須同步 origin。

import facade 時會載入套件內目錄，隨後呼叫 `set_language(tag)`；語言選擇為程序級。標準函式庫保留在 facade 旁，可選位元碼快取放在應用程式自有、真實可寫目錄。同一工作執行緒建立、設定、執行並關閉 Session，結束後再把結果交給 UI。宿主回呼仍須自行執行權限和取消策略。行動 SDK 不包含原生 JIT；有執行治理需求時明確使用 VM。

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

原始碼編譯須在原生堆疊至少為 8 MiB 的工作執行緒執行；在相同執行緒建立、使用並關閉 Session。應在啟動應用程式 Python 執行緒前設定工作執行緒堆疊。繫結保留執行緒擁有權，不會跨執行緒搬移已建立的 Session 或回呼。

<a id="mobile-compiler-thread"></a>

此行動嵌入設定須提供至少 8 MiB 原生編譯堆疊。在建立應用程式 Python 工作執行緒前設定 `threading.stack_size`；它只影響新建 Python 執行緒，不改變既有原生／JNI／dispatch 執行緒。宿主直接提供工作執行緒時，應在該處設定原生堆疊。每個 Session 在同一工作執行緒建立、使用和關閉，既有 Session 不會自動遷移。範例會等待結果，須在非 UI 執行緒執行；輸出為 `["42"]`。原生堆疊大小與位元組緩衝區、VM 執行配額分別設定。

<!-- docs-navigation -->

---

[使用者文件](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](mobile.md)
