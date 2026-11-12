# C++ 형식 매핑

VeroScriptJsonMapping.h를 포함하고 VeroScript::Json에 링크합니다. Core는 필요 없습니다. TObjectMapping<T>로 필드/속성을 명시하여 TryEncode/TryDecode를 호출합니다. 성공할 때만 출력을 바꿉니다. 디코딩은 변환을 준비한 뒤 정상 생성과 대입을 수행하며 기본 생성자가 없으면 shared_ptr<T> 팩터리를 제공합니다. 생성자/속성/변환기의 외부 부수 효과는 롤백하지 않습니다.

이름은 올바른 UTF-8과 NUL을 허용하고 경로는 RFC 6901로 이스케이프합니다. 알 수 없는 필드는 bIgnoreUnknownFields가 아니면 거부합니다. FFieldOptions는 required/ignore/serialize/deserialize를 제어합니다. 출력 전용 필드가 입력에 있으면 실패하고 ignore는 건너뜁니다. 중복/방향 충돌은 등록 시 실패합니다. 기본 클래스 필드는 명시적으로 추가하고 정적 멤버/속성을 자동 열거하지 않습니다. 등록된 Property만 실행합니다.

bool, 64비트 이하 정수, 유한 실수와 소유 UTF-8 문자열의 JSON 형식을 유지합니다. 정수는 double을 거치지 않습니다. bExactFloatingPoint는 반올림을 거부하고 기본값은 검사된 반올림입니다. vector/list/deque/고정 배열은 JSON 배열이며 고정 길이를 검사합니다. set은 중복 거부, multiset은 유지하며 순서는 컨테이너 계약을 따릅니다. 유일한 string 키 map은 객체, 다른 map/multimap은 bMapAsPairs가 필요합니다. optional/shared_ptr/unique_ptr는 null을 허용하고 일반 값은 거부합니다. RegisterEnum은 등록 이름을, bEnumsAsNumbers는 정수를 사용하지만 미등록 값은 여전히 거부합니다. 미지원 형식을 빈 객체로 바꾸지 않습니다.

RegisterConverter<T>는 형식화된 encode/decode/Schema를 제공합니다. 재귀 FMappingContext는 경로, 예산과 취소를 공유합니다. 호출마다 불변 레지스트리 스냅샷을 얻고 레지스트리 복사는 등록 상태를 공유하지만 교체는 진행 중인 호출을 바꾸지 않습니다. Owner는 코드/소멸자/RTTI를 유지해야 합니다. 반환 및 중첩 shared_ptr는 복사/별칭도 호출의 변환기 집합을 보존하여 레지스트리와 외부 객체보다 오래 살 수 있습니다. 값으로 이동하거나 다른 제어 블록을 쓰면 호출자가 Owner를 유지합니다. 최초 형식 키의 RTTI Owner는 레지스트리/스냅샷 해제까지 남습니다. 콜백/팩터리는 Owner보다 먼저 잠금 밖에서 파괴하며 삽입 실패도 같습니다.

활성 경로 순환은 json.cycle, 다른 분기의 공유 참조는 값으로 반복 출력하며 그래프 정체성을 복구하지 않습니다. TryMappingSchema<T>(Input/Output,...)는 방향, $defs/$ref, 정수 범위, 길이와 enum을 반영합니다. 사용자 Schema가 없으면 실패합니다. 생성 불변 조건, 사용자 비교, 쌍 배열의 키 유일성과 정확한 실수 변환은 디코더가 검사합니다. FMappingOptions::Limits와 빌린 FWork가 압축 JSON 바이트, 깊이, 노드, 텍스트와 숫자를 제한합니다. 첫 오류/경로를 보존하고 취소 시 출력하지 않습니다.

```cpp
#include <VeroScriptJsonMapping.h>
using namespace VeroScript::Json;

struct Settings
{
	std::string Name;
	int Port = 8080;
};
FMappingRegistry registry;
FError error;
FFieldOptions required;
required.bRequired = true;
bool registered = TObjectMapping<Settings>("app::Settings")
	.Field("name", &Settings::Name, required)
	.Field("port", &Settings::Port)
	.Register(registry, error);

FJsonValue input;
std::shared_ptr<Settings> settings;
bool decoded = registered && TryParse("{\"name\":\"Vero\"}", input, error)
	&& TryDecode(input, settings, error, registry);
// Successful decoding preserves the constructed default port, 8080.
```

```cpp
FJsonValue input_schema, output_schema;
bool ok = TryMappingSchema<Settings>(EMappingDirection::Input, input_schema, error, registry)
	&& TryMappingSchema<Settings>(EMappingDirection::Output, output_schema, error, registry);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/native-mapping.md) · [fr-FR](../../fr-FR/Reference/native-mapping.md) · [ja-JP](../../ja-JP/Reference/native-mapping.md) · [ko-KR](native-mapping.md) · [zh-CN](../../zh-CN/Reference/native-mapping.md) · [zh-TW](../../zh-TW/Reference/native-mapping.md)
