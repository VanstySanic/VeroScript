# 코어 JIT 서비스와 동시성

[교체 가능한 VM 백엔드](vm-backends.md)

<a id="native-abi-version"></a>

`VEROSCRIPT_VERSION_CODE`는 제품 버전 `VEROSCRIPT_VERSION`을 major:8/minor:8/patch:16 비트로 인코딩합니다. JIT 모듈 핸드셰이크와 JIT/AOT의 `CoreVersion`은 이 값을 공유하고 C AOT 설명 필드는 `core_version`입니다. 별도의 JIT 계약 또는 API 버전 카운터는 없습니다. 바이너리 호환성은 공개 헤더 지문, 설명 구조체 레이아웃, 도구 체인 및 플랫폼으로 검사하며 AOT는 Core 구현 식별자도 검사합니다. 정적 C++ 사용자와 백엔드 바이너리는 일치하는 헤더 및 라이브러리로 다시 빌드해야 합니다. 패키지 릴리스 버전은 별도의 패키지 메타데이터입니다.

## 소유권과 선택

[교체 가능한 VM 백엔드](vm-backends.md#vm-provider-assembly)을 참조하세요.

[선택적 AsmJit 모듈](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/ko-KR/index.md)이 IR, lowering, 네이티브 생성, 이식 가능한 Linear 실행, ABI 어댑터, 직접 호출 대상, 실행 scratch를 소유합니다. 비공개 헤더와 아키텍처 매크로는 Core SDK에 포함되지 않습니다. 코어는 `backend-services`을 보고하며 실제 대상과 네이티브 기능은 선택된 팩터리 설명으로 조회합니다.

## 컴파일과 호출

`EJitCompilationResult`는 `Ready`, `Unsupported`, `Deferred`, `Failed`를 구분합니다. 미지원과 자원 연기는 VM 폴백이 가능하지만 컴파일 실패는 오류입니다. 소유권, 서비스, 자원 검증 후 게시합니다. 프로그램 식별자는 모듈 및 네이티브 바인딩 세대를 포함합니다.

`EJitInvocationResult`는 `NotEntered`, `Returned`, `ControlTransfer`, `Failed`를 구분합니다. `NotEntered`만 인터프리터에서 처음부터 실행할 수 있습니다. 진입 후 오류, throw, exit, 취소는 부작용을 반복하지 않고 전달됩니다. 코어가 반환값을 검증합니다. 실행 산출물은 코드와 모듈 소유자를 유지하고 컨텍스트, 프레임 토큰, 데이터 뷰는 차용입니다. 중첩 호출이나 호스트 재진입 후 객체 및 컨테이너 저장소를 다시 얻어야 합니다.

공개 서비스는 `VeroScriptJitRuntimeServices.h`, `VeroScriptJitFrameServices.h`, `VeroScriptJitValueServices.h`, `VeroScriptJitCallServices.h`, `VeroScriptJitScalarServices.h`입니다. 복사, 참조, const, 메서드 분배, 변경 알림, 오류, 논리 호출 깊이는 코어 의미를 유지합니다.

`SetJitProgramOwner`로 제공한 소유자는 불변 모듈 세대를 유지합니다. 다른 쓰기 가능한 별칭으로 내용을 바꾸지 말고 유휴 경계에서 모듈을 교체합니다. 직접 사용하는 `VeroScriptVm`은 설정된 프로그램 세대나 소유자를 자동으로 바꾸지 않습니다. 프로그램 교체 후에도 값을 유지한다면 호스트가 해당 식별자를 갱신하고 해제 또는 지원되는 마이그레이션이 끝날 때까지 정의 모듈을 유지해야 합니다. Session의 교체와 다시 로드 경로는 이 식별자를 관리합니다.

## 정책과 Session 재사용

`FJitPolicy`는 열도, 컴파일 시도, 캐시 판단, 사전 컴파일을 제어하며 정책 예산 0은 무제한입니다. `Vm`은 백엔드를 우회하고 `Mixed`는 열도를 기다리며 `Jit`는 정책 안에서 컴파일을 우선합니다. 실행 거버넌스와 백엔드 기능에 따라 검증된 제어 JIT 또는 VM을 선택합니다. 디버깅과 미지원 경로는 VM을 사용합니다.

`bReuseSessionCode`는 기본 활성화입니다. 적격 `Run`, `CallFunction`, `CallMethod`, 유지된 callable 호출은 같은 세대 캐시와 누적 시도 예산을 사용하며 무제한 정책도 재사용됩니다. 주입된 팩터리와 비 VM 모드가 필요합니다. 지속 실행과 안정된 콜백은 재사용할 수 있습니다. 네이티브 카탈로그는 `IHostBindingCatalog::GetRevision()`으로 런타임 리비전을 게시합니다. 정책, 설정, Reload, Clear는 상태를 무효화하고 실패한 Reload는 기존 프로그램을 보존합니다.

`FSessionJitCacheLimits`의 기본값은 실행 저장소 8 MiB와 보존 페이로드 32 MiB이며 scratch를 포함합니다. 0은 해당 자원 보존 금지입니다. 자원 거절은 VM 폴백이 가능하지만 소비한 컴파일 시도를 돌려주지 않습니다. `FSessionJitCacheStats`는 정산된 세대 회계이고 `FJitStats`는 최근 실행입니다. `NativeTypedLoopExecutions`, `DirectNativeCalls`, `CostModelVmFallbacks`는 실제 경로를 각각 세며 비용 폴백은 실행 오류가 아닙니다.

## 동시성과 검증

코어는 태스크, join, timeout, 협력 취소, channel, mutex, semaphore, 동기화 값을 관리합니다. 차단된 channel은 닫힘을 관찰합니다. worker는 불변 팩터리와 소유 프로그램을 공유하고 인스턴스, 통계, scratch, 오류는 독립입니다. 종료 시 언어 루트를 해제합니다.

계약, 차분, 수명, 디버그, 동시성 회귀를 검증합니다. 모듈 활성, 비활성, 네이티브 생성만 비활성인 빌드를 수행하고 백엔드 디렉터리가 없는 소스에서 Core와 C++ 소비자를 빌드합니다. 기존 성능 기준을 유지하며 SDK 명시적 주입을 확인합니다.

<a id="construction-dispatch"></a>

생성 또는 소멸 중의 가상 호출 대상은 Core의 현재 수명 단계에서 결정해야 합니다. `bConstructionIncomplete`와 `bDestroying`은 이 검사가 필요함을 나타내며, `GetTypeName`과 `RuntimeTypeIndex`는 실제 타입과 필드 배치를 그대로 나타냅니다. 단계별 디스패치를 유지할 수 없는 최적화는 부수 효과가 발생하기 전에 폴백해야 하며 일반적인 실제 타입의 가상 호출 캐시를 재사용하면 안 됩니다. 정적으로 바인딩된 비가상 호출과 Core가 대상을 이미 선택한 네이티브 생성자 진입점은 기존 경로를 유지합니다.

## 실행 진단

`PlanReason`/`PlannedKind`는 snapshot 조회 시 cache plan이며 `Fallbacks`는 성공 컴파일 전 warm-up을 포함하는 이번 실행의 과거 거부입니다. 상세 수집은 공식 VM의 계측 dispatch를 사용해 명령 융합을 줄이고 Core 경계를 더 드러낼 수 있습니다. 그 시간/경계 수를 비계측 성능으로 보지 않습니다. 집계 진입 수는 상세 수집 없이도 읽을 수 있습니다.

`NativeLeafEntries`/`NativeCfgEntries`는 컴파일 artifact, `NativeLeafExecutions`/`NativeCfgExecutions`는 오류 종료도 포함하는 실제 진입 수입니다. controlled, typed loop, direct call, Linear/Bridge/VM trampoline은 별도 수치입니다. `ScalarNativeFastPathCalls`는 host scalar-native fast path만 집계합니다. 겹치는 이벤트를 독립 script call로 합산하지 않습니다.

실행 전 `FJitPolicy::Diagnostics`에 예를 들어 `MaxFunctions=128`, `MaxDepth=256`, 선택적 `bMeasureTime`을 지정합니다. 실행 뒤 `VeroScriptVm::GetJitDiagnostics()` 또는 `VeroScriptSession::GetLastJitDiagnostics()`를 읽습니다. 세대별 함수 identity, module, 실제 진입 종류, 결과, fallback histogram, 선택적 inclusive/self 나노초를 기록합니다. Core 호출 경계만 기록하며 inline 연산/직접 native 재귀에는 별도 기록이 없습니다. inclusive는 겹치며 self는 기록된 자식만 제외합니다. 최외곽 실행이 reset하고 재진입/cleanup은 같은 실행에 속하며 `ResetJitStats()`도 지웁니다.

기본 `MaxFunctions=0`에서는 기록 allocation/clock/logging을 하지 않습니다. 최대 4096 함수, 깊이 32768, 이름/이유당 4096 bytes이며 `DroppedRecords`는 부분 수집을 뜻합니다. 집계만 필요하면 timing을 끄고 성능 측정과 진단 pass를 분리합니다.

`GetFunctionReason()`/`GetFunctionFallbackReason()`는 성공 VM plan까지 포함해 컴파일 결정과 최근 runtime 거부를 제공합니다. 예외/ref/소유권 제한은 cost-model과 구별하고 알 수 없는 runtime 제한은 `RuntimeGuard`입니다. `CostModelVmFallbacks`는 필수 finally/ref fallback을 제외합니다. finally는 Core unwinding/제한 정리를 유지합니다. 제한된 scalar 식은 callback/governance 없이 단순 writable ref capture 하나를 갱신할 수 있으나 alias/다중 capture/readonly/mutation-origin/probe 실패는 기본 경로를 사용합니다. frame cache는 함수/module/generation으로 보호하며 getter 순서/횟수와 binding 상태를 유지합니다.

집계는 C Interop/C++/C#/Python, 상세 snapshot은 Core/C++로 노출합니다. public layout/service 변경 뒤에는 제품 버전이 같아도 일치하는 Core/VM/JIT/Interop/native consumer를 다시 빌드합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/jit-and-concurrency.md) · [fr-FR](../fr-FR/jit-and-concurrency.md) · [ja-JP](../ja-JP/jit-and-concurrency.md) · [ko-KR](jit-and-concurrency.md) · [zh-CN](../zh-CN/jit-and-concurrency.md) · [zh-TW](../zh-TW/jit-and-concurrency.md)
