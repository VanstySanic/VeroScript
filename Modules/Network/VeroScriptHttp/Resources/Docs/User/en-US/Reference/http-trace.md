# HTTP response chains and statistics

FAttemptDetails is immutable. A successful response shares its final attempt details; FAttemptHistory also exposes completed heads after transport failure/cancel/timeout/close. Snapshots retain data/code beyond context close; copies share details but new history/chain storage remains charged.

Responses records complete informational, redirect and final heads in arrival order, excluding partial heads and CONNECT proxy heads. Status/Url/Method describe the actual response; Locations preserves duplicates/empty values/order. bRedirectFollowed indicates the backend actually followed, RedirectUrl the resolved target; RedirectMethod is known only once a complete next head proves it, otherwise empty. Redirects default off. 301/302 may change POST to GET, 303 to GET except HEAD, 307/308 preserve method/body. Streams need replay factories; all hops share deadline/cumulative limits. Failed follow is a transport failure visible in history, not a fabricated success.

Custom request headers follow the pinned curl forwarding rules, including ordinary headers across destinations; Authorization/Cookie/Host use curl’s special defaults. This trace API does not control forwarding. SendStream’s early head has independent details with Statistics.bFinal=false; it never changes as later body/final details arrive.

Optional native uint64 backend statistics map to script int64, -1 unavailable; overflow fails, zero remains valid. Times are microseconds: TotalTimeUs is this attempt only, QueueTimeUs is curl waiting after module admission, NameLookup/Connect/AppConnect/PreTransfer/StartTransfer are cumulative milestones, RedirectTimeUs totals redirects before final transfer. Reused/non-TLS AppConnect may be zero. Redirected milestones cannot be treated as isolated connection stages. NewConnections/FollowedRedirects count actual events.

LastTransferDownloadedBytes/UploadedBytes are final curl payload, no headers, download before content decoding. ReceivedHeaderBytes includes all delivered head lines, informational responses/trailers, excluding suppressed CONNECT. AcceptedBodyBytes includes delivered redirect body, not duplicate paused replay. Attempt DownloadedBytes is accepted body and UploadedBytes final backend payload; prepaid upload limits are not traffic measurements. No TCP retransmission, TLS framing, full backend buffer or physical-wire total is reported. Script response/attempt.details exposes response_count/response/statistics, head status/url/method/location_count/location/redirect_followed/redirect_url/redirect_method, snake_case stats and is_final. Manually constructed responses have no client details.

## Examples

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](http-trace.md) · [fr-FR](../../fr-FR/Reference/http-trace.md) · [ja-JP](../../ja-JP/Reference/http-trace.md) · [ko-KR](../../ko-KR/Reference/http-trace.md) · [zh-CN](../../zh-CN/Reference/http-trace.md) · [zh-TW](../../zh-TW/Reference/http-trace.md)
