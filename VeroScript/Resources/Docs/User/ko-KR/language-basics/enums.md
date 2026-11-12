# 열거형

열거형은 명명된 정수 값의 유한 집합을 나타냅니다. VeroScript는 일반 열거형과 범위가 지정된 열거형(`enum class`)을 지원하며 기본 정수 유형을 명시적으로 지정할 수 있습니다.

## 열거형 선언

일반 열거형의 멤버는 기본적으로 `0`에서 시작하고 후속 멤버는 1씩 증가합니다.

```VeroScript
enum State
{
	Stopped,
	Running,
	Paused
};
```

멤버에게 명시적인 정수 값이 할당될 수도 있습니다. 명시적인 값이 없는 멤버는 이전 멤버에서 계속됩니다.

```VeroScript
enum ErrorCode
{
	None = 0,
	NotFound = 404,
	Timeout = 408
};
```

열거형 값은 기본 형식으로 표현 가능한 정수 상수 식이어야 합니다. 검사된 연산, 앞선 열거형 멤버 및 적합한 [constexpr 호출](../language-reference/constexpr.md)을 지원합니다. 전방 참조, 순환 및 오버플로는 오류입니다.

## 범위가 지정된 열거형

열거형 유형의 범위 내에 멤버를 유지하려면 `enum class`을 사용하십시오.

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u
};

RunState state = RunState::Running;
```

범위가 지정된 열거형의 멤버는 둘러싸는 범위에서 기본 이름으로 소개되지 않습니다. 따라서 멤버 이름이 충돌할 수 있거나 여러 열거형 유형이 명확하게 구분되어야 하는 경우 `enum class`을 선택하는 것이 좋습니다. 문서 및 새 코드에서는 멤버 액세스를 위해 `Type::Member` 형식을 선호해야 합니다.

일반 열거형의 멤버는 직접 사용할 수 있습니다:

```VeroScript
State state = Running;
```

## 기본 정수 유형

기본 유형이 지정되지 않으면 열거형은 기본적으로 `int`을 사용합니다. 표현 가능한 범위를 명시적으로 제어하려면 `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32` 및 `int64`과 같은 고정 폭 정수 타입을 사용합니다.

```VeroScript
enum class ByteState : uint8
{
	Empty = 0u,
	Full = 1u
};

enum class SignedLimit : int16
{
	Minimum = -32768,
	Maximum = 32767
};
```

부호 없는 기본 유형은 음수 값을 나타낼 수 없습니다. 기본 유형이 지정되면 모든 멤버 값이 해당 유형의 범위 내에 맞아야 합니다.

## 열거형 값 사용하기

열거형 값은 비교될 수 있으며 컨테이너의 키 또는 요소로 사용될 수 있습니다.

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u,
	Paused = 2u
};

map<RunState, string> labels = map<RunState, string>();
labels[RunState::Running] = "running";

set<RunState> states = set<RunState>();
states.add(RunState::Paused);
```

열거형은 유한하고 안정적인 상태 집합에 적합합니다. `enum class`의 경우 관련 없는 값과 자유롭게 혼합할 수 있는 임의의 정수로 처리하는 대신 열거형 유형 의미를 유지합니다.

## 열거형 내보내기

`import`을 통해 다른 모듈에서 사용하기 위해 열거형을 내보낼 수 있습니다.

```VeroScript
export enum class Operation : uint8
{
	Add = 1u,
	Multiply = 2u
};
```

모듈을 가져온 후 전체 열거형 유형 이름을 사용하여 범위가 지정된 멤버에 액세스합니다.

```VeroScript
Operation operation = Operation::Add;
```

중첩 형식, 지역 선언, 접근 규칙 및 단계별 제네릭 인수는 [형식 범위](scope.md)를 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/enums.md) · [fr-FR](../../fr-FR/language-basics/enums.md) · [ja-JP](../../ja-JP/language-basics/enums.md) · [ko-KR](enums.md) · [zh-CN](../../zh-CN/language-basics/enums.md) · [zh-TW](../../zh-TW/language-basics/enums.md)
