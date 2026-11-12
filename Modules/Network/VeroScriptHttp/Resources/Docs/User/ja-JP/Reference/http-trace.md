# HTTP 応答チェーンと統計

FAttemptDetails は不変、成功 response は最終 attempt と共有し、`FAttemptHistory` は failure/cancel/timeout/close でも取得済み head を保持します。context 後も Owner/data 有効、新 snapshot は quota 対象。Responses は完全な informational/redirect/final 順、partial/CONNECT は除外。Status/Url/Method 実値、Locations は重複/空保持。bRedirectFollowed は実追従、RedirectUrl は解決先、RedirectMethod は次の完全 head 確認前は空です。

redirect 既定なし。301/302 は POST→GET 可、303 は HEAD 以外 GET、307/308 は method/body 保持。stream は replay factory、全 hop 共通期限/上限、失敗は history 上の transport error です。普通 custom header は curl 転送規則、Authorization/Cookie/Host は特別規則で trace から変更しません。SendStream 早期 head は Statistics.bFinal=false の独立 snapshot で後から変わりません。

native optional<uint64>統計は script int64、不明-1、overflow 失敗、0 有効。µs の TotalTimeUs は今回 attempt、QueueTimeUs は受理後 curl 待機、NameLookup/Connect/AppConnect/PreTransfer/StartTransfer は累積 milestone、RedirectTimeUs は最終転送前合計。reuse/nonTLS で AppConnect=0 可。redirect 累積値を単一 connection 区間と解釈せず、NewConnections/FollowedRedirects は実数です。

LastTransferDownloadedBytes/UploadedBytes は最後 curl payload、header 除外、download は decode 前。ReceivedHeaderBytes は informational/trailer 込み、抑制 CONNECT 除外。AcceptedBodyBytes は redirect 込み受理 body、pause 再送二重なし。attempt DownloadedBytes は受理量、UploadedBytes は最終 payload、予約 quota は traffic ではありません。TCP 再送/TLSframing/全 buffer/物理 wire 総量を示しません。details の response_count/response/statistics、head status/url/method/location*/redirect*、snake_case stats/is_final を使い、手動 response には client 詳細がありません。

## 例

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/http-trace.md) · [fr-FR](../../fr-FR/Reference/http-trace.md) · [ja-JP](http-trace.md) · [ko-KR](../../ko-KR/Reference/http-trace.md) · [zh-CN](../../zh-CN/Reference/http-trace.md) · [zh-TW](../../zh-TW/Reference/http-trace.md)
