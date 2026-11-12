# 닫힌 다형 매핑

명시적 닫힌 테이블과 {"kind":"cat","value":{"lives":9}} 외부 객체를 사용합니다. 키 이름은 바꿀 수 있으며 kind는 문자열 태그, value는 파생 변환기의 null 아닌 결과입니다. 루트 자체는 null일 수 있습니다. 멤버 충돌을 피하고 문자열/배열 payload도 허용합니다. 구체 형식 1–256개를 형식/태그별로 유일하게 등록합니다. 미등록 실제 형식/태그와 누락/null payload는 실패하며 기본 형식으로 축소하지 않습니다.

C++은 각 파생 변환기 다음에 TPolymorphicMapping<Base>.Add<Derived>(tag).Register를 등록합니다. Base는 다형적이어야 하고 추상일 수 있으며 파생은 구체적/공개/비모호해야 합니다. 정상 팩터리를 호출하고 실제 반환 형식을 확인합니다. 생성자의 네 번째 인수는 테이블 Owner, Add의 두 번째는 파생 Owner로 RTTI/변환/소멸 코드를 유지합니다. 결과 shared_ptr도 일반 매핑 규칙으로 유지합니다. 등록 실패는 이전 테이블 보존, 호출은 불변 스냅샷, 디스패치 콜백은 Owner보다 먼저 파괴됩니다.

TryDecode<T> 결과 shared_ptr<T>는 새 T를 소유합니다. TryDecode<Animal>은 null 아닌 Animal을 만들고 nullable shared_ptr<Animal> 대상이면 바깥쪽은 shared_ptr<shared_ptr<Animal>>입니다. 객체 필드 shared_ptr<Animal>은 직접 nullable이며 Schema도 실제 대상 형식에 맞춥니다.

JsonPolymorphic(base,discriminator,payload,variants)는 비템플릿 테이블 클래스에 정적 create/encode/decode/schema를 만듭니다. cat="Cat" 또는 dog=(type="Dog",tag="good-dog")로 Unicode 태그도 지정합니다. 모듈/네임스페이스/지역/중첩 클래스가 가능하지만 형식/값 매개변수나 외부 매개변수 캡처는 불가합니다. 기본/파생은 닫힌 제네릭과 일반 접근 검사를 따릅니다. 이름은 테이블 범위, 별칭은 선언 모듈과 import/re-export 가시성으로 해석합니다. Base를 바꿀 수 없으면 custom_converter<Base,Table>, 보통 converter<Base>로 적용하려면 Base의 JsonConverter/JsonFactory를 사용합니다. 내보낸 형식/테이블은 모듈과 프로세서 기반 .vslib를 넘으며 일반 라이브러리에 함수가 자동 추가되지는 않습니다.

입출력 Schema는 oneOf, 파생 Schema와 재귀 $defs를 사용합니다. 추가 외부 키를 허용하는 ignore_unknown은 입력에만 적용하고 출력은 항상 거부합니다. 태그/payload/필드 오류의 이스케이프된 JSON Pointer를 보존합니다. 실패는 출력 보존, 예산/취소/순환은 같은 문맥, 외부 부수 효과는 롤백하지 않습니다. 공유 그래프는 값으로 반복하고 순환은 Cycle입니다.

```json
{"kind":"cat","value":{"lives":9}}
```

```cpp
TObjectMapping<Cat> cat("cat");
cat.Field("lives", &Cat::Lives);
cat.Register(registry, error);
TObjectMapping<Dog> dog("dog");
dog.Field("name", &Dog::Name);
dog.Register(registry, error);
TPolymorphicMapping<Animal> animals("animal");
animals.Add<Cat>("cat").Add<Dog>("dog");
animals.Register(registry, error);
std::shared_ptr<Animal> source = std::make_shared<Cat>();
TryEncode(source, value, error, registry);
std::shared_ptr<Animal> result;
TryDecode(value, result, error, registry);
```

```VeroScript
@JsonConverter(type="AnimalJson")
@JsonFactory(type="AnimalJson")
class Animal
{
public:
	virtual string voice() = 0;
};
class Cat : public Animal
{
public:
	string voice() override
	{
		return "cat";
	}

	int lives = 9;
};
class Dog : public Animal
{
public:
	string voice() override
	{
		return "dog";
	}
};
@JsonPolymorphic(base="Animal", discriminator="kind", payload="value",
	variants=(cat="Cat", dog=(type="Dog",tag="good-dog")))
class AnimalJson
{
};

Animal source=Cat();
json::value data=json::converter<Animal>::write(ref source);
Animal copy=json::converter<Animal>::read(data);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/polymorphic-mapping.md) · [fr-FR](../../fr-FR/Reference/polymorphic-mapping.md) · [ja-JP](../../ja-JP/Reference/polymorphic-mapping.md) · [ko-KR](polymorphic-mapping.md) · [zh-CN](../../zh-CN/Reference/polymorphic-mapping.md) · [zh-TW](../../zh-TW/Reference/polymorphic-mapping.md)
