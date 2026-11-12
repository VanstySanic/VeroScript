# Interop 값, 콜백과 패키지

## value와 container

ABI는 fixed-width scalar, string, enum, host object, array, map, ordered map, set, queue, stack, deque, channel을 노출합니다. container enumeration callback은 borrowed value를 받습니다. 타입은 VeroScript 계약과 일치해야 합니다. 원소 값의 타입 검증은 `TYPE_MISMATCH`를 반환하며 실행 실패는 `ENGINE_ERROR`를 반환할 수 있습니다. 암시적 변환은 하지 않습니다. multiset 개수는 원소를 복사하거나 snapshot을 할당하지 않고 발생 배열의 길이를 합산합니다.

객체 슬롯은 원본 값의 모듈 메타데이터를 사용하여 선언된 기반 클래스나 인터페이스에 대입할 수 있는 파생 스크립트 객체를 허용합니다. `Base`를 상속하고 `IRead`를 구현하는 `Child`는 `array<Base>` 또는 `queue<IRead>`에 넣을 수 있습니다. 컨테이너 선언은 불변이며, `array<Child>`를 `array<Base>`로 변환하거나 숫자 변환을 추가하지 않습니다.

map/set 키 타입 선언은 빈 컨테이너와 multimap/multiset에서도 검증합니다. 지원하지 않는 내장 타입(`bool`, `float`, `double`, 8비트 부호 없는 정수, 16비트 정수, 컨테이너, 함수, 참조)은 이전 값을 바꾸지 않고 `INVALID_ARGUMENT`를 반환합니다. 기본 키 타입은 `char`, `int`, `unsigned int`, `int64`, `uint64`, `string`, `wstring`과 해당 별칭입니다. 이름이 있는 enum/host 타입과 구체화된 host 타입은 이를 사용하는 세션에서 해석합니다.

channel capacity는 first use 전에 configure합니다. send, receive, try, timed, count, capacity, closed, close는 separate status behavior를 가집니다. closing은 blocked operation을 wake하고 API가 제공하면 error text는 owned UTF-8 buffer로 반환됩니다.

## 콜백 컨텍스트

native function, generic host-type factory, instance-call interceptor, mutation callback, external serializer/deserializer는 context setter/callback result value로 failure를 report합니다. callback exception은 C를 통해 return하기 전 convert합니다. user data, host-object payload, managed root는 가능한 모든 callback entry 동안 살아야 합니다.

잘못된 ApplyConfig 설명자는 적용 전에 거부합니다. Core 적용 중 할당 실패나 내부 오류가 발생하면 설정이 일부 적용될 수 있으므로, 해당 설정에 의존하기 전에 전체 구성을 다시 적용해야 합니다. C 취소 핸들은 성공과 실패 모두에서 세션에 실제로 설치된 제어를 따릅니다. Linux 할당 실패 회귀는 제어 활성화/비활성화, 취소 일관성과 재시도 복구를 검증합니다.

external serialization은 ABI visitor contract로 reference를 enumerate할 수 있습니다. reference kind, visitor order, borrowed-value lifetime은 machine-facing data입니다. visitor는 borrowed value를 retain하거나 owning session을 reentrant mutate하면 안 됩니다. map-entry/set-element visitor도 borrowed-view rule을 따르고 partial traversal을 success로 바꾸지 않고 callback failure를 propagate합니다.

reentrant destruction/session clearing은 general callback feature가 아닙니다. implementation이 reject하면 ABI는 engine error를 반환하고 owning session을 partial clear하지 않고 보존합니다.

## package와 reflection

package API는 package handle read/destroy, kind/name/version, metadata, canonical signed byte, load/reload/build operation을 노출합니다. reflection은 loaded type/field/method/global/enum/interface의 JSON metadata를 export합니다. JSON field name/type name은 machine contract로 localize하지 않습니다.

`vs_interop_session_build_package_file`은 `message_id` `diag.failed_to_build_package`를 가진 structured diagnostic으로 build failure를 report합니다. 첫 diagnostic argument는 underlying build reason입니다. consumer는 selected locale에서 `vs_interop_session_format_diagnostic`을 사용하며 `message`를 diagnostic contract 대체물로 취급하지 않습니다.

## 채널

channel value는 `vs_interop_value_create_channel` 또는 `vs_interop_value_create_bounded_channel`로 만듭니다. zero bounded capacity는 unbounded channel입니다. `vs_interop_value_channel_send`는 accepted까지 block, `vs_interop_value_channel_try_send`는 non-blocking, `vs_interop_value_channel_send_for`는 timeout을 적용합니다. receive family에는 대응하는 blocking/non-blocking/timed operation이 있습니다. count, capacity, closed-state, close는 separate ABI call입니다. closing은 waiter를 wake합니다. host는 closed result를 terminal로 다루고 callback scope 뒤 borrowed value를 reuse하지 않습니다.

## 제네릭 호스트 형식 컨텍스트

`vs_interop_generic_type_context_*` function은 generic host-type factory의 template argument count/argument를 노출합니다. factory는 result type, member, protocol declaration, error를 같은 borrowed context로 제공합니다. context accessor/setter는 factory callback 동안만 valid합니다. callback은 return 전에 registration을 끝내야 하며 context는 retained builder object가 아닙니다.

## call과 interception

`vs_interop_intercept_context_*`는 receiver, resolved target, argument, `proceed`, callback error channel을 노출합니다. `proceed`는 이미 resolved된 call의 interceptor continuation입니다. 이후 inbound dispatch가 필요한 host는 entry interceptor를 bypass하는 session call API를 쓸 수 있으며 method body의 nested call은 interceptable로 남습니다.

`vs_interop_call_context_*`는 빌린 인수와 변경 가능한 인수에 대한 접근, `host_this`, 프로그램 세대 및 오류 채널을 제공합니다. 함수와 callable 호출은 세션의 재진입 및 수명 규칙을 따릅니다. callable 보유 인터페이스는 `vs_interop_session_retain_callable`, `vs_interop_callable_destroy`, `vs_interop_callable_is_valid`, `vs_interop_callable_invoke`입니다. 소유 스레드 전용 호출과 약한 `this` 캡처는 수명 정책이며 스레드 간 접근을 동기화하지 않습니다.

## 외부 직렬화 컨텍스트

serializer context는 등록된 타입, mutation version 및 빌린 필드를 제공합니다. serializer는 `vs_interop_external_serialize_context_set_bytes`로 출력 바이트를 복사하며 교체 실패 시 이전에 승인된 payload를 보존합니다. deserializer는 바이트와 encoded version을 읽고 이름으로 지정한 필드 할당을 임시 transaction에 추가합니다. 둘 다 error setter와 callback result로 실패를 보고합니다. reference enumeration은 별도 순회이며 빌린 값의 수명을 연장하지 않습니다. adapter는 callback 이후에도 필요한 byte/string을 복사합니다.

## 어노테이션 이름 비교 규칙

`vs_interop_session_register_annotation`으로 등록한 어노테이션 이름은 기본적으로 대소문자를 구분합니다. 해당 정의에서 ASCII 대소문자를 구분하지 않으려면 `name_flags`를 `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE`로 설정합니다. 인수 이름과 메타데이터 키는 계속 정확히 비교하며 리플렉션은 소스 표기를 보존합니다. 이름이 충돌하는 등록은 카탈로그를 변경하지 않고 거부합니다. `struct_size`는 `sizeof(vs_interop_annotation_desc)`로 설정하십시오. 불완전한 설명자와 알 수 없는 플래그는 거부합니다. 일치하는 최신 Interop 헤더와 런타임으로 호스트를 다시 빌드하십시오.

## variant

[인터롭 SDK — variant](../../User/ko-KR/index.md#exported-value-lifetime)을 참조하세요.

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

목표가 variant로 확정된 위치에서는 암시적 상자화가 가능합니다. 읽기는 cast<T> 또는 try_cast<T>를 명시해야 합니다. variant_is<T>는 변환 없이 정확한 형식을 검사합니다. try_cast의 optional<T>는 core.vs가 필요합니다. 빈 상자 강제 추출은 실패하며 일반 변환 불일치는 빈 optional을 반환합니다. 복사 실패, 취소, 제한 초과는 오류입니다.
## 세션 소유권과 콜백 스냅샷

C로 내보낸 변경 가능한 모듈 컨테이너는 현재 비어 있어도 원래 세션에 속합니다. 세션이 열려 있는 동안 공유 C 별칭과 복제본이 원래 그래프를 유지하므로, 한 핸들을 해제해도 다른 핸들이 보유한 객체는 파괴되지 않습니다. 마지막 보유 참조가 해제된 뒤 소유 세션의 안전 지점에서 정리할 수 있습니다. 독립적인 스칼라, 호스트 값, 열거형 값에는 세션 제한을 추가하지 않습니다.

C에서 만든 변경 가능한 컨테이너가 모듈 의존 그래프를 담을 수 있다면, 비어 있어도 처음 전달한 세션에 귀속됩니다. 워커는 독립적인 값만 소유 C 핸들로 내보낼 수 있으며, `variant` 또는 함수 슬롯을 가진 컨테이너는 비어 있어도 제외됩니다. 이런 외부 C 저장소를 워커에 넘겨 변경하게 할 수도 없습니다.

모듈에 의존하는 객체와 컨테이너 그래프는 복사한 뒤에도 원래 세션에 속합니다. 호출, 필드, 전역, 훅, 직렬화기는 다른 세션의 그래프를 메타데이터 접근이나 변경 전에 `ENGINE_ERROR`로 거부합니다. 독립적인 스칼라와 호스트 값의 전달 규칙은 유지됩니다. 빌린 네이티브 호출 및 역직렬화 컨텍스트는 다른 세션에서 동기 작업을 하는 동안에도 원래 세션을 유지하며, 수명은 원래 콜백이 반환할 때 끝납니다.

교체 모듈의 초기화가 확정되기 전에는 모듈에 의존하는 스크립트 결과를 소유 C 값 핸들에 보관할 수 없습니다. 대입과 복제는 `ENGINE_ERROR`를 반환하며, 거부된 대입은 대상 소유 핸들의 기존 값을 보존합니다. 콜백의 빌린 반환 슬롯을 통해 같은 스크립트 호출에 결과를 돌려줄 수 있으며, `vs_interop_intercept_context_proceed(context, out_return)`도 이를 지원합니다. 임시 결과는 계속 소유 세션의 안전 지점에서 정리됩니다.

`vs_interop_value_reset`은 콜백의 빌린 인수와 반환 슬롯에 대해 `INVALID_ARGUMENT`를 반환합니다. 소유 핸들을 재설정하거나 값을 독립적인 스칼라로 바꾸면 이전 세션에 대한 소속이 해제되어 독립적으로 사용할 수 있습니다.

map/set visitor는 원본 소유 핸들을 초기화하거나 파괴할 수 있습니다. 열거는 스냅샷의 모든 뷰가 사라질 때까지 등록된 그래프를 유지합니다. 빌린 항목 뷰는 해당 visitor 호출 동안만 유효하므로 보관하려면 복제해야 합니다. 세션 실행과 구성의 재진입 제한은 계속 적용됩니다.

전체 구성을 적용해도 전용 setter로 설정한 숫자, 메모리, 표준 라이브러리 제한은 유지됩니다. 외부 메모리 등록은 출력 게시에 성공한 뒤에만 소유권을 인수하며 실패하면 호출자에게 소유권이 남습니다. 실행 예외는 유효한 결과 출력을 실패로 초기화하고 할당 실패는 `OUT_OF_MEMORY`를 유지합니다.

값 교체 또는 초기화가 확정된 뒤 이전 값 정리가 실패할 수 있습니다. 게시된 값은 유지되며 정리 오류는 이전 세션의 오류 채널에 남습니다. 이미 확정된 게시를 나중에 실패로 바꾸지 않습니다.

C 함수 값과 보존된 callable은 Core의 클로저 규칙을 따릅니다. 복제본은 가변 캡처 환경을 공유하고, 별도로 생성된 클로저는 독립된 환경을 가지며, 참조 캡처는 원본 프레임이 끝난 뒤에도 유효합니다. 지역 static 저장소는 실행 중인 프로그램 상태의 선언 위치에 속하며 그 상태 안에서 공유됩니다. C 호출 사이에 상태를 유지하려면 `vs_interop_session_set_hot_reload_enabled`를 사용합니다. 일반적인 새 실행은 자체 상태를 초기화하고 정리합니다. 내부 슬롯은 일반 전역 변수로 리플렉션에 노출되지 않습니다. 기존 C API에서 선택한 VM/JIT가 이러한 언어 기능을 처리합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/values-and-callbacks.md) · [fr-FR](../fr-FR/values-and-callbacks.md) · [ja-JP](../ja-JP/values-and-callbacks.md) · [ko-KR](values-and-callbacks.md) · [zh-CN](../zh-CN/values-and-callbacks.md) · [zh-TW](../zh-TW/values-and-callbacks.md)
