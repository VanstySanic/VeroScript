# multipart 전송과 파싱

`FMultipartReader::TryCreate(content_type, out, error, options, context)`는 경계를 분석하고 실패 시 `out`을 보존합니다. 네이티브 컨텍스트는 선택 사항이며, 제공하면 유한 저장소와 보유한 불변 이벤트를 전달 후에도 사용량에 포함합니다. 복사본은 데이터를 공유하며 이벤트는 컨텍스트 종료 후에도 유효하지만 리더는 새 작업을 받지 않습니다. `Feed(bytes, final, event, work)`는 소비 바이트 수와 최대 한 이벤트를 반환합니다. 남은 접미부는 같은 `final`로 다시 전달하세요. `EventReady`만 `event`를 바꿉니다. `final`은 실제 EOF를 뜻하며, 빌린 work의 취소와 예산은 호출 중에만 검사합니다.

상태 0 NeedInput/1 EventReady/2 Done/3 Failed(새 reader), 이벤트 0 PartBegin(index/head)/1 Data(비어 있지 않고≤16 KiB/context)/2 PartEnd/3 End 입니다. End 뒤에도 HTTP epilogue/EOF 를 비웁니다. head 는 이름/filename 없음과 빈 값/타입/반복 헤더 순서를 보존합니다. form-data 는 단일 명확한 Disposition/form-data/name(빈 값 가능), 다른 하위형식 name 은 선택, 중첩은 원본입니다. 헤더 대소문자 무시/접힘 공백, 중복 메타데이터/모호한 타입은 거부합니다. NUL/인코딩/순서를 유지하고 charset/percent/transfer 해독이나 filename 디스크 접근 없이 경계/패딩/빈 부분을 처리합니다.

기본 256 부분, 헤더 64 KiB/부분·1 MiB 총량, 본문 64 MiB/부분·256 MiB 총량, framing 포함 입력 272 MiB, pre/epilogue 각 64 KiB 입니다. 한도 독립, 전체 본문 집계 없음, 보유 이벤트도 Limit 요인입니다. script 옵션 8 개 max_*, reader feed/state/event/error, event kind/part_index/name/has_filename/filename/content_type/header_count/header/data 형식 검사를 사용합니다. 이벤트/접미부 먼저, HTTP End 후 final 로 Done, VM 취소는 호출 실패/형식 오류는 reader 상태입니다.

업로드 Body/일회 Upload/nativefactory 는 배타적입니다. 알려진 길이만 보내며 짧으면 Protocol, 초과 미전송, 미지는 Finish, WouldBlock 은 EOF 아닙니다. 버퍼/스트림 혼합과 반복 바이너리 보존, replay 는 I/O 상 새 factory 와 일관 길이/비차단/스크립트 없음이 필요합니다. 단일 소비이며 종결 때 미사용 소스 취소입니다. 업로드 단일/총량은 MIME framing 제외, 버퍼 선차감/스트림 수락량, metadata/buffer 는 context, pipe 별도입니다. backend 가 Content-Type/Length/Transfer-Encoding 을 생성합니다. script begin_multipart/add_field/add_file/*_stream, length=-1,256 부분/metadata+buffer16 MiB 이하이며 script I/O factory 는 없습니다. 파일 어댑터는 별도 API, filename 만으로 디스크에 접근하지 않습니다.

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/multipart.md) · [fr-FR](../../fr-FR/Reference/multipart.md) · [ja-JP](../../ja-JP/Reference/multipart.md) · [ko-KR](multipart.md) · [zh-CN](../../zh-CN/Reference/multipart.md) · [zh-TW](../../zh-TW/Reference/multipart.md)
