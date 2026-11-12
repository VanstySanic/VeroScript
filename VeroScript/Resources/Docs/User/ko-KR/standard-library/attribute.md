# 속성과 메서드 함수 값

`attribute.vs` · `callable.vs` · `core.vs`

`std::attribute<T>`는 고정 값 또는 동기 getter를 저장합니다. 복사본의 설정은 독립적이며 캡처 소유권은 기존 언어 규칙을 따릅니다. 읽을 때마다 다시 평가합니다.

```VeroScript
import "attribute.vs";

int read_count()
{
	return 42;
}
void main()
{
	std::attribute<int> count = 10;
	count.bind(read_count);
	int snapshot = count;
	assert(snapshot == 42 && count() == 42);
	count.reset();
	assert(!count.try_get().has_value());
}
```

`get()`, `operator()`, `T`로의 변환은 각각 한 번 읽습니다. `try_get()`은 optional을 반환하며 `get_or(fallback)`의 인수는 먼저 평가됩니다. 실행 오류는 전파됩니다. 미설정, 빈 reader 결과, 만료된 약한 대상만 값 없음으로 처리합니다.

`set(value)`, `bind(fn()->T)`, `bind_optional(fn()->optional<T>)`, `reset()`은 전체 설정을 교체합니다. `is_set()`과 `is_bound()`는 getter를 실행하지 않습니다.

`try_get(ref output)`는 기존 변수로 한 번 읽고 값이 있었는지 반환합니다. 설정되지 않은 속성이나 비어 있는 optional 리더는 `output`을 바꾸지 않으며 getter 예외는 전파됩니다. 기존 변수에 쓰려면 이 오버로드를 사용하며 optional을 반환하는 기존 `try_get()`도 계속 사용할 수 있습니다.

`has_same_configuration(ref other)`는 getter를 평가하지 않고 설정 식별자를 비교합니다. 복사 직후에는 같은 식별자를 공유하지만 어느 쪽 설정을 교체하면 값이 같아도 달라집니다. 바인딩 결과는 설정 변경 없이 달라질 수 있으므로 이 비교로 동적 getter 호출을 생략할 수 없습니다.

`std::make_attribute_by_name<T>(target, "read")`는 강한 참조의 인수 없는 getter를 만듭니다. `T`는 명시하고 대상 형식은 추론하거나 두 번째 형식 인수로 지정합니다. 이름은 대소문자를 구분하며 생성 시 확인합니다. 일반 접근 및 가상 호출 규칙을 유지합니다.

`std::make_weak_attribute<T>(weak_target, getter)`는 읽을 때 한 번 잠그며 getter 서명은 `fn(U)->T`입니다. `std::make_weak_attribute_by_name<T>(weak_target, "read")`는 인수 없는 메서드를 이름으로 바인딩합니다. 대상이 만료되어도 설정 상태는 유지됩니다. 구조체는 `shared<S>` 또는 `handle<S>`로 감쌉니다.

`std::bind_method<F>(target, "method")`는 일반적인 형식화된 함수 값을 반환합니다. 서명이 정확히 일치하는 public 인스턴스 메서드가 필요합니다. 강한 대상으로 유지하고 가상 디스패치를 보존합니다. 결과는 `attribute.bind`, `delegate.bind`, `multicast_delegate.add`에 전달할 수 있습니다. 제거할 때는 실제 추가한 함수 값을 보관해서 사용합니다. 최소 모듈은 `callable.vs`입니다.

독립 AOT: C++은 고정 값, getter, 약한 대상을 지원하고 C#과 Python은 고정 값, 이름 있는 getter, 상수 강한 메서드 이름을 지원합니다. Python 소스 Lambda와 C#/Python 약한 바인딩은 기존 지원 제한을 유지합니다. 동적 이름, 호스트 메서드 이름, 모호한 제네릭 선택은 런타임이 필요하며 내보내기에서 거부됩니다. VM, Mixed, JIT는 코어 구현을 공유합니다.

C++/C# 소스 내보내기는 `try_get()`과 `try_get(ref output)`을 지원합니다. Python은 `try_get()`을 지원하지만 쓰기 가능한 `ref` 출력은 거부합니다. 읽기 전용 `has_same_configuration(ref other)` 비교는 세 형식 모두 지원하며 이 특별한 변환이 Python에 일반 쓰기 참조를 추가하지는 않습니다.

[`attribute_demo.vs`](../../../../Scripts/examples/attribute_demo.vs) · [`attribute_functions.vs`](../../../../Scripts/examples/attribute_functions.vs) · [`attribute_methods.vs`](../../../../Scripts/examples/attribute_methods.vs) · [`attribute_weak.vs`](../../../../Scripts/examples/attribute_weak.vs)

표준 라이브러리 선언은 하나의 모듈 ID로 통일하세요. 패키지가 attribute를 포함하고 다시 내보내면 해당 선언 또는 공통 의존성을 사용하세요. 별도의 소스 `core.vs` / `attribute.vs`를 함께 가져오면 내보낸 이름이 충돌합니다.

여러 라이브러리는 먼저 `export import "attribute.vs";`로 하나의 `shared-stdlib.vslib`를 만듭니다. 각 라이브러리는 이 컴포넌트를 import하고 애플리케이션도 별도 소스 복사본 대신 같은 컴포넌트와 라이브러리를 import합니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/attribute.md) · [fr-FR](../../fr-FR/standard-library/attribute.md) · [ja-JP](../../ja-JP/standard-library/attribute.md) · [ko-KR](attribute.md) · [zh-CN](../../zh-CN/standard-library/attribute.md) · [zh-TW](../../zh-TW/standard-library/attribute.md)
