# HTTP ルーティング

`FRouter` のコピーは登録を共有します。不変の `FRouteSnapshot` は編集後も有効で、更新ロックを保持せずに照合します。ルートにはメソッド、`/` で始まるパターン、一意な `uint64` ID（スクリプトでは非負の `int64`）を指定します。メソッド `*` はすべてに一致し、ほかは大小文字を区別して HEAD→GET を暗黙変換しません。優先順位は完全一致のセグメント、`:name`、末尾の `*name` の順で、同じ形なら明示メソッドが優先します。ID／パラメーター名の重複、同一メソッドで同優先度の重複パターン、末尾以外のワイルドカードは拒否されます。

/files/*path は/files/に一致、/files 不可。空/末尾/点 segment 保持、rawslash で分けて一回 decode、a%2Fb は一 segment a/b、%252F は%2F。RawTarget 保持、Path は slash 境界を保ち再 encode。query 順/反復/key と key=/plus を保持。不一致は成功 bFound=false、invalid/limit は別。FRouteLimits は数/segment/patternbyte/target/query を制限、失敗時旧 table/out 保持、Remove 不存在 false。scriptcreate_router/add/remove/snapshot.size/match/exchange.route、found/id/raw_target/path/parameter*/query*/has_equals を提供し、handler は既存 acceptcontext で呼びます。

## 例

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/routing.md) · [fr-FR](../../fr-FR/Reference/routing.md) · [ja-JP](routing.md) · [ko-KR](../../ko-KR/Reference/routing.md) · [zh-CN](../../zh-CN/Reference/routing.md) · [zh-TW](../../zh-TW/Reference/routing.md)
