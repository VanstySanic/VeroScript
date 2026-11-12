# Python 세션과 실행

세션을 비우거나 핫 리로드를 끄면 스크립트 소멸자와 Python 콜백이 실행될 수 있습니다. 콜백 예외는 즉시 `CallbackError`로 보고되며, 이미 완료된 정리나 설정 변경은 그대로 적용됩니다.

진행 중인 작업이나 콜백에서 열린 세션을 닫으면 `Error`가 발생하며 세션은 계속 사용할 수 있습니다. 바깥 작업이 반환된 후 닫으세요. 종료 처리 중에는 `closed`가 이미 참이며 `close()`를 다시 호출해도 안전합니다.

## 세션 생성과 결과

`Session(...)`을 context manager로 사용하고 생성 스레드에서 세션을 닫으십시오.
`load_source`, `load_file`, `load_package`, `reload_package`는 `RunResult`를
반환하며 `run(RunMode.SCRIPT)`는 `main`을 호출합니다. `RunMode.EVAL`은
incremental evaluation 경로를 사용합니다.

결과에는 `success`, `exit_requested`, `exit_code`, `diagnostics`가 포함됩니다.
0이 아닌 종료 코드도 실행 성공일 수 있습니다. 값을 반환하는 호출이 실패하면
`VeroScriptError`가 발생하므로 구조화된 `VeroScriptError.result`를 확인하십시오.
transport, 타입, 스레드와 callback 오류에는 모듈의 전용 예외가 사용됩니다.

## 주요 공개 이름

상태 접근의 정확한 이름은 `is_loaded`, `get_global(name)`,
`set_global(name, value)`입니다. `load_source`, `load_file`, `load_package`,
`reload_package`는 `RunResult`를 반환하고 `call`, `construct`, `call_method`,
`get_field`, `set_field`는 실패 시 `VeroScriptError`를 발생시킵니다.

`Session`은 import 경로, 호스트 바인딩, 입력, 진단과 수명을 관리합니다. 생성된 스레드에서 사용하고 `close()` 또는 컨텍스트 관리자로 종료하십시오.

주요 구성은 `backend`（`Backend.VM`, `Backend.MIXED`, `Backend.JIT`）, `import_paths`, `implicit_import_paths`, `script_args`, `defines`, `stdlib_search_paths`, `source_overlays`, `bytecode_cache_directory`, `use_bytecode_cache`, `max_call_depth`, `max_instructions`, `timeout_milliseconds`, `enable_execution_control`, `capability_policy_enabled`, `allowed_script_capabilities`, `allow_circular_imports`, `allow_project_wide_access`, `hot_reload_enabled`, `print_callback`, `diagnostic_callback`입니다.

`load_source`, `load_file`, `load_package`로 로드하고 `reload`, `reload_package`로 다시 로드합니다. `run`은 진입점을 실행하며 `call`, `construct`, `call_method`, `get_field`, `set_field`, `get_global`, `set_global`은 구조화된 접근을 제공합니다.

`max_instructions`와 `timeout_milliseconds`는 실행별 협력적 제한입니다. `0`은 제한 없음을 의미합니다. 어느 한 제한을 설정하거나 `enable_execution_control=True`를 사용하면 요청한 백엔드가 `MIXED` 또는 `JIT`여도 VM 안전 지점 경로를 사용합니다. 제한이나 취소 요청은 이미 실행 중인 호스트 콜백을 강제로 종료하지 않습니다.

`enable_execution_control=True`일 때 어느 Python 스레드에서든 `request_cancellation()`을 호출하여 현재 실행의 취소를 요청할 수 있습니다. 요청이 반환될 때까지 세션을 유지해야 합니다. 취소 요청은 해당 세션에 남으므로 다음 실행을 시작하기 전에 새 `Session`을 만들어야 합니다.

`capability_policy_enabled=True`이면 표준 라이브러리 부작용은 `allowed_script_capabilities`가 허용한 범위로 제한됩니다. 대상은 파일 읽기/쓰기/삭제, 환경 읽기/쓰기, 콘솔 입력/출력, 작업 생성 및 프로세스 디렉터리 변경입니다. `chdir`에는 `ENVIRONMENT_WRITE`와 `PROCESS_DIRECTORY_CHANGE`가 모두 필요합니다. 이 정책은 내장 함수에만 적용되며 호스트 콜백의 권한 검사는 호스트가 담당합니다. 정책이 비활성화되면 이 내장 기능은 기본적으로 모두 허용됩니다.

`RunResult`에는 성공, 종료 요청, 종료 코드와 구조화된 진단이 있습니다. `set_jit_policy`로 hot threshold, 컴파일 시도 수, 캐시 항목과 선행 컴파일 예산을 조정하고 `jit_stats`로 결과를 확인합니다. 여기에는 VM 값 브리지를 거치지 않고 다른 네이티브 진입점에 도달한 세대 범위 JIT 호출 수를 나타내는 `direct_native_calls`도 포함됩니다.

`jit_stats`는 최근 호출의 독립적인 스냅샷입니다. 실제 실행 진입은
`native_leaf_executions`, `native_cfg_executions`, `bridge_executions`,
`linear_executions`, `vm_trampoline_executions`, `controlled_native_executions`,
`aot_executions`로 집계하며 오류나 제어 이동으로 끝난 진입도 포함합니다. 컴파일 수와
캐시 항목 수는 별도입니다. `direct_native_calls`, 스칼라 빠른 경로, 형식화된 루프는
중첩 이벤트이므로 독립 함수 진입으로 더하지 않습니다. VM 실행에서는 이 실행 진입
카운터가 0입니다. 컴파일된 코드를 재사용해도 호출마다 초기화되며 `clear()`는 세션
스냅샷을 초기화합니다. `jit_cache_stats`는 재사용 컨텍스트의 누적 컴파일 및 리소스
통계를 유지합니다.

<a id="session-jit-cache"></a>

`JitCacheLimits`는 적격한 일반 `Session.call`/네이티브 `CallFunction` 호출 사이에 보존할 코드를 관리하며 바이트코드 캐시와 별개입니다. `JitPolicy.reuse_session_code`의 기본값은 `True`입니다. `max_compile_attempts` 또는 `max_cached_entries`의 0은 보존된 컨텍스트에서도 횟수 무제한을 뜻합니다. 재사용에는 호환 JIT 팩터리와 JIT를 허용하는 유효 실행 정책이 필요합니다. 지속 세션과 안정된 콜백은 코드를 재사용할 수 있습니다. 네이티브 바인딩 카탈로그는 런타임 리비전을 제공해야 합니다. 실행 제어 요구 사항이 같으면 제어된 네이티브 실행도 컨텍스트를 재사용할 수 있습니다.

`max_native_bytes`의 기본값은 8 MiB, `max_retained_payload_bytes`는 32 MiB이며 0은 해당 리소스의 보존을 막습니다. `session.set_jit_cache_limits(...)`, `session.jit_cache_limits`, `session.jit_cache_stats`로 설정하고 확인합니다. 통계는 확정된 컨텍스트 누적값이며 실시간 또는 프로세스 전체 메모리 측정값이 아닙니다. 정책, 제한, 설정, 모듈, 백엔드 또는 관찰 가능한 호스트 경계가 바뀌면 컨텍스트를 무효화하고 리소스 제한에서는 안전하게 폴백합니다.

제어된 `Backend.MIXED`는 기본적으로 VM을 사용합니다. `session.set_jit_policy(JitPolicy(enable_controlled_mixed=True))`로 호환되는 제어된 네이티브 실행을 선택하며 명시적 `Backend.JIT`는 이 설정과 별개입니다. 지원하지 않는 진입점은 실행 전에 폴백하고 디버깅, 상태를 보존하는 초기화, 한도가 적용된 정리는 VM을 사용합니다.

`set_input`은 입력을 설정하고 `set_hot_reload_enabled`는 핫 리로드를 제어합니다. 컨테이너는 스냅샷이므로 Python 목록이나 사전의 수정은 스크립트로 쓰기 반영되지 않습니다. 변경 가능한 세션을 스레드 사이에서 공유하지 마십시오.


선택적으로 제공되는 함수나 메서드를 호출하기 전에 `has_function(name, argument_count)`와 `has_method(instance, name, argument_count)`로 존재 여부를 확인할 수 있습니다. 전역 변수 읽기와 쓰기에는 Core 실행 계약에 따라 `hot_reload_enabled=True`를 통한 상태 유지가 필요합니다.

세션을 만든 스레드에서 `session.reload_status`를 읽기 전에 `CAPABILITIES & RuntimeCapabilities.RELOAD_STATUS`로 지원을 확인합니다. `ReloadStatus` 스냅샷은 `operation_id`로 리로드를 식별하며 `current_generation`과 `committed_generation`을 구분합니다. `PENDING`은 후보 프로그램이 초기화를 기다리는 상태입니다. `REJECTED`는 이전 프로그램을 유지하고 `ROLLED_BACK`은 복원이 끝났음을 뜻합니다. `APPLIED_WITH_ERRORS`는 후보가 커밋된 뒤 실행이나 정리에 실패했음을 뜻합니다. `RESET_REQUIRED`는 후속 실행을 막습니다. `clear()`로 초기화할 수 없으면 세션을 다시 만드세요. 호스트가 보유한 외부 값으로 인해 후보 적용 준비가 차단되고 이전 상태가 유지될 수 있습니다.

## VM 선택

표준 Python 패키지에는 정적 바이트코드 VM이 포함되므로 일반 세션에는 외부 VM 라이브러리가 필요하지 않습니다. `RuntimeCapabilities.VM_BACKEND_SELECTION`은 선택 API 지원을 나타냅니다. 첫 실행 전에 세션을 만든 스레드에서 유휴 상태로 `set_vm_backend(path)`를 호출하여 호환되는 VM 파일이나 디렉터리를 선택할 수 있습니다. `None`이나 빈 문자열은 선택을 해제합니다. 교체 실패 시 기존 VM을 유지합니다. 실행이 시작되면 선택이 고정되며 `clear()`로도 해제되지 않으므로 변경하려면 새 세션을 만드세요. 이 선택은 `Backend.VM`, `MIXED`, `JIT` 실행 정책과 독립적입니다.

`get_vm_backend_info()`는 `id`, `version`, `target`, `library_path`, `capabilities`, `selected`, `frozen`을 포함한 독립적인 `VmBackendInfo` 스냅샷을 반환합니다. 정적 VM의 `library_path`는 비어 있으며 `selected`로 VM 미선택 상태와 구분합니다. 백엔드 교체나 세션 종료 후에도 스냅샷은 유효합니다.

## 명시적 JIT 선택

실행 정책과 backend 선택은 별개입니다. 설치된 모듈 환경이 없으면 Runtime에는 기본 모듈이 없으며 미선택 mixed는 VM, 강제 jit는 실행 전 실패입니다. 소유 스레드에서 Session이 유휴 상태일 때 `Session.set_jit_backend(path)`로 별도 설치한 호환 파일/디렉터리를 선택합니다. `set_jit_backend(None)` / `set_jit_backend("")`은 선택을 해제합니다. 교체 실패는 이전 선택을 유지합니다. `get_jit_backend()` / `get_jit_backend_info()`는 실제 식별/네이티브 기능이며 `backend-services`은 Core 서비스 계약일 뿐입니다. 지속 세션에서 조건을 충족하는 호출은 선택된 백엔드를 사용할 수 있습니다. 검색/ABI 규칙은 JIT 가이드를 참조하세요.

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/ko-KR/index.md)

```python
with Session(backend=Backend.MIXED) as session:
	session.set_jit_backend("/absolute/path/to/Jit")
	session.load_source("int main(){ return 0; }")
	result = session.run()
```

## 일회 평가와 백엔드 기능

일반 세션에서 `run(RunMode.EVAL_ONCE)`는 호출마다 모듈 초기화를 한 번 실행하고 `main`을 호출하지 않습니다. 선택한 JIT 백엔드로 초기화 코드를 컴파일할 수 있습니다. `RunMode.EVAL`은 증분 평가를 유지하고 `RunMode.SCRIPT`는 `main`도 호출합니다. `RuntimeCapabilities.EVAL_ONCE`는 이 모드의 지원을 나타냅니다. 핫 리로드 세션은 초기화된 전역 상태를 유지하고 상태를 보존하는 초기화에 VM을 사용합니다.

`get_jit_backend_info()`는 소유된 `JitBackendInfo` 스냅샷을 반환합니다. `capabilities`는 `JitBackendCapabilities`의 `NATIVE_CODE`, `PORTABLE_EXECUTION`, `NATIVE_TYPED_LOOPS`, `DIRECT_SCALAR_CALLS`로 선택한 모듈을 설명합니다. `CAPABILITIES`는 Core 서비스를 설명합니다. 스냅샷은 백엔드 교체나 Session 종료 후에도 유효합니다.

## 컴파일 서비스와 진단 심각도

`COMPILE_CAPABILITIES`와 `CompileCapabilities.ANNOTATION_PROCESSING`은 `CAPABILITIES` 및 선택한 JIT 백엔드와 독립적인 컴파일 서비스를 나타냅니다. `Diagnostic.severity`는 `DiagnosticSeverity`의 `ERROR`, `WARNING`, `INFORMATION`, `HINT` 값입니다. 작업 성공 여부는 `RunResult.success`로 판단하세요. 진단 목록이 비어 있지 않아도 치명적이지 않은 메시지만 포함할 수 있습니다.

`session.set_annotation_processing_service(handle)`는 세션이 유휴 상태일 때 네이티브 SDK 확장이 만든 유효한 불투명 핸들을 연결합니다. 세션은 설정을 복사하므로 연결 후 생성자는 자신의 핸들을 해제할 수 있습니다. 서비스 해제 또는 세션 종료까지 프로세서의 네이티브 모듈과 콜백 데이터를 유지하세요. `None` 또는 `0`은 서비스를 해제합니다. 임의의 정수 주소는 유효한 서비스 핸들이 아닙니다. `register_annotation(...)`은 주석 어휘를 선언하며 컴파일 시점 프로세서를 설치하지 않습니다.

## 호스트 터미널 연결

`Session(console_query=..., console_control=..., console_write=...)`로 터미널 콜백을 설정합니다. 조회는 `ConsoleInfo`를 반환하며 크기 0은 미상입니다. 제어는 `ConsoleOperation`와 정수 두 개를 받습니다: ANSI 색상 0..15／기본 -1, 줄 모드 after/before/all=0/1/2, 0부터 시작하는 열／행, 표시 0/1. 동기 성공이면 `True`를 반환합니다. 텍스트 콜백은 줄바꿈 없는 정확한 문자열을 받고 `bool`을 반환합니다. 생략 시 기존 줄 출력 버퍼를 유지합니다. 종료 완료까지 콜백을 유지하고 작업 호출을 동기화하세요. 예외는 `CallbackError`로 전달합니다. 작업 간 전체 터미널 순서를 조정하세요.

## 해석된 모듈 환경과 일반 컴파일 처리

`Session(module_environment=handle)`은 일치하는 네이티브 SDK의 `VeroScript::Interop::CreateModuleEnvironment`가 만든 불투명 스냅샷 핸들을 받습니다. 세션이 불변 환경을 유지하므로 생성자는 세션 생성 후 자신의 핸들을 해제할 수 있습니다. 환경은 소스 소유 범위, 정확한 가져오기 매핑, 리소스, 컴파일 서비스 및 실행 의존성을 제공합니다. 표준 라이브러리가 필요하면 검색 루트와 소유 범위도 환경에 지정해야 합니다. 이 모드에서 Python은 패키지의 표준 라이브러리 루트를 추가하지 않습니다. 스냅샷을 바꾸려면 새 세션을 만드세요. 환경이 바인딩 카탈로그를 소유하면 로컬 등록이 거부되므로 게시 전에 바인딩을 통합하세요. `None` 또는 `0`은 일반 설정을 사용하며 임의의 정수 주소는 유효하지 않습니다. `RuntimeCapabilities.MODULE_ENVIRONMENT`로 지원 여부를 확인합니다.

`set_compile_processing_service(handle)`은 네이티브 SDK의 일반 처리 서비스를 연결합니다. 주석 서비스와 동일한 유휴 상태, 생성 스레드 및 수명 규칙을 따르며 `None` 또는 `0`으로 해제합니다. 기능 비트는 `CompileCapabilities.GENERAL_PROCESSING`입니다. 스크립트 컴파일 확장은 `set_script_compile_extensions(...)`로 설정합니다.

## 클로저와 지역 static 저장소

스크립트 함수 값은 초기화 캡처와 `mutable`을 지원합니다. `Value.clone()`과
`retain_callable()`은 기존 클로저 환경을 공유하며 lambda 식을 다시 평가하면 독립된
환경이 만들어집니다. 참조 캡처는 생성 함수가 반환된 뒤에도 저장소를 유지합니다.
세션이 닫히면 보존된 callable은 무효화됩니다.

지역 `static` 변수는 실행 상태에 속합니다. 일반 `call()`은 새 상태를 만들고
`hot_reload_enabled=True`는 호출 사이에 상태를 유지합니다. 세션을 닫으면 보존된
정적 상태가 해제됩니다. 독립형 [Python AOT](aot.md)는 소스 lambda와 지역 `static` 선언을 거부합니다.

## Session 백엔드 선택 인수

`Session.set_vm_backend` / `set_jit_backend`

이 Session 설정자는 호환 파일／디렉터리 또는 전달된 환경에 해석기가 있는 경우 설치된 백엔드 ID를 받습니다. `auto`는 환경 후보를 요청하며 VM에서는 보존된 제품 대체 VM도 사용할 수 있습니다. `none`이나 네이티브 빈 문자열은 선택을 비활성화하고 `None` / `""`도 빈 요청이 됩니다. 새 세션에서 백엔드 설정을 생략하면 환경 선택을 허용하며 명시적 해제와 다릅니다. 기존 세션을 재구성할 때 VM 선택을 생략하면 실행 도메인을 보존합니다. 명시적 선택 실패는 이전 백엔드를 보존합니다. 생성 스레드의 유휴 상태에서 설정하고 VM 변경은 실행 전에 하세요. Clear/reload로도 도메인 고정이 해제되지 않습니다. JIT 선택은 VM/Mixed/JIT 실행 정책과 독립적입니다.

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
