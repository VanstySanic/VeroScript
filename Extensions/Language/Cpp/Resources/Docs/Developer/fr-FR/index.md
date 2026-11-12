# Documentation technique VeroScript C++

Cette documentation développeur est liée aux sources, en-têtes publics, targets CMake, tests et artefacts générés C++. Elle ne traduit jamais noms API, options CLI, clés JSON, diagnostics ou exemples.

## Propriété des modules

`VeroScriptCpp` est la surface embedding/session ; `VeroScriptCppAot` émet un bundle C++ à unité de traduction unique ; `VeroScriptCppFrontend` parse le sous-ensemble C++17 dans un AST stable ; `VeroScriptCppBindGen` produit Binding Catalog ; `VeroScriptCppTranspiler` lower le sous-ensemble préservant la sémantique et valide par le compilateur réel. Le cœur est `VeroScript/` : les modules C++ peuvent en dépendre, pas l’inverse. Frontend est partagé par BindGen/Transpiler mais leur politique sémantique après parse reste distincte.

## Frontière sémantique

Les outils acceptent délibérément un sous-ensemble borné. Propriété, linkage, pointeur, modèle, préprocesseur et flot non supportés produisent un diagnostic ; une approximation silencieuse est une régression ABI/correction source. Le frontend préserve noms qualifiés, alias, attributs, plages, constructeurs/destructeurs, friends, templates à paramètres de type et définitions hors classe représentables. BindGen peut conserver des métadonnées que Transpiler doit refuser sans équivalent source VeroScript.

## Sortie et transactions

Les outils multi-input parse/fusionnent l’ensemble avant de remplacer une sortie. Catalogues, références API, maps source et manifests sont atomiques comme résultat logique unique ; basenames dupliqués sont désambiguïsés de façon déterministe à partir du chemin. Parse/fusion/validation/compilation finale échoué ne laisse pas d’arbre partiel. Les références API générées sont documentaires, tandis que Binding Catalog JSON est l’entrée machine compilateur/IDE.

## Concurrence et contrat hôte

Les hôtes embedding/AOT gardent l’état callback vivant pour tout le run et synchronisent l’état atteignable par worker. Les globales et statiques sont isolées par worker natif, `channel<T>` est le partage synchronisé explicite. Callbacks natifs, intercepteurs, observateurs mutation et symboles AOT hôte accessibles peuvent être concurrents.

## Contrat engine et session

`VeroScriptEngine` fournit `CheckSource`, `CheckFile`, `RunSource`, `RunFile`; les opérations fichier peuvent utiliser cache bytecode et prennent labels/flux/politiques explicitement. `VeroScriptSession` est stateful, refuse les mutations pendant run, et `LoadFile` seul établit le chemin de reload. `Reload`, `ReloadCompiledModule`, `ReloadPackage` font stage/validate/commit ; type de paquet et entrée doivent correspondre. `Run(ERunMode::Eval)` initialise le module sans appeler `main` ; les globales persistent entre appels uniquement avec `SetHotReloadEnabled(true)`. `Clear()` retire état, statistiques JIT et code réutilisable. `SetJitPolicy`, `SetJitCacheLimits`, `GetJitCacheStats` contrôlent réutilisation des appels admissibles entre VM neuves.

## Sorties AOT et outils C++ publics

L’API AOT retourne une chaîne `.cpp` ou `FCppAotSourceBundle` avec fichiers relatifs et manifest. BindGen retourne catalogue fusionné, références optionnelles et manifests ; Transpiler retourne source/maps et en mode projet un record par input/manifeste. Le writer partagé est atomique. L’AST frontend conserve plages, attributs, alias, templates, linkage C, qualificateurs, corps et définitions hors classe. `FTypeMapOptions` contrôle pointeurs hôte bruts, références rvalue, types hôte/enum/paramètre/alias connus.

## Pages détaillées

- [Architecture du groupe C++](architecture.md)
- [Embedding et session C++](session.md)
- [Frontend C++, BindGen et Transpiler](frontend-and-tools.md)
- [Émetteur AOT C++](aot.md)
- [Contrat de build et tests C++](build-and-tests.md)

## Gouvernance et observabilité de session

`VeroScriptSession` expose `SetExecutionLimits`, `SetExecutionControl`, `RequestCancel`, `SetCapabilities`, `SetJitPolicy`, `GetLastJitStats` et le backend demandé `EExecutionBackend`. Zéro est illimité, annulation coopérative et capacité imposée par VM.  La surface inclut appels instance, construction, champs/globales, callable, reflection, sérialisation externe et `CallMethodBypassingInterceptor`, lequel saute uniquement l’entrée sélectionnée.

## Source de vérité

Les en-têtes `Public/`, implémentations `Private*`, CMake du module et tests ciblés font foi. Toute évolution de contrat public, format de sortie, sous-ensemble, propriété ou garantie de concurrence met à jour cette page ; identifiants, chemins, targets, diagnostics et versions restent ceux des sources.

## Réutilisation JIT Session

L’éligibilité du cache Session, l’exécution gouvernée, les limites et la comptabilité sont maintenues dans [C++ Session](session.md#session-jit-cache).

## Quotas de rétention en octets

Zéro `MaxNativeBytes` ou `MaxRetainedPayloadBytes` interdit la rétention correspondante ; ces limites en octets diffèrent des budgets de stratégie zéro illimités.

## Annulation et livraison à l’hôte

Voir [Embedding et session C++ — Annulation et livraison à l’hôte](session.md#session-control-and-streams).

Les Sessions partageant le même jeton `FExecutionControl` partagent son état
d’annulation. Attendez la fin des appels d’annulation précédents avant `Reset()`
et réutilisation, pour éviter qu’une ancienne requête annule l’exécution suivante
qui réutilise ce jeton.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
