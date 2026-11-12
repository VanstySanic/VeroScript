# Python 값, 컨테이너와 수명

## 타입 지정 컨테이너와 참조

공개 factory는 `Value.array`, `Value.map`, `Value.set`, `Value.queue`,
`Value.priority_queue`, `Value.stack`, `Value.deque`, `Value.channel`입니다.
요소 타입 또는 키와 값의 타입을 명시하므로 빈 컨테이너도 모호하지 않습니다.
일반 map과 set은 고유 항목을 사용하며 multi-map과 multi-set은 해당 런타임 또는
호스트 바인딩을 사용해야 합니다. `to_python()`은 지원되는 경우 snapshot을
반환하며 snapshot의 수정은 스크립트 컨테이너에 반영되지 않습니다.

`HostParameter(..., by_ref=True)`는 `Value`로 전달되는 변경 가능한 참조를,
`const_ref=True`는 읽기 전용 참조를 나타냅니다. 두 플래그는 함께 사용할 수 없고,
실패한 호출 또는 `exit` 이후에는 write-back이 수행되지 않습니다.

## channel 공개 API

정확한 작업 이름은 `send`, `try_send`, `send_for`, `receive`, `try_receive`,
`receive_for`, `close_channel`입니다. `try_send`와 `send_for`는 bool을 반환하고
비차단 또는 시간 제한 수신은 값이나 `None`을 반환합니다. `count`,
`channel_capacity`, `channel_closed`로 현재 상태를 확인할 수 있습니다.

`Value`는 Interop ABI가 관리하는 스크립트 값이며 독립된 네이티브 핸들을 소유합니다. 세션 작업과 유지된 callable에는 세션 수명에 따른 제약도 적용됩니다. 호스트 객체를 소유하는 값과 해당 프로토콜은 생성한 세션을 닫은 후에도 사용할 수 있습니다. [호스트 바인딩](host-bindings.md)을 참조하세요.

정수, 부동소수점, bool, 문자열, wide 문자열, enum, 배열, 시퀀스, map, set, channel에 타입이 지정된 접근이 있습니다. Python 쪽 컨테이너는 스냅샷입니다.

`Session.retain_callable()`은 `RetainedCallable`을 반환합니다.

```Python
from veroscript import Session

with Session() as session:
	session.load_source("""
int add(int left, int right) { return left + right; }
function(int, int)->int make_callback() { return add; }
void main() {}
""", "callback.vs")
	function_value = session.call("make_callback")
	with session.retain_callable(function_value) as callback:
		result = callback(20, 22)
		assert result == 42
```

보존 callable은 생성 스레드에 속하며 리로드나 세션 종료 뒤 무효화될 수 있습니다. `is_valid`를 확인하고 사용 후 닫으십시오. map/set 반복과 `keys()`, `values()`, `items()`는 소유한 스냅샷을 반환하므로 보관할 수 있습니다. 스칼라는 일반 Python 값으로 변환되며 나머지 요소는 독립된 소유 `Value` 핸들을 가집니다. 일반 map/set의 순서는 정해져 있지 않으며 `ordered=True`는 최초 삽입 순서를 보존합니다. 이러한 읽기 연산은 요소에 직접 쓰지 않습니다. 변경에는 VeroScript 함수 또는 호스트 콜백을 사용하세요. 타입, 스레드, 콜백 오류는 `TypeMismatchError`, `ThreadError`, `CallbackError`로 보고됩니다.

`Value(None)`은 VeroScript `null` 값을 만듭니다. nullable이 아닌 타입을 선언한
값에 `None`을 대입하면 `TypeError`가 발생합니다. `Value.priority_queue("int",
[3, 1, 2])`로 priority queue를 만들 수 있으며 일반 `map`과 `set`은 중복을
보존하지 않습니다.

`kind`, `type_name`, `value` 속성은 현재 표현을 보여 줍니다. `clone()`은 호출자가 소유하는 별도의 `Value` 핸들을 만듭니다. 런타임의 clone 규칙을 따르며 깊은 복사를 보장하지 않습니다. 각 `Value`는 네이티브 핸들을 소유하고 Python 객체가 소멸할 때 해제됩니다. `Value`에는 공개 `close()` 메서드가 없습니다. 세션 작업이 반환한 값은 호출자가 소유합니다. 세션과 보존 callable은 컨텍스트 관리자로 닫으십시오.

차단 `receive()`는 GIL을 해제하므로 다른 Python 스레드가 계속 실행될 수 있습니다. channel 요소 형식은 런타임 channel 계약을 따라야 합니다.

소유한 Python 호스트 값은 참조 순환이 있거나 원래 세션이 닫힌 뒤에도 살아 있는 동안 clone, 동등 비교, hash 콜백을 유지합니다. 생성 스레드에서 세션을 자동으로 정리할 때도 살아 있는 프로토콜을 보호합니다. 확실한 해제에는 세션 컨텍스트 관리자를 사용하고, 마지막 값을 다른 스레드로 넘기기 전에 생성 스레드에서 세션을 닫으십시오.

컨테이너는 `len`, 반복, 인덱싱, `get`, `contains`로 읽습니다. `Session.construct`는 객체 값을 만들며 `get_field`, `set_field`, `call_method`는 등록된 멤버에 접근합니다. `weak_this=True`는 보존 메서드의 수신 객체를 유지하지 않으므로 만료를 처리해야 합니다. `closed`는 래퍼의 명시적 종료를, `is_valid`는 대상의 유효성을 나타냅니다. 위 예제의 결과는 `42`입니다.

`count`, `channel_capacity`, `channel_closed`는 괄호 없이 읽는 읽기 전용 속성입니다. 다음 완전한 예제는 출력 없이 성공합니다.

```Python
from veroscript import Value

channel = Value.channel("int", 1)
assert channel.channel_capacity == 1 and not channel.channel_closed
assert channel.try_send(42)
assert channel.count == 1
assert channel.receive() == 42
channel.close_channel()
assert channel.channel_closed
```

## 정확한 수치

`RuntimeCapabilities.NUMERICS`는 Core의 bigint/rational 지원을 나타냅니다. 이 값은
`kind == "host_object"`인 `Value`로 유지되며, `NumericKind(value.numeric_kind)`로
`BIGINT`, `RATIONAL`, `NONE`을 확인합니다. 스크립트와 호스트 콜백도 `Value`를 주고받으며
Python 정수나 부동소수점 수로 자동 변환하지 않습니다.

`session.numerics.bigint(text)`, `rational(text)`,
`parse(text, NumericKind.BIGINT)`는 명시적인 문자열을 해석하고 `to_text(value)`는 정규화된
문자열을 반환합니다. `convert(value, type_name, exact_float=False)`는 요청한 고정 폭 정수,
`float` 또는 `double` 타입을 유지하는 `Value`를 반환합니다. 범위 초과나 `exact_float=True`일 때
정확히 표현할 수 없는 변환은 오류를 발생시킵니다.

`Session.set_numeric_limits`의 키워드 인수는 `max_value_bits`, `max_input_bytes`,
`max_output_bytes`, `max_scratch_bytes`, `max_work_units`입니다. 0은 Core의 유한한 기본값을
선택합니다. 이 작업 예산은 프로세스 전체 메모리를 제한하지 않습니다. 세션 작업은 생성 스레드에서
세션이 유휴 상태일 때 수행해야 합니다. 호스트 콜백 안에서는 `current_numerics()`로 활성
컨텍스트를 빌립니다. 활성 콜백 밖에서 메서드를 호출하면 실패합니다. 네이티브 수치 작업은 GIL을
해제합니다. 독립 Python AOT 출력은 bigint/rational을 지원하지 않습니다.

## 모듈 종속 값의 소유권

[인터롭 SDK — 모듈 종속 값의 소유권](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#value-domain-confinement)을 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](../zh-TW/values.md)
