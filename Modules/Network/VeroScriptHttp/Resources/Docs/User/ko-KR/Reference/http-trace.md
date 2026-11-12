# HTTP 응답 체인과 통계

FAttemptDetails 는 불변이며 성공 응답은 마지막 시도와 공유하고 `FAttemptHistory`는 실패/취소/시간초과/닫기 뒤의 완성 헤더도 유지합니다. 데이터/코드는 context 보다 오래 살며 새 스냅샷은 할당량에 포함합니다. Responses 는 완성 informational/redirect/final 순서, partial/CONNECT 제외입니다. Status/Url/Method 는 실제 값, Locations 는 반복/빈 값/순서 보존입니다. bRedirectFollowed 는 실제 추적, RedirectUrl 은 해석된 목표, RedirectMethod 는 다음 완성 헤더 확인 전에는 비어 있습니다.

redirect 기본 꺼짐, 301/302 POST→GET 가능, 303 은 HEAD 외 GET, 307/308 은 메서드/본문 유지입니다. 스트림에는 replay factory 와 공통 기한/상한이 필요하며 추적 실패는 history 의 전송 오류입니다. 보통 사용자 헤더는 curl 의 목적지 전달 규칙, Authorization/Cookie/Host 는 특수 기본 규칙이며 trace 가 제어하지 않습니다. SendStream 초기 head 는 Statistics.bFinal=false 인 독립 불변 스냅샷입니다.

native optional<uint64> 통계는 script int64, 미지 -1, 초과 오류, 0 유효입니다. µs TotalTimeUs 는 이번 시도, QueueTimeUs 는 수락 후 curl 대기, NameLookup/Connect/AppConnect/PreTransfer/StartTransfer 는 누적 지표, RedirectTimeUs 는 마지막 전송 전 합계입니다. 재사용/nonTLS 의 AppConnect=0 가능, redirect 지표를 독립 연결 단계로 해석하지 않습니다. NewConnections/FollowedRedirects 는 실제 수입니다.

LastTransferDownloadedBytes/UploadedBytes 는 마지막 curl payload, 헤더 제외, 다운로드는 해독 전입니다. ReceivedHeaderBytes 는 informational/trailer 포함, 억제 CONNECT 제외입니다. AcceptedBodyBytes 는 redirect 수락 본문과 pause 재전달 중복 제외입니다. 시도 DownloadedBytes 는 수락량, UploadedBytes 는 마지막 payload, 선예약은 통계가 아닙니다. TCP 재전송/TLS framing/전체 버퍼/실제 회선 총량은 제공하지 않습니다. details 의 response_count/response/statistics, head status/url/method/location*/redirect*, snake_case stats/is_final 을 쓰며 수동 응답은 client details 가 없습니다.

## 예제

```cpp
request.follow_redirects(true);
request.set_redirect_limit(4);
http::transfer_details details = response.details();
http::response_head first = details.response(0);
http::transfer_statistics stats = details.statistics();
print(first.url());
print(to_string(stats.total_time_us()));
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/http-trace.md) · [fr-FR](../../fr-FR/Reference/http-trace.md) · [ja-JP](../../ja-JP/Reference/http-trace.md) · [ko-KR](http-trace.md) · [zh-CN](../../zh-CN/Reference/http-trace.md) · [zh-TW](../../zh-TW/Reference/http-trace.md)
