# Python runtime과 static tool

## typed-loop 통계

`native_typed_loop_executions`는 native typed-loop kernel invocation을 셉니다. source-visible runtime failure를 보고하고 끝난 invocation도 포함합니다. 이는 execution observability counter이며 success counter가 아닙니다.

## 런타임 어댑터

`VeroScriptPython`은 `VeroScriptInterop` 위 `Session`, `Value`, `Package`, retained callable, `HostParameter`, host function/class를 제공합니다. documented session/callable은 creator-thread confined이며 explicit close가 필요합니다. native operation은 허용될 때 GIL을 release하고 callback은 reacquire하므로 callback-owned state는 외부 동기화가 필요합니다. `ExternalReference`, `GenericHostClass`, `available_languages`, `get_language`, `set_language`, `merge_catalog_directory`도 public surface이며 deployed catalog가 language를 발견하고 API/catalog/reference-kind token은 번역하지 않습니다. `Value` container는 explicit type/owned snapshot을 사용합니다. signed `char`/`int8`는 `-128..127` 범위의 Python `int`로 표현됩니다. character map key와 set element는 snapshot, lookup, `to_python()`에서도 이 표현을 유지하며 범위를 벗어난 입력은 잘라내지 않고 거부합니다. mutation callback exception은 mutation 뒤 보고됩니다.

## 정적 프런트엔드

`VeroScriptPythonFrontend`는 input module import/execute 없이 typed declaration과 range/function/class/enum/field/parameter/decorator/doc/import/type annotation을 parse합니다. unsupported execution은 structured diagnostic으로 실패합니다. `VeroScriptPythonBindGen`은 Catalog/API reference/input manifest를, `VeroScriptPythonTranspiler`는 typed subset, relative import, source map, complete-tree validation을 제공합니다. BindGen과 Transpiler는 ASCII VeroScript identifier가 아니거나 `do` 같은 language keyword인 생성 이름을 compiler validation이 꺼져 있어도 거부합니다. Transpiler는 core의 statement nesting 32단계 제한도 적용하고 Python `pass`를 유효한 빈 VeroScript block으로 출력합니다. 이름이 `_`로 시작하는 class와 top-level function은 export되지 않으며 생성된 public contract에서 참조할 수 없습니다. 출력되는 숫자 상수는 유한해야 하며 core의 suffix 없는 signed 32-bit integer literal 범위에 들어가야 합니다. Python list, set, dictionary literal은 core의 brace-initializer grammar에 맞게 typed declaration initializer에서만 허용됩니다. typed subset에 nullable-type contract가 생기기 전까지 Optional annotation과 Python identity comparison은 거부됩니다. `break`와 `continue`에는 이를 둘러싸는 출력된 `while` loop가 필요합니다.

지원되는 integer `IntEnum`/`IntFlag` expression은 안전하게 평가되어 core enum grammar가 요구하는 signed 64-bit literal로 출력됩니다.

`self`는 instance method receiver일 때만 `this`로 다시 쓰며 일반 `self` identifier는 그대로 유지합니다. `None`은 `-> None` function 또는 constructor의 값 없는 `return;`으로만 출력합니다.

non-`void` function은 정적으로 지원되는 모든 path에서 값을 반환해야 합니다. annotated local은 function body 바로 아래에서 선언해야 하며 Python과 VeroScript의 scope 차이를 방지하기 위해 `if` 또는 `while` block 안의 선언은 거부됩니다.

multi-file project에서는 export되는 module variable과 다른 module variable 또는 type의 충돌을 core symbol namespace에 맞게 출력 전에 거부합니다.

## 세션 실행 계약

`Session`은 `load_source`, `load_file`, `load_package`, `reload_package`, `build_package_file`, `run`, `reload`, `diagnostics`, `set_input`을 제공합니다. `set_hot_reload_enabled`는 state-preserving reload를 제어하고 `request_cancellation`은 다른 thread의 cooperative cancel로 active callback을 강제 중단하지 않습니다. `set_jit_policy`/`jit_stats`는 adaptive backend/last run을 공개합니다. `RuntimeCapabilities`는 `CAPABILITIES`의 Core 서비스를 해석합니다. 선택한 모듈은 `session.get_jit_backend_info()`로 확인합니다. `native_code`는 네이티브 코드 생성 여부이며 `capabilities`는 `JitBackendCapabilities`의 `NATIVE_CODE`, `PORTABLE_EXECUTION`, `NATIVE_TYPED_LOOPS`, `DIRECT_SCALAR_CALLS`를 나타내고 `HOST_PLATFORM`과 독립적입니다. `cost_model_vm_fallbacks`와 `native_typed_loop_executions`는 해당 last-run 결정과 실행을 셉니다.

Python 어댑터는 `reuse_session_code`, `enable_controlled_mixed`, 캐시 제한과 통계를 네이티브 Session에 전달합니다. 적격 조건과 세대별 집계는 [C++ Session](../../../../../Cpp/Resources/Docs/Developer/ko-KR/session.md#session-jit-cache)에서, Python API의 기본값과 할당량 의미는 [Python Session](../../User/ko-KR/session.md#session-jit-cache)에서 관리합니다. 어댑터에서 0이 아닌 횟수를 별도로 요구하거나 제어된 실행의 재사용을 일괄 차단하지 않습니다.

## 값, 호출과 채널

`Value.channel`은 bounded/unbounded channel을 만들고 `send`, `try_send`, `send_for`, `receive`, `try_receive`, `receive_for`, `close_channel`을 제공합니다. channel은 cross-worker 공유이고 global은 worker-local입니다. call surface는 `has_function`, `has_method`, `call`, `call_method`, `call_method_bypassing_interceptor`, `construct_object`, `retain_callable`, `get_field`, `set_field`, `get_global`, `set_global`, `apply_class_defaults`를 포함하며 bypass는 entry interceptor만 건너뜁니다.

## 호스트 바인딩, 리플렉션과 영속성

registration은 `register_function`, `register_annotation`, `register_type_alias`, `register_generic_host_type`, `instantiate_generic_host_type`, `register_host_class`, `register_host_enum`, `register_host_protocol`, `register_host_member`를 포함합니다. `export_reflection_json`, `export_bindings_json`은 inspection output입니다. `register_serializer`, `serialize_object`, `deserialize_object`, `enumerate_object_references`는 Interop external-serialization/borrowed-reference contract를 따릅니다. `set_mutation_callback`은 mutation을 보고하고 `set_instance_call_interceptor`는 resolved target을 continue/handle합니다.

## Python 스레드 경계

channel operation은 native 대기 중 GIL을 release하지만 `Session`/`Value`를 일반 thread-safe로 만들지 않습니다. `request_cancellation`만 다른 Python thread에서 명시적으로 안전하며 caller는 native request 반환까지 session을 보유합니다. native callback exception은 call boundary 뒤 Python으로 변환되고 `direct_native_calls`는 별도 JIT counter입니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/runtime-and-tools.md) · [fr-FR](../fr-FR/runtime-and-tools.md) · [ja-JP](../ja-JP/runtime-and-tools.md) · [ko-KR](runtime-and-tools.md) · [zh-CN](../zh-CN/runtime-and-tools.md) · [zh-TW](../zh-TW/runtime-and-tools.md)
