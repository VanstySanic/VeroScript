# Schema 형식과 방언

Draft 2020-12 format은 기본 주석입니다. FSchemaOptions::bAssertFormats 또는 schema_options.set_assert_formats(true)는 전체 컴파일 리소스의 단언을 켜고 미지 형식은 컴파일 실패로 처리합니다. 문자열에만 적용하며 불일치는 일반 무효, 취소/할당/제한은 미완료입니다. DNS/네트워크/전송/리소스 접근이나 문자열 변경은 없습니다.

19개를 지원합니다. date-time/date/time은 RFC 3339 구문, 오프셋, 비어 있지 않은 소수 초와 오프셋 환산 후 윤초 위치를 검사하지만 공고를 조회하지 않습니다. duration은 부록 A의 주/순서/중간 성분을 확인합니다. email/idn-email은 SMTP 인용/이스케이프/길이/IP와 국제 UTF-8 로컬 부분을 지원합니다. hostname/idn-hostname은 길이, A-label/Punycode/NFC/IDNA 문맥/bidi, 비전환 UTS #46을 사용하고 끝 루트 점은 받지 않습니다. ipv4/ipv6는 엄격하며 IPv4 선행 0은 거부합니다. uri/uri-reference는 RFC 3986, iri/iri-reference는 RFC 3987 Unicode 및 사적 문자 위치를 따릅니다. uuid는 하이픈 있는 대소문자 hex이며 URN은 불가, uri-template은 RFC 6570, json-pointer/relative-json-pointer는 fragment 디코딩 없는 문자열, regex는 Unicode ECMAScript입니다.

IDNA는 Unicode 17 / python-idna 3.12의 생성 C++ 데이터로 Python 런타임이 필요 없습니다. 매핑 입력 4096바이트, 레이블당 256코드 포인트, 정규화 임시 메모리 64 KiB이며 초과하면 Limit입니다. DNS A-label은 레이블당 63, 전체 253바이트이며 취소도 검사합니다.

사용자 메타 Schema에 format-assertion 어휘를 선언하고 $schema로 선택할 수 있습니다. 알려진 선택 어휘는 완전한 의미를 가지며 미지 필수 어휘는 실패, 미지 선택 키워드는 주석입니다. 직접 선택한 메타 Schema의 $vocabulary만 적용하고 $ref로 상속하지 않으며 업무 Schema의 $vocabulary로 방언을 바꾸지 않습니다. 포함 리소스는 자체 $schema가 없으면 상속하고 별도 검색 문서에 $schema가 없으면 모듈의 `Draft 2020-12`를 사용합니다. 자체/순환 메타 Schema도 검증/예산을 따릅니다. bAssertFormats는 명시적 덮어쓰기이며 meta/format-assertion은 내장됩니다. 주석 모드의 미지 형식은 허용됩니다.

```VeroScript
json::schema_options options = json::schema_options();
options.set_assert_formats(true);
json::schema schema = json::compile_schema_with_options(
	json::parse("{\"format\":\"email\"}"), options, json::object());
bool valid = schema.is_valid(json::from_string("x@example.test"));
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/schema-formats.md) · [fr-FR](../../fr-FR/Reference/schema-formats.md) · [ja-JP](../../ja-JP/Reference/schema-formats.md) · [ko-KR](schema-formats.md) · [zh-CN](../../zh-CN/Reference/schema-formats.md) · [zh-TW](../../zh-TW/Reference/schema-formats.md)
