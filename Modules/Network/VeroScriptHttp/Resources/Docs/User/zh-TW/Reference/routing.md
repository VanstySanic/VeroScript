# HTTP 路由

`FRouter` 複本共享登記；不可變的 `FRouteSnapshot` 在修改後仍有效，比對不持有更新鎖。路由包含方法、`/` 開頭的模式及唯一 `uint64` ID（腳本為非負 `int64`）。方法 `*` 匹配全部，其餘區分大小寫，不會隱式將 HEAD 交給 GET。逐段優先序為精確值、`:name`、末尾 `*name`；形狀相同時明確方法優先。重複 ID／參數名、同方法同優先級的重疊模式及非末尾萬用字元均拒絕登記。

/files/*path匹配/files/但非/files。空/尾/點段保留，先rawslash分再一次decode：a%2Fb一段a/b，%252F為%2F。RawTarget保留、Path重新編碼保slash邊界，query保順序/重複/key與key=/+。不匹配為成功bFound=false，無效/超限另報。FRouteLimits限制數/段/patternbyte/target/query，失敗保舊表/out、Remove不存在false。腳本create_router/add/remove/snapshot.size/match/exchange.route及found/id/raw_target/path/parameter*/query*/has_equals；路由僅匹配，handler由原accept context執行。

## 範例

```cpp
Http::FRouter router;
Network::FError error;
router.TryAdd({1, "GET", "/users/:id"}, error);
const auto routes = router.Snapshot();
Http::FRouteMatch match;
if (routes.TryMatch(exchange->Request().Method, exchange->Request().Target, match, error) && match.bFound)
{
	// Select by match.Id; match.Parameters retains ordered path parameters.
}
```

```VeroScript
http::router router = http::create_router();
router.add("GET", "/users/:id", 1);
http::route_match match = exchange.route(router);
if (match.found())
{
	print(match.parameter_value(0));
}
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/routing.md) · [fr-FR](../../fr-FR/Reference/routing.md) · [ja-JP](../../ja-JP/Reference/routing.md) · [ko-KR](../../ko-KR/Reference/routing.md) · [zh-CN](../../zh-CN/Reference/routing.md) · [zh-TW](routing.md)
