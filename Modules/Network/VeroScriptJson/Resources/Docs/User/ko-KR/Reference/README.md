# JSON 참조

스크립트의 값 캡처는 Core의 읽기 전용 바인딩 규칙을 따릅니다. 캡처한 호스트 값을 변경하는 네이티브 메서드를 호출하려면 `mutable` 람다 또는 쓰기 가능한 바인딩의 참조 캡처를 사용합니다. 초기화 캡처와 로컬 static은 일반 Core 의미 규칙을 따릅니다.

VeroScriptJson.h는 VeroScript::Json에 링크하는 독립 C++17 API이며 VM이 필요 없습니다. 제품/패키지 버전은 0.1.1입니다. json을 사용하는 스크립트를 컴파일하기 전에 호환되는 vs-json을 설치합니다. 객체 매핑에는 모듈 컴파일 프로세서도 필요하며 바인딩 카탈로그만으로 등록되지 않습니다.

JSON의 여섯 값 종류는 불변 공유 스냅샷으로 원래 숫자 텍스트, 정확한 십진 비교와 검사된 변환을 유지합니다. 변경은 성공할 때만 적용합니다. 읽기는 공유할 수 있지만 같은 변수/builder 변경은 동기화합니다. Try*는 성공 시 오류를 지우고 실패 시 출력을 보존합니다. 생성자/팩터리는 할당 예외를 던질 수 있습니다. 오류 텍스트가 비어도 FError::Code와 무할당 Id()를 확인합니다. 작업 검사 예외는 Callback, sink 예외는 Sink, 할당 실패는 Allocation입니다. 크기/인덱스는 int64, DOM 기본/최대 깊이는 256입니다. as_double은 반올림할 수 있고 정확한 변환은 명시합니다.

큰 컨테이너는 반복 스냅샷 복사 대신 FBuilder로 만듭니다. 복사한 builder는 상태를 공유하고 TryFinish가 닫으며 TryReset으로 재사용합니다. FPointer(RFC 6901), Patch(RFC 6902), Merge Patch(RFC 7396)는 정확한 값을 유지합니다. 루트 삭제는 FDocument::bHasValue로 표현합니다.

FReader::Feed는 소비한 바이트 수를 반환합니다. 남은 접미부를 유지하여 재전달하고 EventReady를 하나씩 읽으며 마지막 청크를 표시해 Done까지 처리합니다. 비최종 `1` 뒤의 `2 `는 숫자 `12`입니다. Visit는 동기 호출이며 false이면 현재 이벤트 뒤에 멈추고 반복 전달하지 않습니다. 예외는 reset 전까지 Failed입니다. visitor/work 컨텍스트는 해당 호출 동안만 빌립니다. FWriter::Write의 bAccepted가 참이면 이벤트를 재전달하지 말고 Resume합니다. `0 + NeedOutput`은 정상 역압력, `0 + Progress`는 오류입니다. Finish는 중단될 수 있고 완료 후 반복 호출할 수 있습니다. 스트리밍은 순서를 유지하며 정렬 출력은 DOM TryWrite를 사용합니다.

json::limits/parse_options/write_options는 값 복사입니다. 반환된 limits를 수정한 뒤 set_limits로 적용합니다. input_bytes/output_bytes/token_bytes/nodes/depth/number_bytes/exponent/work_units와 setter를 제공합니다. 수량은 음이 아닌 int64이며 depth를 높여도 256을 넘지 못합니다. duplicate_keys는 0 Reject, 1 KeepFirst, 2 KeepLast, 3 ReportAll(reader/SAX 전용)입니다. allow_leading_bom, pretty/sort_keys/indent(0–16)도 설정합니다. parse_with_options, try_parse_with_options, write_with_options, make_reader_with_options, make_writer_with_options를 사용합니다.

비예외 함수는 try_parse, try_write, try_write_with_options, try_number, `try_from_string`, `try_from_double`, try_compile_pointer, try_as_int64/uint64/double/double_exact/bool/string, try_resolve, try_patch, try_merge_patch입니다. 결과 참조는 마지막 오류 참조 앞에 오며 옵션 변형은 공개 선언을 따릅니다. 일반 데이터 실패는 false와 json::error를 반환하고 출력을 보존합니다. 취소나 wrapper 전달 실패는 네이티브 호출을 실패시킵니다. 오류 접근자는 code/message/message_id/has_source/byte_offset/line/column/instance_pointer/patch_operation입니다.

array_builder/object_builder는 push/set/reserve/size/capacity/finish, reset_array/reset_object와 try 변형을 제공합니다. 별칭은 같은 상태를 보고 finish의 DOM은 독립입니다. try_finish/size/capacity는 첫 참조가 결과, 마지막이 오류입니다. DOM에는 try_contains/try_keys도 있습니다. reader 상태는 0 NeedInput, 1 EventReady, 2 Done, 3 Failed이며 event()가 데이터를 소유합니다. writer 상태는 0 Active, 1 NeedOutput, 2 Done, 3 Failed입니다. write는 수락 여부, take는 최대 64 KiB를 반환하고 resume으로 계속합니다. 실패 후 error를 확인하고 명시적으로 reset합니다.

Schema는 Draft 2020-12, 정확한 수치 제약, 동적 참조, unevaluated 조합과 Unicode ECMAScript 정규식을 지원합니다. format/content는 기본적으로 주석이며 암시적으로 디코딩하지 않습니다. schema_options는 retrieval_uri, limits, max_resources를 제공하고 빈 URI는 urn:veroscript:json-schema:root입니다. compile_schema_with_options(schema,options,resources)는 URI → Schema 객체를 받습니다. C++ 동기 resolver는 컴파일 동안만 빌리고 결과가 리소스를 소유합니다. 다운로드나 cwd 해석은 하지 않습니다. 표준 메타 Schema와 여덟 어휘 리소스가 내장됩니다.

컴파일/검증은 작업, 취소, 노드, 텍스트와 깊이를 검사합니다. URI는 64 KiB, 정규식 패턴은 4096바이트, 정규식 실행 1회의 유지 할당은 16 MiB입니다. 스택/단계 보호도 Limit를 반환합니다. 보고서는 중첩 오류를 포함해 1–16384항목이며 잘림은 성공이 아닙니다. schema_report의 valid/truncated/size/issue 및 JSON details를 확인합니다.

추가 검사 함수는 try_compile_schema, try_validate_schema, try_compile_schema_with_options(value,options,resources,ref schema,ref error), try_validate_schema_with_limits(schema,value,max_issues,limits,ref report,ref error), try_compile_pointer_with_limits, pointer.try_resolve[_with_limits], document.try_get입니다. 루트가 없으면 출력을 보존하고 false를 반환합니다. 컴파일된 Pointer도 사용 시 현재 token/depth/work 제한을 다시 검사합니다.

- [C++ 형식 매핑](native-mapping.md)
- [스크립트 객체 매핑](script-mapping.md)
- [닫힌 다형 매핑](polymorphic-mapping.md)
- [Schema 주석 보고서](schema-annotations.md)
- [Schema 형식과 방언](schema-formats.md)
- [진단과 현지화](diagnostics.md)

```VeroScript
json::schema schema = json::compile_schema(json::parse("{\"type\":\"integer\",\"minimum\":0}"));
bool valid = schema.is_valid(json::from_number("1.0"));
json::schema_report report = schema.validate(json::from_string("bad"));
print(report.issue(0).get("keyword").as_string());
```

```cmake
find_package(VeroScriptJson 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Json)
```

`FSchemaOptions::MaxResources = 256`

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
