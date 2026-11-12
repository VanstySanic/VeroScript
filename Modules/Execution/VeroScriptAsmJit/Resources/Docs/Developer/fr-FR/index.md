# Maintenance du backend

Voir [Services JIT du cœur et concurrence](../../../../../../../VeroScript/Resources/Docs/Developer/fr-FR/jit-and-concurrency.md#native-abi-version).

Core possède le contrat d’exécution, les contrats JIT publics, les services runtime, la validation, l’ordonnancement et la comptabilité des générations. IR concret, abaissement, émission native, exécution Linear portable et stockage exécutable appartiennent à `Modules/Execution/VeroScriptAsmJit`, pas à Core. `Private/JitSupport` implémente le cache générique et le chargeur de modules. L’empreinte des en-têtes publics et la négociation compilateur/STL/runtime/plateforme déterminent la compatibilité binaire, pas la seule version du produit. L’interpréteur bytecode appartient au module indépendant `VeroScriptBytecodeVm`.

## Compilation et ABI

`VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` construit Core sans backend concret. Avec `ON`, `VEROSCRIPT_ENABLE_ASMJIT=ON` active l’émission native et `OFF` conserve Linear portable. Les hôtes source lient `VeroScriptAsmJit` et injectent `CreateAsmJitBackendFactory()`. `VeroScriptAsmJitBackend` produit un module partagé sans copie de Core. Les émetteurs importés doivent fournir le PIC compatible requis ; leur propriétaire déploie les dépendances partagées. Le chargement dynamique Windows exige `/MD` ou `/MDd`.

```cmake
target_link_libraries(MyHost PRIVATE VeroScriptAsmJit)
```

## Contrats runtime

Distinguez la compilation `Ready / Unsupported / Deferred / Failed` et l’invocation `NotEntered / Returned / ControlTransfer / Failed`. Seul `NotEntered` autorise le rejeu VM. Core valide propriété, services et budgets avant publication. Les exécutables conservent le code et le module ; contextes, frames et valeurs sont empruntés uniquement pendant l’appel. Réacquérez les vues après un appel imbriqué. Relisez les indicateurs vivants d’erreur/contrôle/callback à chaque décision de chemin rapide.

L’éligibilité du cache Session, l’exécution gouvernée, les limites et la comptabilité sont maintenues dans [C++ Session](../../../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/fr-FR/session.md#session-jit-cache).

## Contrôles de publication

La négociation POD `veroscript_jit_query` vérifie API, empreinte des en-têtes, compilateur/STL, mode C++, runtime et plateforme avant de créer la fabrique C++. Conservez la bibliothèque pendant la vie des fabriques, workers et artefacts. Testez Core seul, contrats injectés, bibliothèques incompatibles, sélection explicite, fallback sans rejeu, récupération des ressources et invalidation du cache Session. Utilisez `package-veroscript-jit.py` ; les versions indépendantes ne dispensent pas de l’ABI correspondante. iOS appareil et simulateur interdisent chargement dynamique et émission native ; les backends portables statiques exigent une validation distincte.

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/fr-FR/jit-and-concurrency.md)

## Quotas de rétention en octets

Zéro `MaxNativeBytes` ou `MaxRetainedPayloadBytes` interdit la rétention correspondante ; ces limites en octets diffèrent des budgets de stratégie zéro illimités.

Les archives officielles du backend utilisent `Release/Modules/Execution/Jit/<target>/<version>/archives/`. Les Modules n’ont pas de nouvel installateur autonome. Le payload fonctionnel conserve dépendances et notices légales, sans documents, exemples, tests, benchmarks ni symboles. Les originaux restent dans Resources/Docs et sont publiés via la documentation Resources intégrée.

## Maintenance de la dépendance intégrée

Ce module possède AsmJit. Gardez les sources sous `ThirdParty/asmjit`, conservez la [licence originale](../../../../ThirdParty/asmjit/LICENSE.md) et indiquez la révision amont dans [VERSION.txt](../../../../ThirdParty/asmjit/VERSION.txt). Ne déplacez pas ces sources dans Resources/Docs ni dans Core. `VEROSCRIPT_ENABLE_ASMJIT=OFF` sélectionne l’implémentation portable d’un backend construit ; `VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` omet ce module backend.

Pour une mise à jour, remplacez la révision intégrée, conservez ses notices et celles de la base de génération de code, actualisez VERSION.txt et la reproduction exacte de la licence dans [la notice du module](../../../Licenses/NOTICES.md), puis reconfigurez et exécutez les tests JIT et de paquet. Préservez la compatibilité compilateur/STL/runtime et validez les configurations native et portable. Le composant d’installation exporte LICENSE.md et VERSION.txt vers `licenses/asmjit/` ; comparez leurs octets aux sources propriétaires. L’index commun se trouve dans Resources/Licenses/README.md ; chaque module consommateur possède les notices de ses dépendances.

## Blocs scalaires contrôlés et couverture partielle des boucles

Les courtes boucles entières contrôlées peuvent utiliser un CFG natif et des blocs de valeurs primitives Core. Chaque instruction vérifie et comptabilise toujours limites, annulation et délai, sans prépaiement par lots. EnableScalarBlocks refuse les vérifications hôtes personnalisées et les observateurs de mutations avant l’entrée. Une représentation incompatible reprend les services ordinaires à l’instruction courante, sans double contrôle/comptage ni répétition de fonction. Les boucles complexes gardent le choix VM selon le coût.

Le += de chaînes de types compatibles participe à la validation CFG normale. Une boucle textuelle non optimisée ne masque pas une autre boucle native de la même fonction. Mixed contrôlé reste explicitement activé.

Les courtes expressions entières sur un receveur, sans branchement, peuvent préférer le plan borné du Core au pont natif. Ce choix de coût intervient après la compilation et la validation ordinaires du backend. Les échecs, observateurs de mutation et contrôles d’exécution conservent leur chemin VM ; les corps plus grands, appels et boucles gardent leurs plans indépendants.

`bPreferVmForNestedExecution` concerne uniquement un appelant interprété sans contrôle d’exécution ; les appels directs de l’hôte et les appelants natifs conservent leur entrée native.

Voir [Services JIT du cœur et concurrence — Concurrence et validation](../../../../../../../VeroScript/Resources/Docs/Developer/fr-FR/jit-and-concurrency.md#construction-dispatch).

<!-- docs-navigation -->

---

[Documentation développeur](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
