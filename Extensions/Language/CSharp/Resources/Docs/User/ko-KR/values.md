# C# 값과 소유권

`VeroScriptValue`는 독립된 Interop 값 핸들을 소유하며 세션의 자식 객체가 아닙니다. 소유 값은 `Dispose()` 또는 `using`으로 해제하세요. `Clone()`은 런타임 복제 규칙에 따른 별도의 소유 핸들을 만들며 깊은 복사를 보장하지 않습니다. `Reset()`은 빈 값으로 바꿉니다. 동시 변경이나 해제는 호출자가 동기화해야 합니다.

## 모듈에 의존하는 내용

핸들을 소유해도 그 내용이 세션에서 독립하는 것은 아닙니다. 스크립트 객체와 함수,
모듈 값 슬롯이 있는 가변 컨테이너는 비어 있어도 세션 생성 스레드에서만 사용할 수
있으며 세션이 닫히면 접근할 수 없습니다. 호스트가 만든 같은 종류의 저장소는 세션에서
처음 사용할 때 연결되며 기존 공유 별칭에도 같은 소유권이 적용됩니다. `Clone()`은
이 제한을 제거하지 않습니다. VM 워커 콜백은 독립된 소유 값만 내보낼 수 있으며,
variant/함수 슬롯이 있는 빈 컨테이너도 외부 소유 저장소로 복제할 수 없습니다.
비공개 빌린 반환 슬롯은 워커 안에서 값을 전달할 수 있습니다. 세션이 닫힌 뒤에도
사용이 끝난 소유 핸들은 해제하세요.

## 스칼라 값

읽기 메서드는 스크립트 형식과 일치해야 합니다.

| 설정 | 읽기 | 스크립트 형식 |
| --- | --- | --- |
| `Set(sbyte)` | `GetInt8()` | `int8` |
| `Set(byte)` | `GetUInt8()` | `uint8` |
| `Set(short)` | `GetInt16()` | `int16` |
| `Set(ushort)` | `GetUInt16()` | `uint16` |
| `Set(int)` | `GetInt32()` | `int32` |
| `Set(uint)` | `GetUInt32()` | `uint32` |
| `Set(long)` | `GetInt64()` | `int64` |
| `Set(ulong)` | `GetUInt64()` | `uint64` |
| `Set(float)` | `GetFloat()` | `float` |
| `Set(double)` | `GetDouble()` | `double` |
| `Set(bool)` | `GetBool()` | `bool` |
| `SetString` | `GetString` | `string` |
| `SetWideString` | `GetWideString` | `wstring` |

형식이 있는 열거 값에는 `SetEnum(typeName, value)` / `GetEnum()`을 사용합니다. `Kind`와 `TypeName`은 현재 표현을 나타냅니다. `SetNull()`은 `nullptr`를 만들고 `IsNull`은 이 값만 검사합니다. `Reset()` 후에는 `Kind`가 `Invalid`이고 `IsNull`은 false입니다. 해제 후 접근하면 `ObjectDisposedException`이 발생합니다. null 호스트 참조 허용 여부는 수신 매개변수의 계약을 따릅니다.

`Set(VeroScriptValue)`는 전체 값을 대입하며 컨테이너, 호스트 객체, 함수, 채널의 런타임 정체성을 보존합니다. 대상은 쓰기 가능해야 하며 읽기 전용 빌린 값은 대상이 될 수 없습니다.

## 컨테이너와 채널

`SetArray`, `SetSequence`, `SetMap`, `SetSet`, `SetChannel`에는 명시적인 형식 이름이 필요합니다. `VeroScriptValueKind`를 받는 오버로드로 정확한 종류를 고릅니다. `SetSequence`는 `Queue`, `PriorityQueue`, `Stack`, `Deque` 등의 시퀀스, `SetMap`은 `Map`, `OrderedMap`, `MultiMap`, `OrderedMultiMap`, `SetSet`은 `Set`, `MultiSet`, `OrderedMultiSet`을 지원합니다.

일반 map은 중복 키의 값을 바꾸며 multi-map은 모든 키/값 연결을 보존합니다. 일반 set은 중복을 제거하며 multi-set은 개수를 보존합니다. 우선순위 큐는 런타임의 `top` / `pop`에서 가장 높은 우선순위 요소를 먼저 제공합니다.

시퀀스는 `SequenceCount` / `GetSequenceElement`, map은 `MapCount` / `TryGetMapValue`, set은 `SetCount` / `SetContains`로 읽습니다. `EnumerateMap`과 `EnumerateSet`은 `VeroScriptCapabilities.ContainerEnumeration`이 필요하며 읽기 전용 스냅샷을 방문합니다. `VeroScriptBorrowedValue` 인수는 각 콜백 반환 시 만료됩니다. 보존하려면 해당 콜백 안에서 `Clone()`을 호출하세요.

```CSharp
using var key = new VeroScriptValue();
key.SetString("answer");
using var number = new VeroScriptValue();
number.Set(42);
using var map = new VeroScriptValue();
map.SetMap("string", "int", [key], [number]);

map.EnumerateMap((entryKey, entryValue) =>
{
	Console.WriteLine(entryKey.GetString());
	Console.WriteLine(entryValue.GetInt32());
});
```

채널 API는 `ChannelSend`, `ChannelTrySend`, `ChannelSendFor`, `ChannelReceive`, `ChannelTryReceive`, `ChannelReceiveFor`, `ChannelClose`입니다. `ChannelTrySend`와 `ChannelSendFor`는 bool을 반환하며 제한 시간은 밀리초입니다. 비차단 및 시간 제한 수신은 소유 값 또는 `null`을 반환합니다. `ChannelCount`, `ChannelCapacity`, `IsChannelClosed`로 상태를 읽습니다. 응답성이 필요한 스레드에서 차단 수신하지 마세요. 런타임은 채널 연산을 동기화하지만 동시 사용 중에 래퍼를 초기화, 교체, 해제하면 안 됩니다.

## 호스트 객체와 보존 callable

`SetHostObject(typeId, typeName, managedObject)`는 네이티브 값이 보유하는 동안 관리 객체를 유지합니다. `Func<string, bool> isType` 오버로드는 호환되는 하위 형식을 보고합니다. `GetHostObject()`는 `VeroScriptHostObjectInfo`를 반환하고 `GetHostObject<T>()`는 관리 형식을 검사합니다.

`VeroScriptCallable`은 보존된 함수나 클로저를 소유하며 사용 후 해제해야 합니다. 기본적으로 생성 스레드에서만 호출할 수 있습니다. `RetainCallable(..., ownerThreadOnly: false)`로 다른 스레드의 호출을 허용해도 호스트가 연결된 세션의 모든 연산과 직렬화하고 자체 상태를 보호해야 합니다. callable 내부 잠금이 전체 세션을 보호하지는 않습니다.

## 빌린 값과 수명

네이티브 함수, 인터셉터, 제네릭 팩터리, 직렬화, 변경 알림 및 방문자의 빌린 컨텍스트와 값은 현재 콜백 스레드에서만 유효합니다. 다른 스레드에서 접근하면 `WrongThread` 상태의 `VeroScriptException`, 콜백 반환 후에는 `InvalidOperationException`이 발생합니다. 전송이 허용된 독립된 내용만 콜백 스레드에서 복제한 뒤 다른 스레드로 전달하세요. 복제해도 모듈 의존 제한은 없어지지 않습니다.

## 정확한 수치 API

C#의 Core bigint/rational 결과는 불투명 VeroScriptValue 호스트 값입니다. VeroScriptCapabilities.Numerics는 네이티브 기능이며 System.Numerics.BigInteger 자동 변환이나 공개 관리 수치 서비스를 추가하지 않습니다. 형식화된 스크립트 함수 또는 공개 C ABI를 사용합니다.

<a id="managed-call-lifetime"></a>

## 관리 호출 수명

네이티브 작업은 핸들을 제공한 값/패키지 래퍼를 호출과 반환된 네이티브 뷰 복사가 끝날 때까지 유지합니다. `IReadOnlyList<VeroScriptValue>` 인덱서가 생성한 값과 블로킹 channel 작업 또는 visitor의 수신 객체도 포함합니다. 보존한 래퍼는 스냅샷이므로 콜백 중 인수 배열 요소를 교체해도 네이티브 `ref`/`out` 쓰기 대상은 교체한 값으로 바뀌지 않습니다. 이 수명 보장은 동시 명시적 해제를 허용하거나 콜백의 빌림 범위를 연장하지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](../zh-TW/values.md)
