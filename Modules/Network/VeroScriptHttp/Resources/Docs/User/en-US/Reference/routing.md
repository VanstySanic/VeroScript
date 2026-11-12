# HTTP routing

FRouter copies share registrations; immutable FRouteSnapshot matching holds no update lock and survives edits. Routes have a method, slash-leading pattern and unique uint64 ID (script nonnegative int64). Method * matches all; other methods are case-sensitive, without implicit HEAD→GET. Exact segment wins over :name over final *name, then explicit method wins for identical shapes. Duplicate IDs/names, overlapping equal-priority patterns under one method and nonfinal wildcards fail registration.

/files/*path matches /files/ but not /files. Empty/trailing/dot segments remain; split by raw slash then decode each once. a%2Fb stays one segment with parameter a/b; %252F becomes %2F. RawTarget is preserved, Path re-encodes segments without losing encoded-slash boundaries. Query preserves order/duplicates/key versus key=, and plus remains plus. No match is success with bFound=false, distinct from invalid/limited targets. FRouteLimits bounds count/segments/pattern bytes/target/query. Update/match failures preserve old table/output; Remove reports false for absent ID.

Script create_router/add/remove/snapshot, snapshot.size/match, exchange.route expose found/id/raw_target/path/parameter_count/name/value/query_count/name/value/has_equals. Router only matches; execute handlers from the existing accept/operation context.

## Examples

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](routing.md) · [fr-FR](../../fr-FR/Reference/routing.md) · [ja-JP](../../ja-JP/Reference/routing.md) · [ko-KR](../../ko-KR/Reference/routing.md) · [zh-CN](../../zh-CN/Reference/routing.md) · [zh-TW](../../zh-TW/Reference/routing.md)
