# 응용 프로그램 국제화 실행 계약

핵심은 값, 원자적 설정, 불변 공급자 스냅샷 및 네이티브 호출 진단을 관리합니다. 진단 언어는 독립적입니다. ICU, 리소스 파일 형식 및 GUI 갱신은 확장 책임입니다.

## 공개 API

- [`VeroScriptI18n.h`](../../../../Public/VeroScriptI18n.h)는 `FI18nArgument`, `FI18nArguments`, `FI18nConfiguration`, `FI18nText`, `FI18nContext`, `FI18nSnapshot`, `FI18nVersion`을 공개합니다.
- `CreateContext(config, services, out, error)`로 만들고 `CaptureSnapshot()`, `TryLookup`, `TryResolve`, `TryFormat`으로 조회합니다. `ToScriptValue` / `FromScriptValue`는 Core 래퍼 식별 정보를 검증합니다.
- `FI18nServices::Publish`는 두 불변 뷰를 원자적으로 게시합니다. `PublishResources` / `PublishFormatter`는 다른 쪽의 최신 뷰를 보존하며 `Acquire`는 소유한 스냅샷을 반환합니다. 리소스/설정 내용이 바뀌면 새 뷰를 사용하세요. 같은 뷰를 소유자 없이 다시 게시하면 기존 모듈 유지 참조를 보존합니다. 제어 블록을 소유한 null 별칭 shared_ptr도 유효한 유지 참조입니다.
- `ConfigureAndSetServices`는 설정과 서비스 참조를 원자적으로 교체합니다. 다른 컨텍스트와 공유하는 서비스 자체를 트랜잭션으로 수정하지 않습니다. `Set*` 메서드는 나머지 필드의 최신 값과 병합합니다.
- 해석기는 호출마다 정규화된 정확한 언어 하나를 받고 `Found` / `NotFound` / `Failed`를 반환합니다. 찾은 빈 템플릿도 유효합니다. 명시적 대체 언어 체인은 Core만 처리합니다.
- 포매터는 불변이며 `SupportsFormat`과 `Format`을 동시에 호출할 수 있어야 합니다. 요청에는 원래 타입 인자, 요청 언어, 일치 언어, 유효 서식 로캘과 타입 옵션이 들어갑니다. `literal` / `vero.named`는 Core용으로 예약됩니다.
- `FI18nWork`는 현재 작업의 확인 함수와 출력 한도를 빌리며 0은 무제한입니다. 긴 공급자 작업에서는 취소를 확인하고 출력 할당 전에 예산을 지켜야 합니다. Core는 콜백 전후를 확인하지만 차단된 네이티브 호출을 강제로 중단하지 못합니다.
- 값과 스냅샷은 데이터를 소유합니다. 콜백 컨텍스트, VM, 빌린 요청 뷰나 `FI18nWork`를 보관하지 마세요. 일반 네이티브 값 하나의 변경에는 외부 동기화가 필요하며 컨텍스트/서비스 메서드는 스레드 안전합니다. 사용자 정의 값과 공급자는 불변이고 동시 읽기에 안전해야 합니다.
- 공급자/사용자 정의 모듈 소유자는 콜백과 가상 소멸자보다 오래 살아야 합니다. 동적 코드에는 소유자를 명시하고 컨텍스트/서비스 → 공급자 → 컨텍스트/서비스의 강한 참조 순환 대신 약한 참조를 사용합니다.
- 같은 컨텍스트의 `Try*` 재귀는 스레드별로 거부됩니다. 공급자가 설정을 읽거나 갱신할 수 있지만 콜백이 호출자의 값 변수를 교체/초기화해도 실행 중 작업은 시작 스냅샷과 텍스트를 유지합니다. 빌린 string_view 입력은 반환까지 유효해야 하며 오류 출력이 입력 문자열을 소유해서는 안 됩니다. 실행 중인 VM에 재진입하지 마세요.
- 버전 동등성에는 컨텍스트 식별 정보와 설정, 리소스, 포매터 리비전이 포함됩니다. 리비전 소진 시 실패하며 순환하지 않습니다. 영속적이거나 프로세스 간 공유되는 의미는 없습니다.
- 구조화된 오류는 `FI18nError::Status`, 소유한 `Diagnostic`, 도메인/키/언어/이름을 가집니다. `NotFound`에는 오류가 없어야 하며 실패에는 Ok가 아닌 상태와 진단이 필요합니다. 성공 콜백은 오류를 반환하지 않습니다.
- `FNativeCallContext::DiagnosticOutput`은 소유한 `FScriptError`의 빌린 출력입니다. VM/JIT 브리지는 ID/인자를 보존하고 현재 호출 위치를 적용합니다. `I18nServices`는 현재 호출의 빌린 서비스 접근입니다. 스크립트가 만든 컨텍스트는 소유한 서비스 선택을 캡처하며 VM을 보관하지 않습니다.
- `VeroScriptVm::SetI18nServices` 또는 C++ Engine/Session의 `FEngineConfig::I18nServices` / `I18nLimits`를 설정합니다. 워커는 선택과 예산을 상속하고 자체 실행 제어를 사용합니다. 기존 컨텍스트는 호스트 설정이 바뀌어도 서비스 참조를 유지합니다.

## 컴파일과 실행

파서와 컴파일러는 공통 `StandardLibrary::GetBindingCatalog`를 사용하며 정규 메모리 항목이 i18n보다 먼저 옵니다. 기존 Core 상대 opcode는 내부 이름 `CallMemory`를 유지하고 이 공통 카탈로그를 인덱싱합니다. 새 opcode나 `FValue` 대안은 추가하지 않습니다. `Memory::UsesRuntimeMemory`는 i18n 항목을 제외합니다. 바이트코드/패키지 로더도 같은 카탈로그를 검사합니다. 살아 있는 컨텍스트/공급자/사용자 정의 페이로드는 실행 시 값이며 라이브러리에 직렬화되지 않습니다.

AsmJit는 지원하지 않는 Core 상대 호출을 진입 전에 거부하여 콜백 재실행 없이 기존 VM 대체 실행을 허용합니다. 표준 값은 일반 호스트 복제/동등성 프로토콜을 사용하며 일반 값 동등성에는 키 해시가 필요하지 않습니다. Map/Set 키 동등성에는 여전히 유효한 해시 프로토콜이 필요합니다.

일반 명명된 호스트 메서드는 매개변수 형식에 따른 오버로드를 지원합니다. 스크립트 API 는 `text::message_key(domain,key)` / `text::message(domain,key,args)` 와 `format` / `format_with` 를 유지하며 형식화된 서식은 문자열 전용 위치 인수 `std::format` 을 암시적으로 호출하지 않습니다.

## Validation

`VeroScriptI18nTests`는 스칼라 정밀도, 소유/COW 데이터, 공급자, 버전, 동시 게시, 재진입, 취소/출력 한도, 스크립트 제네릭/컨테이너/Attribute, 워커 상속, 바이트코드/라이브러리 왕복, VM/Mixed/JIT와 진단 브리지를 검증합니다. JIT 부분은 호환 백엔드가 있을 때 실행합니다. `VeroScriptI18nExample`은 Core와 호환되는 Bytecode VM SDK를 링크합니다.

[i18n SDK 예제](../../../Examples/I18n/README.md) · [사용자 API](../../User/ko-KR/standard-library/i18n.md).

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [en-US](../en-US/i18n-runtime.md) · [fr-FR](../fr-FR/i18n-runtime.md) · [ja-JP](../ja-JP/i18n-runtime.md) · [ko-KR](i18n-runtime.md) · [zh-CN](../zh-CN/i18n-runtime.md) · [zh-TW](../zh-TW/i18n-runtime.md)
