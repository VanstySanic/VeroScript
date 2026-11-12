# Android/iOS의 Python

대상 CPython 마이너 버전과 일치하는 임베딩 SDK를 사용하세요. Android는 GIL을 사용하는 CPython 3.13/3.14 release, arm64-v8a/x86_64 및 API 26+를 지원합니다. iOS는 같은 Python 버전, iOS 15+, 기기 arm64와 시뮬레이터 arm64/x86_64입니다. 데스크톱 wheel로 대체할 수 없습니다. 앱이 CPython과 의존 라이브러리를 제공합니다. 실험적 통합이므로 배포 전에 시뮬레이터/에뮬레이터와 기기에서 검증하세요.

Android에는 일치하는 slice의 `jniLibs/<abi>/`와 대상 CPython 라이브러리를 배포합니다. 네이티브 의존성 사이에서 하나의 호환 `libc++_shared.so`를 사용하세요. `stdlib/`, `localization/`, `py.typed`를 포함한 `python/veroscript/`를 앱 소유 디렉터리에 풀어 둡니다. 첫 import 전에 `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR`를 설치된 앱의 절대 `ApplicationInfo.nativeLibraryDir`로 설정하고 `veroscript/`의 상위 디렉터리를 `sys.path`에 추가합니다. JNI 라이브러리는 추출해야 합니다(`useLegacyPackaging = true`). APK asset 이름은 실제 라이브러리 경로가 아닙니다.

iOS에서는 일치하는 `Python.xcframework`와 `VeroScriptPython.xcframework`를 포함하고 서명합니다. 번들의 `python/veroscript/`, `_veroscript.fwork`와 framework의 `.origin`을 유지하세요. `<app-bundle>/python`을 `sys.path`에 추가하고 읽기 전용 번들에 bytecode를 쓰지 않도록 설정합니다. 대상 CPython의 Apple framework loader가 준비된 확장을 읽습니다. 데스크톱 `.so`로 대체하거나 origin을 변경하지 않고 리소스를 이동하지 마세요.

facade를 import하면 함께 제공된 카탈로그를 읽습니다. 이후 `set_language(tag)`를 호출하세요. 언어 선택은 프로세스 전체에 적용됩니다. 표준 라이브러리는 facade 옆에 두고 캐시는 실제 앱 소유 쓰기 가능 디렉터리에 둡니다. Session 생성, 설정, 실행과 종료는 같은 작업자 스레드에서 수행하고 결과를 UI에 전달하세요. 호스트 callback은 자체 권한과 취소를 처리해야 합니다. 네이티브 JIT는 제공되지 않습니다. 실행 제한을 적용하려면 VM을 지정하세요.

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

소스 컴파일은 네이티브 스택이 8 MiB 이상인 작업 스레드에서 실행하고 같은 스레드에서 Session을 생성, 사용, 닫아야 합니다. 애플리케이션 Python 스레드를 시작하기 전에 작업 스택을 설정하세요. 바인딩은 기존 Session이나 콜백을 다른 스레드로 이동하지 않습니다.

<a id="mobile-compiler-thread"></a>

이 모바일 임베딩 프로필은 컴파일러에 최소 8 MiB의 네이티브 스택을 제공합니다. `threading.stack_size`는 앱 Python 작업자를 만들기 전에 설정하며 새 Python 스레드에만 적용됩니다. 기존 네이티브/JNI/dispatch 스레드에는 적용되지 않으므로 호스트가 작업자를 직접 제공하면 그곳에서 스택을 설정합니다. Session 생성·사용·종료는 같은 작업자에서 수행하며 기존 Session을 자동 이동하지 않습니다. 예제는 결과를 기다리므로 UI 스레드 밖에서 실행하세요. 출력은 `["42"]`입니다. 네이티브 스택은 바이트 버퍼 및 VM 실행 할당량과 독립적입니다.

<!-- docs-navigation -->

---

[사용자 문서](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
