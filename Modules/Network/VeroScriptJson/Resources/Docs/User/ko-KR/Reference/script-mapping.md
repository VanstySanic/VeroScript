# 스크립트 객체 매핑

json::converter<T>는 활성 VM에서 스크립트 객체를 변환하고 입출력 Schema를 생성합니다. 네이티브 확장을 등록한 후 bindings, processors, dependencies, Owners를 모듈 환경에 함께 넣고 프로젝트가 json 공급자에 의존하도록 선언한 뒤 컴파일합니다. 카탈로그만으로 프로세서를 등록할 수 없습니다. 네이티브 C++ 매핑은 독립입니다.

write(ref value)는 const ref를 명시합니다. write_with_options/read_with_options/try_write/try_read와 옵션 변형은 일반 실패 시 false와 json::error를 반환하고 기존 출력을 보존하며 성공 시 오류를 지웁니다. 취소, 시간 초과 및 회복 불가 VM 오류는 실행을 끝냅니다.

bool/정수/유한 실수/string은 JSON 형식과 범위를 유지하며 정수는 double을 거치지 않습니다. enum은 이름을 쓰며 JsonName으로 바꿀 수 있습니다. array/deque/queue/stack/priority_queue는 values() 순서, stack은 바닥부터입니다. set은 중복 거부, multiset은 개수 유지입니다. string 키 map/ordered_map은 객체, 다른 map과 모든 multimap은 key_value_pairs가 필요합니다. 공개 struct/class 인스턴스 필드를 변환하고 class는 null, json::value는 기존 값을 보존합니다. 해시 순서는 계약을 따르며 삽입 순서가 필요하면 ordered를 사용합니다. 특별한 호스트 형식/함수는 명시적 변환기가 필요합니다.

JsonName(value)은 이름, JsonIgnore는 입출력 제외, JsonRequired는 존재 필수(null 여부는 별도), JsonSerializeOnly는 입력 거부, JsonDeserializeOnly는 출력 제외입니다. JsonProperty(name,setter)는 인수 없는 const getter를 명시하고 setter가 없으면 출력 전용입니다. JsonFactory(type)는 create(value,context), JsonConverter(type)는 어댑터입니다. 접근 가능한 상속 공개 필드를 포함하고 정적/접근 불가 필드는 제외하며 const는 출력 전용입니다. 생략한 비필수 필드는 정상 생성 기본값을 유지합니다. 중복/충돌은 실패하고 미지 필드는 set_ignore_unknown(true)가 아니면 거부합니다.

mapping_options는 ignore_unknown/numeric_enums/key_value_pairs/exact_floating 및 setter, set_limits(depth,nodes)를 제공하며 깊이는 1–256, 노드는 양수입니다. 설정은 값 복사하고 변환마다 독립 컨텍스트를 가집니다. instance_pointer는 JSON Pointer 이스케이프를 사용합니다. class 순환은 거부하고 다른 분기의 공유는 값으로 반복합니다. 새 값을 성공 시에만 적용하지만 팩터리/속성/변환기의 외부 효과는 롤백하지 않습니다.

어댑터의 정적 함수는 encode(const Target&,json::value&,mapping_context), decode(json::value,Target&,mapping_context), schema(mapping_context)입니다. custom_converter<Target,Adapter>에는 create(value,context) -> Target도 필요합니다. 기본 생성자가 없는 struct는 팩터리/create가 필요하고 class는 null에서 시작할 수 있습니다. context.fail/fail_range/adopt(error)로 진단하고 진단 없는 false는 Callback입니다. 중첩 converter::encode/decode/schema_nested는 문맥을 공유하며 사용자 Schema는 실제 변환을 설명해야 합니다.

schema()는 입력, schema_with_options(options,false)는 출력 Schema입니다. 필수/무시/미지/금지 필드, 정수 범위, 원소, enum과 null 여부를 방향별로 반영합니다. 재귀는 $defs를 쓰며 생성자를 실행하지 않습니다. schema_nested 캐시는 형식과 방향을 구분하여 set_schema_input 변경 시 반대 방향의 정의를 재사용하지 않습니다.

자동 생성은 모듈/네임스페이스에 직접 선언된 형식 대상입니다. 지역/중첩 데이터 형식에는 모듈 도우미를 만들지 않고 JsonFactory/JsonConverter는 범위 제한을 보고합니다. 보이는 적법한 범위에서 custom_converter를 사용하세요. JsonPolymorphic는 비템플릿 테이블 클래스 자체에 정적 함수를 생성하므로 지역/중첩도 가능합니다. 소유권, 가시성, 생성 및 접근 규칙을 지킵니다. 필드/별칭/기본 클래스/어댑터는 선언 모듈과 import/re-export, 템플릿 인수 수/기본값/모듈 우선순위로 해석합니다. 형식/값 매개변수(int/bool/enum)와 한정 constexpr 식을 보존합니다. 서로 다른 닫힌 형식의 Schema 캐시는 구분하고 static constexpr 필드는 제외합니다.

JSON 프로세서로 만든 .vslib는 생성 변환 함수를 유지합니다. 일반 라이브러리의 접근 가능한 공개 필드도 소비자가 매핑할 수 있지만 생성/접근 규칙은 유지합니다. 프로세서 작업 수, 생성 소스 바이트와 작업 예산이 적용됩니다. AOT/다른 호스트는 실제 지원 경로를 확인해야 하며 VM 검증은 다른 플랫폼 보장이 아닙니다.

```cpp
Description.Bindings = Registry.GetBindings();
Description.CompileProcessing.Registry = Registry.GetProcessors();
Description.AvailableDependencies = Registry.GetDependencies();
Description.Owners = Registry.GetOwners();
```

```VeroScript
struct Record
{
public:
	@JsonName(value="record_id") @JsonRequired int64 id = 7;
	string name = "default";
	@JsonIgnore int transient = 0;
};

int main()
{
	Record source = Record();
	json::value document = json::converter<Record>::write(ref source);
	Record result = json::converter<Record>::read(document);
	json::schema shape = json::compile_schema(json::converter<Record>::schema());
	assert(result.id == 7 && shape.is_valid(document));
	return 0;
}
```

```cpp
static bool encode(const Target& input, json::value& output, json::mapping_context context);
static bool decode(json::value input, Target& output, json::mapping_context context);
static json::value schema(json::mapping_context context);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/script-mapping.md) · [fr-FR](../../fr-FR/Reference/script-mapping.md) · [ja-JP](../../ja-JP/Reference/script-mapping.md) · [ko-KR](script-mapping.md) · [zh-CN](../../zh-CN/Reference/script-mapping.md) · [zh-TW](../../zh-TW/Reference/script-mapping.md)
