# 애너테이션과 소스 메타데이터

선언 앞에 @Name 또는 @Name(arguments)를 붙입니다. 로드한 확장이 동작을 부여하기 전까지는 설명용 메타데이터입니다. 값을 보관하는 `std::attribute<T>`와는 별개입니다.

인수는 플래그, 스칼라 종류, 이름 있는 값 및 중첩 그룹을 중복 이름까지 소스 순서대로 보존합니다. 임의 실행식은 아닙니다. 중복을 보존하려면 인덱스로 열거하세요. find_annotations는 등록된 이름 규칙을 따르고 find_arguments는 저장된 정확한 인수 이름으로 검색하며 애너테이션 이름에는 @가 포함됩니다.

```VeroScript
/// Example metadata.
@Tag(data=(size=4, enabled=true), label="Example")
class Value
{
};

int main()
{
	type<Value> info = type<Value>();
	assert(info.has_annotation("@Tag"));
	annotation_info tag = info.annotation(0);
	assert(tag.name() == "@Tag");
	assert(tag.argument(0).argument(1).bool_value());
	print(tag.argument(0).argument(0).int64_value());
	return 0;
}
```

## 구조화된 리플렉션

[반사 — 구조화된 리플렉션](reflection.md#reflection-metadata-contract)을 참조하세요.

[리플렉션](reflection.md) · [주석](comments.md) · [std::compiler](../standard-library/compiler.md)

## 애너테이션 이름 비교

기본적으로 대소문자를 구분합니다. `annotation_definition.case_sensitive = false`를 설정하면 `@GuiWidget`, `@guiWidget`, `@GUIWIDGET`가 하나의 정의를 사용합니다. 처리기 구독, `has_annotation`, `find_annotations`, `annotation_info.matches_name(name)`가 같은 규칙을 사용합니다. `name()`과 `raw_text()`는 원래 표기, `definition().name()`은 등록 이름, `definition().case_sensitive()`는 규칙을 반환합니다. 인수와 메타데이터 키는 계속 대소문자를 구분합니다. `find_annotations`와 `matches_name`에는 `@`가 필요하며 `has_annotation`에서는 생략할 수 있습니다. 등록되지 않은 이름은 대소문자를 구분합니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md) · [en-US](../../en-US/language-basics/annotations.md) · [fr-FR](../../fr-FR/language-basics/annotations.md) · [ja-JP](../../ja-JP/language-basics/annotations.md) · [ko-KR](annotations.md) · [zh-CN](../../zh-CN/language-basics/annotations.md) · [zh-TW](../../zh-TW/language-basics/annotations.md)
