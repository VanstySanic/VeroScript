# HTTP 路由

`VeroScriptHttpRouter.h` 提供独立的 `FRouter`、不可变 `FRouteSnapshot` 与 `FRouteMatch`。
请求匹配读取快照，不持有路由更新锁。复制 router 共享注册状态；已经取得的快照不会被后续增删改变。

## 路径与优先级

- 路由指定方法、以 `/` 开头的模式与独立的 `uint64` ID；脚本 ID 使用非负 `int64`。
- 方法 `*` 匹配任意方法；其他方法大小写敏感，不隐式把 HEAD 当作 GET。
- 普通段精确匹配；`:name` 捕获一段，最后一段 `*name` 捕获剩余段。
- 逐段优先级为精确段、命名参数、尾部通配符；同一路径形状优先显式方法。
- 重复 ID、同方法下具有相同优先级且重叠的模式、重复参数名及非末尾通配符在注册时失败。

尾部通配符要求它前面的分隔斜杠存在：`/files/*path` 匹配 `/files/`，不匹配 `/files`。
空段、尾斜杠和点段都保留，不执行合并或点段折叠。路径先按原始斜杠分段，再分别解码一次；
`a%2Fb` 是一段，其参数值为 `a/b`；`%252F` 的参数值为 `%2F`。

`RawTarget` 保留请求目标；`Path` 是逐段重新百分号编码的规范表示，保留编码斜杠边界。
Query 保留顺序、重复项以及 `key` 与 `key=` 的区别；普通查询中的 `+` 保持加号。
匹配没有路由时仍成功返回，`bFound=false`；非法目标或超限与未匹配分开报告。

## C++

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

`FRouteLimits` 固定路由数、段数、模式总字节、目标长度及查询参数数目。更新失败保留原表；
`TryMatch` 失败保留原输出。TryRemove 的 Removed=false 表示该 ID 不存在。

## 脚本

```cpp
http::router router = http::create_router();
router.add("GET", "/users/:id", 1);
http::route_match match = exchange.route(router);
if (match.found())
{
	print(match.parameter_value(0));
}
```

`router.snapshot().match(method, target)` 可独立匹配；router 提供 remove，snapshot 提供 size。
match 提供 found、id、raw_target、path、parameter_count/name/value 及 query_count/name/value/has_equals。
注册和匹配失败使用普通脚本错误；原生数据 API 提供结构化 FError。
路由表只负责匹配，处理器仍由已有 accept/operation 所属执行上下文调用。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/routing.md) · [fr-FR](../../fr-FR/Reference/routing.md) · [ja-JP](../../ja-JP/Reference/routing.md) · [ko-KR](../../ko-KR/Reference/routing.md) · [zh-CN](routing.md) · [zh-TW](../../zh-TW/Reference/routing.md)
