# Adaptateurs JSON

VeroScriptHttp COMPONENTS Json et VeroScript::HttpJson avec VeroScriptHttpJson.h sont facultatifs, sans rendre JSON obligatoire ni ajouter des méthodes script. TrySetJsonBody encode avant remplacement atomique corps/Content-Type ; retire upload/factory/multipart/framing au succès, requête conservée sinon. TryParseJsonBody exige un corps tamponné complet, application/json ou +json par défaut, casse/paramètres admis, sans décider par statut HTTP. FJsonBodyError.Http indique contrainte média/corps, .Json erreur JSON. Échec conserve sortie/statut/têtes/corps ; tête stream précoce insuffisante. TryParseJsonEvent lit data SSE seulement, garde type/ID. Précision/Unicode/doublons/limites/travail JSON s’appliquent.

## Exemples

```cmake
find_package(VeroScriptHttp 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::HttpJson)
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
