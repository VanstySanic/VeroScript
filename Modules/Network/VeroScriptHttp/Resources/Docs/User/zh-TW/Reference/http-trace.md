# HTTP 回應鏈與統計

FAttemptDetails 不變，成功回應與最後嘗試共用；`FAttemptHistory` 在失敗/取消/逾時/關閉後仍保完整頭。資料/程式碼晚於 context 存活，新快照仍計額度。Responses 依序保 informational/redirect/final，不含未完整/CONNECT 頭。Status/Url/Method 實值，Locations 保重複/空/順序；bRedirectFollowed 是實際追隨，RedirectUrl 為解析目標，下一完整頭確認前 RedirectMethod 為空。

預設不追隨，301/302 可 POST→GET，303 除 HEAD 為 GET，307/308 保方法/正文；流須重放工廠，共期限/總限額，追隨失敗仍為傳輸錯。自訂頭依 curl 轉送規則，Authorization/Cookie/Host 為特別規則，trace 不控制。SendStream 初始頭為獨立 Statistics.bFinal=false 快照，後續不變。

原生 optional<uint64> 統計對腳本 int64，不可用 -1、溢位失敗、0 合法。微秒 TotalTimeUs 是本次，QueueTimeUs 是接納後 curl 等待，NameLookup/Connect/AppConnect/PreTransfer/StartTransfer 是累積里程碑，RedirectTimeUs 為最後轉送前合計。重用/非 TLS AppConnect 可零，不把 redirect 累積當單連線獨立階段；NewConnections/FollowedRedirects 為實數。

LastTransferDownloadedBytes/UploadedBytes 為最後 curl payload、不含頭且下載在解碼前。ReceivedHeaderBytes 含 informational/trailers、不含抑制 CONNECT。AcceptedBodyBytes 含 redirect 交付、不重計 pause replay。嘗試 DownloadedBytes 是接收正文、UploadedBytes 是最終 payload，預扣非流量；不含 TCP 重傳/TLS framing/全部內部快取/物理總線量。details 提供 response_count/response/statistics，head 的 status/url/method/location*/redirect*，stats snake_case/is_final；手動回應無 client details。

## 範例

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/http-trace.md) · [fr-FR](../../fr-FR/Reference/http-trace.md) · [ja-JP](../../ja-JP/Reference/http-trace.md) · [ko-KR](../../ko-KR/Reference/http-trace.md) · [zh-CN](../../zh-CN/Reference/http-trace.md) · [zh-TW](http-trace.md)
