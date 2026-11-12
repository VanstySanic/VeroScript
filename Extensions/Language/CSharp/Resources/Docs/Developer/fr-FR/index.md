# Documentation technique VeroScript C#

Ce répertoire est la documentation développeur liée aux sources C#, à l’en-tête Interop, aux targets CMake et aux tests managés. Ce n’est ni un tutoriel utilisateur ni une copie de la documentation XML API publique.

## Propriété des modules

Le groupe comprend `VeroScriptCSharp`, surface runtime .NET 8/9 compatible NativeAOT sur `VeroScriptInterop`; `VeroScriptCSharpAot`, surface d’émetteur C# orientée NativeAOT dépendant du cœur; `VeroScriptCSharpBindGen`, qui lit les déclarations C# supportées et émet Binding Catalog/code d’enregistrement; et `VeroScriptCSharpFrontend`, modèle de déclarations sémantique partagé. L’adaptateur managé utilise l’ABI C stable, sans en-tête privé C++ ni réimplémentation du compilateur/runtime.

## Règles ABI et durée de vie

Les handles Interop sont des objets opaques de propriété. Chaque lease, buffer, package, callable, enregistrement callback et opération de session suit `VeroScriptInterop.h` et les règles miroir du wrapper. Aucun pointeur, chaîne empruntée ou contexte callback ne dépasse sa durée documentée ; dispose est déterministe et sûr sur le thread imposé. Le wrapper marshal UTF-8/UTF-16, buffer natif, status, diagnostic et exception callback sans changer leur sens, et ne transforme jamais une erreur ABI en succès.

## Frontend et générateur

Le frontend parse les déclarations sans exécuter le code utilisateur. BindGen consomme ce modèle et émet des métadonnées, pas des thunks d’invocation native. Signature, propriété, argument par défaut ou héritage non pris en charge échoue avec diagnostic source avant remplacement de la sortie. AOT est une voie de lowering distincte, dont langage accepté/contrat de sortie proviennent de son émetteur et de ses tests.

## Règles de compatibilité

Le contrat API contrôlé et les tests de vérification de package gouvernent types publics, layouts de marshaling, valeurs enum, conventions callback, métadonnées de package et frameworks. Versions, identifiants ABI, clés JSON, options CLI et identifiants C# sont des tokens techniques non traduits.

## Détails du runtime managé

`VeroScriptRuntime` est la porte de compatibilité processus : `EnsureCompatible()` précède toute session et version de package/ABI restent distinctes. `VeroScriptSession` possède handle natif et racines managées; `VeroScriptValue` owning ou empruntée est limitée à son borrow scope. `VeroScriptCallable` garde son thread créateur et diffère dispose durant invocation sans autoriser l’appel inter-thread. `VeroScriptPackage` possède handle, métadonnées, bytes signés et état de module chargeable. L’énumération de conteneur capture l’exception callback puis la rapporte après retour natif.

## Détails binding et code généré

Les bindings hôte préservent documentation, annotation, paramètres de type, métadonnées de paramètre, type sous-jacent enum, flags const/mutable, opérateurs, protocoles clone/égalité/hash, factories de sous-classe et callbacks de type hôte générique. Le modèle d’enregistrement généré est métadonnées plus callbacks, non une table de pointeurs de fonctions natives.

## Pages détaillées

- [Runtime C# et adaptateur Interop](runtime-and-interop.md)
- [Liaisons, paquets et compatibilité C#](bindings-and-packages.md)
- [Outillage et contrat de paquet C#](toolchain-and-packaging.md)

## Contrat d’exécution managé

`VeroScriptConfiguration` porte overlays/import/cache, profondeur d’appel, `MaxInstructions`, `TimeoutMilliseconds`, contrôle d’exécution, politique/capacités autorisées et `VeroScriptExecutionBackend`. Zéro instruction/timeout signifie illimité. `RequestCancellation` est la seule opération session conçue pour un thread non propriétaire et la session reste vivante jusqu’au retour. `VeroScriptJitPolicy`, `GetLastJitStats`, `SetJitCacheLimits` et `GetJitCacheStats` exposent politique adaptative, snapshot et statistiques du cache borné quand `SessionJitCache` est annoncé. Une entrée JIT non prise en charge revient à la VM. Le débogueur et l’initialisation de l’état conservé utilisent la VM. Les appels admissibles peuvent réutiliser le code de la même génération.

## Source de vérité

Les fichiers de `VeroScriptCSharp/`, projets frontend/BindGen, en-tête Interop public, CMake et tests managés font foi. Mettre ce document à jour avec toute évolution ABI, propriété, thread, code généré ou contrat de package.

`VeroScriptCSharp`: [Android et iOS / MAUI](../../User/fr-FR/mobile.md).

La compilation mobile utilise le même projet et paquet : `VeroScriptIncludeMobile=true` inclut les cibles .NET 8/9 et .NET 10 Android/iOS avec le SDK natif vérifié.


[CSharp sur Android et iOS](mobile.md)

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
