# Routage HTTP

Les copies de `FRouter` partagent les inscriptions. Le `FRouteSnapshot` immuable survit aux modifications et recherche sans verrou de mise à jour. Une route possède une méthode, un motif commençant par `/` et un ID `uint64` unique (`int64` positif ou nul en script). La méthode `*` accepte tout ; les autres sont sensibles à la casse, sans conversion implicite HEAD→GET. Un segment exact prime sur `:name`, puis sur le dernier `*name` ; à forme identique, la méthode explicite prime. IDs ou paramètres dupliqués, chevauchements de même priorité pour une méthode et jokers non finaux sont refusés.

/files/*path exige /files/, pas /files. Segments vides/finaux/points conservés ; division avant décodage unique : a%2Fb reste un segment a/b, %252F devient %2F. RawTarget conservé, Path réencode sans perdre les frontières. Query conserve ordre/doublons/key vs key= et + littéral. Aucun match : succès/bFound=false, distinct d’erreur/limite. FRouteLimits borne table/segments/octet/cible/query ; échec préserve table/sortie, Remove absent=false. Script create_router/add/remove/snapshot.size/match/exchange.route et found/id/raw_target/path/parameter*/query*/has_equals ; les handlers s’exécutent dans le contexte accept existant.

## Exemples

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

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/routing.md) · [fr-FR](routing.md) · [ja-JP](../../ja-JP/Reference/routing.md) · [ko-KR](../../ko-KR/Reference/routing.md) · [zh-CN](../../zh-CN/Reference/routing.md) · [zh-TW](../../zh-TW/Reference/routing.md)
