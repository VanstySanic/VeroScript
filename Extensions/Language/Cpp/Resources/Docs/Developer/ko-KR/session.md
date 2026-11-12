# C++ embedding과 session

## 상태 없는 엔진

`VeroScriptEngine`는 persistent state가 필요 없는 caller를 위해 `CheckSource`, `CheckFile`, `RunSource`, `RunFile`을 제공합니다. file operation은 bytecode cache directory를 사용할 수 있습니다. source label, input/output/error stream, circular-import policy, project-wide-access policy는 explicit parameter입니다. result에는 success/failure와 structured diagnostic이 들어가며 stream에 error line을 썼다고 result object를 대체할 수는 없습니다.

## 상태가 있는 세션

`VeroScriptSession`은 loaded module, host registration, input/output bridge, runtime configuration, optional hot-reload state를 소유합니다. 정상 flow는 다음과 같습니다.

1. `IEngineHost`로 session을 construct합니다.
2. configuration을 적용하고 native function 또는 host type을 register합니다.
3. source, file, validated package를 load하거나 module을 compile/commit합니다.
4. run, function/method call, value inspection, metadata enumeration을 수행합니다.
5. operation state precondition이 허용할 때만 clear 또는 reload합니다.

configuration, loading, compilation, committing, reloading, clearing, binding mutation은 run 중에 거부됩니다. `LoadFile`은 file-backed reload path를 기록하지만 `LoadSource`는 만들지 않습니다. reload operation은 replacement와 package kind를 validate한 뒤 commit합니다.

## evaluation과 value

`Run(ERunMode::Eval)`은 `main`을 호출하지 않고 모듈을 초기화합니다. 호출 사이의 전역 상태는 `SetHotReloadEnabled(true)`를 설정했을 때만 유지됩니다. function/method call은 `FValue`를 반환하고 object construct, field/global access, callable retain을 수행할 수 있습니다. retained callable은 명시적으로 release하며 owner-thread-only invocation으로 설정할 수 있습니다. external serializer는 registered runtime type과 encoded version에서 동작하고 serialization, deserialization, reference enumeration은 별개의 operation입니다.

## host binding과 metadata

registration은 native function, host type, generic host-type template, member, operator, clone callback, inheritable type, scoped enum, subclass factory를 포괄합니다. registration error는 stable diagnostic ID로 보고됩니다. reflection은 loaded type, field, method, function, global, enum, interface를 enumerate하며 read-only metadata access입니다.

## JIT와 clear

<a id="session-jit-cache"></a>

재사용에는 선택한 팩터리, VM이 아닌 유효 정책 및 `FJitPolicy::bReuseSessionCode`가 필요합니다. 대상 `Run`, `CallFunction`, `CallMethod`와 보관한 callable 호출은 세대 캐시와 컴파일 할당량을 공유합니다. `MaxCompileAttempts` 또는 `MaxCachedEntries`의 0은 무제한이며 비활성화가 아닙니다. 지속 실행과 안정된 콜백은 재사용할 수 있습니다. 네이티브 카탈로그는 `IHostBindingCatalog::GetRevision()`으로 런타임 리비전을 게시합니다. 백엔드가 필요한 실행 제어 기능을 갖추면 제어된 네이티브 실행도 코드를 재사용하며, Session은 VM 사이에 해당 제어 컨텍스트를 보존합니다. 구성, 정책, 백엔드, 모듈 또는 바인딩 변경은 보관 코드를 무효화하며 리로드 실패 시 이전 프로그램을 유지합니다. 기본값은 실행 코드 8 MiB, 보관 페이로드 32 MiB입니다. `GetJitCacheStats`는 확정된 세대 집계이고 `GetLastJitStats`는 최근 호출 정보이며 프로세스 전체 메모리 측정이 아닙니다.

## 실행 제어와 제한

`SetExecutionLimits`는 instruction과 wall-clock limit을 받고 해당 field의 zero는 unlimited입니다. `SetExecutionControl`은 shared thread-safe cancellation object를 설치하며 `RequestCancel`은 `Run`이 active일 때 호출할 수 있습니다. cancellation은 cooperative이며 native callback을 forcefully terminate하거나 unsynchronized host state를 safe하게 만들지 않습니다.

`SetCapabilities`는 파일, 환경, 콘솔 및 작업 같은 표준 라이브러리 부작용을 제어하며, Core는 해석 경로와 지원되는 네이티브 경로에서 권한을 검사합니다. `bAllowProcessDirectoryChange=false`는 `chdir`의 호스트 작업 디렉터리 변경을 금지하며, `chdir`에는 환경 쓰기 권한도 필요합니다. `SetExecutionBackend`는 `Vm`, `Mixed`, `Jit`를 선택합니다. 제어된 `Mixed`는 기본적으로 VM을 사용하며, `SetJitPolicy`와 `FJitPolicy::bEnableControlledMixed=true`로 동적 네이티브 실행을 활성화합니다. 명시적 `Jit`와 `Mixed`에서 선택한 정적 Core AOT는 이 스위치와 독립적입니다. 필요한 제어 기능이 없으면 진입 전에 VM으로 돌아가며, 디버깅, 유지된 초기화 구간과 제한된 정리는 VM을 사용합니다.

## 호출, 가로채기와 영속성

public call surface에는 `HasFreeFunction`, `HasInstanceMethod`, `CallFunction`, `CallMethod`, `CallMethodBypassingInterceptor`, `ConstructObject`, `RetainCallable`, `InvokeCallable`, field/global access, `ApplyClassDefaults`, reflection enumeration이 포함됩니다. bypass method는 선택한 entry interceptor만 skip합니다. method body가 하는 nested call은 정상적으로 interceptor dispatch를 따를 수 있습니다. retained callable ownership, owner-thread mode, weak `this` capture, module-generation invalidation은 명시적 lifetime rule입니다.

`RegisterExternalSerializer`, `SerializeObject`, `DeserializeObject`, `EnumerateObjectReferences`는 Interop serialization 및 borrowed-reference contract를 사용합니다. `Reload`, `ReloadCompiledModule`, `ReloadPackage`는 replacement를 transactional하게 stage/validate합니다. initialization failure가 나도 마지막 working module을 유지하고 structured diagnostic을 보고합니다.

## 보관 바이트 할당량

`MaxNativeBytes` 또는 `MaxRetainedPayloadBytes`의 0은 해당 바이트 보관을 허용하지 않습니다. 무제한을 뜻하는 정책 예산 0과 다릅니다.

## 취소 및 호스트 전달

<a id="session-control-and-streams"></a>

`RequestCancel()`은 현재 제어 객체의 공유 스냅샷을 원자적으로 가져오며, 소유자가 실행하지 않는 동안 제어를 교체해도 안전합니다. null이면 아무 작업도 하지 않고 다른 제어 객체로 취소를 전달하지 않습니다. `GetExecutionControl()`은 소유자 스레드 전용의 빌린 참조입니다. Session 파괴 전에 취소 호출과 실행의 종료를 기다립니다. 행 뷰는 콜백 중에만 유효합니다. 전달 콜백이 예외를 던지면 스트림은 실패를 보고하며 재사용이나 파괴 시 같은 이벤트를 다시 전달하지 않습니다. `ResetForReuse`는 스트림 상태를 복구하고 소멸자는 콜백 예외를 억제합니다.

같은 `FExecutionControl` 토큰을 공유하는 Session은 취소 상태도 공유합니다. `Reset()`과 재사용 전에 이전 취소 호출이 끝날 때까지 기다려, 오래된 요청이 토큰을 재사용하는 다음 실행을 취소하지 않도록 합니다.

## 구현 책임

<a id="session-debug-implementation"></a>

`VeroScriptSessionDebug.h`는 Session 디버그 브리지와 빌린 뷰를 정의하며 구현은 현재 `VeroScriptSession.cpp`에 있습니다. `VeroScriptLiveDebugDomain.h`는 호스트가 소유하는 라이브 도메인 계약을 정의하고 `VeroScriptDapAdapter.cpp`가 사용합니다. 같은 이름의 `.cpp` 파일에는 현재 헤더 포함만 있습니다. 파일 이름만으로 구현 책임을 판단하지 말고 위의 제어 스레드 및 수명 계약을 지키세요.

## 선택적 JIT 진단

`FJitPolicy::Diagnostics.MaxFunctions` 기본값 0은 함수별 수집을 끕니다. 기존 policy 복사본에 레코드/깊이 한도와 선택적 `bMeasureTime`을 설정한 뒤 `SetJitPolicy`를 호출합니다.

`GetLastJitDiagnostics()`는 최근 실행의 소유 snapshot이며 임시 VM 해제 뒤에도 유효합니다. 이후 호출을 넘어 유지하려면 복사합니다. Run, debug, 함수/메서드 호출, 생성, 유지 Callable 호출에서 수집하고, 수집을 끈 다음 실행 또는 `Clear()`가 지웁니다. `DroppedRecords`는 부분 수집을 나타내며 timing을 끄면 시간 필드는 0입니다.

`GetLastJitStats()`의 실제 진입 수는 `NativeLeafExecutions`, `NativeCfgExecutions`, `BridgeExecutions`, `LinearExecutions`, `VmTrampolineExecutions`입니다. 컴파일 수와 구별하고 중첩 scalar/direct-call/typed-loop 수를 함수 진입 수에 더하지 않습니다. inclusive 시간도 함수 간 합산하지 않습니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
