# 호스트 통합

VeroScript는 C++ 호스트에 내장될 수 있습니다. 호스트는 일반적으로 일회성 확인 또는 실행을 위해 `VeroScriptEngine`을 사용하고 장기 모듈, 바인딩, 런타임 상태 및 핫 리로드를 위해 `VeroScriptSession`을 사용합니다.

## 명시적인 VM 선택

Core는 암시적 인터프리터를 제공하지 않습니다. 아래 예제는 별도로 배포되는 정적 바이트코드 VM을 사용합니다. 두 SDK 패키지를 찾고 두 대상을 링크한 뒤 각 엔진 설정 또는 Session을 명시적으로 구성하세요. 링크만으로는 백엔드가 선택되지 않습니다.

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Runtime)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScriptCpp VeroScript::VeroScriptBytecodeVm)
```

동적으로 로드하려면 배포된 VM 파일이나 디렉터리를 `FEngineConfig::VmBackendPath` 또는 `Session::SetVmBackendPath`에 지정하세요. 팩토리와 경로는 동시에 지정할 수 없습니다. 실행이 시작되면 선택이 고정되며 `Clear()`도 이를 해제하지 않습니다. `ScriptCompileExtensions`에 자체 팩토리나 경로가 없으면 컴파일러 스크립트는 선택된 VM을 상속합니다. 컴파일러 스크립트를 실행하지 않는 일반 컴파일에는 VM이 필요하지 않습니다.

기존 Session에서 VM 팩터리와 경로가 없는 `FEngineConfig`는 현재 선택을 유지합니다. 고정 전에 명시적으로 지우려면 `bClearVmBackend=true`를 지정하며 팩터리, 경로, 지우기는 배타적입니다. `Clear()`는 로드한 프로그램을 초기화하지만 실행 후 VM 도메인 고정은 해제하지 않습니다. 이후 인터프리터 변경에는 새 Session을 만드세요.

## 상위 수준 엔진 진입점

SDK에서 제공하는 공개 `VeroScriptEngine.h` 헤더를 포함합니다. `VeroScriptEngine`은 소스 버퍼 및 파일 진입점을 제공합니다.

- `CheckSource` / `CheckFile`은 어휘, 구문 및 의미 검사를 수행합니다.
- `RunSource` / `RunFile` 스크립트를 로드하고 실행합니다.

이러한 진입점은 가져오기 경로, 스크립트 인수, 정의, 표준 라이브러리 검색 경로, 바이트코드 캐시 디렉터리, 실행 백엔드 및 바인딩 카탈로그를 받을 수 있습니다.

`FEngineConfig`은 구조화된 사전 정의 매크로, 암시적 가져오기 경로,
메모리 내 소스 오버레이, 최대 호출 깊이, 순환 가져오기 및
프로젝트 전체 액세스 스위치와 입력 스트림도 제공합니다. 임베디드 `RunFile`은
기본적으로 바이트코드 캐시를 사용하지 않습니다. 필요한 경우
`bUseBytecodeCache`와 `BytecodeCacheDirectory`로 명시적으로 활성화할 수 있습니다.
실행 백엔드는 `Vm`, `Mixed` 또는 `Jit`이며 기본값은 `Mixed`입니다.

바이트코드 캐시는 Core 구현과 공개 API의 식별 정보를 포함합니다.
제품 및 바이트코드 버전이 같아도 Core 코드가 변경되면 기존 캐시가 무효화됩니다.
같은 빌드를 반복 실행할 때는 캐시를 재사용합니다.

`ExecutionLimits.MaxInstructions`와 `ExecutionLimits.TimeoutMilliseconds`는 실행별 협력적 제한이며 `0`은 무제한입니다. 공유 `FExecutionControl`을 `ExecutionControl`에 설정하고 다른 스레드에서 `RequestCancel()`을 호출할 수 있습니다. Core는 해석 실행과 지원되는 네이티브 실행에 같은 명령 수, 취소 및 권한 검사를 적용합니다. 제어된 `Mixed`는 기본적으로 VM을 사용하며, Session의 `SetJitPolicy`에서 `FJitPolicy::bEnableControlledMixed=true`를 설정하면 동적 네이티브 실행을 활성화합니다. 명시적 `Jit`와 선택한 정적 Core AOT는 이 스위치와 독립적입니다. 필요한 제어 기능이 없는 코드는 네이티브 실행에 들어가기 전에 VM으로 돌아갑니다. 실행 중인 네이티브 콜백은 강제로 종료하지 않습니다. 제어 토큰은 자동 초기화되지 않으므로 재사용 전에 `Reset()`을 호출하세요.

`Capabilities`는 표준 라이브러리 부작용을 제어합니다. 파일 읽기/쓰기/삭제, 환경 읽기/쓰기, 콘솔 입력/출력 및 작업 생성을 포함합니다. 기본값은 모두 허용하며 제한은 내장 함수에만 적용됩니다. 등록된 콜백의 권한 검사는 호스트가 담당합니다.
`bAllowProcessDirectoryChange`는 `chdir`이 호스트 프로세스 전체의 작업 디렉터리를 변경하는 동작을 별도로 금지합니다. `chdir`에는 환경 쓰기 권한도 필요합니다.

진입점은 `FRunResult`을 반환합니다. `bSuccess`이 정상을 보고합니다.
컴파일/실행; 성공적인 `int main` 또는 `exit(n)`은 여전히
0이 아닌 `ExitCode`. `bExitRequested`는 명시적인 `exit(n)`에 의해서만 설정됩니다.
`exit(0)` 포함; 일반적인 `int main` 반환은 이를 설정하지 않습니다. `Error` 읽기
오류 세부정보는 `Diagnostics`을 참조하세요.

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptEngine.h"

#include <iostream>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	config.ScriptArgs = {"alpha", "beta"};

	const VeroScript::FRunResult result = VeroScript::VeroScriptEngine::RunFile(
		"main.vs",
		host,
		config);

	if (!result.bSuccess)
	{
		return result.ExitCode;
	}
	return result.ExitCode;
}
```

`ERunMode`은 백엔드와 독립적입니다. `Eval`은 증분/REPL 초기화 경로를
사용하고, `EvalOnce`는 소스를 한 번 평가하며, `Script`은 로드 후
`main`을 호출합니다. 디버거 실행과 상태 보존 핫 리로드를 사용하는 세션은
VM 경로를 강제합니다.

## 임베디드 VM 디버거

`VeroScriptDebugger`는 `VeroScriptVm`을 직접 소유하는 호스트를 위한 동기식
인프로세스 디버거입니다. DAP와는 별개이며 wire 연결을 열거나 JSON을 인코딩하지
않습니다. `VeroScriptCompile.h`, `VeroScriptDebugger.h`, `VeroScriptVm.h`를
포함하고 소스 또는 파일을 `FBytecodeModule`로 컴파일한 다음, 소유권을 이전하지
않는 디버거를 `SetDebugger`로 연결하고 모듈을 실행합니다.

바이트코드 인터프리터를 사용하려면 `VeroScriptBytecodeVmAssembly.h`를 포함하고
실행 전에 `VeroScript::BytecodeVm::Configure(vm)`를 호출하십시오.

`SetBreakpoint`의 소스 줄 번호는 1부터 시작합니다. 빈 파일 레이블은 모든 파일과
일치하고, 비어 있지 않은 레이블은 전체 레이블 또는 기본 이름과 일치합니다. 조건부
중단점의 식은 최상위 프레임에서 평가됩니다. 평가 오류와 거짓 조건은 중단을 건너뛰며,
부울 true, 0이 아닌 숫자 및 기타 비어 있지 않은 결과는 참으로 처리됩니다.

중단 콜백은 VM이 일시 중지된 동안 동기적으로 실행됩니다. `GetStackTrace`,
`GetLocals`, `GetChildren`으로 상태를 검사하고, `Evaluate`로 선택한 프레임의 식을
읽기 전용으로 평가하십시오. 디버거 변수에 값을 쓰려면 `SetLocal` 또는 `SetChild`를
사용합니다. 콜백에서 `SetRunMode`를 호출하여 `Continue`, `StepIn`, `StepOver`,
`StepOut` 중 하나를 선택합니다.

`RequestPause()`는 스레드 안전한 협력적 요청이며 VM 안전 지점에서 관찰되어
`EDebugStopReason::Pause`로 보고됩니다. 실행 중인 네이티브 콜백을 강제로 중단하지
않습니다. 이전 일시 중지 요청이 남아 있을 수 있는 디버거를 재사용하기 전에
`ResetPauseRequest()`를 호출하십시오. `SetDebugger`는 소유권을 이전하지 않으므로 VM이
실행되는 동안 디버거를 유지하고, 디버거를 사용하는 콜백 동안 VM도 유지해야 합니다.
연결된 동안 유효 백엔드는 `Vm`으로 고정되며 JIT 캐시가 무효화됩니다.

비동기 편집기 통합에는 [Tooling](../../../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/index.md) 및
[디버깅](../../../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/debugging.md)에 설명된 `VeroScriptDebugSession` 또는 DAP 어댑터를
사용하십시오.

## 진단 현지화

엔진 진입점은 핵심 현지화를 자동으로 초기화합니다. 우선
`VERO_LOCALIZATION_DIR`을 사용하고, [진단 현지화](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/localization.md)에 설명된
배포 검색 순서에 따라 `VERO_LANG` 또는 시스템 UI 언어에서 문화를 선택합니다.
영어 진단만 받기 위해 호스트가 별도의 초기화 호출을 할 필요는 없습니다.

명시적인 카탈로그 및 언어 제어를 위해서는 `VeroScript::Locale`을 직접 사용하십시오.

```cpp
#include "VeroScriptLocale.h"

#include <iostream>
#include <string>

int main()
{
	std::string error;
	if (!VeroScript::Locale::LoadCatalogDirectory("localization", &error))
	{
		std::cerr << error << '\n';
		return 1;
	}

	if (!VeroScript::Locale::TrySetLanguageFromTag("zh-CN"))
	{
		return 2;
	}

	const std::string language = VeroScript::Locale::GetLanguageTag();
	std::cout << language << '\n';
	return 0;
}
```

`LoadCatalogDirectory`는 프로세스 전체 카탈로그 트리를 교체합니다. 라이브러리나
표준 라이브러리 루트는 `MergeCatalogDirectory`를 사용하여 형제
`localization/` 트리를 추가할 수 있으며, 재정의를 요청하지 않는 한 기존
메시지 ID가 유지됩니다. 핵심 언어 및 카탈로그 작업은 스레드로부터 안전하지만
`VeroScriptSession` 자체는 내부적으로 스레드로부터 안전하지 않습니다. 공유 프로세스가
한 요청 중간에 표시 언어를 변경하지 않도록 호스트는 언어 변경이 허용되는 시점을
정해야 합니다. 메시지 ID, 자리 표시자 및 영어 대체 규칙은
[진단 현지화](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/localization.md)를 참조하세요.

## 오래 지속되는 세션

`VeroScriptSession`은 여러 실행에 걸쳐 스크립트를 소유하는 편집기, 게임 또는 호스트 도구용입니다. 세션은 다음을 수행할 수 있습니다.

- 모듈 로드, 실행, 다시 로드 및 지우기
- 기본 함수, 호스트 유형, 필드, 속성, 메서드, 연산자 및 일반 호스트 유형 템플릿을 등록합니다.
- 출력, 진단, 호출 인터셉터 및 변경 콜백을 설치합니다.
- VM, 혼합 또는 JIT 실행을 선택하고 바이트코드 캐싱을 구성합니다.
- 협력적 명령/시간 제한, 취소 및 표준 라이브러리 기능을 설정합니다.
- 상태 보존 핫 리로드를 명시적으로 선택합니다.

세션 호출은 내부적으로 동기화되지 않습니다. 호스트는 `Load`, `Run`, `Reload` 및 `Clear`에 대해 하나의 제어 스레드를 사용해야 하며 실행이 비활성화된 동안 다시 로드를 요청해야 합니다.

일반적인 장기 세션은 다음과 같습니다.

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptSession.h"

#include <iostream>
#include <string>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::VeroScriptSession session(host);

	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	std::string configError;
	if (!session.ApplyConfig(config, &configError))
	{
		std::cerr << configError << '\n';
		return 1;
	}

	const VeroScript::FRunResult load = session.LoadFile("main.vs");
	if (!load.bSuccess)
	{
		return load.ExitCode;
	}

	const VeroScript::FRunResult run = session.Run();
	return run.ExitCode;
}
```

<a id="apply-config-result"></a>

`ApplyConfig`는 구성이 거부되면 `false`를 반환하며 `OutError`로 이유를 제공할 수 있습니다. 로드나 실행 전에 결과를 확인하세요. 유효성 검사 거부는 구성 확정 전에 발생하지만 할당 또는 콜백 복사 예외는 일부 변경 후 전파될 수 있으므로 롤백을 보장하지 않습니다.

`IEngineHost`은 세션보다 오래 지속되어야 하며 `print` 및 진단 라인을 받습니다.
콜백. `RegisterNative`, `RegisterHostType`을 완료하고
컴파일러가 해결할 수 있도록 `Load` 이전에 `RegisterHostMember` 등록
호스트 선언. 레지스트리는 `Reload` 및 `Clear`을 유지합니다. 끝까지 읽어보세요
`GetBindingCatalog` 또는 IDE, LSP 및 `ExportBindingsToFile`를 사용하여 내보내십시오.
DAP 소비자.

세션은 `CompileFile`/`CommitCompiledFile`도 노출합니다.
`ReloadCompiledModule`, `LoadPackage` 및 `ReloadPackage`. 이를 통해 편집자는
패키지 로더 및 핫 업데이트 시스템은 컴파일과 커밋을 분리합니다.
신뢰 경계를 넘기 전에 패키지의 `CanonicalSignedBytes`를 확인하세요.

`BuildPackage`/`ReadPackage` 및 Session의 `LoadPackage`/`ReloadPackage`는 Core 이미지(`.vslib` 또는 원시 컴파일 앱 이미지)를 처리합니다. 표준 ZIP `.vsapp`는 외부 앱 컨테이너이므로 Application 서비스와 `LoadApplication`/`ReloadApplication`을 사용하세요. 원시 Core 이미지를 `.vsapp` 이름으로 저장해도 표준 컨테이너가 되지 않습니다.

## 바인딩 경계

스크립트는 호스트가 등록하고 컴파일러가 확인할 수 있는 호스트 함수 또는 멤버만 호출할 수 있습니다. 바인딩 설명은 호스트 유형, 컨테이너 요소 또는 키/값 유형 정보와 함께 매개변수 및 반환 정보를 제공해야 합니다. 중복된 이름, null 콜백 및 내장 기능과의 충돌은 거부됩니다.

호스트 구조체 값 복사에는 적절한 복제 콜백이 필요합니다. 호스트 개체 핸들은 호스트 등록에 의해 설정된 수명 및 소유권 규칙을 따릅니다. 완전한 바인딩 API는 공개 `VeroScriptCpp` 헤더에 의해 정의됩니다.

공개 호출 및 확장 기능에는 다음도 포함됩니다.

- `CallFunction`, `CallMethod`, `CallMethodBypassingInterceptor`, `ConstructObject`, `GetField`/`SetField` 및 전역 액세스
- 실패 또는 종료 시 다시 쓰기 없이 변경 가능한 `ref` 인수의 다시 쓰기
- 기본 콜백 이상의 스크립트 함수 값을 유지하기 위한 `RetainCallable`/`InvokeCallable`
- `SetInstanceCallInterceptor`, `SetMutationCallback` 및 `ApplyClassDefaults`;
- `RegisterGenericHostTypeTemplate`/`InstantiateGenericHostType`, 유형 별칭, 외부 직렬화 및 개체 참조 열거
- `SetJitPolicy`, `SetJitCacheLimits`, `GetJitCacheStats`, `GetLastJitStats`: 재사용에는 선택한 팩터리, VM이 아닌 유효 정책 및 `FJitPolicy::bReuseSessionCode`가 필요합니다. 대상 `Run`, `CallFunction`, `CallMethod`와 보관한 callable 호출은 세대 캐시와 컴파일 할당량을 공유합니다. `MaxCompileAttempts` 또는 `MaxCachedEntries`의 0은 무제한이며 비활성화가 아닙니다. 지속 실행과 안정된 콜백은 재사용할 수 있습니다. 네이티브 카탈로그는 `IHostBindingCatalog::GetRevision()`으로 런타임 리비전을 게시합니다. 백엔드가 필요한 실행 제어 기능을 갖추면 제어된 네이티브 실행도 코드를 재사용하며, Session은 VM 사이에 해당 제어 컨텍스트를 보존합니다. 구성, 정책, 백엔드, 모듈 또는 바인딩 변경은 보관 코드를 무효화하며 리로드 실패 시 이전 프로그램을 유지합니다. 기본값은 실행 코드 8 MiB, 보관 페이로드 32 MiB입니다. `GetJitCacheStats`는 확정된 세대 집계이고 `GetLastJitStats`는 최근 호출 정보이며 프로세스 전체 메모리 측정이 아닙니다.

지원되는 가로채기 메서드 본문도 백엔드 기능에 따라 실행합니다. `Proceed`의 중첩 호출, 참조 쓰기 반영 및 실행 제어를 유지합니다. 호스트가 위치를 지정하지 않은 가로채기 오류는 호출 지점을 사용하며 명시한 위치와 원래 본문 오류는 보존합니다.

`Proceed`의 원래 결과를 직접 전달하면 구조체, 네이티브 값 형식 및 고정 배열의 값 반환 복사 경계를 유지하며 복사 생성자를 중복 실행하지 않습니다. 대체하거나 빌려 온 결과에는 일반 형식 검사와 값 복사를 적용합니다. 참조를 반환하도록 선언된 메서드는 참조 의미를 유지합니다.

`VeroScriptSession`은 내부적으로 스레드 접근을 동기화하지 않습니다. 일반 작업과 최종 해제는 세션을 생성한 스레드에서 수행하세요. 호스트 콜백은 VM 작업자 스레드에서 실행될 수 있으므로 외부 상태 접근을 동기화해야 합니다. `RetainCallable`의 기본값은 `bOwnerThreadOnly = true`입니다. 호스트가 접근을 직렬화해도 다른 스레드에서 보존된 함수 값을 호출하면 실패합니다. 다른 스레드의 호출을 허용하려면 `RetainCallable`의 두 번째 인수에 명시적으로 `false`를 전달하고, 실행, 설정, 리로드 및 해제를 포함한 모든 세션 접근을 호스트에서 직렬화하세요.

스크립트 정리도 진행 중인 Session 작업입니다. 프로그램 지우기 또는 교체, 핫 리로드 비활성화, 보존된 callable 해제 및 Session 소멸은 스크립트 소멸자와 호스트 콜백을 호출할 수 있습니다. 해당 콜백에서 같은 Session을 실행하거나 변경하는 API에 재진입해서는 안 됩니다. 정리 중에는 `RetainCallable`도 새 루트를 거부합니다. 후속 작업은 정리가 반환된 후 실행하세요. 일반 실행 콜백에서는 여전히 callable을 보존할 수 있습니다.

## 네이티브 JIT 관측 값

`FJitStats::CostModelVmFallbacks`는 유효한 compiled path가 더 느릴 것으로
예측되어 의도적으로 VM에 남긴 `Fallbacks`의 부분 집합입니다. 컴파일 실패나
의미 변경이 아닙니다. `FJitStats::NativeTypedLoopExecutions`는 native backend의
typed-loop kernel invocation을 세며 source-visible runtime failure를 보고한 invocation도
포함합니다. success counter가 아닙니다. 두 값 모두 가장 최근 실행의 관측값이며,
0이라고 해서 target 또는 capability가 없다는 뜻은 아닙니다.

<a id="threads-and-hot-reload"></a>

## 스레드 및 핫 리로드

호스트는 VM 실행, 세션 제어 및 기본 콜백 간의 스레드 경계를 조정하는 역할을 담당합니다. 핫 리로드는 선택 사항입니다. 함수 식별 정보와 캡처 이름 및 타입이 호환되면 캡처한 `this`를 포함한 클로저를 마이그레이션할 수 있습니다. 캡처 추가·삭제·타입 변경, 함수를 다시 매핑할 수 없는 경우, 활성 채널 및 호환되지 않는 타입 변경은 거부됩니다. 호스트가 보관한 callable 핸들은 등록한 세대가 성공적으로 교체되면 무효화되지만, 정리 과정은 유효한 전역 값이나 값 루트와 공유하는 그래프를 보존합니다. 네이티브 부작용은 계속 스크립트 상태 트랜잭션 외부에 있습니다.

상태 보존 핫 리로드가 활성화되면 호환되는 전역, 객체 및 컨테이너 ID를
마이그레이션할 수 있으며, 추가된 필드에는 언어 기본값이 적용됩니다. 리로드는
커밋 및 초기화 전에 컴파일, 검증 및 마이그레이션을 수행하고, 실패하면 마지막으로
정상인 모듈을 계속 사용할 수 있습니다. 핫 리로드를 비활성화하거나 `Clear`를
호출하면 참조 주기를 포함한 보존 값이 정상적으로 해제됩니다. 핫 리로드는
바이트코드 캐시를 건너뛰므로 `Reload`는 현재 소스를 다시 컴파일합니다.

안정적인 다언어 C ABI는 [Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md)를 참조하세요. 브라우저 및
WebAssembly 메모리 내 진입점은 [WebAssembly/WASM SDK](../../../../../../Platform/WebAssembly/Resources/Docs/User/ko-KR/index.md)를 참조하세요.

## 기존 Session 디버깅

[기존 Session 디버깅](session-debugging.md). `SetDebugBridge`, `ISessionDebugBridge`, `SetDebugBridge(nullptr)`.

## 보관 바이트 할당량

`MaxNativeBytes` 또는 `MaxRetainedPayloadBytes`의 0은 해당 바이트 보관을 허용하지 않습니다. 무제한을 뜻하는 정책 예산 0과 다릅니다.

## 명시적인 JIT 백엔드 선택

설치된 모듈 환경이 없으면 Core와 C++ SDK는 구체적인 JIT 구현을 자동으로 선택하지 않습니다.
`VeroScriptJitBackendLoader.h`의 `LoadJitBackend`로 파일 또는 디렉터리를 로드한 뒤
공유 팩토리를 `FEngineConfig::JitBackendFactory`에 지정합니다. Session은
`SetJitBackendFactory` 또는 `SetJitBackendPath`를 사용할 수 있습니다. 빈 경로는
백엔드를 해제하며, 로드 실패 시 현재 백엔드와 캐시를 유지합니다.

스트림 `RunSource` / `RunFile` 오버로드는 JIT 팩토리 다음에 VM 팩토리를 받을 수 있으며 짧은 오버로드는 인터프리터를 제공하지 않습니다. 경로 또는 모듈 환경 기본값에는 `FStreamEngineHost`와 `FEngineConfig`를 사용하고 [서비스 백엔드 선택](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection)을 따르세요. `Vm`과 `Mixed`의 해석 실행에는 사용 가능한 VM 팩토리가 필요하며 명시적 `Jit`에는 JIT 팩토리도 필요합니다. 구성된 호스트는 환경에서 이를 결정하거나 명시적 팩토리를 받을 수 있습니다. 디버깅과 상태 보존 핫 리로드에는 적절한 VM이 필요합니다. 정적 Core AOT는 동적 JIT 팩토리 없이 `Mixed`를 사용할 수 있지만 해석 실행 초기화와 폴백에는 VM이 필요합니다.

디버그 브리지를 설치하면 Session이 보관한 JIT 코드를 해제합니다. 이후 브리지가
디버거를 반환하지 않으면 선택한 팩토리로 다음 호출을 다시 컴파일할 수 있습니다.
연결 해제는 스크립트를 취소하거나 Session을 파괴하지 않습니다. Core와 C++의 공개
레이아웃이 바뀌었으므로 대응하는 SDK로 네이티브 소비자를 다시 빌드해야 합니다.

## 협력적 취소

`ApplyConfig` 또는 `SetExecutionControl`로 공유 `FExecutionControl`을 설정합니다.
프로토콜 스레드는 `Session::RequestCancel()`을 호출할 수 있으며, 소유자가 실행하지 않는 동안
제어 객체를 교체할 때도 안전합니다. 이 호출은 현재 제어 객체의 공유 스냅샷을 취소합니다.
null이면 아무 작업도 하지 않고 다른 제어 객체로 취소를 전달하지 않습니다.
`GetExecutionControl()`은 소유자 스레드 전용의 빌린 참조를 반환합니다.
취소는 협력적이며 네이티브 콜백을 중단할 수 없습니다. 토큰을 다시 사용하기 전에 초기화하세요.
Session을 파괴하기 전에 취소 호출과 실행이 끝날 때까지 기다려야 합니다. 디버거 연결 해제는 별도 작업입니다.

같은 `FExecutionControl` 토큰을 공유하는 Session은 취소 상태도 공유합니다. `Reset()`과 재사용 전에 이전 취소 호출이 끝날 때까지 기다려, 오래된 요청이 토큰을 재사용하는 다음 실행을 취소하지 않도록 합니다.

## C++ 호스트의 메모리 관리

Core `Memory::*` API를 사용하려면 `VeroScriptMemory.h`를 포함합니다.
`FEngineConfig::MemoryLimits`는 설정을 받는 `RunSource`와 `RunFile`에 모두 적용됩니다.
`MaxBufferBytes`는 소유 버퍼 하나의 용량과 외부 영역의 길이를, `MaxRetainedBytes`는 소유 및 빌린 영역의 공유 바이트 예산을 제한합니다. 두 제한 모두 0은 무제한입니다.

Session은 실행 VM 사이에서 하나의 메모리 컨텍스트를 유지합니다. 소유자 스레드에서
`GetMemoryContext()`, `SetMemoryLimits()`, `GetMemoryStats()`를 사용합니다.
제한 변경은 실행이 유휴 상태일 때 가능하며, 거부된 축소는 기존 제한을 유지합니다.
`ApplyConfig`의 메모리 제한이 거부되면 다른 실행 설정도 유지됩니다.
작업 스레드는 예산을 공유하되 각자의 스레드 도메인을 사용합니다. 네이티브 콜백에서는 빌린
`FNativeCallContext::MemoryServices`로 현재 실행의 컨텍스트, 작업 검사, 바인딩 카탈로그와 인수 주소를 가져옵니다. 콜백이 끝난 뒤 이 서비스를 보관하지 마세요.
`Capabilities.bAllowMemoryAddress`는 주소 접근을 제어하며, 원시 주소는 바이트 수명을 연장하지 않습니다. 사용자 정의 네이티브 콜백에서 주소를 노출하기 전에 `MemoryAddressesAllowed()`를 확인해야 합니다. 호스트가 Core 함수를 직접 호출할 때의 권한 검사는 호스트의 책임입니다.

`Clear`와 다시 로드는 컨텍스트를 유지합니다. 호환되는 핫 리로드는 영역의 동일성을 보존하며 초기화 실패 시 이전 프로그램을 복구합니다.
Session 소멸 시 스크립트를 정리한 뒤 접근 도메인을 닫습니다.
빌린 컨텍스트를 직접 닫거나 Session 소멸 뒤 참조를 보관하지 마세요.
획득한 임대는 Session 소멸 후에도 `Memory::ReleaseLease`까지 바이트를 유지합니다.
임대 해제는 스레드 안전하지만 호스트는 해제 전에 네이티브 접근을 끝내거나 동기화해야 합니다.
외부 영역의 소유자와 해제 콜백 데이터는 모든 별칭과 임대보다 오래 살아 있어야 하며, 해제 콜백은 예외를 던지면 안 됩니다.

SDK `CppMinimal/memory.cpp` 예제와 Core
[메모리 참조](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md)를 참고하세요.
독립 C++ AOT 출력은 런타임 메모리 연산과 영역 타입을 지원하지 않아 생성기가 오류를 보고합니다. 사용하지 않는 `memory.vs` 또는 `core.vs` 가져오기는 지원됩니다.

`RetainCallable`에 `false`를 전달해도 Session 메모리 도메인은 이전되지 않습니다.
접근을 직렬화해도 영역 연산은 Session 소유자 스레드에서 수행해야 합니다. 다른 스레드에서 영역에 접근하는 callable은 `diag.memory_wrong_thread`로 실패합니다. 스크립트 작업 스레드는 각자의 메모리 도메인을 사용합니다.

## 모듈 공개 매크로

`Defines`와 `PredefinedMacros`는 각 모듈을 독립적으로 초기화합니다. Engine과 Session은 코어 모듈 로더를 사용합니다. 명시적 import는 `#define_export` 매크로를 가져오고 `export import`는 이를 다시 내보냅니다. 일반 `#define`은 비공개이며 `ImplicitImportPaths`는 선언만 가져옵니다. `SourceOverlays`에도 같은 규칙이 적용됩니다. [전처리기 참조](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/preprocessor.md)를 참고하세요.

매크로 의존성을 수정한 뒤에는 파일 Session에서 `Reload()`를 호출하고 다시 실행하여 초기화를 완료하거나 `LoadSource()`로 수정한 소스를 로드하세요. 핫 리로드가 활성화되면 `Reload()`는 호환되는 전역 상태를 이관합니다. `LoadSource()`는 모듈을 교체하고 상태를 초기화합니다. 공개 매크로 인터페이스가 바이트코드 레이아웃을 바꾸므로 배포하는 `.vslib`／`.vsapp`를 현재 코어로 다시 빌드하고 네이티브 소비 프로그램도 일치하는 Core 및 C++ SDK로 다시 컴파일해야 합니다.

## 컴파일 시점 어노테이션 처리

Engine과 Session은 `FEngineConfig::AnnotationProcessing`으로 설정합니다. Session은 `SetAnnotationProcessing`／`GetAnnotationProcessing`도 제공합니다. 레지스트리에 `Freeze()`를 호출하고 이후에는 변경하지 마세요. 팩터리는 선언 준비 작업마다 독립적인 처리기를 생성합니다. 네이티브 라이브러리와 콜백이 캡처한 데이터의 수명을 유지해야 합니다. C++ AOT는 `FCodegenOptions::AnnotationProcessing`으로 `FCppCodegenBackend`를 설정합니다.

처리기는 컴파일하는 스레드에서 동기적으로 실행됩니다. 준비 콜백에서 같은 Session을 실행하거나 변경하는 재진입 작업은 거부됩니다. 취소는 코어의 `FAnalysisCancelled` 예외로 전달됩니다. 취소나 컴파일 실패 시 현재 프로그램을 유지합니다. `FRunResult::Diagnostics`는 Warning／Information／Hint를 유지하며 오류 스트림에도 출력합니다. 치명적이지 않은 진단은 성공 결과를 실패로 바꾸지 않습니다. `RunSource`／`RunFile` 결과에는 컴파일과 실행 진단이 포함됩니다.

생성 결과에 영향을 주는 외부 입력은 `ReadDependency`로 읽으세요. 캐시가 적중해도 선언 준비를 먼저 수행하며 처리기 식별 정보, 설정, 생성 내용과 의존성이 캐시 키에 포함됩니다. 재현할 수 없는 처리기는 `Cacheable=false`로 설정하세요. 사용자 지정 예산은 생성 선언을 포함한 라이브러리를 가져올 때도 적용됩니다. Core의 `VeroScriptAnnotationProcessing.h`, `VeroScriptProgramPreparation.h`와 빌드 가능한 SDK 예제 `CppMinimal/main.cpp`를 참고하세요.

## 호스트 터미널 연결

`IEngineHost`는 `VeroScriptConsole.h`의 형식으로 `QueryConsole`／`ExecuteConsole`를 구현할 수 있습니다. 기본은 미지원입니다. `HasConsoleTextOutput`／`WriteConsoleText`는 줄바꿈 없는 정확한 출력, `FlushConsoleText`는 플러시를 제공합니다. `FStreamEngineHost`는 기반 스트림으로 전달합니다. 호스트는 작업 호출을 동기화하고 정리 완료까지 살아 있어야 합니다. VM은 종료 시 스타일과 커서 표시를 복원합니다. 터미널 상태는 공유되므로 작업 간 전체 출력 순서를 조정하세요.

터미널 인터페이스에 가상 메서드가 추가되었습니다. 업그레이드할 때 같은 빌드의 Core／C++ SDK로 호스트와 관련 네이티브 확장을 다시 컴파일해야 합니다. JIT를 사용하는 경우 해당 백엔드도 배포해야 합니다.

## 일반 컴파일 처리

Engine과 Session은 `FEngineConfig::CompileProcessing`을 전달하며, Session은 `SetCompileProcessing`/`GetCompileProcessing`도 제공합니다. C++ AOT는 `FCodegenOptions::CompileProcessing`을 사용합니다. 컴파일 전에 `FCompileProcessorRegistry`를 동결하세요. 각 컴파일은 `RawSource`, `ActiveSource`, `Types`, `Members`, `Bodies`에서 같은 처리기 인스턴스를 유지합니다. 준비된 프로그램의 AOT 출력은 승인된 생성 결과를 재사용합니다. 콜백은 컴파일 스레드에서 동기 실행되며 같은 Session에 재진입할 수 없습니다.

생성 입력에는 `ReadDependency`를 사용하고 비결정적 처리기에는 `Cacheable=false`를 설정하세요. 생성 결과, 의존성, 주석 정의, 소스 주석 보존 정책과 예산이 캐시 식별에 포함되며 캐시 읽기 전에도 처리를 실행합니다. 교체 실패나 취소 시 현재 프로그램을 유지합니다. `CompileProcessing`과 기존 `AnnotationProcessing` 레지스트리는 Core 트랜잭션과 예산을 공유합니다. 기존 주석 API만 사용하는 호스트에서 작업 수나 소스 예산을 늘리면 변경하지 않은 일반 기본값도 늘어납니다. 명시적으로 설정한 일반 레지스트리나 예산이 우선합니다.

주석 이름은 기본적으로 대소문자를 구분합니다. ASCII 표기 변형을 허용하려면 `RegisterAnnotation`의 마지막 인수를 `false`로 지정하거나 `CompileProcessing.AnnotationDefinitions`의 `FAnnotationDefinition::CaseSensitive=false`를 설정하세요. Session은 자체 바인딩 레지스트리, 불변 모듈 환경은 공개 카탈로그를 사용하며 C++ AOT는 `FCodegenOptions::Bindings`로 카탈로그를 받습니다. 이 정책은 프로세서 구독과 런타임 리플렉션에 적용되며 `annotation_info.matches_name()`과 `annotation_definition_info.case_sensitive()`로 조회할 수 있습니다. 소스 표기는 보존되고 인수 이름과 메타데이터 키는 정확히 일치해야 합니다. `matches_name()`과 `find_annotations()`는 `@`를 구분하며 `has_annotation()`은 생략도 허용합니다. 정책이 겹치는 등록은 거부되고 정책 변경 시 바이트코드 캐시가 무효화됩니다. 이 리플렉션 조회에는 Core 런타임(VM, Mixed 또는 호스트 JIT)이 필요하며 독립 C++ AOT는 이를 명시적으로 거부합니다.

`CommentRetention`은 소스 주석 보존을 제어하며 VM은 실행 중 주석과 소스 주석 조회를 지원합니다. 이번 메타데이터 형식 변경 후에는 일치하는 Core/C++ 라이브러리로 네이티브 소비자와 바이너리 스크립트 패키지를 다시 빌드하세요. `VeroScriptCompileProcessing.h`, `VeroScriptSourceMetadata.h`, SDK의 `CppMinimal/main.cpp`를 참조하세요.

Core/C++ SDK를 업데이트할 때는 새 빌드 디렉터리를 사용하거나 모든 네이티브 개체 파일을 다시 빌드하세요. Windows에서 현지화된 MSVC와 Ninja를 사용하는 경우 CMake 구성과 빌드 전에 개발자 명령 프롬프트에서 `chcp 65001`을 실행하여 검색된 포함 접두사와 컴파일러 출력의 인코딩을 일치시키세요. 그렇지 않으면 Ninja가 헤더 변경을 추적하지 못할 수 있습니다.

## 스크립트 컴파일 확장

`FEngineConfig::ScriptCompileExtensions`로 컴파일 스크립트를 설정합니다. Session은 `SetScriptCompileExtensions`/`GetScriptCompileExtensions`를 제공합니다. 로드 전에 `Roots`, 선택적 `RegistrationEntry`, `Configuration`, 소스 오버레이와 표준 라이브러리 경로를 설정하세요. `import compiler "builder.vs";`는 기본적으로 허용되며 `bAllowSourceImports=false`로 이 진입 경로를 비활성화합니다. SDK와 일치하는 `compiler.vs`를 배포하세요.

확장은 컴파일 스레드의 독립 VM에서 실행하며 단계 간 상태를 유지합니다. 생성된 업무 코드는 선택한 백엔드에서 실행합니다. 업무 바인딩은 자동으로 공개되지 않습니다. 컴파일 전용 `Bindings`를 명시하고 컴파일 동안 유효하게 유지하세요. 컴파일 콜백은 같은 Session을 실행하거나 수정할 수 없습니다. 네이티브 콜백의 권한은 호스트가 관리합니다.

확장의 `ExecutionControl`이 비어 있으면 Engine/Session 실행 토큰을 상속합니다. Session에서는 `SetExecutionControl`을 따릅니다. 명시한 토큰은 처음에 실행 토큰과 같더라도 독립적으로 유지됩니다. 설정은 다음 컴파일과 다시 로드에 적용됩니다. 확장의 `MaxInstructions`, `TimeoutMilliseconds`, 경로 및 전송 제한은 업무 실행 제한과 별개입니다. 협력적 취소로 네이티브 콜백을 중단할 수는 없습니다.

확장 식별 정보, 설정, 소스, 생성 결과 및 선언한 종속성을 통해 캐시를 검증합니다. 생성 입력은 컴파일러 종속성 서비스로 읽으세요. 추가 네이티브 바인딩은 결정성과 안정적인 `BindingConfiguration`을 명시한 경우에만 재사용할 수 있습니다. 확장 컴파일이나 다시 로드가 실패하면 Session의 활성 프로그램을 유지합니다. 업그레이드 후 일치하는 Core/C++ SDK로 네이티브 소비자와 바이너리 스크립트 패키지를 완전히 다시 빌드하세요. `CppMinimal`은 Session과 AOT 확장 경로를 검증합니다.

`GetScriptCompileExtensions`는 설정한 옵션을 반환하며 상속하는 제어는 빈 값으로 유지합니다. 옵션을 읽고 다른 필드를 수정한 후 다시 설정해도 상속을 유지합니다.

## 모듈 환경과 네이티브 확장

검사·로드·실행 전에 `FModuleEnvironment::Create`가 반환한 불변 스냅샷을 `FEngineConfig::ModuleEnvironment`에 설정합니다. Engine과 Session은 모듈 매핑, 의존 범위, 리소스, 컴파일 정책과 네이티브 계약을 사용합니다. 메모리 모듈은 정규 `veroscript-module:/...` 경로로 `CheckFile`/`RunFile`/`LoadFile`에 전달할 수 있습니다. import는 스냅샷이 허용하며 설치만으로 선언을 자동으로 import하지 않습니다.

환경이 `Bindings`를 제공하면 `GetBindingCatalog`, 바인딩 내보내기와 Session 디버그 뷰는 그 카탈로그를 사용합니다. 게시 전에 동일한 임시 레지스트리(또는 `FNativeExtensionRegistry`의 기반 레지스트리)에서 바인딩을 합쳐 타입 ID를 보존합니다. Session은 충돌하는 로컬 바인딩과 추가 등록을 거부하고, 게시된 환경이 카탈로그를 소유하는 동안 `GetBindingRegistry`는 `std::logic_error`를 던집니다. 카탈로그가 없는 환경은 Session 로컬 등록을 사용할 수 있습니다.

프로그램이 로드된 동안 환경을 교체할 수 없습니다. 실행을 끝내고 `Clear`를 호출한 뒤 새 설정을 적용하고 다시 로드합니다. 유효성 검사에서 구성이 거부되면 이전 프로그램과 설정을 보존하고, 컴파일 실패 시에는 활성 프로그램을 보존합니다. `ApplyConfig`의 예외는 [구성 결과 계약](#apply-config-result)을 따릅니다. 콜백과 외부로 반환된 네이티브 객체는 확장 코드를 유지하므로 호스트 리소스를 해제하기 전에 작업, 호출 가능한 핸들과 객체를 해제합니다. 네이티브 라이브러리는 `VeroScriptNativeExtensionLoader.h`의 명시적 API로 로드하고 소비자와 모듈을 동일한 Core/C++ SDK 및 공유 CRT로 다시 빌드합니다. JIT 로드 프로토콜은 별도로 유지됩니다.

환경 컴파일 경로는 Core 준비 절차를 직접 사용하며 Session/Engine의 영구 바이트코드 캐시를 사용하지 않습니다. 순수 스크립트 AOT는 지원하지만 런타임 네이티브 패키지 계약이 필요한 프로그램은 대상 런타임 어댑터가 제공될 때까지 거부합니다.

`GetModuleEnvironment`로 현재 스냅샷을 유지할 수 있습니다. 디버그 뷰의 빌린 `ModuleEnvironment`는 메모리 소스와 제공자 계약을 제공하며 명시적 overlay가 우선합니다. 이는 컴파일 입력이며 로드된 메인 소스의 고정 사본이 아닙니다. 호스트는 실제 `LoadSource` 리비전을 공개해야 합니다.

## 정확한 수치 API

Session의 SetNumericLimits/GetNumericLimits는 FNumericLimits를 설정합니다. VeroScriptNumerics.h의 불변 FBigInteger/FRational과 검사된 Numerics::Try*는 FNumericOperationContext(Limits/Work)를 사용합니다. 0은 유한 기본값이며 작업/취소/오류/실패 출력 보존을 명시합니다. 독립 source AOT는 현재 Core 정확 수치 연산을 거부합니다.

## 중단 후 제한된 정리

<a id="bounded-interruption-cleanup"></a>

명령 수 제한, 시간 초과 또는 취소가 발생하면 일반 실행을 종료합니다. Core는 진입한 범위의 `finally`와 필요한 소멸자를 별도의 공유 예산인 **논리 명령 16,384개 및 50 ms** 안에서 실행합니다. 동기 중첩 호출과 임시 정리 VM이 이 예산을 공유합니다. 기존 제한이나 취소 토큰은 정리를 즉시 거부하지 않으며 권한은 유지됩니다. 새 태스크 생성, 콘솔 입력 대기, `exit`로 원래 오류 대체는 허용하지 않습니다.

최초 중단 원인을 주 오류로 유지합니다. 추가 오류는 `FRunResult.Diagnostics`와 `VeroScriptVm::GetCleanupDiagnostics()`에 기록됩니다. 정리가 완료되면 호스트가 제한을 해제하거나 취소 토큰을 재설정한 뒤 다시 호출할 수 있습니다. 업무 변경 사항은 롤백하지 않습니다.

정리 미완료, 처리되지 않은 정리 오류 또는 예산 소진 시 `VeroScriptVm::RequiresReset()`과 `VeroScriptSession::RequiresReset()`이 true가 됩니다. 이미 실행 중인 finally 또는 소멸자 안에서 중단되면 미완료 정리를 재실행하지 않고 재설정을 요구합니다. 이후 실행과 보존 상태 작업은 `diag.execution_state_reset_required`로 실패합니다. `Session.Clear()` 후 새 프로그램을 불러오세요. Clear는 사용할 수 없는 상태의 미완료 스크립트 정리를 중단하고 네이티브 자원을 해제하지만 호스트의 취소 토큰은 재설정하지 않습니다. 독립 VM과 보존된 Core 상태도 폐기한 뒤 새로 만들어야 합니다. 시간 제한은 협조 방식이며 차단된 호스트 콜백을 강제로 종료할 수 없습니다. VM, Mixed, JIT 및 적용 가능한 AOT는 동일한 Core 계약을 사용합니다. 실패한 재로드 복원이 끝나지 않았거나 외부 루트가 `Clear()`를 막으면 Clear 후 `RequiresReset()`과 `IsLoaded()`를 확인하십시오. 초기화할 수 없으면 Session을 새로 만들고 사용할 수 없는 인스턴스의 실행을 재개하지 마십시오.

## Session 백엔드 선택 인수

`VeroScriptSession::SetVmBackendPath` / `SetJitBackendPath`

이 Session 설정자는 호환 파일／디렉터리 또는 전달된 환경에 해석기가 있는 경우 설치된 백엔드 ID를 받습니다. `auto`는 환경 후보를 요청하며 VM에서는 보존된 제품 대체 VM도 사용할 수 있습니다. `none`이나 네이티브 빈 문자열은 선택을 비활성화하고 `""`도 빈 요청이 됩니다. 새 세션에서 백엔드 설정을 생략하면 환경 선택을 허용하며 명시적 해제와 다릅니다. 기존 세션을 재구성할 때 VM 선택을 생략하면 실행 도메인을 보존합니다. 명시적 선택 실패는 이전 백엔드를 보존합니다. 생성 스레드의 유휴 상태에서 설정하고 VM 변경은 실행 전에 하세요. Clear/reload로도 도메인 고정이 해제되지 않습니다. JIT 선택은 VM/Mixed/JIT 실행 정책과 독립적입니다.

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)`는 `FallbackVmBackendFactory`를 제공하며 명시적인 `VmBackendFactory` / `VmBackendPath`가 우선합니다. Session은 이 대체 팩토리를 보존하므로 실행 전에 `SetVmBackendPath("auto")`로 복원할 수 있습니다.

## 지속 실행과 리로드 확정

지속 Session의 일반 함수는 선택한 Mixed/JIT 백엔드를 사용할 수 있습니다. 유지된 초기화 구간, 디버깅, 제한된 정리는 VM을 사용합니다. 지원하지 않는 네이티브 연산은 진입 전에 폴백하며 명령 수, 시간, 취소, 권한, 호출 깊이 규칙을 유지합니다.

안정된 네이티브 바인딩과 콜백은 세대 코드를 재사용합니다. `IHostBindingCatalog::GetRevision()`은 런타임 리비전을 게시합니다. 0을 반환하는 사용자 카탈로그에 네이티브 콜백이 있으면 재사용하지 않습니다. `FHostBindingRegistry`는 자동 갱신합니다. 외부 변경이 콜백 코드나 수명 소유권에 영향을 주면 `InvalidateRuntimeBindings()`을 호출합니다. 콜백이 읽는 일반 가변 데이터에는 리비전 변경이 필요 없습니다. 실행 중 카탈로그를 바꾸지 마세요. Session의 콜백 설정 API는 보관된 코드를 무효화하고 호출마다 현재 콜백과 서비스를 제공합니다.

`RetainMigratableValue(value, root, error)`은 호스트 값 그래프를 전역 상태와 같은 마이그레이션 그래프에 명시적으로 등록합니다. 객체, 컨테이너, 별칭, 순환, 클로저의 호환 규칙을 공유합니다. 교체 초기화가 확정되기 전에는 `root.Read()`가 null이며 커밋이나 완료된 롤백 후 읽을 수 있습니다. 대기 중에는 복사한 원시 핸들을 사용하지 마세요. 원시 핸들은 자동 재배치되지 않습니다. 일반 루트는 교체를 막고 세대 루트는 기존 규칙대로 만료됩니다. Load/Clear 전에 마이그레이션 루트를 해제합니다. 소스 재로드에는 `SetHotReloadEnabled(true)`가 필요하며 트랜잭션 패키지/앱 재로드도 이 마이그레이션 서비스를 사용합니다.

소유 스레드에서 `GetReloadStatus()`를 읽습니다. `OperationId`는 `PreviousGeneration`, `CandidateGeneration`, `CurrentGeneration`, `CommittedGeneration`과 독립적이며 롤백 후 후보 세대 번호는 재사용될 수 있습니다. `Rejected`는 미적용, `Pending`은 스테이징, `RolledBack`은 복원 완료, `Applied`는 초기화 커밋, `AppliedWithErrors`는 이후 업무 실행 또는 퇴역 값 정리 실패를 뜻합니다. `ResetRequired`는 `RequiresReset()`과 일치하며 추가 실행을 금지합니다. 롤백이나 정리를 끝낼 수 없으면 Session을 새로 만듭니다. 최근 수락된 리로드 요청을 나타내며 네이티브 호스트 부수 효과는 롤백하지 않습니다.

Core, C++ 임베딩, Application 소스는 `PathUtil::FromUtf8` / `ToUtf8`로 C++17/C++20 UTF-8 경로를 처리합니다. `char8_t`를 끌 필요가 없습니다. 제품 버전이 같아도 네이티브 백엔드와 모듈은 같은 Core 구현으로 다시 빌드합니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](../zh-TW/embedding.md)
