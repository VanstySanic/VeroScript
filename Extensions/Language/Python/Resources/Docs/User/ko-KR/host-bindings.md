# Python 호스트 바인딩

호스트 객체를 소유하는 `Value`와 하위 타입 검사는 생성한 `Session`을 닫은 후에도 안전하게 사용할 수 있습니다. 타입 조회는 Python 클래스의 약한 참조를 사용합니다. clone／equals／hash 프로토콜이 있는 값은 map/set 키 사용을 포함하여 프로토콜 호출에 필요한 콜백을 마지막 관련 값이 해제될 때까지 유지합니다. 이 프로토콜이 없는 값은 관련 없는 클래스나 세션 콜백을 유지하지 않습니다.

`HostClass`의 공개 메서드는 `constructor(...)`, `method(...)`,
`static_method(...)`, `property(...)`, `field(...)`입니다. 이 메서드로
VeroScript에 노출할 멤버를 명시합니다.

호스트 바인딩은 Python 함수, 클래스와 직렬화기를 명시적인 VeroScript 타입 계약으로 노출합니다. `HostParameter`는 `type_name`, 이름, `by_ref`, `const_ref`, 문서를 표현합니다.

```Python
from veroscript import HostParameter

session.register_function(
	"Add",
	add,
	parameters=(
		HostParameter("int", "left"),
		HostParameter("int", "right"),
	),
	return_type="int",
)
```

`register_function`과 `host_function`의 계약은 실제 콜백 서명과 일치해야 합니다. `HostClass`는 생성자, 인스턴스/정적 메서드, 프로퍼티를 선언합니다. `construct`, `call_method`, `get_field`, `set_field`는 등록된 멤버만 사용합니다.

`register_serializer`, `serialize_object`, `deserialize_object`는 데이터 경계이므로 타입과 버전을 검증하십시오. `set_mutation_callback`의 이벤트는 짧게 처리하고 열거 중인 객체나 세션을 파괴하지 마십시오. 이미 완료된 변경은 알림으로 되돌릴 수 없습니다. 콜백 예외는 `CallbackError`로 변환됩니다.

호스트 멤버의 시그니처는 명시적으로 지정해야 합니다. 스칼라, 등록된 호스트
객체, `enum<Name>`, `array<int>` 및 `map<string,PythonObject>` 같은 중첩
컨테이너를 사용할 수 있으며, 스칼라가 아닌 콜백 값은 `Value`로 표현합니다.
호스트 클래스 멤버는 현재 값으로 전달되는 매개변수를 사용하고, 자유 함수는
변경 가능한 `ref`와 읽기 전용 `const_ref`도 지원합니다.

`field(...)`는 기본적으로 라이브 getter와 쓰기 가능한 setter를 노출하며
`writable=False`로 읽기 전용으로 만들 수 있습니다. `register_struct(...)`로
등록한 값 형식은 복사될 수 있으므로 `protocol(...)`에 clone을 등록해야 하며,
map/set 키로 사용할 때는 equality와 hash도 필요합니다. `register_enum(...)`은
`IntEnum` 또는 `IntFlag`를 요구하며 일반 `enum.Enum`은 거부합니다.
`register_generic_class(...)`와
`register_generic_struct(...)`로 등록한 템플릿은 사용하는 소스를 로드하기
전에 `instantiate(...)`로 구체화하십시오.
상속 가능한 호스트 클래스에는 `register_class(..., inheritable=True)`를 사용하고
`protocol(subclass_factory=...)`를 제공하면 스크립트 서브클래스가 관리 인스턴스를
생성할 수 있습니다.

주석 이름은 기본적으로 대소문자를 구분합니다.
`register_annotation("@GuiWidget", case_sensitive=False)`를 사용하면 프로세서 구독과
리플렉션에서 주석 이름의 ASCII 대소문자를 구분하지 않으며 소스 표기는 유지됩니다.
인자 이름과 메타데이터 키는 계속 정확히 일치해야 합니다. 이름 정책이 충돌하면
`Error`가 발생하고 기존 등록은 유지됩니다. 이 등록만으로 컴파일 시점 프로세서가
설치되지는 않습니다.

`register_annotation(...)`은 주석 어휘와 인자를 등록하고,
`register_type_alias(...)`는 구체 또는 제네릭 형식 별칭을 등록합니다.
`set_instance_call_interceptor(...)`의 `InterceptContext`에서는 일반 호출에
`InterceptResult.UNHANDLED`, `context.proceed()` 후 `InterceptResult.HANDLED`, 대체 결과 또는 실패에
`InterceptResult.FAILED`를 사용할 수 있습니다. `context.proceed()`는 최대 한 번만 호출할 수
있고 콜백이 끝나면 context를 보관할 수 없습니다. 신뢰된 호스트 호출은
`call_method_bypassing_interceptor(...)`로 우회할 수 있습니다.

등록된 호스트 기본 형식을 시그니처에서 사용하면 등록된 호환 하위 형식의 인스턴스를
전달할 수 있습니다. 호환성은 등록된 관계를 사용하며 임의의 Python 클래스에서
추론하지 않습니다.

## 콜백 제한

등록한 콜백은 세션을 닫을 때까지 유지됩니다. 예외는 `CallbackError`로 변환되어 네이티브 ABI를 넘어 전파되지 않습니다. 스크립트 콜백은 VM 작업자 스레드에서 실행될 수 있으므로 공유 Python 상태를 동기화하십시오. 작업자 콜백에서 생성 스레드 전용 `Session` 또는 보존 callable 메서드를 호출하지 마십시오. 소스를 로드하기 전에 제네릭 호스트 형식을 인스턴스화하고, 호스트 구조체의 복사 및 키 사용에 필요한 protocol을 등록하십시오.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
