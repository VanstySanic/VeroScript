# 코어 실행, 상태, package

[교체 가능한 VM 백엔드](vm-backends.md)

## VM 상태

[교체 가능한 VM 백엔드](vm-backends.md#vm-provider-assembly)을 참조하세요.

## bytecode metadata와 control flow

bytecode는 source/package behavior 보존에 필요한 metadata를 전달합니다. reference-parameter flag, closure capture name/type, declaration-only 또는 abstract function state, interface/enum table, debug-file ownership, exception region, `finally` range, global initializer range, package component ownership이 포함됩니다. reference opcode, checked/optional cast, `throw`, protected-region cleanup은 semantic operation이지 서로 교환 가능한 VM optimization이 아닙니다. serialization, hot reload, reflection, tooling은 metadata와 executable table의 alignment를 유지해야 합니다.

## worker와 synchronization

script global과 class static field는 native worker별로 격리됩니다. worker task registry와 task ID도 worker-local입니다. `channel<T>`는 명시적이고 synchronized된 cross-worker capability이며 host callback/reachable native state에는 host-provided synchronization이 필요합니다. worker는 다른 worker task ID를 자신의 local ID로 사용할 수 없습니다.

## package와 reload

package load는 module metadata와 application/library entry shape를 validate합니다. session reload는 opt-in이고 commit 전에 replacement를 stage합니다. file-backed, compiled-module, package reload는 서로 다른 input을 갖습니다. failure는 previous working module을 보존하고 structured diagnostic을 공개합니다.

## debug와 diagnostic

debug location, stack frame, variable, breakpoint, evaluation, stop reason은 Tooling/DAP가 소비하는 public data contract입니다. diagnostic ID, source range, substitution argument는 stable machine data이고 localized message는 presentation입니다.

## 실행 제어와 제한

`FExecutionLimits`는 instruction limit과 wall-clock timeout을 가지고 field zero는 unlimited입니다. `FExecutionControl`은 thread-safe shared control object입니다. `Run`이 active인 동안 host는 `RequestCancel`을 호출할 수 있고 VM은 cooperative point에서 request를 관찰합니다. cancellation은 진행 중 native/managed callback을 강제 중단하지 않으므로 callback state에는 일반 synchronization이 여전히 필요합니다.

`FScriptCapabilities`는 standard-library side-effect policy로 file, environment, console, task operation을 제한합니다. default configuration은 permissive지만 host가 restriction을 제공하면 execution path가 모든 JIT entry 전 강제합니다. 이는 optimization hint가 아닌 security policy입니다. `FEngineConfig`는 limit/control/capability와 source overlay, requested `EExecutionBackend`를 함께 운반합니다.

active limit, cancellation control, restricted capability는 enforcement에 필요할 때 interpreter path를 사용합니다. debugger attachment, 유지된 초기화 구간과 제한된 정리는 effective backend를 `Vm`으로 만듭니다. requested backend는 configuration value로 남으며 diagnostic에서 effective backend로 보고하지 않습니다.

## 재진입 정리와 예외 전파

값 해제는 스크립트 소멸자를 호출하여 실행에 재진입하고 호출 프레임 저장소를 재배치할 수 있습니다. 이런 호출을 지날 때 프레임 인덱스를 유지하고 호출 후 참조를 다시 얻습니다. 바이트코드 피연산자 읽기는 현재 프레임의 `StackBase` 위에 있어야 하며, 프레임 간 해제 정리는 저장된 피연산자를 명시적으로 해제합니다.

대기 중인 반환값이나 예외는 바깥 `finally` 연속 상태에 속합니다. `finally` 안의 반복문과 내부에서 잡은 예외는 이를 유지하며, 영역을 벗어나는 전파는 대체된 값을 정상 소유권 경로로 해제합니다. 정리 중 잡히지 않은 예외는 전파 중인 스크립트 예외를 대체할 수 있지만, 소멸자 내부에서 잡은 예외는 바깥 예외를 유지해야 합니다. 실행 실패와 VM 재사용은 대기 예외 객체를 해제해야 합니다. 디버거 및 명령 수 제한 모드를 포함하여 `VeroScriptExceptionTests`에서 확인합니다. 실행 제어 중에도 `throw`를 별도로 계산하고 검사해야 합니다.

## 숫자 서비스

`VeroScriptNumerics.h`는 불변 `FBigInteger`/`FRational` 및 트랜잭션 noexcept `Numerics::Try*` 연산을 제공합니다. `FNumericError`는 고정 메타데이터입니다. 복사, 이동, 교환, 소멸은 할당하지 않으며 이동한 원본은 0입니다. 불변 값은 호스트 스레드 간 공유할 수 있지만 컨텍스트와 콜백은 호스트가 동기화합니다.

`FNumericOperationContext`는 `FNumericLimits`와 동기적으로 빌린 작업 콜백을 결합합니다. 0은 유한한 기본값을 선택합니다: 65,536비트(최대1,048,576), 입력/출력 각1 MiB, scratch16 MiB, 연산당16,777,216단위. scratch는 임시 저장소와 커밋 전 출력의 요청량이며 VM 힙이나 RSS가 아닙니다. 실패 시 출력을 유지합니다.

실행 전에 `VeroScriptVm::SetNumericLimits`, `FEngineConfig::NumericLimits` 또는 `VeroScriptSession::SetNumericLimits`로 설정합니다. worker가 제한을 상속하며 전역 초기화에도 명령, 시간 제한 및 취소가 적용됩니다. `VeroScriptNumericBindings.h`는 검증된 FValue 교환을 제공합니다. C Interop은 `VS_INTEROP_CAP_NUMERICS`, `vs_interop_numeric_parse/to_text/convert`, 형식 확인과 세션 제한을 제공합니다. 문자열 길이는 명시적이며 각 언어의 BigInteger/Fraction은 자동 매핑되지 않습니다. 독립 AOT는 이러한 연산을 거부합니다.

설치 훅은 초기화 전에 `Packages::FLimits::HookNumericLimits`를 설정합니다. 패키지 형식, 레지스트리 프로토콜과 JSON 숫자 규칙은 변경하지 않습니다.

```cpp
#include "VeroScriptNumerics.h"

VeroScript::FNumericOperationContext context;
VeroScript::FNumericError error;
VeroScript::FBigInteger value;
bool parsed = VeroScript::Numerics::TryParse("18446744073709551616", value, context, error);
```

## variant / FValueRoot

[인터롭 SDK — variant](../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#exported-value-lifetime)을 참조하세요.

상자화 전에 VM이 `FVariantTypeDescriptor`를 검증합니다. `Variant::Box`와 `Variant::Snapshot`은 기존 복사를 재사용하며 쓰기 가능한 내부 값을 노출하지 않습니다. `VisitValueGraph`는 상자, 객체, 클로저, 컨테이너를 순회하며 종료자 스레드에서 스크립트를 실행하지 않습니다. 콜백이나 복사 전에 결과 슬롯을 예약하고 추가 할당 없이 확정합니다. 외부 작업은 실행 후 해제 대기 값을 정리합니다. 콜백에서 세션 종료를 요청하면 활성 호출이 끝난 뒤 파괴를 완료합니다.

VM은 전체 의미를 지원합니다. Mixed/asmjit는 지원하지 않는 variant 명령을 안전하게 VM으로 실행합니다. 이식 가능한 바이트코드와 패키지는 선언과 명령을 유지합니다. 독립 C++, C#, Python AOT는 출력 생성 전에 variant 의존성을 명시적으로 거부합니다. 디버거는 읽기 전용 $present, $type, $value를 표시하며 type<variant>().kind()는 "variant"를 반환합니다.
호출 안전 지점은 실행 중 빌린 인수와 수신 객체를 보호합니다. 종료 시에는 호출자가 루트를 등록할 때까지 반환값, 생성된 객체, 쓰기 가능한 인수의 결과를 유지하며 입력 전용 대여는 호출과 함께 끝납니다. 폐기된 값의 정리에 실패하면 성공한 호출도 런타임 실패로 보고하며, 기존 실행 오류가 있으면 해당 오류를 우선합니다. 종료 중 정리 실패는 호스트 진단 스트림으로 보고합니다.

## 네이티브 비동기 콜백 루트

실행 중인 native 호출은 `IScriptCallInvoker::RetainForGeneration`으로 함수와 캡처를 유지합니다. 반환된 `FValueRoot`만 저장하고 빌린 invoker, VM, Args는 저장하지 않습니다. 다른 스레드에서 해제를 요청할 수 있지만 읽기와 언어 정리는 소유 스레드에서 실행합니다. 일반 내보내기와 완료 루트는 같은 생존 그래프를 보호합니다.

native 안전 지점에서 완료 루트를 정리하며 `DrainGeneration`은 일반 호스트 차용 루트를 보존합니다. 소멸자는 현재 실행 예산, 취소와 절대 기한을 공유합니다. 영구 Session은 같은 세대의 루트를 유지하고 독립 VM의 최상위 실행 종료, 교체, 다시 로드와 닫기는 루트를 무효화합니다. 일반 내보내기는 계속 교체를 막습니다. 컴파일 실패는 이전 세대를 보존하며 마이그레이션 시작 뒤의 무효화는 롤백으로 복원되지 않습니다.

## 준비 캐시와 JIT 실행 서비스

`VeroScriptProgramPreparation::TryGetCacheInputFingerprint`는 처리기 실행, 패키지 재적용 또는 호스트 바인딩 검증이 필요 없는 파싱된 입력에만 준비 전 캐시 조회를 허용합니다. 로더의 전체 소스 및 해결 지문과 컴파일러 빌드 식별자를 함께 사용해야 합니다. 대상이 아니면 일반 준비와 종속성 검증을 수행합니다. 소스 메타데이터와 실제 상수 평가 제한도 식별에 포함됩니다.

`ICompiledFunctionServices::ExecuteBlock`은 길이가 제한된 명령 시퀀스에서 네이티브와 Core 사이 호출 비용을 줄입니다. 각 명령의 예산, 시간 제한, 취소, 권한 검사는 그대로 수행하며 분기, 반환, 실패 시 시퀀스를 종료합니다. 제어 실행 Mixed의 기본 정책은 바뀌지 않습니다.

빌린 캐시, 유지하지 않는 캐시 및 독립 작업자의 JIT 캐시는 진입점을 먼저 준비하며 도달한 호출 대상도 계속 컴파일할 수 있습니다. 유지 캐시는 실행 요구 사항이 동일할 때만 완료된 준비를 재사용합니다. 지원하지 않거나 비용상 불리한 경로는 VM을 사용합니다.

이 C++ 서비스 변경 후 Core, 백엔드 및 SDK 사용 코드를 일치하는 소스에서 다시 빌드해야 합니다.

전체 모듈 진입점은 설정된 사전 컴파일 예산을 계속 따릅니다.

`FJitCompiledFunction::bPreferVmForExecution`은 백엔드가 인수에 의존하지 않는 VM 선호 결정을 게시하도록 합니다. Core는 백엔드 서비스를 시작하기 전에 캐시된 결정을 적용하며 대체 실행 통계, 일반 VM 검사 및 호출 대상의 독립적인 선택을 유지합니다.

## 중단 후 제한된 정리

[호스트 통합 — 중단 후 제한된 정리](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/embedding.md#bounded-interruption-cleanup)을 참조하세요.

## 형식 호환성 캐시 소유권

`VeroScriptVm::IsActiveObjectTypeAssignableTo`는 정확한 정규 클래스／인터페이스 이름의 호환성을 모듈과 `ProgramGeneration`별로 캐시합니다. 각 실행 컨텍스트가 캐시를 소유하고 모듈／세대 변경 또는 4096개 항목 상한 도달 시 비웁니다. 소유한 불변 프로그램은 활성 호출 밖에서도 재사용할 수 있습니다. 빌린 프로그램은 실행 중에만 캐시하고 `BeginExecutionGovernance`에서 호출 사이에 비웁니다. 별칭과 비정규 이름은 권위 있는 메타데이터 해석을 유지합니다. JIT 호출, 값 복사, 호스트 조회를 최적화할 때도 이 소유권과 무효화 조건을 보존하세요.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/execution-and-state.md) · [fr-FR](../fr-FR/execution-and-state.md) · [ja-JP](../ja-JP/execution-and-state.md) · [ko-KR](execution-and-state.md) · [zh-CN](../zh-CN/execution-and-state.md) · [zh-TW](../zh-TW/execution-and-state.md)
