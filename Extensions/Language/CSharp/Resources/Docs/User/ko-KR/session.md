# C# 세션과 런타임

## 실행 모드

`Run(VeroScriptRunMode.Script)`는 모듈을 초기화하고 `main`을 호출합니다.
`Eval`은 증분 평가를 유지합니다. `EvalOnce`는 `main` 없이 초기화만 실행하며
선택한 JIT 백엔드의 초기화 실행을 허용합니다. 일반 세션은 실행할 때마다 새 상태를
초기화합니다. 핫 리로드 세션은 초기화된 전역 상태를 유지하고 그 상태의 초기화에는 VM을 사용합니다.
이 모드는 `VeroScriptCapabilities.EvalOnce`가 필요합니다. 이전 런타임에서는 실행 전에
`NotSupportedException`이 발생합니다. 실행 정책과 JIT 백엔드는 별도로 설정합니다.

## typed-loop 통계

`NativeTypedLoopExecutions`는 native typed-loop kernel invocation을 셉니다. source-visible runtime failure를 보고하고 끝난 invocation도 포함합니다. 이는 execution observability counter이며 success counter가 아닙니다.

## 네이티브 JIT 기능과 카운터

`NativeLeafExecutions`, `NativeCfgExecutions`, `BridgeExecutions`, `LinearExecutions`,
`VmTrampolineExecutions`는 마지막 실행에서 일반 실행 진입점에 실제로 진입한 횟수이며
오류나 제어 이전으로 끝나는 진입도 포함합니다. 대응하는 `*Entries`는 생성된 코드의 수입니다.
웜 캐시는 컴파일 없이 실행할 수 있고 `GetJitCacheStats().CompileAttempts`는 유지된 캐시
컨텍스트의 누적값을 유지합니다. `ScalarNativeFastPathCalls`는 호스트 네이티브 호출 수입니다.
직접 호출과 타입 루프 커널은 중첩 이벤트이므로 일반 진입 횟수에 더하지 마십시오.
호환되는 이전 런타임이 제공하지 않는 카운터는 0입니다. 이 통계는 집계값이며 함수별 선택적
JIT 진단은 네이티브 Core/C++ API에 속합니다.

`CostModelVmFallbacks`는 유효한 compiled path가 VM보다 비용이 클 것으로
예측되어 선택된 `Fallbacks`의 부분 집합입니다. 이는 컴파일 실패나 스크립트
의미 변경이 아닙니다. `NativeTypedLoopExecutions`는 native backend의 typed-loop
kernel invocation을 세며 source-visible runtime failure를 보고한 invocation도
포함합니다. success counter가 아닙니다. `session.GetJitBackendInfo().CapabilityFlags`의
`VeroScriptJitBackendCapabilities.NativeCode`와
`VeroScriptJitBackendCapabilities.NativeTypedLoops`를 각각 확인하십시오. 어느 기능도
`HostPlatform`이나 0이 아닌 카운터로 추정해서는 안 됩니다.

## 배포와 런타임 메타데이터

`VeroScript.CSharp` 패키지를 참조하고 프로세스의 운영 체제와 아키텍처에 맞는
`VeroScriptInterop` 네이티브 라이브러리를 배포하십시오. 관리 패키지는 .NET 8과
.NET 9 자산을 제공하지만 .NET 런타임이나 네이티브 RID 자산을 설치하지 않습니다.
안전한 로더 디렉터리에 라이브러리가 없으면 `VEROSCRIPT_INTEROP_LIBRARY`에
절대 경로를 지정하십시오. 상대 경로는 거부됩니다.
첫 SDK 호출 전에 경로를 설정하십시오. 이후 호출은 선택된 네이티브 라이브러리를 사용합니다.

`VeroScriptRuntime`은 `AbiVersion`, `RuntimeVersion`, `HostPlatform`,
`Capabilities`, `CurrentLanguage`와 사용 가능한 언어 JSON을 제공합니다.
호환성은 이 메타데이터로 확인하고 플랫폼에서 의미를 추측하지 마십시오.
진단 코드와 인자는 표시 언어가 바뀌어도 안정적입니다.

`VeroScriptSession`은 구성, 소스, 패키지, 호스트 바인딩, 값과 진단을 관리합니다. 생성 스레드에 종속되며 `Dispose`로 종료해야 합니다.

`VeroScriptConfiguration`의 주요 속성은 `ImportPaths`, `ImplicitImportPaths`, `ScriptArguments`, `Defines`, `PredefinedMacros`, `StandardLibrarySearchPaths`, `SourceOverlays`, `BytecodeCacheDirectory`, `UseBytecodeCache`, `MaxCallDepth`, `MaxInstructions`, `TimeoutMilliseconds`, `EnableExecutionControl`, `CapabilityPolicyEnabled`, `AllowedScriptCapabilities`, `ExecutionBackend`, `AllowCircularImports`, `AllowProjectWideAccess`입니다.

`LoadSource`, `LoadFile`, `LoadPackage`, `Reload`, `ReloadPackage`, `BuildPackageFile`, `Clear`로 콘텐츠를 관리하고 `Run`, `CallFunction`, `CallMethod`, `Construct`, `GetField`, `SetField`, `GetGlobal`, `SetGlobal`로 실행하거나 접근합니다.

`SetInput`은 다음 실행에서 표준 라이브러리 입력 함수가 사용할 버퍼를 제공합니다.

선택적 호출 전에는 `HasFunction` 및 `HasMethod`를 사용합니다. 상태를 보존하는 소스 핫 리로드는 `Reload` 전에 명시적으로 활성화합니다.

```CSharp
session.HotReloadEnabled = true;
var reloaded = session.Reload();
```

`Reload`, `ReloadPackage` 또는 교체 프로그램을 초기화하는 실행 뒤에 생성 스레드에서 `GetReloadStatus()`를 호출합니다. `Pending`은 초기화 대기, `RolledBack`은 이전 세대로 복구, `AppliedWithErrors`는 커밋 후 오류를 뜻합니다. `CommittedGeneration`과 `OperationId`로 결과를 확인합니다. `ResetRequired`이면 `Clear`를 호출하고 프로그램을 다시 로드합니다. 세션을 비울 수 없다면 다시 생성합니다. 이 조회는 진단을 유지하며 `VeroScriptCapabilities.ReloadStatus`가 필요합니다.

백엔드 확인자가 `ApplyConfiguration`을 거부하면 네이티브 원인이 포함된 `VeroScriptException`이 발생하고 이전 취소 정책이 유지됩니다. `HotReloadEnabled` 변경이 거부되어도 예외가 발생합니다. `GetReloadStatus()`로 상태를 확인하고 오류에 제시된 복구 절차를 따르십시오. 취소 요청이 실패하면 취소 작업 자체의 네이티브 상태를 예외로 보고합니다. 실행 제어가 비활성화된 경우 이전 네이티브 오류는 생성 스레드에서 계속 조회할 수 있습니다.

`VeroScriptRunResult`는 `Success`, `ExitRequested`, `ExitCode`와 진단 수를 분리합니다. `GetDiagnostics`, `GetDiagnosticArguments`, `FormatDiagnostic`으로 구조화된 진단을 사용하십시오. `SetJitPolicy`와 `GetLastJitStats`로 hot threshold, 시도, 캐시와 예산을 관리합니다. 통계의 `DirectNativeCalls`는 VM 값 브리지를 거치지 않고 다른 네이티브 진입점에 도달한 세대 범위 JIT 호출 수입니다. 세션 재사용 코드 캐시는 bytecode cache와 별개입니다. `VeroScriptRuntime.Capabilities`에 `SessionJitCache`가 있으면 `SetJitCacheLimits`, `GetJitCacheLimits`, `GetJitCacheStats`로 조건을 충족하는 실행 사이에 보존할 컴파일 코드를 제어합니다. `VeroScriptJitPolicy.ReuseSessionCode`는 기본 `true`입니다. `MaxCompileAttempts` 또는 `MaxCachedEntries`가 0이면 해당 횟수는 무제한이지만 보존 native code와 전체 payload의 바이트 한도는 계속 적용됩니다. 재사용에는 VM이 아닌 유효 백엔드와 네이티브 함수 바인딩의 안정된 리비전이 필요합니다. 바인딩이나 콜백이 바뀌면 캐시가 무효화됩니다. 실행 제어가 활성화되어도 요구 사항이 일치하는 cache context는 재사용할 수 있습니다. 기본 한도는 native code 8 MiB와 전체 retained payload 32 MiB이고, 0은 해당 리소스의 보존을 막습니다. 통계는 현재 cache context의 안정화된 누적값이며 실시간 또는 프로세스 전체 메모리 카운터가 아닙니다. 정책, 한도, backend, configuration, module generation, binding, hot reload/observability 상태를 바꾸면 context가 무효화되며, 한도 초과는 의미를 바꾸지 않고 안전하게 fallback합니다.

`MaxInstructions`와 `TimeoutMilliseconds`는 실행별 협력적 제한입니다. `0`은 제한 없음을 의미합니다. VM과 지원되는 제어형 JIT 경로는 동일한 명령 계수, 실행 제한, 취소 및 capability 제한을 유지합니다. 제한이나 취소 요청은 이미 실행 중인 네이티브 또는 관리 호스트 콜백을 강제로 종료하지 않습니다.

실행 제어가 활성화된 `Mixed`는 기본적으로 VM을 사용합니다. 대상 부하를 측정한 뒤 `VeroScriptJitPolicy.EnableControlledMixed`로 지원되는 제어형 JIT를 명시적으로 활성화하십시오. 명시적인 `Jit` 선택은 이 스위치와 독립적입니다. 미지원 또는 비용 모델이 VM을 선호하는 진입점은 제한을 유지하는 VM으로 fallback합니다. backend의 `ControlledExecution`은 지원 여부이며, `ControlledNativeExecutions`와 `AotExecutions`는 실제 제어형 네이티브 및 AOT 진입점 호출 횟수입니다.

`EnableExecutionControl`이 true이면 `RequestCancellation()`을 호출하여 현재 실행의 취소를 요청할 수 있습니다. 이 메서드는 스레드 안전하며 생성 스레드가 아닌 곳에서 호출할 수 있는 유일한 세션 작업입니다. 호출이 반환될 때까지 세션을 유지해야 하며 취소는 실행 안전 지점에서 협력적으로 관찰됩니다.
구성을 성공적으로 다시 적용하면 세션의 취소 토큰이 초기화되므로 취소 후 다음 실행을 준비할 수 있습니다.

`CapabilityPolicyEnabled`가 true이면 표준 라이브러리 부작용은 `AllowedScriptCapabilities`의 플래그로 제한됩니다. 대상은 `FileRead`, `FileWrite`, `FileDelete`, `EnvironmentRead`, `EnvironmentWrite`, `ConsoleInput`, `ConsoleOutput`, `TaskSpawn`, `ProcessDirectoryChange`입니다. `chdir`에는 `EnvironmentWrite`와 `ProcessDirectoryChange`가 모두 필요합니다. 정책은 내장 함수에만 적용되며 호스트 콜백의 권한 검사는 호스트가 담당합니다. 비활성화하면 기본적으로 모두 허용됩니다.

## 실행 결과

`VeroScriptRunResult.Success`는 컴파일 또는 실행의 성공 여부를 나타냅니다. 스크립트가 반환한 0이 아닌 종료 코드도 성공입니다. 컴파일과 스크립트 실행 실패는 일반적으로 `Success == false`로 반환됩니다. 네이티브 소유권 거부도 구조화된 진단과 함께 실패 결과로 반환될 수 있으므로 결과와 `GetDiagnostics()`를 확인하세요. 다른 네이티브 상태 오류는 `VeroScriptException`을, 인수·수명·ABI 호환성의 관리 검사는 각각에 해당하는 관리 예외를 발생시킵니다.

## 백엔드와 콜백

`Vm`은 인터프리터만 사용합니다. 기본값인 `Mixed`는 지원되는 함수에 JIT를 사용하고 나머지는 VM으로 fallback합니다. `Jit`은 eager compilation을 요청하지만 VM fallback을 금지하지 않습니다. 디버거가 연결되면 VM을 사용합니다. 핫 리로드로 유지할 상태는 VM으로 초기화하며 지원되는 호출은 선택한 JIT 백엔드로 실행할 수 있습니다.

세션은 생성 스레드에 종속됩니다. 스크립트 콜백은 VM 작업자 스레드에서 동시에 호출될 수 있으므로 공유 관리 상태를 동기화하십시오. 출력, 진단, mutation 콜백의 예외는 내부에서 처리됩니다. 호스트 함수, 인터셉터, 제네릭 팩토리, 직렬화기 예외는 콜백 실패로 변환됩니다.

## 구성 속성 요약

| 속성 | 역할 |
| --- | --- |
| `ImportPaths`, `ImplicitImportPaths` | import 검색 경로와 암시적 import 경로. |
| `ScriptArguments` | `main(string[] args)`에 전달할 인수. |
| `Defines`, `PredefinedMacros` | 매크로와 조건부 컴파일 정의. |
| `StandardLibrarySearchPaths`, `SourceOverlays` | 표준 라이브러리 경로와 우선 적용되는 메모리 소스. |
| `BytecodeCacheDirectory`, `UseBytecodeCache` | bytecode cache 경로와 활성화 여부. |
| `MaxCallDepth` | 재귀 깊이 제한. 0은 런타임 기본값을 사용합니다. |
| `MaxInstructions`, `TimeoutMilliseconds` | 실행별 협력적 제한. 0은 무제한입니다. |
| `EnableExecutionControl` | 협력적 취소를 활성화합니다. |
| `CapabilityPolicyEnabled`, `AllowedScriptCapabilities` | 표준 라이브러리 부작용 정책과 허용 권한. |
| `ExecutionBackend` | `Vm`, `Mixed`, `Jit` 선택. |
| `AllowCircularImports`, `AllowProjectWideAccess` | 순환 import 및 프로젝트 전체 접근 규칙. |

## 독립적인 VM 선택

표준 Interop 제품은 바이트코드 VM을 명시적으로 포함합니다. VM 선택은 `ExecutionBackend` 및
JIT 선택과 독립적입니다. 유휴 상태의 생성 스레드에서 `SetVmBackend(path)`를 호출하여 호환
라이브러리 파일이나 디렉터리를 선택합니다. null 또는 빈 문자열은 선택을 지우며 실패한 교체는
기존 선택을 보존합니다. `GetVmBackendInfo()`는 식별, 버전, 대상, 경로, 원시 VM 기능 및 선택/고정
상태를 복사합니다. 정적 선택의 경로는 비어 있습니다. VM 기능 비트는 바이트코드(1), 실행 제어(2),
디버그(4), 핫 리로드(8), 정리(16)를 뜻하며 프로세스 기능 플래그와 별개입니다.

`VeroScriptCapabilities.VmBackendSelection`을 확인하세요. 같은 ABI의 이전 런타임에 기능이
없으면 선택적 내보내기를 호출하기 전에 `NotSupportedException`이 발생합니다. 실행이 확정되면
JIT 및 컴파일 확장도 VM 도메인을 고정합니다. `Clear`와 다시 로드는 고정을 해제하지 않으므로
고정된 선택을 바꾸려면 새 세션을 만드세요. 선택을 지워도 제품 VM이 복원되지 않으며 실행에 필요한
인터프리터가 없어질 수 있습니다. 복사된 관리 메타데이터는 교체나 해제 후에도 읽을 수 있습니다.

## 명시적 JIT 선택

실행 정책과 backend 선택은 별개입니다. 설치된 모듈 환경이 없으면 Runtime에는 기본 모듈이 없으며 미선택 mixed는 VM, 강제 jit는 실행 전 실패입니다. 소유 스레드에서 Session이 유휴 상태일 때 `VeroScriptSession.SetJitBackend(path)`로 별도 설치한 호환 파일/디렉터리를 선택합니다. `SetJitBackend(null)` / `SetJitBackend("")`은 선택을 해제합니다. 교체 실패는 이전 선택을 유지합니다. `GetJitBackend()` / `GetJitBackendInfo()`는 실제 식별/네이티브 기능이며 `backend-services`은 Core 서비스 계약일 뿐입니다. 실행 제어에는 위의 제어형 JIT 정책이 적용됩니다. 검색/ABI 규칙은 JIT 가이드를 참조하세요.

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/ko-KR/index.md)

```CSharp
using var session = new VeroScriptSession();
session.SetJitBackend("/absolute/path/to/Jit");
session.LoadSource("int main(){ return 0; }");
var result = session.Run();
```

<a id="jit-backend-capabilities"></a>

선택한 backend 기능은 `session.GetJitBackendInfo().CapabilityFlags`와 `VeroScriptJitBackendCapabilities`로 확인합니다. `Capabilities`는 알 수 없는 비트를 포함한 원래 네이티브 마스크를 보존합니다. 스냅샷은 문자열을 소유하므로 backend 교체나 session 해제 후에도 읽을 수 있습니다. backend를 선택하지 않으면 플래그는 `None`입니다. `VeroScriptRuntime.Capabilities`는 Core 서비스를 나타내며 선택한 backend의 네이티브 기능을 나타내지 않습니다.

<a id="diagnostic-translation"></a>

`VeroScriptRuntime.Translate(englishMessage)`는 프로세스 전체의 현재 언어와 카탈로그로 진단 메시지를 번역합니다. 포함된 NUL 문자를 비롯한 알 수 없는 메시지는 그대로 반환하며, 빈 입력은 빈 문자열을 반환합니다. null 입력과 잘못된 UTF-16은 거부됩니다. 바인딩은 `vs_interop_locale_translate`가 반환한 모듈 소유 UTF-8 버퍼를 관리 문자열로 복사하고, 성공 또는 실패 시 `vs_interop_buffer_destroy`로 해제합니다.

## 유지된 callable 해제

<a id="callable-disposal"></a>

`VeroScriptCallable.Dispose()`는 관리 래퍼를 즉시 무효화합니다. 호출 중이면 마지막 활성 호출이 반환할 때까지 네이티브 파괴를 연기합니다. 해제는 네이티브 실행이 끝나기를 기다리지 않으므로 콜백은 다른 스레드가 이 래퍼를 해제하기를 기다릴 수 있습니다. 같은 래퍼의 호출은 계속 직렬화됩니다. 이 수명 규칙은 소유 스레드 검사나 호스트가 세션 접근을 외부에서 직렬화해야 한다는 요구 사항을 변경하지 않습니다.

## 어노테이션 서비스와 진단 심각도

`VeroScriptRuntime.CompileCapabilities`는 실행 및 JIT 기능과 독립적으로 컴파일 서비스를 보고합니다. `AnnotationProcessing`이 있으면 네이티브 확장이 제공하는 유효한 불투명 서비스 핸들을 `SetAnnotationProcessingService(nint)`에 전달할 수 있습니다. 세션이 유휴 상태일 때 생성 스레드에서 설정하거나 해제하며, `0`은 해제를 뜻합니다. 세션이 설정을 복사하므로 연결 성공 후 호출자가 자신의 핸들을 해제할 수 있습니다. `ApplyConfiguration`은 서비스를 유지합니다. 프로세서 코드와 콜백 데이터는 마지막 사용이 끝날 때까지 유효해야 합니다. 같은 ABI의 이전 런타임은 `None`을 반환하고 이 작업에 `NotSupportedException`을 발생시킵니다.

`GetDiagnostics()`는 `Severity`의 `Error`, `Warning`, `Information`, `Hint`를 유지합니다. 진단 수가 0이 아니어도 컴파일 실패를 뜻하지 않습니다. 프로세서 경고, 정보 및 힌트는 `Success == true`와 함께 나타날 수 있습니다. 심각도 조회가 없는 이전 런타임의 진단도 읽을 수 있으며 `Error`로 반환됩니다.

## 호스트 콘솔 콜백

`VeroScriptSessionOptions.ConsoleQuery`는 `VeroScriptConsoleInfo`로 출력 터미널, 색상/커서/화면 지우기 지원과 표시 열/행 수를 반환합니다. 양수가 아닌 크기는 알 수 없음을 뜻합니다. 제어에는 조회와 `ConsoleControl` 모두 필요합니다. `ConsoleControl`은 `VeroScriptConsoleOperation`과 정수 두 개를 받습니다. ANSI 전경/배경 색상은 `0..15`, 기본값은 `-1`, 행 지우기 after/before/all은 `0/1/2`, 열/행은 0부터 시작하며 커서 표시 `0/1`은 첫 번째 인수입니다. 사용하지 않는 인수는 0입니다.

`ConsoleWrite`는 `std::console::write`의 정확한 텍스트를 받고 내장 NUL을 보존하며 줄바꿈을 추가하지 않습니다. 생략하면 `Print`의 버퍼 출력을 유지합니다. 동기 작업이 성공한 뒤에만 `true`를 반환하세요. 조회 예외는 기능 없음, 제어 예외는 실패를 뜻하며 쓰기 콜백의 `false` 또는 예외는 스크립트 실행을 실패시킵니다.

콜백은 세션 삭제가 끝날 때까지 참조되며 VM 작업 스레드에서 동시에 실행될 수 있으므로 공유 상태를 동기화해야 합니다. 실행 경계에서는 실패나 취소 후에도 스타일 초기화와 커서 표시가 수행될 수 있습니다. 콜백에서 세션을 삭제하거나 일반 세션 작업에 재진입하지 마세요. 터미널 입력은 출력 기능과 독립적입니다. 새 콜백에는 대응하는 네이티브 런타임이 필요합니다. 같은 ABI의 이전 런타임은 행 출력을 유지하지만 추가된 콘솔 콜백은 무시합니다.

C# SDK로 로드한 스크립트는 실행 가능한 리플렉션과 함수 시그니처에서 기본 형식 별칭을 사용할 수 있습니다. 중첩 값 컨테이너와 참조 저장소도 포함되며, 예를 들어 `array<long>`을 `array<int64>`로 요청할 수 있습니다. 부호, 정수 너비, 개체 형식 또는 참조 읽기 전용 검사를 완화하지 않습니다. 소스와 직렬화된 패키지는 같은 Core 검사를 사용합니다.

호스트 프로토콜 콜백(`Clone`, `Equals`, `Hash`, 하위 클래스 팩터리)은 프로토콜을 보유한 마지막 네이티브 값이 해제될 때까지 참조됩니다. 세션 삭제 후 호출되거나 다른 스레드에서 최종 해제될 수도 있습니다. 삭제된 세션에 접근하지 마세요. 관리 콜백에는 선택적 네이티브 보유 진입점이 필요하며 같은 ABI의 이전 런타임은 등록 전에 `NotSupportedException`을 발생시킵니다. 등록 후 보유에 실패하면 데이터를 보호하기 위해 세션을 닫습니다. 제네릭 팩터리 롤백은 이전된 참조 또는 관리 참조를 한 번만 해제합니다.

## 일반 컴파일 서비스와 모듈 환경

`SetCompileProcessingService(nint)`는 네이티브 SDK의 컴파일 파이프라인을 연결하며 `CompileCapabilities.GeneralProcessing`이 필요합니다. `0`은 서비스를 해제합니다. 유효한 불투명 핸들을 전달하고 유휴 세션의 생성 스레드에서 호출하세요. 세션이 구성을 복사한 후 호출자는 원래 핸들을 해제할 수 있습니다. 일반 `ApplyConfiguration`은 서비스를 유지합니다. 프로세서 코드와 콜백 데이터는 마지막 사용까지 유효해야 합니다.

`ApplyConfiguration(configuration, moduleEnvironment)`는 변경 불가능한 네이티브 SDK 스냅샷을 명시적으로 선택하며 `Capabilities.ModuleEnvironment`가 필요합니다. `0`은 환경을 제거하고 단일 인수 오버로드는 현재 선택을 유지합니다. 유휴 세션의 생성 스레드에서 호출하세요. 성공 후 세션이 스냅샷을 유지하므로 호출자는 원래 핸들을 해제할 수 있습니다. 환경을 바꾸기 전에 `Clear()`를 호출하세요. 환경 소유 바인딩 카탈로그에는 로컬 등록 및 카탈로그 가져오기를 할 수 없으므로 네이티브 측에서 통합 스냅샷을 준비해야 합니다. 로컬 바인딩 충돌은 구성이나 취소 상태 변경 전에 거부됩니다. 공급자, 바인딩 및 프로세서의 네이티브 모듈은 마지막 사용까지 로드된 상태로 유지하세요. 지원하지 않는 이전 런타임은 `NotSupportedException`을 발생시킵니다.

## Session 백엔드 선택 인수

`VeroScriptSession.SetVmBackend` / `SetJitBackend`

이 Session 설정자는 호환 파일／디렉터리 또는 전달된 환경에 해석기가 있는 경우 설치된 백엔드 ID를 받습니다. `auto`는 환경 후보를 요청하며 VM에서는 보존된 제품 대체 VM도 사용할 수 있습니다. `none`이나 네이티브 빈 문자열은 선택을 비활성화하고 `null` / `""`도 빈 요청이 됩니다. 새 세션에서 백엔드 설정을 생략하면 환경 선택을 허용하며 명시적 해제와 다릅니다. 기존 세션을 재구성할 때 VM 선택을 생략하면 실행 도메인을 보존합니다. 명시적 선택 실패는 이전 백엔드를 보존합니다. 생성 스레드의 유휴 상태에서 설정하고 VM 변경은 실행 전에 하세요. Clear/reload로도 도메인 고정이 해제되지 않습니다. JIT 선택은 VM/Mixed/JIT 실행 정책과 독립적입니다.

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

`VeroScriptRuntime.CompileCapabilities`는 실행 기능과 별도의 `VeroScriptCompileCapabilities`를 사용합니다. 같은 ABI의 이전 런타임에 조회 기능이 없으면 `VeroScriptCompileCapabilities.None`을 반환합니다. `GetJitBackendInfo().CapabilityFlags`는 `DirectScalarCalls`를 포함한 `VeroScriptJitBackendCapabilities`를 사용하며 실제 네이티브 실행이 아닌 백엔드 지원을 나타냅니다. `VeroScriptDiagnostic.Severity`는 각 구조화된 진단을 분류합니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
