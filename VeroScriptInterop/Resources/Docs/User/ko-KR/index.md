# 인터롭 SDK

## JIT 통계 호환성

`vs_interop_session_get_last_jit_stats`에서 `struct_size`가 0이면 legacy form이며 원래 statistics prefix만 씁니다. current caller는 appended counter를 받으려면 `sizeof(vs_interop_jit_stats)`를 전달합니다. `native_typed_loop_executions`는 native typed-loop kernel invocation을 세며 source-visible runtime failure를 보고하고 끝난 invocation도 포함합니다. 이는 success counter가 아닙니다.

`native_leaf_executions`, `native_cfg_executions`, `bridge_executions`, `linear_executions`, `vm_trampoline_executions`는 일반 실행 진입점에 실제로 진입한 횟수를 셉니다. 오류나 제어 이동으로 종료된 호출도 포함합니다. 대응하는 `*_entries`는 컴파일 산출물 수입니다. 이 통계는 마지막 실행에 해당하며, 캐시 적중 시 컴파일 없이 실행할 수 있습니다. `scalar_native_fast_path_calls`는 호스트 네이티브 빠른 경로 호출만 셉니다. 직접 호출과 형식화된 루프 호출은 중첩 이벤트이므로 독립 함수 호출 수로 합산하면 안 됩니다.

C API는 집계 통계를 제공합니다. 선택적인 함수별 진단 스냅샷, 폴백 사유 및 시간 측정은 현재 Core/C++에서 제공하며, C JIT 정책은 이 진단 수집을 활성화하지 않습니다.

`VeroScriptInterop`은 C, Python, C#, Rust 및 기타 어댑터를 위한 언어 중립적 프로세스 ABI입니다.
C++ 객체 레이아웃이나 STL 타입에 의존하지 않고 VeroScript 세션, 값, 호출, 바인딩, 패키지를
제공합니다. C++에서 직접 임베드하려면 먼저 [호스트 통합](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/embedding.md)을 참조하세요.
언어 간 연동이나 안정적인 동적 라이브러리 경계에는 이 SDK를 사용합니다.

함수 시그니처, 구조체 레이아웃, 상수 및 기능 제공 여부는 함께 제공되는 공개 헤더에 정의되어
있습니다. 이 페이지는 계약과 사용 패턴을 설명하며, 전체 선언 목록은 해당 헤더에서 확인할 수
있습니다.

## ABI 및 설치

SDK는 C11 및 C++17에서 유효한 `VeroScriptInterop.h` 공개 헤더를 제공합니다.

```c
#define VS_INTEROP_ABI_VERSION UINT32_C(0x00000101)
```

현재 기준은 ABI 0.1.1입니다. 상위 16비트는 major, 다음 8비트는 minor,
하위 8비트는 patch 버전을 인코딩합니다. 바인딩은 ABI 버전의 정확한 일치를 요구합니다. 설명자 및 출력 구조
`struct_size`으로 시작합니다. 호환되는 개정판은 끝에 필드를 추가합니다. 고정
문자열 뷰와 키/값 쌍에는 의도적으로 `struct_size`이 없습니다.
레이아웃은 헤더에 의해 고정됩니다.

SDK 패키지는 런타임 라이브러리, C11 헤더 및 CMake 가져오기 대상을 제공합니다.
어댑터에는 VeroScript C++ 헤더나 STL ABI가 필요하지 않습니다.

```cmake
find_package(VeroScriptInterop CONFIG REQUIRED)
target_link_libraries(MyAdapter PRIVATE VeroScript::VeroScriptInterop)
```

Windows에서는 SDK 런타임 DLL, 현지화 자원과 런타임 라이선스 고지를 호스트 실행 파일 옆에 배포하고 애플리케이션 배포 시에도 유지하십시오. `VeroScriptInterop.dll`만 복사하면 필요한 의존성이 누락됩니다. 통합 예제는 문서나 소스 참조에서 별도로 받으십시오.

## 불투명 객체 모델

인터페이스는 불투명 핸들을 사용합니다. 어댑터는 핸들의 내부 레이아웃을 검사하거나
시스템 할당자로 모듈이 소유한 메모리를 해제해서는 안 됩니다.

| 핸들 | 목적 |
| --- | --- |
| `vs_interop_session` | 격리된 로딩, 컴파일, 실행 및 호출 세션. |
| `vs_interop_value` | 스크립트 스칼라, 객체, 컨테이너, 함수 또는 호스트 객체 값입니다. |
| `vs_interop_callable` | 현재 네이티브 콜백이 끝난 뒤에도 보존할 수 있는 스크립트 호출 가능 객체입니다. |
| `vs_interop_call_context` | 네이티브 콜백 인수를 읽고, 참조 인수를 쓰고, 콜백 내부의 중첩 호출을 수행합니다. |
| `vs_interop_buffer` | 모듈 소유 오류 텍스트, 패키지 바이트 또는 직렬화 출력. `vs_interop_buffer_destroy`를 사용하여 릴리스합니다. |
| `vs_interop_package` | 검증된 인메모리 `.vsapp`/`.vslib` 패키지. |
| `vs_interop_intercept_context` | 인스턴스 인터셉터에 대한 수신기, 확인된 대상 및 인수입니다. |
| `vs_interop_generic_type_context` | 구체적인 일반 호스트 값 유형, 멤버 및 프로토콜을 정의합니다. |
| `vs_interop_external_serialize_context` / `deserialize_context` | 외부 객체 직렬화 및 역직렬화 콜백을 위한 컨텍스트입니다. |

가장 작은 C 작업 흐름은 "세션 생성 → 구성 적용 → 소스 또는 파일 로드 → 실행 → 진단 읽기 →
삭제"입니다.

```c
#include "VeroScriptInterop.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static vs_interop_string_view view(const char* text)
{
	vs_interop_string_view result;
	result.data = text;
	result.size = (uint64_t)strlen(text);
	return result;
}

int main(void)
{
	vs_interop_session* session = NULL;
	vs_interop_session_options options = {0};
	options.struct_size = sizeof(options);

	if (vs_interop_session_create(&options, &session) != VS_INTEROP_STATUS_OK)
	{
		return 1;
	}

	const vs_interop_string_view source = view(
		"int main()\n"
		"{\n"
		"\tprint(\"hello\");\n"
		"\treturn 0;\n"
		"}");
	vs_interop_run_result load = {0};
	load.struct_size = sizeof(load);

	const vs_interop_status load_status = vs_interop_session_load_source(
		session,
		source,
		view("main.vs"),
		&load);

	if (load_status != VS_INTEROP_STATUS_OK || !load.success)
	{
		vs_interop_session_destroy(session);
		return 2;
	}

	vs_interop_run_result run = {0};
	run.struct_size = sizeof(run);
	const vs_interop_status run_status = vs_interop_session_run(
		session,
		VS_INTEROP_RUN_SCRIPT,
		&run);

	const int result = run_status == VS_INTEROP_STATUS_OK && run.success ? 0 : 3;
	vs_interop_session_destroy(session);
	return result;
}
```

스크립트는 C 문자열 이스케이프를 통해 전달됩니다. 실제 스크립트는 다음을 따라야 합니다.
Tab 들여쓰기, 각 줄에 중괄호 및
`int main` 진입점.

## 상태, 진단 및 종료 의미 체계

모든 호출은 명시적인 `vs_interop_status` 값을 반환합니다. 일반적인 상태는 다음과 같습니다.

| 상수 | 의미 |
| --- | --- |
| `VS_INTEROP_STATUS_OK` | 통화가 완료되었습니다. |
| `VS_INTEROP_STATUS_INVALID_ARGUMENT` | 핸들, 포인터, 문자열 보기 또는 배열 인수가 유효하지 않습니다. |
| `VS_INTEROP_STATUS_WRONG_THREAD` | 세션 또는 개체 스레드 규칙을 위반했습니다. |
| `VS_INTEROP_STATUS_ENGINE_ERROR` | 로드 또는 실행에 실패했습니다. |
| `VS_INTEROP_STATUS_TYPE_MISMATCH` | 값 종류 또는 호스트 유형이 일치하지 않습니다. |
| `VS_INTEROP_STATUS_CALLBACK_ERROR` | 호스트 콜백이 실패를 보고했습니다. |
| `VS_INTEROP_STATUS_UNSUPPORTED` | 현재 빌드에서는 해당 기능을 제공하지 않습니다. |
| `VS_INTEROP_STATUS_OUT_OF_MEMORY` / `BUFFER_TOO_SMALL` / `INTERNAL_ERROR` | 리소스, 출력 버퍼 또는 내부 오류입니다. |

`vs_interop_run_result`에는 `struct_size`, `success`, `exit_requested`,
`exit_code` 및 `diagnostic_count`:

- `success`은 `exit_code`이 0인지 여부가 아니라 정상적인 컴파일/실행을 보고합니다.
- 성공적인 `int main`는 0이 아닌 종료 코드를 반환할 수 있습니다.
- `exit_requested`은 `exit(0)`를 포함하여 명시적인 `exit(n)`에 의해서만 설정됩니다.
- 일반적인 `int main` 반환은 `exit_requested`을 설정하지 않습니다.
- 실패 시 `exit_code`은 일반 스크립트 종료 코드가 아닌 엔진 또는 API 오류 코드입니다.

`vs_interop_session_diagnostic_count`와 `vs_interop_session_get_diagnostic`로
진단을 읽습니다. 각 진단에는 `message`, `message_id`, `file_label`, `line` 및
`column`이 포함됩니다. 진단 인수는 전용 인수 API와 `format_diagnostic`으로
읽고 형식화할 수 있습니다. 반환된 문자열 뷰는 해당 세션 작업에 대해 문서화된
소유 핸들의 수명을 따릅니다.

## 현지화

Interop 현지화는 코어가 프로세스 전체의 단일 상태로 유지하며, 현지화 작업
자체는 스레드로부터 안전합니다. 각 `vs_interop_session`마다 독립적인 설정이
있는 것은 아닙니다. 언어를 변경하면 변경 이후 코어가 형식화하는 진단에도
영향을 미치며, 여기에는 `vs_interop_session_format_diagnostic`도 포함됩니다.

| 기능 | 목적 |
| --- | --- |
| `vs_interop_locale_set_language` | 표준 또는 정규화 가능한 BCP 47 태그에서 로드된 언어를 선택합니다. 성공하면 `VS_INTEROP_STATUS_OK`을 반환합니다. |
| `vs_interop_locale_get_language` | `en-US` 또는 `zh-CN`와 같이 실제로 일치하는 표준 문화권 태그를 반환합니다. |
| `vs_interop_locale_translate` | 로드된 catalog로 번역한 UTF-8을 모듈 소유 버퍼로 반환하고 알 수 없는 문장은 그대로 복사합니다. `vs_interop_buffer_destroy`로 해제합니다。 |
| `vs_interop_locale_get_available_languages` | 현재 로드된 표준 문화 태그를 포함하는 모듈 소유 UTF-8 JSON 버퍼를 반환합니다. |
| `vs_interop_locale_load_catalog_directory` | 전체 프로세스 전반의 현지화 트리를 검증하고 대체합니다. |
| `vs_interop_locale_merge_catalog_directory` | 나무를 검증하고 이를 활성 카탈로그에 병합합니다. `override_existing != 0`은 기존 메시지 ID를 대체합니다. |

디렉터리는 다음에 설명된 `<culture>/<component>.json` 레이아웃을 사용해야 합니다.
[진단 현지화](../../../../../VeroScript/Resources/Docs/User/ko-KR/localization.md) 및 전체 내용을 포함해야 합니다.
`en-US` 기준선. 카탈로그 유효성 검사 또는 로드에 실패하면
`load_catalog_directory` 및 `merge_catalog_directory` 반환
`VS_INTEROP_STATUS_ENGINE_ERROR` 및 모듈 소유 오류 버퍼 쓰기
`out_error`; 잘못된 인수, 할당 실패, 내부 예외
해당 상태를 사용하십시오. 오류 버퍼와 사용 가능한 버퍼를 해제합니다.
`vs_interop_buffer_destroy`이 포함된 언어 JSON 버퍼. 문자열보기
`vs_interop_locale_get_language`은 어댑터가 소유하지 않습니다. 그럴 때 복사하세요
유지되어야 합니다.

```c
/* The view(...) helper from the minimal workflow above is reused here. */
int ConfigureLocale(void)
{
	vs_interop_buffer* error = NULL;
	if (vs_interop_locale_load_catalog_directory(
			view("localization"),
			&error) != VS_INTEROP_STATUS_OK)
	{
		if (error != NULL)
		{
			vs_interop_buffer_destroy(error);
		}
		return 1;
	}

	if (vs_interop_locale_set_language(view("zh-CN")) != VS_INTEROP_STATUS_OK)
	{
		return 2;
	}
	return 0;
}
```

`vs_interop_locale_get_available_languages`은 현재 로드된 표준 문화권 태그를 포함하는 JSON 배열을 반환합니다. 어댑터는 이 배열을 사용하여 언어 선택기를 채워야 하며, 호스트 파일 시스템을 다시 스캔할 필요가 없습니다.

어댑터는 `vs_interop_get_capabilities()`에 다음이 포함되어 있는지 확인해야 합니다.
이러한 선택적 내보내기를 호출하기 전에 `VS_INTEROP_CAP_LOCALIZATION`입니다. 카탈로그
세션이 생성되기 전이나 프로세스가 실행되는 동안 로드될 수 있지만
스레드나 세션 전반에 걸쳐 하나의 프로세스를 공유하는 호스트는 언제
하나의 UI 배치에 언어가 혼합되지 않도록 언어 변경이 허용됩니다.

## 세션 구성

`vs_interop_session_apply_config`은 `vs_interop_config`을 허용합니다.

| 필드 | 목적 |
| --- | --- |
| `import_paths` | 일반 모듈 검색 경로. |
| `implicit_import_paths` | 모든 소스 모듈로 암시적으로 가져온 파일. 이는 소스 선언을 편집하거나 다시 내보내지 않습니다. |
| `script_args` | `main(string[] args)`에 인수가 전달되었습니다. |
| `defines` | 객체와 유사한 매크로 또는 조건부 컴파일 정의. |
| `stdlib_search_paths` | 표준 라이브러리 검색 루트. |
| `source_overlays` | 메모리 내 경로/소스가 재정의됩니다. |
| `bytecode_cache_directory`, `use_bytecode_cache` | 바이트코드 캐시 위치 및 스위치. |
| `max_call_depth` | 실행당 재귀/호출 깊이 제한 `0`은 환경 또는 기본값을 사용합니다. |
| `max_instructions` | 실행당 VM 명령 수 제한; `0`은 제한 없음을 의미합니다. |
| `timeout_milliseconds` | 실행당 실제 시간 제한; `0`은 제한 없음을 의미합니다. |
| `execution_backend` | `VM = 0`, `Mixed = 1`, `JIT = 2`. |
| `predefined_macros` | 이름, 매개변수, 본문, 소스 이름 및 가변 플래그가 포함된 구조화된 함수형 매크로입니다. |
| `allow_circular_imports` | 순환 가져오기가 허용되는지 여부 기본적으로 활성화되어 있습니다. |
| `allow_project_wide_access` | 내보내지 않은 선언을 프로젝트 전체에서 공유할 수 있는지 여부 기본적으로 비활성화되어 있습니다. |
| `enable_execution_control` | 협력적 취소 토큰을 설치합니다. 구성을 성공적으로 적용하면 이전 취소 요청을 초기화합니다. |
| `capability_policy_enabled` | 표준 라이브러리 부작용 정책을 활성화합니다. |
| `allowed_script_capabilities` | `FILE_READ`, `FILE_WRITE`, `FILE_DELETE`, `ENVIRONMENT_READ`, `ENVIRONMENT_WRITE`, `CONSOLE_INPUT`, `CONSOLE_OUTPUT`, `TASK_SPAWN`, `PROCESS_DIRECTORY_CHANGE`의 비트 마스크이며 정책이 활성화된 경우에만 사용합니다. `chdir`에는 `ENVIRONMENT_WRITE`와 `PROCESS_DIRECTORY_CHANGE`가 모두 필요합니다. |

`max_instructions`와 `timeout_milliseconds`는 실행별 협력적 제한이며 `0`은 무제한입니다. 0이 아닌 제한 또는 `enable_execution_control`은 VM이나 지원되는 제어형 JIT 경로에서 협력적 검사를 수행합니다. 제어형 Mixed는 기본적으로 VM을 사용하며 대상 부하를 측정한 뒤 `vs_interop_jit_policy.enable_controlled_mixed`로 활성화할 수 있습니다. 명시적 JIT 선택은 이 설정과 독립적입니다. 선택한 백엔드의 `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION`과 실제 실행 통계 `controlled_native_executions`를 확인하십시오. 지원되지 않거나 VM 비용이 더 낮은 진입점은 검사를 유지하며 VM으로 폴백합니다. `vs_interop_session_request_cancel`은 스레드 안전하며 실행 제어가 비활성화되면 `VS_INTEROP_STATUS_INVALID_ARGUMENT`를 반환합니다. 실행 중인 네이티브 콜백을 강제 종료하지 않습니다. 요청이 반환될 때까지 세션을 유지하십시오.

`capability_policy_enabled`가 0이 아니면 표준 라이브러리 내장 함수에는 `allowed_script_capabilities`의 비트만 허용됩니다. 호스트 콜백은 이 정책의 대상이 아니므로 호스트가 직접 권한을 검사해야 합니다. 선택적 거버넌스 기능에 의존하기 전에 `VS_INTEROP_CAP_EXECUTION_GOVERNANCE`를 확인하십시오.

`vs_interop_jit_policy`은 혼합 백엔드 핫니스 임계값을 최대로 제어합니다.
컴파일 시도, 캐시 용량, 즉시 컴파일 예산.
`vs_interop_session_get_last_jit_stats`은 가장 최근 컴파일과 캐시, 디스패치,
폴백, 예산 건너뛰기, 빠른 경로 및 `direct_native_calls` 카운터를 반환합니다.
이는 VM 값 브리지를 거치지 않고 다른 네이티브 진입점에 도달한 세대 범위 JIT
호출 수입니다.

### 재사용 세션 코드 캐시

먼저 `VS_INTEROP_CAP_SESSION_JIT_CACHE`를 확인하십시오. 이 선택 기능은 `vs_interop_jit_cache_limits`, `vs_interop_jit_cache_stats`, 세션 한도 설정/조회 및 통계 조회 함수를 노출합니다. 전달하거나 받는 모든 확장 가능 구조체에서 `struct_size`를 초기화해야 합니다. 기본 한도는 native code 8 MiB와 전체 retained payload 32 MiB이며, 한도를 0으로 설정하면 해당 리소스 보존이 비활성화됩니다. 이 캐시는 bytecode cache와 별개이고 적격 일반 `Run`/`EvalOnce`/`CallFunction` context의 컴파일 코드만 보존합니다. 통계는 안정화된 누적 context 값이며 실시간 또는 프로세스 전체 메모리 측정값이 아닙니다. 재구성 또는 context 무효화는 보존 코드를 지우며, 리소스 한도는 script 의미를 바꾸지 않고 안전하게 VM 실행으로 fallback합니다.

재사용에는 `vs_interop_jit_policy.reuse_session_code`가 0이 아니고(기본값) 유효 백엔드가 VM이 아니어야 합니다. `max_compile_attempts` 또는 `max_cached_entries`가 0이면 해당 카운터는 무제한이지만 네이티브 코드와 보존 페이로드 한도는 적용됩니다. 바인딩 리비전이 안정적이어야 합니다. 영속 세션과 핫 리로드 세션은 프로그램 세대에 맞는 코드를 재사용할 수 있습니다. 네이티브 바인딩, 인스턴스 인터셉터 또는 변경 콜백이 바뀌면 보존 코드가 무효화됩니다. 디버깅, 영속 상태 초기화, 한도 있는 정리에는 VM을 사용합니다. 지원되는 제어형 실행은 호환 캐시와 제어 식별 정보를 보존할 수 있으며 반복 호출은 누적 컴파일 할당량을 초기화하지 않습니다.

### 공개 매크로와 패키지

`defines`와 `predefined_macros`는 각 소스 모듈의 전처리를 초기화합니다. `#define`은
비공개이며 `#define_export`는 모듈의 최종 정의를 공개합니다. 명시적 `import`는 경로
문자열 직후부터 공개 매크로를 활성화하며 `export import`와 `#include`는 다시 내보내기도
합니다. `implicit_import_paths`는 선언만 가져옵니다. `source_overlays`에도 같은 규칙이
적용됩니다. 공개 함수형 매크로는 호출자 환경에서 확장되며 비공개 보조 매크로를
캡처하지 않습니다. 서로 다른 출처의 같은 이름은 충돌하므로 의도적으로 바꾸기 전에
`#undef`를 사용하세요. 순환하는 소스 의존성의 폐포에 `#define_export`가 포함되면
비활성 분기의 지시문이라도 `allow_circular_imports` 설정과 관계없이 거부됩니다.

C ABI로 만든 라이브러리 패키지는 `strip_debug_info`의 두 값 모두에서 공개 매크로를
보존합니다. 조건부 내보내기는 빌드 시 확정되며 사용자의 정의로 다시 평가되지 않습니다.
애플리케이션 패키지는 공개 매크로 인터페이스를 포함하지 않습니다. 현재 바이트코드
는 고정 `VSBC` 식별자와 별도의 형식 버전을 사용하며 로드 시 버전이 정확히 일치해야 합니다.
`VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4`가 포함된 패키지와 캐시는 소스에서 다시
빌드해야 합니다. 공개 C ABI는 0.1.1을 유지합니다. 독립 JIT 백엔드도 현재 코어로 다시
빌드하세요. 헤더 지문이나 레이아웃이 호환되지 않는 모듈은 로드 시 거부됩니다.

## 유형 및 값

`vs_interop_value_create_array`는 요소 형식을 정규화하고 각 값을 검사합니다. 객체 슬롯은 선언된 기반 클래스에서 파생되거나 인터페이스를 구현하는 스크립트 객체를 원본 값의 모듈 메타데이터로 검사해 허용합니다. 컨테이너 선언은 불변이며 `array<Derived>`를 `array<Base>`에 대입할 수 있게 하거나 숫자/컨테이너의 암시적 변환을 추가하지 않습니다.

`vs_interop_native_type_desc`은 반환 유형, 매개변수,
호스트 함수와 멤버에 대한 컨테이너 유형 인수. 공개 네이티브
유형은 다음과 같습니다:

| 카테고리 | 유형 식별자 |
| --- | --- |
| 스칼라 | `VOID`, `BOOL`, `INT32`, `UINT32`, `INT64`, `UINT64`, `FLOAT`, `DOUBLE`, `INT8`, `UINT8`, `INT16`, `UINT16`, `STRING`. |
| 호스트 및 열거형 | `HOST_OBJECT`, `ENUM`; 이름 필드에는 호스트 유형, 열거형 이름 또는 함수 서명이 포함됩니다. |
| 컨테이너 | `ARRAY`, `MAP`, `SET`, `QUEUE`, `STACK`, `ORDERED_MAP`, `DEQUE`, `CHANNEL`, `MULTI_MAP`, `ORDERED_MULTI_MAP`, `PRIORITY_QUEUE`, `MULTI_SET`, `ORDERED_MULTI_SET`. |
| 호출 가능 | `FUNCTION`. |

`vs_interop_value_kind`은 `OBJECT`, `NATIVE_ARRAY`,
`WSTRING`, `FUNCTION` 및 `HOST_OBJECT`. 고정 너비 숫자 값은 다음과 같습니다.
고유한 종류: `FLOAT`은 바이너리32, `DOUBLE`은 바이너리64, `INT8`/`UINT8`/
`INT16`/`UINT16`은 `INT32` 또는 서명되지 않은 C `int`으로 축소되어서는 안 됩니다.
어댑터.

스칼라에는 쌍을 이루는 `vs_interop_value_set_*`/`get_*` 함수를 사용합니다. UTF-8 텍스트
길이로 구분된 `vs_interop_string_view`를 사용합니다. 넓은 문자열은 UTF-16을 사용합니다.
코드 단위와 명시적인 길이. 값을 생성하고, 복제하고, 재설정하고,
파괴되었습니다. `clone`은 값 래퍼의 참조 의미 체계를 복사합니다. 그렇지 않다
컨테이너 또는 객체 페이로드를 자동으로 전체 복사합니다.
완전한 런타임 값을 쓰기 가능한 대상에 복사하려면
`vs_interop_value_assign(destination, source)`를 사용하십시오. 컨테이너와 객체의
identity 의미를 보존하며 읽기 전용 borrowed 대상은 거부됩니다.

배열, 큐, 스택, 데크, 맵, 세트 및 채널에는 전용 생성,
계산, 읽기, 포함, 보내기, 받기 및 닫기 작업. 매핑 및 키 설정
언어가 지원하는 정확한 유형이어야 합니다: 부호 있는/부호 없는 정수, UTF-8/UTF-16
문자열, 범위가 지정된 열거형 또는 호스트 값. 호스트 값은 기본적으로 ID를 사용합니다. 또는
등록된 평등/해시 스탬프 프로토콜. 누락된 지도 키가 성공을 반환합니다.
`out_found == 0`을 사용하며 출력 값을 덮어쓰지 않습니다. 다음에서 수신
열린 채널이 차단될 수 있습니다. 채널을 닫으면 대기 중인 수신 작업이 깨어납니다.

## 네이티브 바인딩 및 호출

어댑터는 다음을 등록할 수 있습니다.

- 컴팩트한 기본 기능을 위한 `vs_interop_session_register_native`
- 구조화된 반환 유형, 매개변수, 문서, 매개변수 이름, 주석 및 일반 메타데이터를 위한 `vs_interop_session_register_host_function`
- 열거형, 구조체, 클래스, 생성자, 메서드, 속성, 필드, 정적 멤버, 연산자, 복제, 같음, 해싱 및 하위 클래스 팩터리용 `vs_interop_session_register_host_type`, `vs_interop_session_register_host_member` 및 `vs_interop_session_register_host_protocol`.

매개변수 플래그는 비트 플래그입니다: `VS_INTEROP_PARAM_REF = 0x01` 및
`VS_INTEROP_PARAM_CONST_REF = 0x02`. `const ref`에는 참조 비트가 포함되어야 합니다.
호스트 콜백은 인수를 읽고, 변경 가능한 인수에 액세스하고,
`host_this` 및 프로그램 생성, 콜백 로컬 호출 수행
스크립트 함수 또는 `vs_interop_call_context`를 통해 호출 가능하게 유지됩니다.

스크립트 함수, 메소드, 생성자, 필드 및 전역 호출은 다음을 사용합니다.

| 능력 | 대표 기능 |
| --- | --- |
| 자유 함수와 메서드 | `vs_interop_session_call_function`, `vs_interop_session_call_method`. |
| 인터셉터 우회 | `vs_interop_session_call_method_bypassing_interceptor`. |
| 쿼리 | `vs_interop_session_has_function`, `vs_interop_session_has_method`. |
| 개체 및 구성원 | `vs_interop_session_construct_object`, `vs_interop_session_get_field`, `vs_interop_session_set_field`, `vs_interop_session_get_global`, `vs_interop_session_set_global`. |
| 보유된 콜러블 | `vs_interop_session_retain_callable`, `vs_interop_callable_invoke`, `vs_interop_callable_destroy`, `vs_interop_callable_is_valid`. |

성공한 호출은 최종 `ref`/`out` 값을 해당 핸들에 반영하므로 인수 핸들은
쓰기 가능해야 합니다. 값 인수와 `const ref` 인수는 다시 기록되지 않습니다.
인수는 스크립트 실행 전에 검증되므로 스크립트 부작용이 발생한 뒤 잘못된
인수로 인해 호출이 실패하지 않습니다. 네이티브 콜백 컨텍스트에서 빌린
읽기 전용 값은 보존하거나 중첩 호출에 전달하기 전에 복제해야 합니다.

## 반사, 차단 및 확장 지점

세션은 두 가지 JSON 보기를 내보낼 수 있습니다.

- `vs_interop_session_export_bindings_json`에는 함수, 호스트 유형, 멤버, 컨테이너 메타데이터,
  매개변수 플래그, 매개변수 이름, 문서 및 주석이 포함됩니다. IDE, LSP 및 정적 검사 어댑터에서
  사용할 수 있습니다.
- `vs_interop_session_export_reflection_json`에는 로드된 모듈의 유형, 필드, 메서드, 전역 및
  선언 메타데이터가 포함되어 런타임 도구에서 사용할 수 있습니다.

주석 어휘는 이름과 지원되는 인수를 설명합니다. 주석 인스턴스는 실제로 해당
주석을 포함하는 선언을 설명합니다. 두 개념은 서로 별개의 데이터입니다.

세션은 다음을 등록할 수도 있습니다.

- `vs_interop_session_set_instance_call_interceptor`, `UNHANDLED`, `HANDLED` 또는 `FAILED`을 반환합니다. `proceed`는 이미 해결된 대상 및 오버로드를 사용합니다.
- 객체 유형, 필드, 값 및 변경 세대를 포함한 필드 쓰기, 컨테이너 추가, 제거, 설정 및 지우기를 위한 `vs_interop_session_set_mutation_callback`
- 호스트 제공 클래스 기본값의 경우 `vs_interop_session_apply_class_defaults`;
- 동기식 콜백 생성 호스트 값 유형의 경우 `vs_interop_session_register_generic_host_type_template` 및 `vs_interop_session_instantiate_generic_host_type`
- 투명 호스트 별칭의 경우 `vs_interop_session_register_type_alias`;
- 외부 인코딩, 디코딩 및 재귀 참조 순회를 위한 `vs_interop_session_register_external_serializer`, `vs_interop_session_serialize_object`, `vs_interop_session_deserialize_object` 및 `vs_interop_session_enumerate_object_references`.

외부 역직렬 변환기는 임시 트랜잭션에 명명된 필드 할당을 추가할 수 있지만 스크립트 개체 저장소에 직접
액세스할 수는 없습니다. 알 수 없는 호스트 패키지 메타데이터는 바이트 단위로 보존됩니다.

## 패키지 및 핫 리로드

<a id="package-container-format"></a>

`.vslib`는 Core 라이브러리 이미지입니다. 원시 Core 앱 이미지는 표준 ZIP `.vsapp` 컨테이너 내부의 컴파일된 페이로드입니다. Application 지원 빌드에서는 패키지 바이트 API가 일반 앱 ZIP도 읽고 앱 빌드는 ZIP 컨테이너를 반환합니다. 해당 지원이 없으면 원시 Core 이미지를 반환합니다. 환경, 서명, 보호 옵션을 명시하려면 Application 로드 API를 사용하세요. 서명용 정규 바이트는 보관된 앱 컨테이너의 경우 원본 컨테이너 바이트이며, 원시 이미지의 경우 Core 정규 바이트입니다. 파싱만으로 게시자의 신뢰성이 확인되지는 않습니다.

Interop은 메모리 내 `.vsapp` 및 `.vslib`을 읽고, 검증하고, 빌드하고, 실행할 수 있습니다.
패키지 관련 기능은 다음과 같습니다.

- `vs_interop_package_read`은 핵심 형식, 버전 및 구조 검증을 수행합니다.
- `vs_interop_package_get_kind`, `vs_interop_package_get_name`, `vs_interop_package_get_version`, `vs_interop_package_metadata_count` 및 `vs_interop_package_get_metadata`는 패키지 정보를 읽습니다.
- `vs_interop_package_get_canonical_signed_bytes`은 플랫폼에서 안정적인 차용 바이트를 반환합니다. 호스트는 신뢰 경계를 넘기 전에 서명을 확인해야 합니다.
- `vs_interop_session_load_package`은 애플리케이션 또는 라이브러리 패키지를 로드합니다.
- `vs_interop_session_reload_package`은 트랜잭션 방식으로 다시 로드하고 성공할 경우에만 모듈을 사용합니다.
- `vs_interop_session_build_package_file`는 메모리에 패키지 바이트를 빌드합니다.

이전 개발 형식으로 만든 패키지와 바이트코드 캐시는 현재 런타임에서 사용하기 전에 소스에서 다시 빌드해야 합니다. `vs_interop_package_read`는 이전 형식을 거부합니다.

`vs_interop_session_set_hot_reload_enabled`로 소스 핫 리로드를 활성화합니다.
`vs_interop_session_reload`와 `vs_interop_session_reload_package`는 다음 실행에서
초기화할 후보를 준비할 수 있습니다. `VS_INTEROP_CAP_RELOAD_STATUS`가 있으면
`vs_interop_reload_status.struct_size`를 설정하고 생성 스레드에서
`vs_interop_session_get_reload_status`를 호출합니다. `PENDING`에서는 후보가 준비되고
이전 세대가 커밋 상태로 남습니다. `ROLLED_BACK`은 이전 세대로 복원되었음을 뜻합니다.
`APPLIED_WITH_ERRORS`는 커밋 이후 실행 또는 정리 오류가 발생해도 후보가 유지된
상태입니다. `RESET_REQUIRED`이면 세션을 비우고 프로그램을 다시 로드합니다.
`vs_interop_session_clear`가 미완료 롤백을 보고하면 세션을 파기하고 다시 만듭니다.
`operation_id`로 동일한 리로드 작업을 추적할 수 있습니다.

호환되는 전역 변수, 필드, 객체 정체성은 마이그레이션할 수 있습니다. 호환되지 않는
타입 변경, 활성 채널 또는 캡처 클로저는 마이그레이션을 막을 수 있습니다. 모듈을
교체하기 전에 해당 모듈에 속한 소유 C 값을 해제합니다. 네이티브 부작용은 스크립트
트랜잭션 범위 밖에 있습니다.

## 스레드, 빌림 및 소멸

어댑터는 다음 경계를 준수해야 합니다.

- 세션 생성, 일반 세션 API 및 최종 삭제는 생성자 스레드에 국한됩니다.
- 잘못된 스레드 소멸은 핸들을 사용하지 않고 `VS_INTEROP_STATUS_WRONG_THREAD`을 반환합니다.
- 호스트가 세션에 대한 모든 액세스를 직렬화하는 경우에만 `owner_thread_only == 0`을 사용하여 유지된 호출 가능 항목을 다른 스레드에서 호출할 수 있습니다. 세션 자체는 여전히 스레드로부터 안전하지 않습니다.
- 스크립트 실행에 의해 트리거된 콜백은 VM 작업자 스레드에서 시작될 수 있으며 겹칠 수 있으므로 외부 런타임은 자체 런타임 잠금을 획득하고 공유 상태를 동기화해야 합니다.
- 콜백 컨텍스트, 문자열 보기, 컨테이너 요소 보기 및 호스트 메타데이터 인수는 문서화된 콜백 또는 작업 수명 동안 차용됩니다. 문자열을 복사하거나 값을 유지하기 전에 복제하십시오.
- 모듈 소유 문자열, 바이트 및 뷰는 어댑터에 의해 해제되어서는 안 됩니다. 모듈 소유 `vs_interop_buffer` 객체는 `vs_interop_buffer_destroy`에서만 릴리스됩니다.
- 외부 콜백은 C ABI 전체에서 예외를 해제해서는 안 됩니다. 컨텍스트에 오류를 저장하고 `VS_INTEROP_CALLBACK_ERROR`를 반환합니다.
- 값, 개체 및 컨테이너 수명은 VeroScript 참조 의미 체계를 따릅니다. C 구조체를 복사하는 것은 깊은 복사가 아닙니다.

## 기능 협상 및 호환성

`vs_interop_get_capabilities()`은 패키지, 핫 리로드, 호스트 바인딩, JSON 바인딩, 리플렉션 JSON, 인스턴스 차단,
변경 콜백, 컨테이너 값, 유지된 호출 가능 객체, 일반 호스트 유형, 외부 직렬화, JIT 진단, 현지화,
개별 숫자 기본형, 컨테이너 열거, 실행 거버넌스, 재사용 가능한 세션 코드 캐시, 명시적 백엔드 선택 및
일회 초기화에 대한 선택적 기능 비트를 보고합니다. ABI를 먼저 확인한 다음 선택적 내보내기를 호출하기
전에 기능을 확인하세요. 기능 검색은 `VS_INTEROP_ABI_VERSION`을 변경하지 않습니다.

`vs_interop_get_capabilities()`는 `VS_INTEROP_CAP_JIT_BACKEND_SELECTION`을 포함한
프로세스 API 기능을 보고합니다. 암시적 백엔드가 없으므로 전역 결과는
`VS_INTEROP_CAP_NATIVE_JIT`와 `VS_INTEROP_CAP_NATIVE_TYPED_LOOPS`를 설정하지 않습니다.
선택한 세션 백엔드의 능력은 `vs_interop_session_get_jit_backend_info`의
`native_code`, `VS_INTEROP_JIT_CAP_NATIVE_CODE`,
`VS_INTEROP_JIT_CAP_NATIVE_TYPED_LOOPS`로 확인합니다. 플랫폼 문자열만으로 네이티브
지원 여부를 판단할 수 없습니다.
ABI 0.1.1에 추가된 `vs_interop_jit_stats`의 `cost_model_vm_fallbacks`와
`native_typed_loop_executions`는 호출자가 충분히 큰 `struct_size`를 제공한 경우에만 보입니다.
이전 호출자는 기존 prefix를 안전하게 유지합니다.

호환성 규칙은 기존 서명, 열거형 값 또는 공개된 구조체 레이아웃을 변경하지 않습니다. 새 구조체 필드는 끝에
추가되고 `struct_size`로 가시 범위가 보호됩니다. 어댑터는 플랫폼별 레이아웃을 하드 코딩하는 대신 진단을
위해 `vs_interop_get_runtime_version` 및 `vs_interop_get_host_platform`을 기록할 수 있습니다.

SDK는 Python, C#, Rust, Unreal 또는 AOT 정책을 규정하지 않으며 해당 런타임을 번들로 제공하지 않습니다.
언어 핵심을 변경하지 않고 공개 `VeroScriptCpp` 임베딩 표면에 의존합니다. 브라우저 안전 메모리 내 진입점은
[웹/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ko-KR/index.md)를, 필드 수준 바인딩 카탈로그 형식은 [호스트 바인딩](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/bindings.md)을 참조하세요.

## ABI 값 종류

ABI 0.1.1에는 `NULL`, `MULTI_MAP`, `ORDERED_MULTI_MAP`, `PRIORITY_QUEUE`,
`MULTI_SET`, `ORDERED_MULTI_SET` 값 종류가 추가되었습니다. VeroScript의
`nullptr`에는 `vs_interop_value_set_null` 및 `vs_interop_value_is_null`을
사용합니다. null은 잘못된 핸들이 아니라 독립적인 값 종류입니다.

`vs_interop_value_create_sequence`는 `PRIORITY_QUEUE`를 지원합니다.
`vs_interop_value_create_map_kind`는 일반 map과 multi-map을 만들며,
multi-map은 반복된 키/값 연결을 모두 보존하고 일반 map은 중복 키의 값을
대체합니다. `vs_interop_value_create_set_kind`는 일반 set과 multi-set을
만들며 multi-set은 중복 요소를 보존합니다.

`vs_interop_value_set_host_object_ex`의
`vs_interop_host_object_is_type` 콜백은 호스트 객체 하위 형식의 호환성을
검사합니다. 콜백은 예외를 던지거나 빌린 형식 이름 뷰를 보존해서는 안 됩니다.

## 실행 모드

`VS_INTEROP_RUN_EVAL`은 증분 평가 의미를 유지합니다.
`VS_INTEROP_CAP_EVAL_ONCE`로 확인하는 `VS_INTEROP_RUN_EVAL_ONCE`는 `main`을
호출하지 않고 모듈 초기화를 한 번 실행하며 선택한 JIT 백엔드가 초기화를 실행할 수
있게 합니다. `VS_INTEROP_RUN_SCRIPT`는 추가로 `main`을 호출합니다. 실행 정책은
별도로 설정합니다. 백엔드를 선택하지 않으면 Mixed는 VM을 사용하고 강제 JIT는
실행 전에 실패합니다.

일반 세션은 실행할 때마다 새 상태를 초기화합니다. EvalOnce는 이후 실행의 초기화를
생략하지 않습니다. 핫 리로드 세션은 기존 영속 상태 규칙에 따라 이미 초기화한 전역
변수를 유지합니다. 상태를 유지하는 초기화는 VM을 사용합니다. 조건을 충족하는
실행은 선택한 백엔드를 사용합니다.

## 명시적 JIT 선택

실행 정책과 backend 선택은 별개입니다. 설치된 모듈 환경이 없으면 Runtime에는 기본 모듈이 없으며 미선택 mixed는 VM, 강제 jit는 실행 전 실패입니다. 소유 스레드에서 Session이 유휴 상태일 때 `vs_interop_session_set_jit_backend`로 별도 설치한 호환 파일/디렉터리를 선택합니다. `vs_interop_session_set_jit_backend(session, (vs_interop_string_view){NULL, 0})`은 선택을 해제합니다. 교체 실패는 이전 선택을 유지합니다. `vs_interop_session_get_jit_backend` / `vs_interop_session_get_jit_backend_info`는 실제 식별/네이티브 기능이며 `backend-services`은 Core 서비스 계약일 뿐입니다. 디버깅, 상태를 유지하는 초기화와 한도가 있는 정리는 VM을 사용합니다. 제어된 Mixed는 기본적으로 VM을 사용하며 `enable_controlled_mixed`로 활성화할 수 있습니다. 명시적 JIT는 이 설정과 독립적입니다. 지원되는 제어 진입점은 JIT를 사용할 수 있고, 미지원 또는 VM 우선 진입점은 검사를 유지하며 VM으로 폴백합니다. 검색/ABI 규칙은 JIT 가이드를 참조하세요.

쿼리 전에 struct_size를 설정합니다. 텍스트 뷰는 선택한 팩터리에서 빌리므로 backend 변경/Session 파괴 전에 복사합니다.

[JIT](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/ko-KR/index.md)

## 관리 메모리

호스트 API: `vs_interop_memory_*` / `vs_interop_session_set_memory_limits`. 소유권, 할당량, 뷰 만료와 임대는 [`std::memory`](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md) 문서를 참고하세요.

추가 기능은 `VS_INTEROP_CAP_MEMORY`로 확인하며 기존 구조체와 ABI 버전은 유지됩니다. 컨텍스트는 하나만 지정합니다: 소유 스레드에서 유휴 상태인 `session` 또는 네이티브 콜백 안의 `call`. 실행 중 session 재진입은 `BUSY`를 반환하며 콜백 작업에는 현재 VM의 실행 제한이 적용됩니다. 제한, 통계, 임대 구조체의 `struct_size`를 설정하세요. 제한 0은 무제한입니다. 설정 변경, Run, Clear는 호스트가 보유한 영역의 사용량을 유지합니다.

`from_array`는 uint8 배열을 요구하며 `to_array`와 memory `clone`은 독립된 바이트 복사본입니다. 정책 활성화 시 주소 조회에는 `VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS`가 필요하지만 ID와 범위 내 바이트 작업은 영향을 받지 않습니다. 쓰기 임대는 경쟁하는 바이트 접근을, 읽기 임대는 바이트 변경을 차단합니다. 모든 임대는 크기 변경과 무효화를 막습니다. 네이티브 작업을 동기화한 뒤 token을 한 번만 해제하세요. 다른 스레드나 session 파괴 후에도 해제할 수 있습니다. 외부 등록 성공 시 `owner`를 인수하고 최종 해제 스레드에서 콜백을 한 번 호출합니다. 실패 시 소유 책임은 호스트에 있습니다. 최종 해제까지 콜백 코드와 사용자 데이터를 유지하세요.

## 컴파일 주석 처리와 진단 심각도

`vs_interop_get_compile_capabilities()`와
`VS_INTEROP_COMPILE_CAP_ANNOTATION_PROCESSING`을 확인하세요. 네이티브 C++ 호스트는
일치하는 Core SDK 및 컴파일러/STL ABI를 사용하여 `VeroScriptInteropAnnotation.h`의
`CreateAnnotationProcessingService`와 동결된 레지스트리로 불투명 서비스를 만듭니다.
C11 호스트는 핸들만 전달합니다. `vs_interop_annotation_service_retain`은 별도의 소유
핸들을 반환하며 각각 `vs_interop_annotation_service_release`로 해제합니다. 유휴 생성
스레드에서 `vs_interop_session_set_annotation_service`를 호출하세요. 세션이 설정을
복사하므로 전달한 핸들은 즉시 해제할 수 있습니다. ApplyConfig는 서비스를 보존하며
NULL은 명시적으로 지웁니다. 컴파일 또는 서비스 해제 콜백에서 설정 재진입은 거부됩니다.
모든 핸들과 연결된 세션이 서비스를 해제할 때까지 네이티브 코드와 콜백 데이터를 유지해야
합니다. 핸들은 현재 프로세스에서만 유효하며 직렬화할 수 없습니다.

기존 진단 레이아웃과 C ABI 0.1.1은 호환성을 유지합니다.
`vs_interop_session_get_diagnostic_severity`로 심각도를 조회하세요.
`VS_INTEROP_DIAGNOSTIC_SEVERITY_`의 접미사 `ERROR`, `WARNING`, `INFORMATION`, `HINT`는
각각 0, 1, 2, 3입니다. 잘못된 인덱스나 다른 스레드의 조회는 출력을 바꾸지 않습니다.
성공한 컴파일에도 참고 진단이 있을 수 있으므로 status와 success를 확인하세요.
참고 진단 뒤에 실패하면 last-error는 주요 오류를 보고합니다.

## variant

<a id="exported-value-lifetime"></a>

스크립트 소유의 내보낸 값은 세션 루트로 등록됩니다. 읽기와 사용은 생성 스레드에서 수행해야 하며 다른 스레드에서 래퍼를 해제하면 소유자 안전 지점에 정리를 예약합니다. 모듈 교체·재로드·초기화 전에 모든 스냅샷을 해제하세요. 세션 종료는 의존 래퍼를 무효화하고 언어 정리를 실행합니다. 독립 기본 값 상자는 종료 후에도 사용할 수 있습니다. C# object/dynamic 또는 임의 Python 객체를 자동 매핑하지 않습니다.

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

대상이 variant로 확정되면 구체 값을 암시적으로 상자화할 수 있습니다. 읽기는 cast<T> 또는 try_cast<T>를 명시하며 variant_is<T>는 변환 없이 저장 형식을 정확히 검사합니다. optional<T>와 try_cast<T>는 Core 내장 기능이므로 core.vs 가져오기가 필요 없습니다. 빈 variant의 cast<T>는 실패하고, try_cast<T>는 빈 variant나 일반 변환 불일치에 빈 optional을 반환합니다. 복사 실패, 취소와 제한 초과는 오류로 남습니다.

값은 HOST_OBJECT이며 numeric_kind는 0 기타/불가, 1 bigint, 2 rational을 검증하고 닫힌 값/다른 스레드의 스크립트 값에는 0을 반환합니다. VS_INTEROP_CAP_NUMERICS를 확인합니다. numeric_parse/to_text/convert에는 유휴 소유 스레드 Session 또는 활성 네이티브 호출의 빌린 context 하나만 제공합니다. UTF-8 길이 명시, to_text는 string 값, convert는 고정 정수/float/double VS_INTEROP_NATIVE_*와 exact_float 0/1이며 실패 시 출력을 보존합니다. limits.struct_size 및 reserved=0을 초기화하고 0은 유한 Core 기본값이며 프로세스 heap cap이 아닙니다.

`vs_interop_session_set_numeric_limits`
`vs_interop_value_numeric_kind`
`vs_interop_numeric_parse`
`vs_interop_numeric_to_text`
`vs_interop_numeric_convert`

## 모듈 종속 값의 소유권

<a id="value-domain-confinement"></a>

핸들을 소유해도 페이로드가 세션과 독립하지는 않습니다. 스크립트 객체/함수와 모듈 값 슬롯을 가진 가변 컨테이너는 비어 있어도 세션 생성 스레드에 한정되며 세션 종료 후 사용할 수 없습니다. 호스트가 만든 저장소는 세션에서 처음 사용할 때 연결되고 공유 별칭과 복제도 이 소유권을 유지합니다. VM 작업자 콜백은 독립적으로 소유한 값만 외부로 내보낼 수 있으며 variant/함수 슬롯을 가진 빈 컨테이너도 독립 값이 아닙니다. 세션 종료 후에도 사용한 소유 핸들을 해제하세요.

## Session 백엔드 선택 인수

`vs_interop_session_set_vm_backend` / `vs_interop_session_set_jit_backend`

이 Session 설정자는 호환 파일／디렉터리 또는 전달된 환경에 해석기가 있는 경우 설치된 백엔드 ID를 받습니다. `auto`는 환경 후보를 요청하며 VM에서는 보존된 제품 대체 VM도 사용할 수 있습니다. `none`이나 네이티브 빈 문자열은 선택을 비활성화하고 `{NULL, 0}`도 빈 요청이 됩니다. 새 세션에서 백엔드 설정을 생략하면 환경 선택을 허용하며 명시적 해제와 다릅니다. 기존 세션을 재구성할 때 VM 선택을 생략하면 실행 도메인을 보존합니다. 명시적 선택 실패는 이전 백엔드를 보존합니다. 생성 스레드의 유휴 상태에서 설정하고 VM 변경은 실행 전에 하세요. Clear/reload로도 도메인 고정이 해제되지 않습니다. JIT 선택은 VM/Mixed/JIT 실행 정책과 독립적입니다.

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

<a id="host-memory-ownership"></a>

## 호스트 공통 메모리 소유권

Session 메모리 API로 버퍼, 유계 뷰와 바이트 스냅샷을 만듭니다. Session이 유휴 상태일 때 소유 스레드에서 사용하고 등록된 네이티브 callback 안에서는 활성 메모리 컨텍스트를 사용합니다. 핸들 복사는 접근 도메인을 이동하지 않습니다.

할당 전에 한도를 설정합니다. 0은 무제한입니다. 버퍼 한도는 용량에 적용되고 보유 총량은 소유 용량과 외부 바이트 및 확장 시 최대 사용량을 포함합니다. 사용량보다 작은 값으로 줄이면 기존 설정을 유지한 채 실패합니다. Run, Clear, reload는 호스트가 보유한 영역을 해제하거나 계산을 초기화하지 않습니다. 바이트 저장소 한도이며 전체 heap 제한이나 sandbox는 아닙니다.

쓰기 대여는 다른 바이트 접근을 배제합니다. 모든 대여는 resize와 무효화를 막고 읽기 대여는 쓰기를 막습니다. 네이티브 작업을 끝낸 후 해제하세요. 다른 스레드와 Session 종료 후에도 해제할 수 있지만 종료된 Session에서 새 연산을 할 수는 없습니다. 외부 저장소는 마지막 별칭이나 대여가 해제될 때까지 소유자를 유지하고 무효화는 모든 별칭을 만료시킵니다. 원시 주소는 소유권을 유지하지 않습니다.

객체 ID는 uint64를 유지하며 수명을 늘리지 않습니다. 정책 활성 상태에서 주소 조회에는 호스트 메모리 주소 권한이 필요합니다. 원래 참조 인수는 해당 네이티브 callback 중에만 조회하며 값 복사에는 원래 참조 주소가 없습니다. callback 이후 컨텍스트를 보관하지 마세요.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
