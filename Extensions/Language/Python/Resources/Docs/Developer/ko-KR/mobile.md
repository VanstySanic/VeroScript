# Python — Mobile SDK

헤더 하위 디렉터리는 실제 디렉터리로 복사하세요. `includeDirectory` 아래의 디렉터리 심볼릭 링크와 Windows junction/reparse 디렉터리는 거부합니다. `includeDirectory` 자체는 SDK 내부의 실제 디렉터리로 해석되면 사용할 수 있습니다. `.h` 파일 링크는 대상이 SDK 안에 있을 때 허용하며 내용도 SDK 지문에 포함됩니다.

이 페이지는 `Extensions/Language/Python/Resources/Tools/Mobile`로 Python 모바일 패키지를 빌드하고 배포하는 유지보수자를 위한 문서입니다. 대상 CPython SDK를 명시적으로 제공하세요. 호스트 인터프리터나 데스크톱/macOS 바이너리는 대상 리소스를 대신할 수 없습니다. 새 디렉터리에서 빌드하고 전체 파일 목록을 검증하며 대상 플랫폼의 검증 증거를 수집한 뒤 안정 버전을 배포하세요.

Python: 같은 major/minor의 호스트 CPython, CMake 3.25+, Android NDK/Ninja 또는 macOS/전체 Xcode가 필요합니다. 대상은 GIL을 사용하는 CPython 3.13/3.14 release입니다. minor별 package를 나누고 모든 slice의 전체 버전을 맞춥니다. debug/free-threaded는 거부합니다. SDK descriptor의 상대 경로는 그 디렉터리 안에 있어야 합니다. 필드는 `schemaVersion`, `platform`, `architecture`, `variant`, `minimumVersion`, `includeDirectory`, `library`, `sysconfigData`입니다. 대상 헤더와 리터럴 `build_time_vars`를 AST로 읽고 대상 interpreter를 실행하지 않습니다. 플랫폼, CPU, 최소 OS, Python ABI, iOS install name을 검사합니다. Android는 arm64-v8a/x86_64(API 26), iOS는 기기 arm64 및 시뮬레이터 arm64/x86_64(15.0)입니다.

Python package는 확장/facade/리소스를 제공하며 앱이 CPython과 의존성을 제공합니다. Android는 추출된 JNI 경로, iOS는 서명된 동적 Python 확장 framework와 `.fwork`/`.origin`을 사용합니다. 정적 C ABI XCFramework 및 금지된 native JIT와 구별합니다. slice별 새 build nonce/identity를 만들고 `python-mobile.json`에 전체 hash, 버전, ABI, SDK 지문 및 기기 검증 대기 상태를 기록합니다.

게시 전에 전체 목록과 binary를 검사합니다. `device_smoke.py`는 desktop을 거부하며 재시도 전에 이전 proof를 무효화합니다. build ID, 정확한 Python/제품 버전, ABI, CPU, 기기/시뮬레이터가 일치해야 합니다. 모든 slice와 Android arm64의 4 KB/16 KB 환경 증거가 필요합니다. 제한 VM smoke는 import, callback, 진단, 유지 값 및 해제를 검사합니다. 서명 앱, 의존성/APK ZIP 정렬, iOS runpath/리소스/서명, 취소 및 앱 테스트는 별도로 검증합니다. 호스트 테스트로 대체할 수 없습니다.

[Python](index.md)


## 배포 문서

모바일 패키지는 기능 파일, 런타임 리소스와 라이선스를 포함합니다. README, 설명서와 예제는 제외하며 통합 User 문서는 별도 Resources 배포물에서 받습니다. 네이티브 리소스 경로와 기기 검증 요구 사항은 유지됩니다.


## 설명자와 재현

아래는 Android arm64 및 iOS 기기 설명자 예시입니다. sysconfigData 이름은 예시이므로 SDK의 실제 파일을 선택해야 하며 다른 ABI 파일의 이름을 바꾸어 사용하면 안 됩니다. Android x86_64는 `architecture`와 모든 경로를 변경합니다. iOS 시뮬레이터는 `variant: simulator`, `architecture: arm64` 또는 `x86_64`와 단일 아키텍처 바이너리를 사용합니다. 모든 슬라이스의 전체 버전을 일치시키고 `@rpath/Python.framework/Python`을 유지하세요.

저장소 루트에서 실행합니다. `--host-python`은 호스트 인터프리터, `--cmake`는 빌드 도구를 선택합니다. `--plan`은 SDK를 검증하고 빌드하지 않은 채 명령을 출력하며 Android에서는 `--ndk`도 필요합니다. 출력 디렉터리는 새로워야 하고 중간 빌드는 임시 영역에서 수행하며 매번 새 nonce를 사용합니다. 포함된 고지 외에도 CPython과 의존성 라이선스를 유지하세요.

호스트 테스트 `Extensions/Language/Python/VeroScriptPython/tests/test_mobile.py`는 AST, 호스트/대상 설정, 유니버설 시뮬레이터 및 증거를 검증합니다. 기기 스모크는 제한된 VM/Mixed(100,000개 명령, 5초, 256프레임, 콘솔 출력만 허용)를 사용하고 데스크톱 실행을 거부하며 `-O`에서도 유효합니다. 네이티브 식별자, ABI 및 버전을 확인합니다. CPython/앱 테스트와 서명된 앱 감사는 별도로 필요합니다.

```json
{
	"schemaVersion": 1,
	"platform": "android",
	"architecture": "arm64-v8a",
	"variant": "device",
	"minimumVersion": "26",
	"includeDirectory": "include/python3.14",
	"library": "lib/libpython3.14.so",
	"sysconfigData": "lib/python3.14/_sysconfigdata__android_aarch64-linux-android.py"
}
```

```json
{
	"schemaVersion": 1,
	"platform": "ios",
	"architecture": "arm64",
	"variant": "device",
	"minimumVersion": "15.0",
	"includeDirectory": "Python.framework/Headers",
	"library": "Python.framework/Python",
	"sysconfigData": "stdlib/_sysconfigdata__ios_arm64-apple-ios.py"
}
```

[Python](../../User/ko-KR/mobile.md)

## 기술 파일에서 빌드하기

도구 구현만으로 교차 컴파일이나 기기 실행이 검증되지는 않습니다. 대상 CPython SDK를 명시적으로 제공하세요. 호스트 CPython은 대상과 major/minor가 같아야 하며 대상은 GIL이 있는 release 빌드입니다. Frontend/BindGen/Transpiler/AOT는 호스트 도구입니다. 확장은 Core, C++ 지원, Interop을 정적으로 링크하며 VM/Mixed는 네이티브 JIT 없이 실행합니다.

Android에는 NDK/Ninja와 두 ABI의 SDK가 필요합니다. libpython 및 확장의 LOAD/RELRO 16 KB 정렬과 호환되는 `libc++_shared.so`를 확인하세요. iOS에는 macOS와 전체 Xcode가 필요합니다. 범용 Python.framework는 `xcrun lipo -thin` 등으로 기술 파일에 맞는 arm64/x86_64 바이너리를 만들고 `@rpath/Python.framework/Python`을 유지합니다.

저장소 루트에서 실행하고 패키지는 대상 플랫폼의 **한** 명령을 선택하세요. 꺾쇠 경로는 실제 SDK 또는 새 출력 경로로 바꿉니다. SDK 검사는 대상 Python을 실행하지 않습니다.

```text
python Extensions/Language/Python/Resources/Tools/Mobile/mobile_sdk.py --sdk <sdk.json> --check-host
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py android --sdk <android-arm64/sdk.json> --sdk <android-x86_64/sdk.json> --ndk <ndk-directory> --out-root <new-output-directory>
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py ios --sdk <ios-device/sdk.json> --sdk <ios-simulator-arm64/sdk.json> --sdk <ios-simulator-x86_64/sdk.json> --out-root <new-output-directory>
```

## Android 배포

각 `slices/<target>/jniLibs/<abi>/`를 앱의 `src/main/jniLibs/<abi>/`로 복사하고 대상 libpython과 의존성도 CPython 공급자 지침에 따라 배치합니다. 앱 전체에서 호환되는 `libc++_shared.so` 하나를 사용하며 `pickFirst`로 비호환성을 숨기지 마세요. 대응 slice의 `python/veroscript/`를 Python 리소스로 배포하고 APK assets이면 실제 디렉터리에 추출합니다. `stdlib/`, `localization/`, `py.typed`를 유지합니다.

로더에는 추출된 실제 파일 경로가 필요합니다. 앱 Gradle 모듈과 첫 import 전 인터프리터를 구성합니다.

```kotlin
android {
    defaultConfig { minSdk = 26 }
    packaging { jniLibs { useLegacyPackaging = true } }
}
```

```python
import os
import sys

os.environ["VEROSCRIPT_ANDROID_NATIVE_LIB_DIR"] = native_library_dir
sys.path.insert(0, application_python_directory)
import veroscript
```

`native_library_dir`는 설치된 앱의 절대 `context.applicationInfo.nativeLibraryDir`이며 라이브러리가 있는지 확인해야 합니다. `application_python_directory`는 `veroscript/`를 포함하는 배포 디렉터리입니다. 로더는 `libveroscript_python.so`를 `veroscript._veroscript`로 읽습니다. 빌드 PC 경로가 아니라 설치된 APK로 검증하세요.

## iOS 배포

외부 **Python.xcframework**와 생성한 **VeroScriptPython.xcframework**를 **Embed & Sign**으로 추가합니다. Xcode가 기기 또는 범용 시뮬레이터를 선택하고 앱 포함 단계에서 서명되지 않은 확장에 서명합니다. runpath에 `@executable_path/Frameworks`를 넣고 다음 배치를 유지합니다.

```text
Application.app/
  Frameworks/Python.framework/Python
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript.origin
  python/veroscript/__init__.py
  python/veroscript/_veroscript.fwork
  python/veroscript/stdlib/...
  python/veroscript/localization/...
```

CPython과 해당 표준 라이브러리는 별도 구성하고 `<app-bundle>/python`을 모듈 검색에 추가합니다. 읽기 전용 bundle의 바이트코드 쓰기를 끄세요. `AppleFrameworkLoader`는 `_veroscript.fwork`를 읽으며 두 시뮬레이터 ABI가 공통 마커와 `.origin`을 사용합니다. 원본 경로를 수정하지 않고 `app_packages`로 이동하지 마세요. 리소스에 `.so`를 복사하거나 준비된 마커를 다시 처리하지 않습니다.

## 감사와 플랫폼 증거

감사는 전체 목록, facade, 표준 라이브러리, 각 언어 카탈로그를 대응 소스 버전과 비교합니다. CPython 초기화 함수, 의존성, Android ABI/API/16 KB 정렬, iOS 플랫폼/최소 OS/framework/Info.plist/마커, XCFramework 구성을 검사합니다. 추가 네이티브 파일은 목록에 해시를 넣어도 거부합니다. 대응 checkout 또는 sdist의 `_native`에서 실행하며 패키지 해시만으로 필수 리소스를 증명하지 마세요.

먼저 감사를 실행합니다. `device_smoke.py`를 앱 테스트 하네스에 복사하고 실제 모바일 인터프리터의 작업 스레드에서 쓰기 가능한 보고서 경로로 `run`을 호출합니다. 모든 slice와 Android arm64의 두 페이지 환경 보고서를 내보내고 필요한 만큼 `--evidence`를 추가합니다.

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory>
```

```python
from device_smoke import run
run(writable_report_path)
```

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory> --require-device-tests --evidence <report-1.json> --evidence <report-2.json> --evidence <report-3.json>
```

새 빌드는 새 증거가 필요합니다. 정체성은 `veroscript.HOST_PLATFORM`, 제품 버전은 `veroscript.__version__`을 사용하며 커널 기기명이나 바이트코드 정보가 포함된 `RUNTIME_VERSION`을 사용하지 않습니다. `python-mobile.json`의 `pending`은 검증 완료가 아닙니다. Android는 모든 네이티브 의존성과 `zipalign -v -c -P 16 4 <application.apk>`, AAB 경로의 서명 앱을 검사합니다. iOS는 서명, 리소스, runpath, 기기와 두 시뮬레이터 ABI의 import를 확인합니다. CPython/의존성 라이선스를 유지하고 CPython/앱 테스트와 스토어 정책 확인은 별도로 수행하세요.

## 빌드 식별 정보와 Android 라이브러리

제품 버전은 `VeroScriptVersion.h`에서 가져오며 CPython 버전이나 여러 정보를 합친 런타임 문자열로 대체하지 않습니다. Android에서는 `ApplicationInfo.nativeLibraryDir`에 라이브러리가 추출되었는지 확인한 뒤 첫 `import veroscript` 전에 그 절대 경로를 `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR`로 전달합니다.

<a id="android-build-contract"></a>

Android 빌드는 `VEROSCRIPT_BUILD_APPLICATION=ON`을 명시하며 `python/veroscript/` 안의 `licenses/application/miniz/LICENSE`, `licenses/application/json/LICENSE.MIT`, `licenses/application/sha256/LICENSE`을 보존합니다. 도구 체인 검증은 NDK의 해석된 `ANDROID_ABI` / `ANDROID_PLATFORM_LEVEL`이 있으면 사용하고 없으면 CMake Android/system 필드로 비교합니다. 구형 NDK의 `CMAKE_SYSTEM_VERSION=1`은 대상 API가 아닙니다. 16 KB 검사는 LOAD 정렬과 페이지 단위 RELRO 보호가 RELRO 밖 쓰기 가능 데이터와 겹치지 않음을 확인하며 모든 RELRO 끝의 16 KB 정렬을 요구하지 않습니다. 지원 NDK 공유 STL에는 정렬되지 않은 RELRO 끝이 있을 수 있습니다.

<a id="compiler-nesting-acceptance"></a>

## 네이티브 컴파일 스택

호스트는 네이티브 스택이 최소 8 MiB인 작업 스레드에서 컴파일해야 합니다. Android JNI 및 Apple dispatch의 작은 스택은 파서의 중첩 한도 진단 전에 넘칠 수 있습니다. Session 생성, 사용, 파괴는 같은 작업 스레드에서 수행하고 기존 Session이나 콜백을 암묵적으로 옮기지 마세요. 앱의 Python 스레드를 만들기 전에 스택을 설정하거나 네이티브 호스트에서 제공합니다. device_smoke.run도 해당 작업 스레드에서 실행합니다. 기존 4개 그룹은 깊이 200 실행, 깊이 512 한도 진단 및 재사용을 검사합니다. 새 증거에는 compilerNestingChecked=true가 필요하며 이전 증거는 허용되지 않습니다.

`compilerNestingChecked`는 증거 메타데이터이며 공개 Session API가 아닙니다. `verify_mobile.py::verify_package`는 schemaVersion 1 및 일치하는 build/runtime/ABI/slice/산출물 식별과 함께 정확한 불리언 true를 요구하며 run은 exercise_runtime 성공 후에만 이를 기록합니다. Python Session/C ABI 컴파일은 Mobile::FRunner 스택 어댑터를 사용하지 않습니다. 아래 예제에 앱 소유의 쓰기 가능한 디렉터리 안의 절대 report_file을 전달하고 이 블로킹 감독 함수는 UI 스레드 밖에서 호출하세요. 모의 회귀는 로직만 검사하며 대상 바이너리를 검증하지 않습니다.

```python
import threading
from concurrent.futures import ThreadPoolExecutor
import device_smoke

def validate_on_worker(report_file):
	threading.stack_size(8 * 1024 * 1024)
	with ThreadPoolExecutor(max_workers=1) as worker:
		return worker.submit(device_smoke.run, report_file).result()
```

<!-- docs-navigation -->

---

[개발자 문서](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
