# 반사

VeroScript는 모듈, 유형, 멤버 및 선언 메타데이터를 조회하는 런타임 리플렉션 API를 제공합니다. 타입이 명시된 작업으로 공개 스크립트 인스턴스 필드에 접근하고 스크립트 클래스 또는 인터페이스에 선언된 공개 메서드를 호출할 수 있으며, 접근 제어는 우회하지 않습니다.

## 반사 객체

언어는 다음과 같은 공개 정보 유형을 제공합니다.

- `type<T>`은 `T` 유형 자체를 설명합니다.
- `field_info<T>`는 `T`의 한 필드를 설명합니다.
- `method_info<T>`는 `T`의 한 가지 방법을 설명합니다.
- `enum_member_info<T>`은 열거형 `T`의 열거자 하나를 설명합니다.
- `module_info`은 현재 모듈 보기를 설명합니다.
- `function_info`는 하나의 표시 가능한 자유 함수를 설명합니다.
- `global_info`은 하나의 가시적인 최상위 글로벌을 설명합니다.

이러한 유형은 컴파일러에서 제공되며 가져올 필요가 없습니다. 쿼리는 정규화된 소유자 이름과 0부터 시작하는 인덱스를 사용합니다. 잘못된 인덱스는 런타임 오류로 보고됩니다.

## 유형 정보

`type<T>`의 공개 멤버는 다음과 같습니다.

| 회원 | 목적 |
| --- | --- |
| `name` | 짧은 유형 이름. |
| `qualified_name` | `field_info<T>` 및 `method_info<T>`을 생성하는 데 적합한 정규화된 유형 이름입니다. |
| `kind()` | 유형 카테고리를 반환합니다. |
| `is_abstract()` | 유형이 추상인지 여부를 보고합니다. |
| `base_count()` / `base_name(i)` | 직접 상속한 기반 형식의 수와 이름을 반환합니다. |
| `field_count()` | 반영 가능한 필드 수를 반환합니다. |
| `method_count()` | 반영 가능한 메소드의 수를 반환합니다. |
| `metadata_count()`, `metadata_key(i)`, `metadata_value(i)` | 유형 선언에 대한 메타데이터를 쿼리합니다. |
| `has_metadata(key)`, `metadata(key)` | 메타데이터를 수동으로 열거하지 않고 조회합니다. |
| `has_annotation(name)` | 주석 이름을 테스트합니다. `name` 및 `@name`이 모두 허용됩니다. |
| `enum_member_count()`, `enum_member_name(i)`, `enum_member_value(i)` | 열거형 멤버를 쿼리합니다. |
| `enum_underlying_type()` | 기본 열거형 유형을 반환합니다. |
| `is_scoped_enum()` | 열거형의 범위가 지정되었는지 여부를 보고합니다. |

열거형 관련 쿼리는 열거형 유형에만 의미가 있습니다. 사용하기 전에 `kind()`을 확인하세요.

## 필드정보

`field_info<T>(owner, index)`을 사용하여 필드 쿼리를 구성합니다.

```VeroScript
type<Record> info = type<Record>();
field_info<Record> field = field_info<Record>(info.qualified_name, 0);

print(field.name());
print(field.declared_type());
print(field.runtime_type());
print(field.is_static());
print(field.is_mutable());
```

필드 정보는 다음을 제공합니다.

| 회원 | 목적 |
| --- | --- |
| `name()` | 필드 이름. |
| `declared_type()` | 소스에 선언된 필드 유형입니다. |
| `runtime_type()` | 런타임에 의해 기록된 유형입니다. |
| `is_static()` | 필드가 정적인지 여부를 보고합니다. |
| `is_mutable()` | 필드가 문맥적 `mutable` 수정자를 사용하는지 보고합니다. |
| `is_declared_on_type()` | 쿼리된 형식에서 필드가 직접 선언되었는지 여부를 보고합니다. |
| `metadata_count()`, `metadata_key(i)`, `metadata_value(i)` | 필드 선언에 대한 메타데이터를 쿼리합니다. |
| `has_metadata(key)`, `metadata(key)`, `has_annotation(name)` | 필드 메타데이터와 주석을 이름별로 조회합니다. |

## 메소드 정보

`method_info<T>(owner, index)`을 사용하여 메서드 쿼리를 구성합니다.

```VeroScript
method_info<Record> method = method_info<Record>(info.qualified_name, 0);

print(method.name());
print(method.return_type());
print(method.parameter_count());
print(method.parameter_type(0));
print(method.parameter_is_ref(0));
print(method.parameter_is_const(0));
```

분석법 정보는 다음을 제공합니다.

| 회원 | 목적 |
| --- | --- |
| `name()` | 메소드 이름. |
| `return_type()` | 반환 유형. |
| `parameter_count()` | 매개변수 수. |
| `parameter_type(i)` | 매개변수 `i`의 기본 유형입니다. |
| `parameter_name(i)` | 매개변수 `i`의 이름입니다. |
| `parameter_is_ref(i)` | 매개변수 `i`이 참조인지 여부를 보고합니다. |
| `parameter_is_const(i)` | 매개변수 `i`이 const인지 아니면 읽기 전용 참조인지 보고합니다. |
| `is_static()` | 메소드가 정적인지 여부를 보고합니다. |
| `is_virtual()` | 메소드가 가상인지 여부를 보고합니다. |
| `is_abstract()` | 메소드가 추상인지 여부를 보고합니다. |
| `is_const()` | 멤버 메서드가 const인지 여부를 보고합니다. |
| `is_deleted()` | `= delete`을 사용하여 메서드가 삭제되었는지 여부를 보고합니다. |
| `is_constructor()` / `is_destructor()` | 메서드가 생성자인지 소멸자인지 보고합니다. |
| `metadata_count()`, `metadata_key(i)`, `metadata_value(i)` | 메서드 선언에 대한 메타데이터를 쿼리합니다. |
| `has_metadata(key)`, `metadata(key)`, `has_annotation(name)` | 메소드 메타데이터 및 주석을 이름별로 조회합니다. |

`parameter_type(i)`은 `ref` 또는 `const ref`를 제거한 후 기본 유형을 반환합니다. 전체 매개변수 의미 체계를 재구성하려면 `parameter_is_ref(i)` 및 `parameter_is_const(i)`도 검사하세요.

## 실행 가능한 리플렉션

`set<V>`는 수신 객체가 쓰기 가능해도 `const`로 선언된 필드를 변경할 수 없습니다. 수신 객체가 `const`라면 `mutable` 필드만 변경할 수 있습니다. `get<V>`로 읽는 것은 가능합니다.

설명자를 선택한 뒤 `field_info<T>.get<V>(객체)`, `set<V>(객체, 값)`, `method_info<T>.invoke<R>(객체, 인수...)`, `invoke_static<R>(인수...)`를 사용할 수 있습니다. `V`와 `R`은 명시해야 하며 멤버의 런타임 타입과 정확히 일치해야 합니다. `R`은 `void`일 수 있습니다. 인수는 순서대로 한 번 평가되고 호출 전에 고정 시그니처와 대조됩니다. 오버로드는 `parameter_count()`와 `parameter_type(i)`로 구별합니다.

공개 스크립트 인스턴스 필드와 스크립트 클래스 또는 인터페이스에 선언된 공개 메서드를 실행할 수 있습니다. 추상 인터페이스 메서드는 수신 객체의 구현으로 연결되며, 본문이 있는 공개 인터페이스 메서드와 공개 정적 인터페이스 메서드도 호출할 수 있습니다. 가상 메서드는 동적 디스패치를 사용하고 구조체 필드 쓰기는 원본 객체를 갱신합니다. 무효, 위조, 만료, 다른 프로그램 소유 또는 접근 불가 설명자는 런타임 오류가 됩니다. 비공개 멤버, 호스트 멤버, 정적 필드, 생성자/소멸자, 삭제된 메서드는 조회만 가능합니다. C++, Python, C# AOT는 현재 실행 가능한 리플렉션을 컴파일 시 거부합니다. 이전 개발 빌드의 바이트코드 패키지와의 호환성은 지원하지 않습니다. 현재 컴파일러로 소스에서 다시 빌드하세요. 초기 출시 버전은 0.1.1으로 유지됩니다. [전체 예제](../../../../Scripts/examples/reflection_executable.vs)를 참조하세요.

`invoke`와 `invoke_static`은 참조 매개변수와 참조 반환을 지원합니다. 참조 인수는 `&value` 또는 `ref value`로 명시하고 타입이 정확히 일치해야 합니다. 쓰기 가능한 참조는 읽기 전용 저장소에 바인딩할 수 없습니다. 반환 타입은 `invoke<int&>(receiver, ...)` 또는 `invoke<const int&>(receiver, ...)`로 지정하며 정적 호출도 같습니다. 반환 참조는 별칭과 const 속성을 유지하고 값으로 읽으면 역참조합니다. 일반 호출과 같은 관리형 참조 셀을 사용하며 복사로 쓰기 반영을 흉내 내지 않습니다. 모든 인수를 검사한 뒤 메서드에 진입하며 예외 이전의 변경은 일반 호출 규칙을 따릅니다.

참조 반환값을 다른 참조 인수로 전달하려면 먼저 지역 참조에 바인딩한 뒤 `&alias` 또는 `ref alias`를 전달하세요.

객체 인수는 호출 지점의 정적 타입으로도 검사합니다. 유효한 업캐스트와 null 핸들은 허용하지만, 암시적 다운캐스트는 메서드 본문 실행 전에 거부합니다. 다운캐스트가 필요하면 먼저 명시적으로 `cast<T>`를 사용하세요. `set<V>`는 소스 타입을 `V`에 대입할 수 있는지 컴파일 시 검사합니다. `void` 식은 인수로 사용할 수 없습니다.

`set<V>`는 일반 대입과 같은 검사된 변환을 값에 적용하므로, `set<int64>`로 `int` 값을 `int64` 필드에 쓸 수 있습니다. 숫자 오버플로는 필드가 변경되기 전에 거부되며, `V`는 여전히 필드의 선언 타입과 정확히 일치해야 합니다.

값으로 전달하는 구조체와 고정 길이 배열 인수는 해당 인수를 평가할 때 복사되므로, 이후 인수가 원본을 수정해도 영향을 받지 않습니다. 참조 반환값으로 값 변수를 초기화하거나 값 인수 또는 값 반환값을 만들 때도 값이 복사됩니다. 클래스와 인터페이스 핸들은 일반 호출 의미를 유지합니다. 이러한 컴파일러 수정을 적용하려면 이전 바이트코드 패키지를 소스에서 다시 빌드하세요.

## 모듈, 자유 함수, 전역 및 열거형 멤버 정보

`module_info()`은 항목 모듈에서 볼 수 있는 선언을 노출합니다. `function_count()`, `global_count()` 및 `type_count()`을 경계로 사용한 다음 `function_info(i)` 또는 `global_info(i)`를 구성합니다. `type_name(i)`은 표시되는 각 스크립트 유형 이름을 반환합니다. 가져온 개인 선언은 노출되지 않습니다. 입력 모듈 개인 선언 및 내보낸 가져온 선언은 일반 가져오기/내보내기 규칙에 따라 계속 표시됩니다.

`function_info`은 이름, 안정적인 링크 이름, 모듈 경로, 반환 및 매개변수 서명, `ref`/`const` 플래그, 삭제/내보낸 플래그 및 선언 메타데이터를 보고합니다. `global_info`은 이름, 링크 이름, 모듈 경로, 선언/런타임 유형, const/내보낸 플래그 및 선언 메타데이터를 보고합니다. 정적 데이터 멤버는 필드로 유지되며 `global_info`에서 중복되지 않습니다.

열거형 `E`의 경우 `enum_member_info<E>(type<E>().qualified_name, i)`을 구성하여 열거자 이름, 전체 `int64` 값 및 선언 메타데이터를 검사합니다. 이는 `type<E>`에서 이미 사용할 수 있는 압축 열거형 쿼리를 보완합니다.

## 선언 메타데이터

선언에는 주석이 포함될 수 있습니다. 언어는 주석 이름, 키 및 값을 저장하지만 애플리케이션별 의미를 할당하지는 않습니다.

```VeroScript
@entity(sample)
struct BaseRecord
{
	int id;
};

struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}
};
```

호스트, 편집기 또는 기타 도구는 `metadata_count()`, `metadata_key(i)` 및 `metadata_value(i)`를 통해 이 정보를 열거하거나 직접 조회를 위해 `has_metadata(key)`, `metadata(key)` 및 `has_annotation(name)`를 사용할 수 있습니다. 누락된 키는 `metadata(key)`에서 빈 문자열을 반환합니다. 빈 값과 누락된 키를 구별해야 하는 경우 `has_metadata(key)`을 사용하세요. 이 예에서 `@entity`, `@data` 및 `@call`은 기본 제공 비즈니스 동작이 아니라 쿼리 가능한 메타데이터 이름입니다. 스크립트 또는 호스트 통합에서 자체 규칙을 정의할 수 있습니다.

호스트 바인딩 카탈로그의 주석과 런타임 스크립트 리플렉션은 두 개의 별도 레이어입니다. 바인딩 카탈로그는 호스트 API를 설명하는 반면, 리플렉션 정보 유형은 현재 컴파일된 스크립트 모듈을 쿼리합니다.

## 완전한 예

다음 예제에서는 상속, 필드, 메서드, 정적 메서드, 최상위 전역 변수, 열거형 멤버 및 선언 메타데이터를 쿼리합니다.

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
};

int main()
{
	type<Record> info = type<Record>();
	print("{} ({})", info.name, info.kind());
	print("bases={}, fields={}, methods={}",
		info.base_count(), info.field_count(), info.method_count());

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}",
			field.name(), field.declared_type(), field.runtime_type(), field.is_static());
	}

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, params={}, static={}, virtual={}",
			method.name(), method.return_type(), method.parameter_count(),
			method.is_static(), method.is_virtual());
	}

	module_info module = module_info();
	print("module={}, functions={}, globals={}, types={}",
		module.path(), module.function_count(), module.global_count(), module.type_count());

	for (int i = 0; i < module.function_count(); i++)
	{
		function_info function_meta = function_info(i);
		print("function {}: returns={}, exported={}",
			function_meta.name(), function_meta.return_type(), function_meta.is_exported());
	}

	for (int i = 0; i < module.global_count(); i++)
	{
		global_info global_meta = global_info(i);
		print("global {}: declared={}, runtime={}, const={}",
			global_meta.name(), global_meta.declared_type(),
			global_meta.runtime_type(), global_meta.is_const());
	}

	type<RecordState> state_type = type<RecordState>();
	enum_member_info<RecordState> state =
		enum_member_info<RecordState>(state_type.qualified_name, 0);
	print("enum member {}={} annotated={}",
		state.name(), state.value(), state.has_annotation("display"));
	return 0;
}
```

실행 가능한 버전은 [예제 인덱스](../examples.md)의 리플렉션 항목을 참조하세요. 리플렉션 API는 진입점 반환 유형과 독립적입니다. 이 예에서는 `int main`을 사용하고 명시적인 프로세스 상태 코드를 반환합니다.

## 경계와 오류

- 리플렉션은 정보 조회와 위에서 설명한 타입 지정 실행 작업을 지원하지만, 모든 멤버에 대한 무제한 동적 접근은 제공하지 않습니다.
- `field_count()`, `method_count()` 및 enum-member 개수는 현재 런타임 유형 정보를 설명합니다. 인덱싱하기 전에 경계를 확인하세요.
- 유형 정보 누락, 잘못된 인덱스, 적용할 수 없는 쿼리는 런타임 오류입니다. 유효하지 않은 인덱스를 빈 결과로 처리하지 마십시오.

기본 형식 규칙은 [데이터 형식](data-types.md), [구조체](structs.md), [클래스](classes.md) 및 [열거형](enums.md)을 참조하세요. 호스트 측 인터페이스 설명은 [바인딩 카탈로그](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/bindings.md)에서 다룹니다.

## 구조화된 리플렉션

<a id="reflection-metadata-contract"></a>

`type<T>`, field_info, method_info, module_info, function_info, global_info, enum_member_info는 annotation_count/annotation/annotations, comment_count/comment/comments, documentation, comments_available, comments_retention, declaration, source_metadata를 제공합니다. 인덱싱 전에 개수를 확인하세요. annotation_info와 annotation_argument_info는 argument_count/argument/arguments를 제공하며 형식화된 값 접근은 스칼라 종류와 일치해야 합니다. definition()은 등록된 설명을 반환하고 없을 수 있으면 is_valid()를 검사합니다.

호스트는 Full, Documentation 또는 NoComments를 선택합니다. Unavailable은 소스 정보가 없음을 뜻합니다. NoComments도 애너테이션과 별도 등록 문서는 제거하지 않습니다. 메타데이터 객체는 불변 스냅샷을 유지하므로 핫 리로드가 기존 객체의 대상을 바꾸지 않습니다. 조회가 접근 제어를 우회하거나 비공개 멤버 호출을 허용하지는 않습니다.

[애너테이션과 소스 메타데이터](annotations.md)

## 중첩 타입 리플렉션

`display_name()`, `is_nested()`, `is_local()`, `declaring_type_name()`, `declaring_function_name()`, `access()`로 소속을 조회합니다. 타입 멤버가 아니면 접근 종류는 `none`입니다. `qualified_name`은 구체 런타임 식별자이며 표시 이름은 식별 키가 아닙니다.

`nested_type_count()`, `nested_type_name(i)`, `nested_type_parameter_count(i)`, `nested_type_declaration(i)`는 접근 가능한 직접 자식 선언을 열거하며 아직 구체화하지 않은 템플릿도 포함합니다. 매개변수 개수에는 바깥 타입에서 가져온 항목을 포함하지 않습니다. 별칭은 별도 런타임 타입을 만들지 않습니다. 조회 생성 시 접근 문맥이 고정되며 지역/비공개 이름이 전역 탐색에 노출되지 않습니다.

## 템플릿 메타데이터

`module_info.template_declaration_count()`는 구체화하지 않고 열린 선언을 열거합니다. `template_declaration_id/name/kind/owner`는 선언 인덱스를 받습니다. `template_parameter_count`, `template_own_parameter_count`, `template_enclosing_parameter_count`로 환경을 구분합니다. `template_parameter_id/name/kind/value_type/default/is_enclosing`는 `(선언 인덱스, 매개변수 인덱스)`를 받으며 결합 목록은 외부 매개변수를 먼저 배치합니다.

`type<T>`, `method_info<T>`, `function_info`는 `template_declaration_id()`, `template_argument_count()` 및 인덱스가 있는 `template_argument_encoding/name/kind/value_type`를 제공합니다. 외부 인수에는 `template_enclosing_argument_count/encoding/name`을 사용합니다. 일반 선언은 빈 식별과 0개를 반환합니다. 인코딩은 정수 너비, 부호, 열거형 식별을 유지하며 표시 이름은 식별 키가 아닙니다. 조회는 실행 메서드를 생성하지 않고 기존 형식 전용 개수의 의미를 유지합니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/reflection.md) · [fr-FR](../../fr-FR/language-basics/reflection.md) · [ja-JP](../../ja-JP/language-basics/reflection.md) · [ko-KR](reflection.md) · [zh-CN](../../zh-CN/language-basics/reflection.md) · [zh-TW](../../zh-TW/language-basics/reflection.md)
