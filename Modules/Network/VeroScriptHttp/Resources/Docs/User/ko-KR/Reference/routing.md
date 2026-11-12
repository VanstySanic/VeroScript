# HTTP 라우팅

`FRouter` 복사본은 등록을 공유합니다. 불변 `FRouteSnapshot`은 수정 후에도 유효하며 업데이트 잠금 없이 매칭합니다. 경로에는 메서드, `/`로 시작하는 패턴, 고유 `uint64` ID(스크립트에서는 음이 아닌 `int64`)가 필요합니다. `*` 메서드는 모두 허용하고 나머지는 대소문자를 구분하며 HEAD→GET을 암시적으로 변환하지 않습니다. 정확한 구간, `:name`, 마지막 `*name` 순으로 우선하며 같은 모양에서는 명시 메서드가 우선합니다. ID/매개변수 이름 중복, 같은 메서드의 동순위 패턴 중첩, 마지막이 아닌 와일드카드는 거부합니다.

/files/*path 는 /files/에만 해당하고 /files 에는 안 됩니다. 빈/끝/점 segment 를 유지하며 raw slash 분리 뒤 한 번 decode 합니다. a%2Fb 는 한 조각 a/b, %252F 는 %2F 입니다. RawTarget 유지, Path 재인코딩으로 경계를 유지합니다. query 순서/중복/key 와 key=/+를 보존하고 미일치는 성공 bFound=false, 오류/한도는 별도입니다. FRouteLimits 는 수/segment/pattern 바이트/target/query 를 제한하고 실패는 이전 table/out, Remove 없는 ID=false 입니다. script create_router/add/remove/snapshot.size/match/exchange.route 와 found/id/raw_target/path/parameter*/query*/has_equals 를 사용하며 handler 는 기존 accept context 에서 호출합니다.

## 예제

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/routing.md) · [fr-FR](../../fr-FR/Reference/routing.md) · [ja-JP](../../ja-JP/Reference/routing.md) · [ko-KR](routing.md) · [zh-CN](../../zh-CN/Reference/routing.md) · [zh-TW](../../zh-TW/Reference/routing.md)
