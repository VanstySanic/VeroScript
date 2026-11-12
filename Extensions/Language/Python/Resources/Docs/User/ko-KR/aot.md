# Python AOT

`veroscript aot --target=python`은 검사된 VeroScript 프로그램을 읽을 수 있는
Python 소스로 변환합니다. 생성된 소스에는 작은 호환 런타임이 포함되며
VeroScript VM, `VeroScriptInterop`, `veroscript` 확장을 실행 시 로드하지 않습니다.

## 실행 경계

생성물은 사용자가 제공하는 CPython 3.11–3.15에서 실행됩니다. Python 실행기,
Python SDK, 호스트 바인딩 및 네이티브 VeroScript 라이브러리는 포함되지 않습니다.
기존 Python 애플리케이션에 런타임을 임베드하려면 [Python SDK](index.md)를
사용하십시오.

생성 worker는 OS 스레드를 사용합니다. 스크립트 전역과 static 필드는 worker별
스냅샷에서 초기화되고, worker 사이의 명시적 동기화 수단은 channel입니다. task
ID는 해당 worker에만 속하므로 다른 worker에서 join하거나 cancel할 수 없습니다.

## 단일 파일

```powershell
veroscript aot --target=python app.vs -o app.py
python app.py
```

애플리케이션 import 경로가 entry 파일과 다르면 `-I`/`--import-path`를 반복해서
지정할 수 있습니다. 변환이 성공한 뒤에만 출력 파일이 교체됩니다.

## 소스 bundle

```powershell
veroscript aot --target=python app.vs --output-dir app_python --bundle-name app
python app_python
python -m app_python
```

bundle에는 `__main__.py`, `__init__.py`, `_veroscript_runtime.py`,
`_veroscript_program.py`, `python-aot.manifest.json`이 포함됩니다. 전체 import
graph를 먼저 검사하며 외부 파일이 섞인 디렉터리는 교체하지 않습니다.

파일을 임시 디렉터리에 준비한 뒤 일반 디렉터리를 교체합니다. 최종 경로의 심볼릭 링크와
Windows junction은 거부합니다. 교체 중 기존 디렉터리를 옮기므로 출력 경로가 잠시
없을 수 있습니다. 교체에 실패하면 기존 디렉터리 복원을 시도합니다.

## 지원 범위와 제한

scalar, string, container, function, namespace, import, enum, class, struct,
interface, 변환, 예외, `switch`, `algorithm`/`numeric`/`random` 및 표준 concurrent
task wrapper를 지원합니다. `ref`/`out`, 사용자 open generic, overload 집합,
destructor, 여러 concrete base, class default, 중복 private type 이름,
`goto`/label, `switch` fallthrough, host binding, source lambda 및 지역 `static` 선언은 거부됩니다.
지원하지 않는 구문은 진단을 반환하고 부분 결과를 남기지 않습니다.

함수 값은 명시적인 `fn(...) -> T` 타입으로 선언해야 하며 `auto` 추론은 지원하지
않습니다. namespace로 한정한 함수 값은 지원합니다. `Super` receiver와 가변 인자
함수 값은 거부하지만 가변 인자 함수의 직접 호출은 계속 지원합니다.

`shared<T>` / `handle<T>` wrapper, 호출 가능한 객체를 사용하는 task 진입점,
실행형 reflection도 거부됩니다. 이러한 런타임 기능은 Python SDK에서 사용할 수
있습니다.

종료 시 기존 채널을 닫고 같은 대기 기한 내에 worker와 그 worker가 파생한 모든 세대의 task가 끝날 때까지 기다립니다. 종료 중 생성한 채널은 닫힌 상태로 시작합니다.

생성 프로그램은 `VERO_JOIN_TIMEOUT_MS`로 worker 종료 대기 시간을 제한하며 기본값은
30000밀리초입니다. worker가 `exit(n)`을 호출하면 해당 task만 실패하고 부모 프로세스는
종료되지 않습니다.

독립적으로 검사할 수 있는 Python 소스 산출물이 목표이면 Python AOT를 사용하고, VM/JIT,
동적 로드, 호스트 바인딩, 리플렉션, 핫 리로드, 보존 callable 또는 런타임 패키지 작업이
필요하면 [Python SDK](index.md)를 사용하십시오. Python 프런트엔드, BindGen과 Transpiler는
별도의 소스 도구 체인입니다.

## 런타임 메모리 지원

독립 AOT는 현재 `std::memory`의 조회, 버퍼, 뷰 및 관련 타입을 지원하지 않습니다. 실제 호출이나 해당 타입 사용 시 명확한 생성 오류를 반환합니다. 사용하지 않는 `memory.vs` 또는 `core.vs`의 import는 계속 지원합니다.

[Runtime SDK](index.md)

독립 Python AOT는 `std::console`의 텍스트 출력, 터미널 기능과 크기 조회, 색상, 커서, 줄 제어를 지원합니다. 리디렉션된 출력은 터미널 기능 없음, 크기 0, 제어 실패를 반환하며 잘못된 이름, 모드, 좌표는 계속 오류로 처리합니다. 알 수 없는 콘솔 내장 함수는 소스 진단과 함께 거부합니다.

Windows의 색상 설정, 스타일 초기화, 화면·줄 지우기와 커서 제어는 ANSI 모드를 켜지 않고 네이티브 콘솔 API를 사용합니다. 크기와 좌표는 표시되는 창을 기준으로 하며, 화면을 지워도 창 밖의 이전 출력은 보존됩니다.

## 속성 소스 내보내기

속성 설정 비교는 읽기 전용 builtin 예외입니다. `has_same_configuration(ref other)`는 쓰기 가능한 별칭을 만들지 않습니다. `try_get()`은 지원하고 `try_get(ref output)`은 여전히 지원하지 않습니다. 연산별 경계는 attribute 참조를 확인하세요.

[std::attribute](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/attribute.md)

<a id="native-bundle-api"></a>

네이티브 SDK는 `VeroScriptPythonAot.h`를 사용합니다. `FPythonAotSourceBundle`은 `Files`와 `ManifestJson`을 가지며 `EmitPythonSourceBundleFromSource` / `EmitPythonSourceBundleFromFile` 후 `WritePythonSourceBundle`로 스테이징한 디렉터리를 교체합니다. `BuildPythonSourceBundle`은 이미 생성한 Python을 패키징합니다. Python 제어 wrapper의 `veroscript_executable`은 CLI 실행 파일 또는 SDK 루트/bin을 고르며 생략하면 `VEROSCRIPT_EXECUTABLE`, `VEROSCRIPT_SDK`, PATH 순으로 검색합니다. 외부 CLI wrapper와 생성된 독립 프로그램은 별개입니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
