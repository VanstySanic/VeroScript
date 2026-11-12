# C++ 그룹 아키텍처

## 의존 방향

C++ group은 `VeroScript/` 위의 adapter 및 toolchain layer입니다. `VeroScriptCpp`는 embedding/session operation을 제공하고 `VeroScriptCppAot`는 core에 직접 의존합니다. `VeroScriptCppFrontend`는 shared C++17 declaration model을 소유하며 `VeroScriptCppBindGen`과 `VeroScriptCppTranspiler`는 그 model을 독립적으로 소비합니다. 어느 consumer도 자신의 lowering policy를 편하게 만들기 위해 frontend AST를 바꾸면 안 됩니다.

core는 C++ tool header를 포함하지 않습니다. `VeroScriptInterop`는 독립 stable C ABI이며 C++ frontend, BindGen, Transpiler의 implementation dependency가 아닙니다. C++ embedding layer는 core의 public runtime/binding interface를 사용할 수 있습니다.

## 소스 구조

각 native module은 `Public/`에서 supported header를 공개하고 implementation/test는 `Private/` 아래에 둡니다. CMake는 private source를 재귀적으로 찾고 focused test source는 library target에서 제외하며 test executable은 별도로 만듭니다. public header는 private include path 없이 public-header test에서 compile되어야 합니다.

## 공통 출력 모델

frontend diagnostic은 code, message, source location, token/declaration data를 보고합니다. BindGen과 Transpiler는 result object에서 이 diagnostic을 보존합니다. project operation은 catalog, reference, source map, manifest를 만들기 전에 모든 input을 수집합니다. output helper는 같은 file을 가리키는 path를 거부하고 전체 operation이 성공한 뒤에만 file을 replace합니다.

## 변경 규칙

public struct, enum, source location, generated file format, CMake target, supported-subset rule 변경은 해당 focused test와 관련 topic page의 갱신을 요구합니다. README는 command를 설명할 수 있으나 behavior의 authority는 public header와 test입니다.

## 네이티브 디버깅 경계

`VeroScriptSessionDebug.h`는 호스트 또는 Tooling 실행 도메인이 사용하는 C++ Session 브리지를 정의합니다. Session은 브리지 등록을 관리하고 실제 실행 VM 범위에서 공유 디버거를 유지합니다. 프로그램 교체, 롤백과 해제를 브리지에 알리며 정리용 VM에는 디버거를 넘기지 않습니다. 프로그램, 바인딩, 소스 및 실행 컨텍스트 뷰는 빌린 데이터입니다. 호스트는 동기화된 메타데이터를 복사하고 안전한 중단 지점의 실행 스레드에서 대기 중인 디버깅 명령을 처리합니다.

Cpp는 Tooling에 의존하지 않습니다. Tooling은 도메인 레지스트리, DAP와 전송 수명 주기를, UE 통합은 편집기/PIE, 객체 수명과 게임 스레드 일시 중지 정책을 담당합니다. 이 공개 경계는 Session의 C++ 레이아웃을 바꾸므로 해당 SDK로 네이티브 호스트를 다시 컴파일해야 합니다.

`VeroScriptCppSessionDebugTests`는 인자 변경, 객체 호출, 보존 콜백, 인터프리터 선택, 다시 로드 롤백, 예외 정리 및 브리지 교체/제거를 다룹니다. `VeroScriptCppJitContractTests`는 독립 공개 팩터리로 백엔드 전환을 확인합니다. SDK 소비 예제는 내보낸 헤더와 라이브러리를 사용합니다. TCP 예제 검증은 대기와 종료 시간을 제한한 자식 프로세스에서 수행하며 로컬 Session을 빌린 서버 스레드를 분리하지 않습니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/architecture.md) · [fr-FR](../fr-FR/architecture.md) · [ja-JP](../ja-JP/architecture.md) · [ko-KR](architecture.md) · [zh-CN](../zh-CN/architecture.md) · [zh-TW](../zh-TW/architecture.md)
