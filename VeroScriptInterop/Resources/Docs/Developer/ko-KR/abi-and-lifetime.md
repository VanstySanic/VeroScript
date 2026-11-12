# Interop ABI와 lifetime

`VeroScriptInterop`는 C11-compatible process ABI입니다. public header는 session, value, callable, package, buffer, call context, interception, generic host type, external serialization의 opaque handle을 정의합니다.

## VM 백엔드 선택

`VS_INTEROP_CAP_VM_BACKEND_SELECTION`은 `vs_interop_session_set_vm_backend`와 `vs_interop_session_get_vm_backend_info` 지원을 나타냅니다. 표준 Interop 제품은 정적 바이트코드 VM을 명시적으로 조립합니다. 사용자 빌드는 `VEROSCRIPT_INTEROP_DEFAULT_VM=OFF`로 설정하고 Core와 일치하는 외부 VM을 선택할 수 있습니다. 선택은 VM/Mixed/JIT 정책과 독립적입니다. 실행 전에 생성자 스레드의 유휴 세션에서 설정하며, 빈 경로는 선택을 지우고 실패는 기존 선택을 보존합니다. Core가 실행을 확정하면 JIT 실행도 선택을 고정하며 `vs_interop_session_clear`로 해제되지 않습니다.

출력 `struct_size`를 초기화하세요. 조회 실패 시 출력을 보존합니다. `VS_INTEROP_VM_SELECTED`와 `VS_INTEROP_VM_FROZEN`은 도메인 상태를, `VS_INTEROP_VM_CAP_*`는 기능을 나타냅니다. 문자열 뷰는 선택 교체나 세션 파괴까지 유효하며 정적 선택의 `library_path`는 비어 있습니다. ApplyConfig는 선택을 보존하고 스크립트 컴파일 확장은 세션 VM을 상속합니다.

## version과 status

ABI version, runtime semantic version, capability bit, execution backend, run mode, package kind, value kind, status code는 별도 field입니다. ABI query function은 이를 따로 보고합니다. binding은 ABI value를 product version처럼 표시하거나 비교하면 안 됩니다.

현재 `VS_INTEROP_ABI_VERSION`은 `0x00000101` (0.1.1)이며 `(major << 16) | (minor << 8) | patch`로 인코딩됩니다. 메이저에 16비트, 마이너와 패치에 각각 8비트를 사용합니다. C# 및 CPython 어댑터는 정확한 일치를 요구합니다. 기능 비트나 `struct_size`에 따른 구조체 끝의 필드 추가가 이 버전 검사를 면제하지는 않습니다. ABI 변경은 제품별 패키지 버전과 별도로 검토하십시오.

각 operation은 해당하는 ABI status를 반환합니다. last-error buffer는 operation-level failure용이며 structured diagnostic은 session diagnostic API로 읽고 presentation boundary에서만 format합니다.

## 소유권

opaque handle은 function에 따라 explicit owner 또는 borrowed reference입니다. string/byte view는 non-owning입니다. ABI가 반환하는 buffer는 matching buffer function으로 destroy하거나 owner release 전에 copy해야 합니다. callback context와 borrowed enumeration value는 documented call 동안만 valid합니다.

C header는 C와 C++에서 compile/link되므로 calling convention, struct layout, enum value, nullability, symbol name이 ABI 일부입니다. additive function도 header, implementation, test, adapter의 동기화를 요구합니다.

## 세션 JIT 캐시 확장

`VS_INTEROP_CAP_SESSION_JIT_CACHE`는 session cache limit/statistic export를 gate합니다. extension은 `vs_interop_jit_policy`에 `reuse_session_code`를 append합니다. original 32-byte layout용 caller는 계속 valid이고 reuse enabled를 default로 받습니다. current caller는 full structure를 보내고 해당 byte를 zero로 설정할 수 있습니다. 이는 explicit opt-out과 ABI 0.1.1을 함께 보존합니다.

재사용은 적격 일반 Run, EvalOnce 및 함수 호출에 적용되며 컴파일 상태만 보존하고 VM 전역이나 스크립트 객체는 보존하지 않습니다. 호환 백엔드와 안정적인 바인딩 리비전이 있으면 영속 세션에서도 재사용할 수 있습니다. 인터셉터 또는 변경 콜백을 바꾸면 보존 코드가 무효화됩니다. 유효 백엔드가 VM이면 이 캐시를 우회합니다. 제어형 실행은 호환 캐시와 제어 식별 정보를 보존하고 누적 컴파일 할당량을 공유할 수 있습니다. 컴파일 시도/캐시 항목 예산 0은 해당 카운터의 무제한을 뜻하며 보존 메모리 한도는 적용됩니다. `vs_interop_session_set_jit_cache_limits`는 현재 컨텍스트를 무효화합니다. 기본 한도는 8 MiB/32 MiB이고 0은 해당 리소스 보존을 금지합니다. 제어형 Mixed는 기본적으로 VM을 쓰며 측정 후 `enable_controlled_mixed`로 활성화합니다. 명시적 JIT 선택은 독립적입니다. 백엔드의 `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION`과 실제 실행 통계 `controlled_native_executions`를 확인하고, 미지원 또는 VM 우선 진입점은 제어형 VM 경로를 유지합니다.

`vs_interop_session_get_jit_cache_stats`는 current context의 cumulative compile attempt, settled retained byte, resource-limit latch를 보고합니다. 이는 `vs_interop_session_get_last_jit_stats` last-run delta와 다르고 process peak-memory limit이 아닙니다. policy, limit, backend, module, 관련 host observability 변경은 context/statistic을 reset할 수 있습니다.

## 새 context와 channel 계약

channel operation은 value handle contract를 사용하며 value를 borrowed callback object로 만들지 않습니다. blocking/timed/non-blocking operation은 separate symbol/result semantics가 있고 `close`가 waiter synchronization point입니다. host는 무관한 `VeroScriptStatus` value에서 timeout/closed result를 추론하면 안 됩니다.

generic host-type, call, interception, external serialization context pointer는 callback duration 동안 borrowed입니다. accessor, mutator, error setter, continuation/invocation function은 callback active 동안만 valid입니다. return 뒤 context pointer, argument/field/byte view를 retain하면 안 됩니다. callback exception/host failure는 return 전 context error channel로 변환합니다.

callable handle에는 explicit retain, validity, invoke, destroy operation이 있습니다. owner-thread-only invocation과 weak-`this` capture는 독립 option이며 session을 generally thread-safe로 만들지 않습니다. callback 뒤 필요한 external serializer byte/reference visitor value는 copy합니다. 이 규칙은 C, C++, managed adapter에 동일하게 적용됩니다。


<a id="localization-abi"></a>

## 현지화 ABI

`vs_interop_locale_translate`는 일반 초기화와 프로세스 언어에 따라 모듈 소유 버퍼를 할당합니다. 알 수 없는 바이트와 NUL을 보존하고 잘못된 입력은 출력을 비웁니다. 모든 경로에서 버퍼를 해제하세요. C 헤더와 `.def` 내보내기를 맞추고 네이티브 검증과 여섯 언어 치환을 확인합니다. 메모리 할당 실패 시 영어 대체 문구가 남을 수 있습니다.

## 출판 검증

독립 및 전체 릴리스의 Interop 패키징은 준비된 SDK로 C11 헤더 테스트를 컴파일하고
실행하여 모든 공개 함수를 링크합니다. 내보내기나 가져오기 라이브러리 항목이 누락되면
패키징이 실패합니다. 외부 예제는 `VEROSCRIPT_SDK_ABI_TEST_SOURCE`로 이 검증을
활성화합니다. Linux 공유 라이브러리 테스트는 모듈이 로드되지 않은 리플렉션 오류 경로에
메모리 할당 실패도 주입하여, 예외가 C ABI를 넘지 않고 `OUT_OF_MEMORY`가 반환되는지
확인합니다. 일반적인 모듈 미로드 상태는 `ENGINE_ERROR`를 반환합니다.

경계 테스트는 첫 version query의 할당 실패(빈 view와 재시도 성공), serializer 교체
실패(이전 바이트 보존), 할당 없는 multiset 개수 및 memory guard의 원래 실패 status도
검증합니다. 확장 테스트는 session 최종 파괴 중 script destructor에서 native,
interception 및 mutation callback을 실행합니다. host callback data는 파괴가 끝날
때까지 살아 있어야 합니다.

UTF-16 view 할당 실패는 출력 인자를 바꾸지 않고 `OUT_OF_MEMORY`를 반환합니다.
재시도는 모든 코드 단위를 보존하며 이후 읽기는 캐시된 view를 재사용합니다.

Console callback은 C ABI version을 바꾸지 않고 session-options의 끝에 추가됩니다.
완전한 후행 필드만 읽으며 이전 options도 유효합니다. query/control은 터미널 작업을
설명하고 선택적 write callback은 줄바꿈을 추가하지 않은 원래 UTF-8 바이트를 받습니다.
기존 callback lifetime 및 worker thread 규칙을 적용합니다.

매크로 회귀 테스트는 공개 C ABI로 객체형, 함수형, 가변 인자 매크로를 다시 내보내는
라이브러리를 빌드, 분석, 가져오기 및 실행합니다. 디버그 정보 보존과 제거 모두에서
빌드 시 확정되는 조건부 내보내기, 호출자 환경의 확장, 비공개 매크로 격리, 가져오기
오류의 물리적 위치, 현재 `VSBC` 리더의 `VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` 거부를 확인합니다.

설치된 네이티브 주석 SDK 테스트는 동결 레지스트리 검증, 독립 retain/release, ApplyConfig의
서비스 보존, 생성 스레드 제한, 프로세서 및 해제 콜백의 설정 재진입을 확인합니다. 메서드를
생성하고 호출하며 네 가지 진단 심각도, 주요 오류 선택 및 서비스 제거 후 생성 비활성화를
검증합니다. 네이티브 생성 함수는 일치하는 Core C++ SDK가 필요합니다. 새 C 내보내기 다섯 개는
ABI 0.1.1과 기존 진단 레이아웃을 유지합니다.

테스트는 ApplyConfig 중 서비스 함수 객체의 복사와 소멸도 확인합니다.
설정 재진입은 거부되며, 복사 중 할당 실패는 설치된 서비스를 유지하면서
`OUT_OF_MEMORY`를 반환합니다. 재시도가 성공하면 이전 오류를 지웁니다.

회귀 테스트는 복사·소멸 콜백의 일반 함수 호출, 실행 및 소스 교체도 거부하고
로드된 프로그램을 유지하는지 확인합니다. 설정 설명자는 서비스 함수 객체를 복사하기
전에 검증하여 수명 주기 콜백이 원래 검증 오류를 덮어쓰지 않도록 합니다.
네이티브 콜백 컨텍스트 호출은 지원되는 중첩 스크립트 실행에 계속 사용할 수 있습니다.

추가 회귀 테스트는 이전 성공·종료 플래그로 결과를 초기화하고 재진입 거부 후
새 실패 결과와 세션 진단을 확인합니다. NULL 결과 출력도 포함합니다. 만료된 callable은
반환 값을 변경하지 않고 실패합니다. 동기 소멸 중에는 최종 콜백이 끝날 때까지 핸들을
유지하고 세션을 먼저 닫아 API 접근과 반복 소멸을 안전하게 거부합니다. destroy가 반환한
뒤 핸들은 무효입니다. 활성 호출이 소멸을 지연하면 콜백 데이터는 해당 호출이 반환할
때까지 살아 있어야 합니다.

`vs_interop_session_retain_callback_user_data`는 성공할 때만 호스트 프로토콜 데이터의
소유권을 이전합니다. 마지막 레지스트리/값 소유 참조가 해제된 뒤 해제 콜백을 한 번 호출하며,
세션 종료 뒤 다른 스레드에서 실행될 수도 있습니다. 같은 데이터를 사용하는 후속 일반 및
제네릭 프로토콜도 소유자를 공유합니다. 보관된 값의 복사, 다른 스레드의 최종 해제와
할당 실패 후 멤버 등록 재시도를 회귀 테스트로 확인합니다.

## 일반 처리 및 스크립트 컴파일 처리

`VS_INTEROP_COMPILE_CAP_GENERAL_PROCESSING`은 `VeroScriptInteropCompileProcessing.h`의 네이티브 서비스를 나타냅니다. 동결된 `FCompileProcessorRegistry`와 `CreateCompileProcessingService`로 만들며 retain/release는 독립적인 opaque handle을 제공합니다. `vs_interop_session_set_compile_processing_service`로 설정하고 NULL로 지우며 ApplyConfig는 유지합니다. 일치하는 Core/Interop 네이티브 SDK를 사용하고 최종 해제까지 코드와 데이터를 유지해야 합니다. 설정은 생성 스레드의 유휴 세션에서만 가능하며 처리기 및 서비스 복사/해제 중 재진입은 거부됩니다.

`VS_INTEROP_COMPILE_CAP_SCRIPT_EXTENSIONS`는 `vs_interop_session_set_script_compile_options`를 나타냅니다. 현재의 전체 `struct_size`를 전달합니다. 루트, 선택적 등록 진입점, 설정 및 허용 디렉터리는 복사됩니다. 경로/진입점의 NUL, 중복 설정 키, 잘못된 포인터/개수, 예약 바이트 및 bool은 기존 설정을 교체하지 않습니다. 제한은 루트64개, 설정/허용 디렉터리 각각4096개입니다. 빈 진입점은 `register_extension`을 선택합니다.

NULL은 Core 기본값(설정 루트 없음, 소스 import 허용, 총1,000,000명령, 2,000 ms)을 복원합니다. 명시적 제한0은 무제한입니다. 소스 import를 비활성화해도 설정 루트는 사용할 수 있습니다. overlay, import 및 표준 라이브러리 경로는 일반 설정을 따릅니다. 표준 소스를 import하는 확장에는 `compiler.vs`를 포함하는 일치하는 디렉터리가 필요합니다. 확장은 격리 VM에서 실행되고 세션 취소를 상속하며 ApplyConfig는 설정을 유지합니다. 업무 binding은 암묵적으로 노출되지 않습니다. 생성 패키지는 생산자 확장을 실행하지 않고 JIT는 생성된 업무 코드를 실행합니다.

C 회귀는 단계 간 상태, 입력 복사, 생성/진단, 잘못된 입력의 롤백, 예산/취소 실패와 복구, import 정책 및 패키지 실행을 검증합니다. 설치된 네이티브 소비자는 수명, 설정 유지 및 처리/복사/해제 중 재진입을 검증합니다.

## 해석된 모듈 환경

`VS_INTEROP_CAP_MODULE_ENVIRONMENT`는 환경 브리지를 나타냅니다. 일치하는 네이티브 Core/Interop SDK로 불변 `FModuleEnvironment`를 만든 후 `VeroScriptInteropModuleEnvironment.h`의 `CreateModuleEnvironment`로 감쌉니다. C에서는 불투명 핸들만 전달합니다. `vs_interop_module_environment_retain`은 독립적으로 소유하는 다른 핸들을 만들며 각 핸들은 한 번 해제합니다. 구성된 세션은 스냅샷, 리소스, 네이티브 코드 소유자 및 의존성 계약을 독립적으로 유지합니다.

`vs_interop_session_apply_config_with_environment(session, config, environment)`는 전체 구성을 적용하고 스냅샷을 선택하며 NULL은 선택을 제거합니다. 일반 `vs_interop_session_apply_config`는 현재 환경을 유지합니다. 두 호출 모두 생성 스레드와 유휴 세션이 필요합니다. 환경 변경 전 로드된 프로그램을 Clear 해야 합니다. 로컬 바인딩과 충돌하면 구성이나 취소 제어를 변경하기 전에 `ENGINE_ERROR`를 반환합니다. 환경 소유 카탈로그의 로컬 변경도 `ENGINE_ERROR`로 거부하므로 네이티브 측에서 결합된 불변 카탈로그를 준비합니다. 카탈로그가 없는 환경은 일반 C 등록을 허용합니다.

로드, 실행, 패키지 및 외부 JIT는 메모리/파일 모듈 매핑, 컴파일 리소스, 현지화 서비스 및 정확한 외부 의존성 검사에 대해 Core 환경 계약을 따릅니다. 브리지는 이미 해석된 환경을 사용하며 패키지 설치나 네이티브 확장 검색을 수행하지 않습니다. 게시된 콜백은 불변으로 유지하고 일치하는 네이티브 SDK 바이너리를 사용합니다. 구성 또는 최종 세션 소멸 중 소유자가 해제될 수 있습니다. 소멸자는 예외를 던지면 안 되며 구성과 실행의 재진입은 거부됩니다. Core 구성 적용 중 할당 또는 내부 오류가 발생하면 설정에 의존하기 전에 전체 구성을 다시 적용해야 합니다.

백엔드 확인이 거부되면 구성이나 취소 상태를 반영하기 전에 `ENGINE_ERROR`를 반환합니다. 유효한 환경으로 다시 시도하세요. 핫 리로드 설정이 거부된 경우에도 `ENGINE_ERROR`를 반환합니다. 한도가 있는 롤백이 `RESET_REQUIRED` 상태로 남으면 다음 실행 전에 세션을 삭제하고 다시 생성하세요.

<a id="interop-regression-targets"></a>

CMake 검증에는 `VeroScriptInteropTests`, `VeroScriptInteropExtendedTests`, `VeroScriptInteropTypeTests`, `VeroScriptInteropMemoryTests`, C11 `VeroScriptInteropCHeaderTest`가 포함됩니다. 활성화된 `VeroScriptInteropNativeSdkTests`는 설치된 네이티브 소비자를 검사합니다. 실제 export를 공개 ABI manifest와 맞추고 별도로 얻은 C11 예제를 추출 SDK로 검증하세요. 일치하는 Core/VM에서 컴파일 서비스 수명, 잘못된 스레드 거부, callback과 보관 값을 검사하고 실제 결과/생략을 기록합니다. 테스트 소스 존재만으로 릴리스 수용을 주장하지 않습니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/abi-and-lifetime.md) · [fr-FR](../fr-FR/abi-and-lifetime.md) · [ja-JP](../ja-JP/abi-and-lifetime.md) · [ko-KR](abi-and-lifetime.md) · [zh-CN](../zh-CN/abi-and-lifetime.md) · [zh-TW](../zh-TW/abi-and-lifetime.md)
