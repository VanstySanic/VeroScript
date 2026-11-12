# 13: 런타임 반영

이 튜토리얼에서는 VeroScript의 리플렉션 API를 설명하기 위해 작은 유형 검사기를 구축합니다. 리플렉션은 현재 스크립트 유형 시스템의 메타데이터와 스크립트의 공개 필드 및 메서드를 대상으로 하는 형식 지정 작업을 제공합니다. 도구, 편집기 및 직렬화 어댑터에 유용하지만 임의의 멤버를 동적 문자열 호출 인터페이스로 바꾸거나 접근 제어를 우회하지 않습니다.

## 1단계: 선언에 메타데이터 추가

유형, 필드, 메소드, 자유 함수, 전역 및 열거형 멤버에 주석을 첨부할 수 있습니다.

```VeroScript
@setting(primary)
export const int ReflectionVersion = 1;

@utility(sample)
export int echo_value(const int& value)
{
	return value;
}

enum class RecordState
{
	@display(primary)
	Ready,
	Archived
};

struct BaseRecord
{
	int id;

	int read_id() const
	{
		return id;
	}
};

@entity(sample)
struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}

	static int identity(int value)
	{
		return value;
	}

	int score;

	int add_score(int amount)
	{
		score += amount;
		return score;
	}
};
```

언어는 주석 이름, 키 및 값을 저장하지만 해당 애플리케이션별 의미를 해석하지는 않습니다. 여기서 `entity`, `data` 및 `call`는 동작을 자동으로 생성하는 키워드가 아니라 사용자 정의 메타데이터 규칙입니다.

다음 단계는 동일한 파일에서 계속됩니다. 이 `Record` 선언을 유지하고 진행하면서 `main`을 대체하거나 확장하십시오.

## 2단계: 유형 정보 얻기

`type<Record>`은 `Record` 자체를 설명합니다.

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	print("name={}, qualified={}", info.name, info.qualified_name);
	print("kind={}, abstract={}", info.kind(), info.is_abstract());
	print("fields={}, methods={}", info.field_count(), info.method_count());
	return 0;
}
```

`name`은 짧은 이름이고 `qualified_name`은 정규화된 이름입니다. 필드 및 메서드 정보 개체는 정규화된 이름과 0부터 시작하는 인덱스로 구성되므로 `type<T>` 개체를 먼저 유지하는 것이 일반적입니다.

## 3단계: 필드 열거

`field_info<Record>(info.qualified_name, i)`을 사용하여 각 필드를 읽습니다.

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}, mutable={}",
			field.name(), field.declared_type(), field.runtime_type(),
			field.is_static(), field.is_mutable());

		for (int j = 0; j < field.metadata_count(); j++)
		{
			print("  metadata {}={}", field.metadata_key(j), field.metadata_value(j));
		}
	}

	return 0;
}
```

필드 정보는 필드가 현재 유형에 직접 선언되었는지와 컨텍스트 `mutable` 한정자를 사용하는지도 알려 줍니다. `declared_type()`은 소스 선언을 표시하는 데 유용하고, `runtime_type()`은 런타임에 기록된 유형을 표시합니다.

## 4단계: 메서드 및 매개변수 열거

`method_info<T>`은 메소드 수정자와 매개변수 정보를 제공합니다.

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, static={}, virtual={}, const={}",
			method.name(), method.return_type(), method.is_static(),
			method.is_virtual(), method.is_const());

		for (int j = 0; j < method.parameter_count(); j++)
		{
			print("  param {}: name={}, type={}, ref={}, const={}",
				j, method.parameter_name(j), method.parameter_type(j),
				method.parameter_is_ref(j), method.parameter_is_const(j));
		}
	}

	return 0;
}
```

`parameter_type(j)`은 `ref` 또는 `const ref`를 제거한 후 기본 유형을 반환합니다. 도구가 전체 매개변수 의미 체계를 재구성해야 하는 경우 `parameter_is_ref(j)` 및 `parameter_is_const(j)`를 별도로 확인하세요.

## 5단계: 모듈 표면 검사

`module_info`은 항목 모듈에서 볼 수 있는 자유 함수, 최상위 전역 및 스크립트 유형을 열거합니다. 개수로 제한된 인덱스를 사용하여 `function_info(i)` 및 `global_info(i)`를 구성합니다.

```VeroScript
module_info module = module_info();
print("module={}, globals={}, types={}",
	module.path(), module.global_count(), module.type_count());

for (int i = 0; i < module.function_count(); i++)
{
	function_info function_meta = function_info(i);
	print("{} -> {}", function_meta.name(), function_meta.return_type());
}

for (int i = 0; i < module.global_count(); i++)
{
	global_info global_meta = global_info(i);
	print("global {}: declared={}, const={}",
		global_meta.name(), global_meta.declared_type(), global_meta.is_const());
}
```

모든 선언 설명자는 `has_metadata(key)`, `metadata(key)` 및 `has_annotation(name)`를 제공합니다. 열거형 멤버 주석의 경우 `enum_member_info<E>(type<E>().qualified_name, i)`을 구성합니다.

예를 들어 `RecordState`의 첫 번째 멤버를 읽어보세요.

```VeroScript
type<RecordState> state_type = type<RecordState>();
enum_member_info<RecordState> state =
	enum_member_info<RecordState>(state_type.qualified_name, 0);

print("{}={}, annotated={}",
	state.name(), state.value(), state.has_annotation("display"));
```

## 6단계: 개수 및 오류 처리

리플렉션 쿼리는 비어 있을 수 있는 동적 개체를 반환하는 대신 개수와 인덱스를 사용합니다. 인덱싱하기 전에 필드, 메서드, 함수, 전역, 유형 또는 열거형 멤버 수를 확인하세요. 범위를 벗어난 인덱스, 유형 정보 누락, 적용할 수 없는 쿼리는 런타임 오류입니다.

실행 가능한 형식 지정 리플렉션은 공개 스크립트 인스턴스 필드와 공개 인스턴스 또는 정적 메서드로 제한됩니다. 접근 제어를 우회하거나 제한 없는 문자열 기반 조회 및 호출을 제공하지 않습니다. `__reflection_*` 형식의 이름은 스크립트 API가 아닌 내부 도우미 기호이므로 직접 사용하면 안 됩니다.

## 7단계: 선택한 멤버 읽기, 쓰기 및 호출

메타데이터에서 descriptor를 선택한 다음 필드 또는 반환 형식을 정확히 지정합니다. 메서드 인수는 위치에 따라 전달합니다.

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	int score_index = -1;
	int add_index = -1;
	int identity_index = -1;
	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> candidate = field_info<Record>(info.qualified_name, i);
		if (candidate.name() == "score")
		{
			score_index = i;
		}
	}
	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> candidate = method_info<Record>(info.qualified_name, i);
		if (candidate.name() == "add_score")
		{
			add_index = i;
		}
		if (candidate.name() == "identity")
		{
			identity_index = i;
		}
	}
	if (score_index < 0 || add_index < 0 || identity_index < 0) return 1;

	field_info<Record> score = field_info<Record>(info.qualified_name, score_index);
	method_info<Record> add = method_info<Record>(info.qualified_name, add_index);
	method_info<Record> identity = method_info<Record>(info.qualified_name, identity_index);
	Record record = Record();
	score.set<int>(record, 10);
	print("score={}", score.get<int>(record));
	print("add_score={}", add.invoke<int>(record, 5));
	print("identity={}", identity.invoke_static<int>(21));
	return 0;
}
```

`get<V>`와 `set<V>`는 인스턴스 필드에 적용하고, `invoke<R>`는 인스턴스 메서드를, `invoke_static<R>`는 정적 메서드를 호출합니다. 선택한 멤버의 값 또는 반환 형식은 `V` 또는 `R`과 정확히 일치해야 합니다. 지원되지 않는 시그니처와 백엔드별 제한은 전체 참조 문서를 확인하세요.

## 연습

1. `Record`에 정적 메서드를 추가하고 `is_static()`을 사용하여 찾습니다.
2. `base_count()` 및 `base_name(i)`를 사용하여 `Record`의 직접 베이스를 표시합니다.
3. `enum_member_count()`, `enum_member_name(i)` 및 `enum_member_value(i)`을 사용하여 `RecordState`의 멤버를 표시합니다.
4. `@data` 메타데이터가 포함된 필드만 인쇄하는 도구를 작성합니다.

전체 규칙은 [반사](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/reflection.md)를 참조하세요. 검사 예제는 [`examples/reflection.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection.vs), 실행 작업 예제는 [`examples/reflection_executable.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection_executable.vs)입니다. 호스트 인터페이스 설명과 스크립트 반영은 별도의 레이어입니다. 호스트 카탈로그는 [바인딩 카탈로그](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/bindings.md)에서 다룹니다.

이것으로 기본 튜토리얼 경로가 완료됩니다. 필요할 때 [툴체인 참조](../../../User/ko-KR/toolchain/index.md) 또는 [표준 라이브러리 참조](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/index.md)를 참조하세요.

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/advanced/13-reflection.md) · [fr-FR](../../fr-FR/advanced/13-reflection.md) · [ja-JP](../../ja-JP/advanced/13-reflection.md) · [ko-KR](13-reflection.md) · [zh-CN](../../zh-CN/advanced/13-reflection.md) · [zh-TW](../../zh-TW/advanced/13-reflection.md)
