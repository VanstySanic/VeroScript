# 이벤트 모듈

모듈 파일: `delegate.vs`

다음을 사용하여 가져옵니다.

```VeroScript
import "delegate.vs";
```

공개 스크립트에서는 `std::delegate`와 `std::multicast_delegate`를 사용합니다. 비한정 이름도 같은 형식이며 두 표기 모두 `delegate.vs`를 가져와야 합니다. 모든 멤버 이름은 소문자를 사용합니다.

## 유니캐스트 대표

`delegate`은 최대 3개의 인수를 포함하는 하나의 핸들러를 저장합니다. 첫 번째 유형 매개변수는 반환 유형이고 그 뒤에는 호출 인수 유형이 있습니다.

```VeroScript
std::delegate<int, int> mitigate = std::delegate<int, int>();
mitigate.bind(function(int value) -> int => value / 2);
int result = mitigate.execute(10);
```

이 모듈은 `delegate<R>`, `delegate<R, A>`, `delegate<R, A, B>` 및 `delegate<R, A, B, C>`을 제공합니다. 회원은 다음과 같습니다:

| 회원 | 설명 |
| --- | --- |
| `bind(handler)` | 핸들러를 바인드하거나 대체합니다. |
| `unbind()` | 현재 핸들러를 제거합니다. |
| `is_bound()` | 핸들러가 바인딩되었는지 테스트합니다. |
| `execute(...)` | 핸들러를 실행합니다. 바인딩이 해제되면 어설션합니다. |
| `execute_if_bound(...)` | 바인딩된 경우에만 실행하고 실행 여부를 반환합니다. |

`execute_if_bound`은 실행이 발생했는지 여부를 보고합니다. 핸들러의 결과를 반환하지 않습니다.

`delegate`의 핸들러 저장소는 비공개입니다. `unbind()`는 델리게이트가 보관한 핸들러 참조를 비웁니다. 상태는 `is_bound()`로 확인합니다.

## 멀티캐스트 대리자

`multicast_delegate`은 `void` 처리기 목록을 저장하고 0~3개의 인수를 지원합니다.

```VeroScript
std::multicast_delegate<int> on_score = std::multicast_delegate<int>();
on_score.add(function(int value) -> void => print(value));
on_score.broadcast(42);
```

회원은 다음과 같습니다:

| 회원 | 설명 |
| --- | --- |
| `add(handler)` | 핸들러를 추가합니다. |
| `remove(handler)` | 일치하는 핸들러를 제거합니다. |
| `broadcast(...)` | 등록 순서대로 모든 핸들러를 호출합니다. |
| `count()` | 핸들러 수를 음수가 아닌 `int64`로 반환합니다. |
| `clear()` | 모든 핸들러를 제거합니다. |

방송은 먼저 처리기 목록의 스냅샷을 찍기 때문에 방송 중에 변경된 내용이 해당 방송에 설정된 호출을 변경하지 않습니다.

`multicast_delegate`의 `handlers` 필드는 소스 모듈에서 공개됩니다. 등록 순서와 스냅샷 의미 체계를 유지해야 하는 경우 `add`, `remove` 및 `clear`를 선호합니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/events.md) · [fr-FR](../../fr-FR/standard-library/events.md) · [ja-JP](../../ja-JP/standard-library/events.md) · [ko-KR](events.md) · [zh-CN](../../zh-CN/standard-library/events.md) · [zh-TW](../../zh-TW/standard-library/events.md)
