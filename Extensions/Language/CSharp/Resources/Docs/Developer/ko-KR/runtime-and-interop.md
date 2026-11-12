# C# runtime과 Interop adapter

## typed-loop 통계

`NativeTypedLoopExecutions`는 native typed-loop kernel invocation을 셉니다. source-visible runtime failure를 보고하고 끝난 invocation도 포함합니다. 이는 execution observability counter이며 success counter가 아닙니다.

`VeroScriptCSharp`는 `VeroScriptInterop`의 managed wrapper입니다. wrapper는 conversion, lease, callback root, managed exception을 소유하고 C ABI는 native layout, status, lifetime rule을 소유합니다.

## 호환성 검증

`VeroScriptRuntime`은 ABI version, semantic runtime version, host platform, capability bit, current language, available-language JSON, catalog load/merge를 공개합니다. `EnsureCompatible()`가 managed entry gate이며 ABI/product version은 다른 계약입니다. `LoadCatalogDirectory`는 `en-US` baseline을 검증해 catalog tree를 교체하고 `MergeCatalogDirectory`는 validated tree를 추가/명시 override합니다. JSON 복사나 fixed culture list로 merge를 흉내 내지 않습니다.

`EnsureCompatible()`는 전체 ABI 값을 `SupportedAbiVersion`(현재 `0x00000101`, 0.1.1)과 비교합니다. 불일치하면 `NotSupportedException`을 던지며, 메이저 버전이 같거나 ABI 값이 더 크다는 이유만으로 허용하지 않습니다. ABI를 변경할 때 `NativeMethods.AbiVersion`, 네이티브 헤더, 공개 API 계약과 패키지 문서를 함께 갱신하십시오.

## session과 handle

`VeroScriptSession`은 native session handle과 registered managed root를 소유하며 idle/running/creator-thread rule을 따릅니다. `VeroScriptValue`는 owning value나 `BorrowScope` 내부 borrowed view입니다. `VeroScriptPackage`는 package handle/metadata/canonical signed byte/loadable-module state를 소유합니다. `VeroScriptCallable` owner-thread mode의 active invocation dispose는 native release까지 defer되지만 cross-thread invocation을 허용하지 않습니다.

## marshaling

adapter는 fixed-width integer, float, bool, UTF-8/wide string, enum, host object, buffer, array/map/set/queue/stack/deque/channel을 marshal합니다. borrowed view는 owner release 전에 copy하고 managed exception은 native callback boundary에서 ABI error channel로 바꾸며 unmanaged C callback frame을 넘지 않습니다.

## 구성, 취소와 JIT

`VeroScriptConfiguration`에는 overlay/import와 `MaxCallDepth`, `MaxInstructions`, `TimeoutMilliseconds`, `EnableExecutionControl`, `CapabilityPolicyEnabled`, `AllowedScriptCapabilities`, `ExecutionBackend`가 있습니다. instruction/timeout zero는 unlimited이며 VM이 capability restriction을 JIT entry 전에 강제합니다. `RequestCancellation`은 non-owner thread에서 가능한 cooperative request로 active callback을 중단하지 않습니다. `SetJitPolicy`는 reusable state를 invalidate하고 `GetLastJitStats`는 last run delta를 반환합니다. eligible `CallFunction`은 fresh VM 간 reuse하며 `VeroScriptJitPolicy.ReuseSessionCode`를 false로 설정해 opt out합니다. 재사용에는 VM이 아닌 유효 백엔드와 리비전이 있는 네이티브 바인딩 카탈로그가 필요합니다. 바인딩, 관찰 콜백 또는 프로그램 세대가 바뀌면 캐시가 무효화됩니다. 실행 제어가 활성화되어도 요구 사항이 일치하는 cache context는 재사용할 수 있습니다. compile-attempt/cached-entry budget이 0이면 해당 횟수는 무제한이지만 native-code/payload의 바이트 한도는 계속 적용됩니다.

실행 제어가 활성화된 `Mixed`는 기본적으로 VM을 사용합니다. 대상 부하를 측정한 뒤 `VeroScriptJitPolicy.EnableControlledMixed`로 지원되는 제어형 JIT를 명시적으로 활성화하십시오. 명시적인 `Jit` 선택은 이 스위치와 독립적입니다. 미지원 또는 비용 모델이 VM을 선호하는 진입점은 제한을 유지하는 VM으로 fallback합니다. backend의 `ControlledExecution`은 지원 여부이며, `ControlledNativeExecutions`와 `AotExecutions`는 실제 제어형 네이티브 및 AOT 진입점 호출 횟수입니다.

`SetJitCacheLimits`/`GetJitCacheLimits`의 native/payload 기본값은 8 MiB/32 MiB, zero는 retention 금지입니다. `GetJitCacheStats`는 cumulative compile/settled byte/limit latch로 live/process peak이 아닙니다. `VeroScriptCapabilities.SessionJitCache`가 없으면 `NotSupportedException`이 발생합니다. 유지할 상태는 VM으로 초기화하고 같은 세대의 지원되는 호출은 컴파일된 코드를 재사용할 수 있습니다. `VeroScriptCapabilities.ReloadStatus`가 있으면 생성 스레드의 `GetReloadStatus()`가 작업과 세대의 스냅숏을 반환합니다.

## 세션 연산

session은 `LoadSource`, `LoadFile`, `LoadPackage`, `Reload`, `ReloadPackage`, `BuildPackageFile`, `Run`, diagnostics, I/O, `CallFunction`, `CallMethod`, `CallMethodBypassingInterceptor`, `Construct`, field/global access, `ApplyClassDefaults`를 제공합니다. bypass는 entry interceptor만 건너뛰고 nested call은 intercept됩니다. host function/annotation/type/member/protocol/alias/generic/interceptor/mutation/serializer registration, reflection/binding JSON, container/channel/callable은 Interop borrow/ownership contract를 보존합니다.

## diagnostics와 managed lifetime

`GetDiagnostics`, `GetDiagnosticArguments`, `FormatDiagnostic`은 ID/file label/source coordinate/argument를 보존합니다. `VeroScriptRunResult`는 success/exit state/code/diagnostic count를 보고하며 printed message가 대체하지 않습니다. borrowed value는 scope 종료에 만료되고 callback exception은 native enumeration 후 재발생하며 buffer는 lease 전 copy합니다.

## JIT 통계 매핑

`VeroScriptJitStats`는 scalar fast path, trampoline entry, `DirectNativeCalls`를 분리합니다. `CostModelVmFallbacks`는 유효한 compiled path가 VM보다 비용이 클 것으로 예측되어 선택된 `Fallbacks` 부분 집합입니다. `NativeTypedLoopExecutions`는 source-visible runtime failure를 보고하고 끝나는 것을 포함한 kernel invocation을 세며 success count가 아닙니다. 각각 `VeroScriptJitBackendCapabilities.NativeCode`와 `VeroScriptJitBackendCapabilities.NativeTypedLoops`로 확인해야 하며 어느 쪽도 `HostPlatform`에서 추정할 수 없습니다. `VeroScriptJitCacheStats`는 policy/limit/backend/module/host-observability invalidation 전 cumulative context를 나타냅니다. weak-`this` lifetime, unsupported/guarded VM fallback도 이 계약을 바꾸지 않습니다.

[C# 세션과 런타임 — 명시적 JIT 선택](../../User/ko-KR/session.md#jit-backend-capabilities)을 참조하세요.

[C# 세션과 런타임 — 명시적 JIT 선택](../../User/ko-KR/session.md#diagnostic-translation)을 참조하세요.

## 유지된 callable 해제

[C# 세션과 런타임 — 유지된 callable 해제](../../User/ko-KR/session.md#callable-disposal)을 참조하세요.

네이티브 호출은 실행과 네이티브 뷰 복사가 끝날 때까지 값과 패키지의 관리 소유자를 유지합니다. 블로킹 채널 작업과 map/set 방문자는 수신 객체를 유지하며 인수 및 요소 목록은 인덱서가 생성한 각 값 래퍼를 네이티브 사용이 끝날 때까지 유지합니다. GC의 조기 종료 처리를 방지하지만 명시적 해제와 콜백 범위 규칙은 계속 적용됩니다. 회귀 검증은 네이티브 수신, 방문자 및 요청 시 생성되는 목록의 마샬링 중 GC를 강제하며 최적화된 .NET 8/9 실행과 NativeAOT 패키지 소비자에서 확인합니다.

<a id="managed-marshalling-maintenance"></a>

네이티브 인수와 요소를 마샬링할 때 핸들을 제공한 값 래퍼의 참조 스냅샷을 유지합니다. 목록 인덱서나 관리 콜백이 입력 배열 요소를 교체해도 원래 래퍼는 네이티브 사용이 끝날 때까지 살아 있고 `ref`/`out` 쓰기 되돌림은 원래 래퍼를 갱신합니다. 회귀 테스트는 map 생성, 직접 함수 호출 및 유지된 callable 호출 중 요소를 교체한 뒤 GC를 강제합니다.

[Android 및 iOS / MAUI](../../User/ko-KR/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

모바일 패키징도 같은 프로젝트와 패키지 이름을 사용합니다. `VeroScriptIncludeMobile=true`는 .NET 8/9 및 .NET 10 Android/iOS 대상과 검증된 네이티브 SDK를 포함합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/runtime-and-interop.md) · [fr-FR](../fr-FR/runtime-and-interop.md) · [ja-JP](../ja-JP/runtime-and-interop.md) · [ko-KR](runtime-and-interop.md) · [zh-CN](../zh-CN/runtime-and-interop.md) · [zh-TW](../zh-TW/runtime-and-interop.md)
