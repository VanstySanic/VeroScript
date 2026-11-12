# Schema 주석 보고서

FSchema::TryValidateWithOptions와 schema.validate_with_options는 오류와 주석을 반환하고 TryIsValid/is_valid는 수집 없는 빠른 모드입니다. 실제 평가에 성공한 분기만 기여하며 실패/미선택 분기와 참조하지 않은 $defs는 제외합니다. anyOf는 모든 성공 분기, contains는 모든 일치 원소를 수집합니다. 전체가 무효이면 주석을 버리고 오류는 유지합니다.

InstancePointer → instance_pointer, SchemaLocation → schema_location, EvaluationPath → evaluation_path, Keyword → keyword, Value → value는 인스턴스 JSON Pointer, 키워드 절대 URI, $ref를 포함한 평가 경로, 이름, 불변 정확 JSON 값입니다. title/description/default/examples/deprecated/readOnly/writeOnly/format, 해당 content 메타데이터와 일반 미지 키워드를 수집합니다. contentSchema는 같은 Schema의 contentMediaType과 문자열 인스턴스일 때만 수집하며 암시적 디코딩/검증은 없습니다. $comment는 제외하고 default는 인스턴스를 바꾸지 않습니다.

properties/patternProperties/additionalProperties/unevaluatedProperties는 속성 이름 배열, prefixItems는 마지막 인덱스 또는 true, contains는 일치 인덱스 또는 true, 적용한 items/unevaluatedItems는 true를 기록합니다. 중첩 표식을 부모에 합치지 않습니다. 같은 조건에서 순서는 재현 가능하지만 고정 인덱스 대신 경로/키워드로 읽습니다.

FSchemaReportOptions는 기본 수집, MaxIssues=MaxAnnotations=256입니다. 오류 범위 1–16384, 주석 0–16384입니다. 주석 상한에서는 유효성을 유지하고 bAnnotationsTruncated/bTruncated를 설정하며 실패 분기의 잘림은 버립니다. MaxAnnotations=0도 누락을 감지하고 완전 중지는 bCollectAnnotations=false입니다. Limits/FWork는 작업, 주석 바이트, 노드/깊이와 취소를 제한합니다. 미완료는 false, 기존 보고서 보존과 Cancelled/Limit/Allocation 구분입니다. 값은 Schema/입력/resolver보다 오래 살고 동시 검증은 별도 보고서를 사용합니다.

TryValidate/schema.validate는 기본 주석 옵션, 기존 MaxIssues/Limits 및 validate_with_limits를 유지합니다. 모듈 고유 안정 구조이며 표준 verbose 트리를 주장하지 않습니다. schema_report는 annotation_count/annotation/annotations_truncated, schema_report_options는 limits/max_issues/max_annotations/collect_annotations와 setter를 제공합니다. try_validate_schema_with_options(schema,value,options,ref report,ref error)는 일반 실패 시 보고서를 보존하지만 취소/wrapper 전달 실패는 네이티브 실행 실패입니다.

```VeroScript
json::schema schema = json::compile_schema(json::parse("{\"title\":\"Count\",\"type\":\"integer\"}"));
json::schema_report_options options = json::schema_report_options();
options.set_max_annotations(16);
json::schema_report report = schema.validate_with_options(json::from_int(3), options);
print(report.annotation(0).get("value").as_string());
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/schema-annotations.md) · [fr-FR](../../fr-FR/Reference/schema-annotations.md) · [ja-JP](../../ja-JP/Reference/schema-annotations.md) · [ko-KR](schema-annotations.md) · [zh-CN](../../zh-CN/Reference/schema-annotations.md) · [zh-TW](../../zh-TW/Reference/schema-annotations.md)
