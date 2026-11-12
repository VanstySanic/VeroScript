# C++ AOT

AOT 백엔드는 VeroScript를 네이티브 빌드용 C++17 소스로 변환합니다. 실행 중
동적 스크립트 로드가 필요하지 않은 배포에 적합합니다.

## 명령줄 생성

```powershell
veroscript aot script.vs -o generated.cpp
```

출력은 링크된 실행 파일이 아니라 C++ 번역 단위이며 필요한 호환 지원 코드가
포함됩니다. 호환되는 C++17 도구 체인과 필요한 시스템 라이브러리로 컴파일하고
링크하세요. 호스트 바인딩에는 대응하는 호스트 구현도 필요합니다.

## 프로그래밍 API

`VeroScriptCppAot`는 `EmitCppFromSource`, `EmitCppFromFile`, 컴파일된 `FProgram`을
위한 `EmitCppFromProgram`, `FCppAotSourceBundle`을 위한
`EmitCppSourceBundleFromProgram`, 생성된 소스에서 bundle을 만드는
`BuildCppSourceBundle`을 제공합니다. `FCppCodegenBackend`의 이름은 `cpp`, 확장자는
`.cpp`입니다. 파일 출력은 트랜잭션이며 입력과 출력 경로는 같을 수 없습니다.

bundle 이름에는 ASCII 영문자, 숫자, `_`, `-`를 사용할 수 있습니다. 생성 파일의 이식성을 위해 Windows 장치 이름(`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`)은 모든 플랫폼에서 대소문자 구분 없이 거부합니다. `CON_model`, `COM10`은 계속 허용됩니다.

환경에 바인딩 카탈로그가 없어도 `FCodegenOptions::Bindings`로 호스트 바인딩을 명시할 수 있습니다. 준비와 검증은 같은 카탈로그를 사용하며, 생성된 자유 함수 선언에 대응하는 구현은 대상 프로젝트에서 제공해야 합니다. 런타임 네이티브 패키지 계약이 필요한 프로그램은 독립 AOT에서 계속 거부됩니다.

## bundle과 범위

`FCppAotSourceBundle`은 `Files`와 `ManifestJson`을 포함합니다. 진입 `.cpp`는
runtime/program `.generated.inl` fragment를 하나의 번역 단위로 재구성하고, manifest는
필요한 C++ 표준과 thread library를 선언합니다. fragment를 별도 번역 단위로 링크하지
마십시오.

스크립트 전역과 클래스 정적 필드는 네이티브 작업자별 격리를 유지합니다. AOT 전역
스토리지는 함수 로컬 `thread_local` 상태를 사용하는 접근자 뒤에 생성됩니다. 작업자가
처음 접근하면 스크립트 선언 순서로 초기화되며, 생성된 진입점은 `main`을 호출하기 전에
이 상태를 설정합니다. 작업자 사이에서 값을 공유해야 할 때는 채널이나 동기화된 호스트
상태를 명시적으로 사용하십시오.

멤버 함수의 lambda가 수신자를 사용하면 생성된 C++은 언어 의미를 보존합니다.
클래스 수신자는 공유 객체로 유지되어 이후 필드 변경을 관찰하고, 구조체 수신자는
값 스냅샷으로 캡처됩니다. 중첩 lambda도 이 수신자를 유지합니다. 캡처 없는 lambda는
`this`를 사용할 수 없으며, 소멸자에서 클래스 수신자를 캡처하는 lambda는 수신자가
이미 소멸 중이므로 거부됩니다.

분석을 통해 클래스 로컬이 어휘적 범위를 벗어나지 않는다고 증명할 수 있으면
emitter가 자동 저장소를 사용할 수 있습니다. 반환되는 객체, 멤버에 대한 참조 또는
이스케이프하는 클로저는 공유 소유권을 유지합니다. 이는 생성된 저장소의 최적화이며
클래스 정체성, 소멸자 동작 또는 언어의 수명 규칙을 변경하지 않습니다.

클래스, 구조체, interface, 상속, virtual dispatch, 제네릭, 제약, 컨테이너, 오버로드,
기본/가변 인자, 참조, lambda, enum, 연산자, `nullptr`, `switch`, `foreach`, label,
jump 및 문서화된 문자열·수학·파일·경로 라이브러리를 지원합니다.

숫자 스칼라의 `++`와 `--`는 대상을 한 번만 평가하고 연산 중 임시 소유자를 유지하며 VM의 타입 승격, 범위를 검사하는 축소 변환 및 오버플로 동작을 보존합니다. 사용자 타입의 `++`와 `--` 오버로드는 현재 지원하지 않습니다. 해당 타입에는 명시적인 갱신 메서드를 사용하세요.

open generic 형식의 constructor field initializer list에서 C++ 대상은 `T`에서 `T`로의
직접 초기화만 지원합니다. 제약 없는 형식 매개 변수를 인자 없이 또는 여러 인자로 생성하는
형식은 AOT 생성 중 거부됩니다. 구체적인 필드 형식을 사용하거나 미리 생성한 `T` 값을
전달하십시오.

동적 로드, 디버깅,
hot reload에는 VM을 사용하고, AOT 콜백 수명과 동기화는 대상 프로젝트에서 검증하십시오.

## 현재 구문 지원 범위

C++ AOT는 signed 8-bit `char` literal, 같은 type의 local declaration group,
`do`/`while`, classic `for`의 initializer 또는 step-expression list도 emit합니다.
lexical shadowing을 보존하기 위해 각 declaration initializer는 새 binding이 보이기 전에
직전 VeroScript environment에서 평가됩니다. 이 source rule을 유지하려고 generated C++가
hygiene temporary 또는 서로 다른 local name을 사용할 수 있지만, 이런 implementation name은
AOT ABI의 일부도 source-level behavior 변경도 아닙니다.

## 런타임 메모리 지원

독립 AOT는 현재 `std::memory`의 조회, 버퍼, 뷰 및 관련 타입을 지원하지 않습니다. 실제 호출이나 해당 타입 사용 시 명확한 생성 오류를 반환합니다. 사용하지 않는 `memory.vs` 또는 `core.vs`의 import는 계속 지원합니다.

[Runtime SDK](index.md)

## 참조 반환과 평가 수명

<a id="reference-return-lifetime"></a>

참조를 반환하는 호출을 값으로 읽을 때 AOT는 저장한 수신자를 해제하기 전에 값을 복사하며 인수가 없는 호출도 같은 규칙을 따릅니다. 참조 바인딩은 값 변환 및 제네릭 추론과 분리됩니다. 숫자 참조에는 일반 값과 같은 타입 승격, 축소 변환 검사 및 오버플로 검사를 적용합니다. 참조를 반환하는 메서드를 통한 대입은 수신자와 인수를 평가하고 메서드를 한 번 호출한 뒤 오른쪽을 평가합니다.

클래스 필드를 직접 접근하거나 자신의 정적 필드나 참조 필드가 아닌 필드를 직접 반환하는 해석된 재정의 불가 메서드를 호출할 때 지역 별칭이 수신자의 저장소를 유지할 수 있습니다. 수신자, 기반 타입, 필드 및 알려진 파생 타입은 스크립트 정리가 필요하지 않고 불투명한 리소스를 소유하지 않아야 합니다. 원래 수신자를 다시 바인딩해도 이 별칭은 유효합니다. 일반 전역 저장소와 직접 구조체 수신자에 대한 참조도 계속 지원합니다.

VM에서 필드 저장소를 유지해도 스크립트 소멸자 실행은 지연되지 않습니다. 따라서 관리되는 정리가 필요한 위 클래스 참조 경로는 AOT가 명시적으로 거부합니다. 지역적으로 유지한 별칭의 반환, 출처를 증명할 수 없는 참조 또는 유지한 callable 저장소 밖으로 나가는 참조에도 VM 백엔드가 필요하며 생성기는 지원하지 않는 경우를 진단합니다.

지속되는 인덱스 별칭과 인덱스 참조 반환에는 관리되는 저장소 경로가 필요합니다. 임시 값 타입 수신자에서 얻은 참조를 지속해서 유지하려면 관리되는 소유자도 필요합니다. AOT는 이 경우를 명시적으로 거부하므로 VM 백엔드를 사용하세요.

암시적 구조체 필드, 제네릭 필드 및 기본 초기화되는 고정 배열 요소는 일반 기본 생성자 선택 규칙에 따라 선택된 생성자 본문을 실행합니다. 명시적 필드 초기화자는 먼저 기본 생성자를 실행하지 않고 필드 값을 직접 생성합니다.

## 컴파일 처리와 소스 메타데이터

`FCodegenOptions::CompileProcessing`으로 소스 및 선언 처리기를 설정합니다. 준비 단계는 처리기 인스턴스를 모든 단계에서 유지하며 준비된 프로그램을 출력할 때 다시 실행하지 않습니다. 컴파일 시 소스 메타데이터와 생성 선언은 C++ AOT에서 사용할 수 있습니다. 실행 중 소스 메타데이터 조회(주석, 소스 주석, 선언/소스 노드와 주석 정의)는 VM이 필요하며 독립 C++ 출력은 명시적으로 오류를 보고합니다.

## 스크립트 확장과 소스 오버레이

`FCppCodegenBackend`는 `FCodegenOptions::ScriptCompileExtensions`, `SourceOverlays`, `StdlibSearchPaths`를 Core 로더로 전달합니다. `EmitFromFile`은 Core/Session과 같은 진입 오버레이 해석을 사용하며 메모리에만 있는 진입 소스도 지원합니다. 빈 경로는 거부합니다. `EmitFromSource`는 명시한 소스 인자를 진입으로 사용하고 종속 모듈에 오버레이를 적용합니다.

컴파일 스크립트는 생성 중에만 실행합니다. 출력된 업무 C++는 해당 VM을 보존하지 않으며 런타임 컴파일 전용 바인딩도 필요하지 않습니다. 업무 호스트 바인딩에는 대응하는 대상 구현이 필요합니다. 컴파일 바인딩, 제어, 제한 및 종속성 설정은 [호스트 통합](embedding.md)을 참조하세요. 런타임 소스 메타데이터 조회에는 여전히 VM이 필요합니다.

## 사용자 pair 템플릿

사용자 정의 `pair<T, U>`는 일반 제네릭 필드와 메서드를 사용합니다. 이 두 매개변수 템플릿이 소프트 내장 타입을 대체하면 독립 AOT는 맵의 `entries()`와 키·값 쌍 순회를 명시적으로 거부합니다. 이러한 작업에는 VM 백엔드를 사용하세요. 맵의 값 자체가 사용자 pair 타입인 경우 일반 값 순회는 계속 지원됩니다.

<a id="cleanup-container-operations"></a>

구조체 `T`가 필드나 기반 타입을 통해서라도 스크립트 정리를 필요로 하면 독립 C++ AOT는 `array<T>.remove` / `remove_at` 및 `map<K,T>` / `ordered_map<K,T>`의 인덱스 대입을 명시적으로 거부합니다. 이 제한은 해당 연산에 적용되며 VM, Mixed, JIT 모드는 이를 완전히 지원합니다.

독립 C++ AOT는 생성이 성공하는 클래스의 `this`를 유지하거나 외부로 공개하는 동작을 지원합니다. 생성이 실패했을 때 다른 참조가 여전히 해당 객체를 유지하면, 생성된 프로그램은 하위 객체 정리가 지연된 상태로 스크립트 catch에서 실행을 재개하지 않고 치명적 오류를 보고합니다. 이 경우 관리형 복구가 필요하면 VM 또는 Core AOT 백엔드를 사용하세요. 스택 되감기 중 해제된 임시 참조에는 이 제한이 적용되지 않습니다.

<a id="standalone-file-io"></a>

지원하는 독립 C++ 파일 연산은 UTF-8 경로를 받고 내장 NUL을 거부합니다. Windows의 긴 절대 경로(완전한 UNC 포함)는 네이티브 긴 경로 처리를 사용합니다. 상대 경로는 상대 상태를 유지하며 확장 경로 접두사를 자동으로 붙이지 않습니다. 파일시스템·권한·OS 제한은 계속 적용됩니다. 어휘적 경로 조회 규칙은 별도이며 [기초 참조](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/foundation.md)를 따릅니다.

컨테이너 원소를 참조 인수로 전달할 때도 Core가 원소의 식별을 관리해야 합니다. 독립 C++ 소스 AOT는 교체된 컨테이너로 참조가 바뀌거나 뒤의 인수가 실행된 후에야 생긴 원소에 연결되는 일을 막기 위해 이러한 호출을 명시적으로 거부합니다. 이 호출에는 Core AOT, VM, Mixed 또는 JIT를 사용하세요. 일반 인덱스 읽기와 대입은 계속 지원됩니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
