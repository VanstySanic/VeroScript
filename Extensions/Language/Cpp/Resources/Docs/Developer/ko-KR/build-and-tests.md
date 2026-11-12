# C++ build와 test 계약

## 대상

C++ group은 `VeroScriptCpp`, `VeroScriptCppAot`, `VeroScriptCppFrontend`, `VeroScriptCppBindGen`, `VeroScriptCppTranspiler` module target으로 build합니다. 각 library target은 focused test source를 제외하며 test는 검증하는 public target을 link합니다.

## test 범주

test는 public-header compilation, engine/session integration, JIT integration, AOT source/bundle output, frontend parse/merge/type mapping, BindGen catalog/reference generation, Transpiler source/map generation을 다룹니다. public result, generated artifact, diagnostic, supported-subset rule이 바뀌면 하나의 end-to-end test에 의존하지 말고 대응하는 범주를 확장해야 합니다.

## 설치 경계

public header와 generated library는 install input입니다. README와 generated API reference는 documentation artifact이지 implementation header가 아닙니다. installed consumer는 build-tree path를 요구해서는 안 됩니다.

독립 배포 C++ package는 필수 `Runtime` module 아래에 `VeroScriptCppConfig.cmake`를 제공합니다. consumer는 matching Core SDK root와 C++ package의 `Runtime` directory를 모두 `CMAKE_PREFIX_PATH`에 추가하고 다음을 호출한 뒤 `VeroScript::VeroScriptCpp`를 link합니다.

`find_package(VeroScriptCpp <version> EXACT CONFIG
REQUIRED)`

optional `Tools`와 `Aot` component는 sibling module이 설치된 경우에만 imported target을 노출합니다.

C++ package는 같은 release build에서 만든 Core library의 SHA-256 identity를 기록합니다. 다른 build에서 온 동일-version Core SDK를 혼용하면 configuration이 실패해야 합니다. archive 또는 installer를 만들기 전 release pipeline은 staged package path만 사용하여 `Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal`을 compile/run해야 하며 repository include/library path는 허용되지 않습니다.

## 메모리 통합 검증

`VeroScriptCppMemoryIntegrationTests`는 Engine 제한, Session 연속 호출, 네이티브 서비스, 핫 리로드 후 동일성과 제한 초과 시 복구, 도메인 종료 전 정리, Session 소멸 후 임대 해제를 검증합니다.
`CppMinimal/memory.cpp`는 설치된 Runtime 소비 프로그램입니다. 메모리 계약 변경 시 두 항목과 `VeroScriptMemoryTests`, Session／디버그／JIT 테스트, AOT 거부 테스트를 함께 실행합니다. 선택적 백엔드 인수는 실제 JIT 모듈을 검증합니다.[호스트 메모리 규칙](../../User/ko-KR/embedding.md)을 참고하세요.

## 정리 및 AOT 회귀 테스트

`VeroScriptCppSessionDebugTests`는 지우기, 교체, 핫 리로드 비활성화, callable 해제 및 소멸 중 콜백 재진입과 새 보존 루트 거부, 정리 후 재사용을 검증합니다. `VeroScriptCppJitContractTests`는 구조화된 네이티브 등록의 캐시 무효화를 확인합니다. 출시 전에는 `VEROSCRIPT_AOT_REQUIRE_NATIVE=1`로 `VeroScriptCppAotHelloTests`를 실행하세요. 생성된 C++을 컴파일하고 실행하여 VM 결과와 비교하며 증감 연산의 부작용, 수명, 제네릭 숫자 폭 및 오버플로 실패를 검증합니다. `VeroScriptCppAotEmitTests`는 제네릭 특수화를 포함하여 지원하지 않는 사용자 타입의 증감 연산이 명시적으로 거부되는지도 검증합니다.

## 모듈 매크로 통합 게이트

`VeroScriptCppEngineIntegrationTests`는 호스트 초기 매크로, 소스 오버레이, 명시적／재내보내기／암시적 가시성, 소스 및 파일 캐시, 의존성 무효화, 핫 리로드, 의존성 실패 후 기존 프로그램 유지, 물리 위치 진단, 디버그 정보를 제거한 라이브러리 매크로를 검증합니다. `VeroScriptCppAotHelloTests`는 공개 가변 인자 매크로와 호출자 환경에서의 확장을 네이티브／VM으로 비교합니다. `CppMinimal/main.cpp`는 준비된 SDK 헤더를 통해 공개 매크로 메타데이터를 사용합니다. 모듈 전처리나 바이트코드 인터페이스가 바뀔 때 실행하세요.

## 어노테이션 통합 검증

`VeroScriptCppAnnotationTests`는 VM／Mixed／실제 JIT에서 Engine 검사 및 실행, Session 소스／파일 로드와 핫 리로드, 치명적이지 않은 진단, 의존성 캐시와 캐시 불가 처리기를 검증합니다. 소스／파일 로드, 독립 컴파일 및 리로드 중 콜백 재진입 거부와 취소 후 상태 보존 및 재사용도 확인합니다. `VeroScriptCppAotEmitTests`는 높인 생성 예산이 두 번째 기본 준비 단계에서 거부되지 않는지 검사합니다. 네이티브 `VeroScriptCppAotHelloTests`는 생성된 가상 메서드 본문, 다른 파일의 제네릭 본문과 선언／구현 스코프를 비교합니다. SDK 예제는 설치된 헤더, 처리기 등록 및 Session／AOT API를 검증합니다.

## 프로젝트 변환과 파일 트랜잭션

`VeroScriptCppTranspilerTests`는 최종 모듈, 구현 범위, 가상 호출, 기본 인수 및 원본 파일 진단을 검증합니다. `VeroScriptCppTranspilerNativeTests`는 C++ → VeroScript → C++를 실제로 컴파일하고 실행합니다. `VeroScriptCppFrontendTests`는 미지원 구문의 명시적 거부, 경로 정규화, 하드 링크와 쓰기 전 충돌 보호를 검증합니다. 배치 처리, CLI 입력 보호와 출력 트랜잭션은 `FFilePathSet`를 공유하며 파일 ID를 읽을 수 없으면 기존 비교를 유지합니다.

## 호스트 터미널 통합 게이트

`VeroScriptCppConsoleTests`는 VM／Mixed／실제 JIT에서 터미널 기능, 정확한 텍스트 출력과 `print` 묶음, 권한 거부, 호스트 콜백 실패, 실행 오류와 명령 한도 소진 후 스타일／커서 복원, Session 반복 호출 및 일반 출력 스트림을 검증합니다. `VeroScriptCppAotHelloTests`의 네이티브／VM 비교는 검증 실패 전 부작용을 포함하여 색상 및 커서 인수의 평가 순서를 확인합니다. 준비된 SDK의 `CppMinimal/main.cpp`는 공개 호스트 인터페이스와 실행 종료 시 복원을 검증합니다. 터미널 계약 변경 시 이 게이트를 실행해야 합니다.

네이티브 게이트는 명령줄 인수가 있는 진입점과 없는 진입점 모두에서 터미널 어댑터 실패, 표준 및 비표준 예외, 진단 스트림 실패 시 실패 코드를 반환하고 정리를 완료하는지도 확인합니다.

작업 스레드 게이트는 터미널 가드 생성 중 표준／비표준 예외가 작업 실패로 처리되고, 터미널 복원이 완료된 뒤 작업 완료가 통지되는지도 확인합니다.

## AOT 평가 및 참조 검증

`VeroScriptCppAotHelloTests`는 네이티브 실행과 VM을 비교하여 일반·제네릭·확장·생성자 호출의 인자 순서와 변환, 값 스냅샷, ref 인자, 단락 연산, 복합 대입 및 전역 callable 초기화를 검증합니다. 함수 값은 인자 이후에, 인스턴스 수신자는 인자 이전에 평가하여 VM과 일치시킵니다. 후속 인자가 컨테이너를 확장하거나 다시 바인딩해도 인덱스 참조가 유효해야 합니다. 인덱스 변환이 실패하면 후속 인자의 부수 효과를 실행하지 않습니다. 순수 스칼라 식은 직접 생성하며, 저장한 값 인자는 이동하여 추가 복사를 피합니다.

대입 회귀 테스트는 저장된 수신자가 소멸한 후에도 식의 결과가 유효한 값인지 확인합니다. 단순 인덱스 쓰기는 저장된 컨테이너를 원래 변수나 필드에 다시 쓰고, 중첩 선형 인덱스와 한정된 전역 변수는 현재 저장 경로를 해석합니다. 우변에서 다시 바인딩해도 별칭과 객체 정체성은 VM과 일치해야 합니다. 단순 인덱스 대입의 범위 검사는 우변의 부작용 이후에 수행하고, 참조 경로의 인덱스 변환은 그 전에 수행합니다. 변환이 필요 없는 단순 스칼라 인덱스 쓰기는 직접 생성하여 고정 길이 배열 복사를 피합니다.

`CppMinimal` 예제는 다시 링크할 필요가 없는 증분 빌드를 포함하여 매번 빌드할 때 일치하는 Core SDK의 표준 라이브러리와 현지화 리소스를 실행 파일 옆에 배포합니다. Visual Studio 및 Ninja/Make 생성기 모두에서 누락된 파일을 복원하고 변경된 SDK 리소스를 갱신해야 합니다.

[C++ AOT — 참조 반환과 평가 수명](../../User/ko-KR/aot.md#reference-return-lifetime)을 참조하세요.

지속되는 인덱스 별칭과 인덱스 참조 반환에는 관리되는 저장소 경로가 필요합니다. 임시 값 타입 수신자에서 얻은 참조를 지속해서 유지하려면 관리되는 소유자도 필요합니다. AOT는 이 경우를 명시적으로 거부하므로 VM 백엔드를 사용하세요.

암시적 구조체 필드에는 스크립트 필드의 기본 초기값을 적용하며 제네릭 필드 타입과 고정 배열도 포함합니다. 저장소 초기화는 구조체 생성자 본문을 실행하지 않습니다.

네이티브 검증은 유지되는 지역 필드 별칭, 인수가 없는 호출의 수신자 수명, 참조 대입 대상의 단일 평가, 숫자 참조의 오버플로와 축소 변환을 확인합니다. 생성 테스트는 정리가 필요한 수신자와 지역 별칭 반환을 명시적으로 거부하는지 확인합니다. 멤버, 소멸자, 정적 필드 및 열린 템플릿 정의는 완전한 타입 레이아웃과 전역 접근자 선언 뒤에 배치합니다. 네이티브 검증에는 뒤에 선언된 전역 접근 및 제약 조건이 있는 템플릿의 멤버와 정적 필드가 포함됩니다.

검증에는 직접 및 중첩 필드 별칭, for 초기화의 참조, 멤버 및 캡처된 수신자, 제네릭 필드와 구조체 필드의 기본 저장소도 포함됩니다. 생성 테스트는 지속되는 인덱스 저장소와 임시 값 타입 수신자의 별칭을 거부합니다. 재귀 정리 검사는 제네릭 특수화를 등록할 수 있는 조회 전에 타입 이름의 스냅샷을 저장합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/build-and-tests.md) · [fr-FR](../fr-FR/build-and-tests.md) · [ja-JP](../ja-JP/build-and-tests.md) · [ko-KR](build-and-tests.md) · [zh-CN](../zh-CN/build-and-tests.md) · [zh-TW](../zh-TW/build-and-tests.md)
