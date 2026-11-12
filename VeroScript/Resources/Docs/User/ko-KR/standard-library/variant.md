# variant — 동적 값 상자

variant는 import가 필요 없는 Core 형식입니다. std::variant와 variant는 같은 형식입니다. 정규화된 원본 형식과 값 하나를 저장하거나 빈 상태를 유지합니다.

목표가 variant로 확정된 위치에서는 암시적 상자화가 가능합니다. 읽기는 cast<T> 또는 try_cast<T>를 명시해야 합니다. variant_is<T>는 변환 없이 정확한 형식을 검사합니다. `optional<T>`와 `try_cast`도 내장 기능이므로 표준 라이브러리를 import할 필요가 없습니다. 빈 상자의 `cast<T>`는 실패하고, 빈 상자 또는 일반 변환 불일치의 `try_cast<T>`는 빈 optional을 반환합니다. 복사 실패, 취소, 제한 초과는 오류입니다.

복사는 불변 상자를 공유합니다. 스크립트 struct는 저장과 추출 시 값으로 복사하고 class와 컨테이너는 기존 참조 의미를 유지합니다. 기본 클래스와 형식이 있는 null을 포함한 정적 선언 형식을 보존합니다. 형식이 있는 null은 값이 있는 상태이며 빈 상자와 다릅니다. 원시 nullptr는 먼저 구체적인 형식 변수에 저장해야 합니다.

variant 사이의 동등 비교만 정의하며 정규 형식과 기존 값의 동등 조건이 모두 일치해야 합니다. 동적 연산, 멤버 검색, 호출, 암시적 추출, 정렬과 해시는 없습니다. variant 키, channel 전송, variant를 포함하는 worker 인수·결과·캡처·전역은 거부합니다. worker 내부 지역 값은 허용하며 array/map 값에도 저장할 수 있습니다.

```vero
import "core.vs";
variant empty;
variant answer = 42;
assert(!variant_has_value(empty));
assert(variant_is<int>(answer));
int exact = cast<int>(answer);
optional<double> converted = try_cast<double>(answer);
map<string, variant> properties = map<string, variant>();
properties["width"] = answer;
properties["text"] = "Save";
assert(cast<int>(properties["width"]) == exact);
```

VM은 전체 의미를 지원합니다. Mixed/asmjit는 지원하지 않는 variant 명령을 안전하게 VM으로 실행합니다. 이식 가능한 바이트코드와 패키지는 선언과 명령을 유지합니다. 독립 C++, C#, Python AOT는 출력 생성 전에 variant 의존성을 명시적으로 거부합니다. 디버거는 읽기 전용 $present, $type, $value를 표시하며 type<variant>().kind()는 "variant"를 반환합니다.

## SDK

[인터롭 SDK — variant](../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#exported-value-lifetime)을 참조하세요.

내보낸 객체가 호스트 호출 후에도 유지되고 메서드나 소멸자가 스크립트 전역 상태를 사용한다면 기존 영구 세션 모드를 사용하세요. C++ SetHotReloadEnabled(true), C# HotReloadEnabled 또는 Python hot_reload_enabled=True를 설정합니다. 일반 임시 실행은 호출 종료 시 전역 상태를 해제합니다.

```text
C:      vs_interop_value_set_variant_empty / vs_interop_value_set_variant
        vs_interop_variant_has_value / vs_interop_variant_get_type_name / vs_interop_variant_get
C#:     value.SetVariantEmpty(); session.CreateVariant(typeName, payload);
        value.VariantHasValue; value.VariantTypeName; value.GetVariantValue();
Python: Value.empty_variant(); session.variant(type_name, payload_value);
        value.set_variant_empty(); value.assign_variant(other_box);
        value.variant_has_value(); value.variant_type_name(); value.variant_get();
C++:    VeroScriptVariant.h, VeroScriptValueRoot.h
        session.MakeVariant(typeName, payload, output, error);
        session.RetainValue(output, root, error); root.Read(); root.Reset();
```

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/variant.md) · [fr-FR](../../fr-FR/standard-library/variant.md) · [ja-JP](../../ja-JP/standard-library/variant.md) · [ko-KR](variant.md) · [zh-CN](../../zh-CN/standard-library/variant.md) · [zh-TW](../../zh-TW/standard-library/variant.md)
