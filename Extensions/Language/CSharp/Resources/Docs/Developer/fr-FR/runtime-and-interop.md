# Runtime C# et adaptateur Interop

## Statistique de boucle typée

`NativeTypedLoopExecutions` compte les invocations de noyaux natifs de boucles typées, y compris une invocation qui se termine après avoir signalé une erreur runtime visible dans le source. C’est un compteur d’observabilité d’exécution, pas un compteur de succès.

`VeroScriptCSharp` est un wrapper managé de `VeroScriptInterop`. Le wrapper possède conversions ergonomiques, leases, racines callback et exceptions managées ; l’ABI C possède layout natif, valeurs de statut et règles de durée de vie.

## Porte de compatibilité

`VeroScriptRuntime` expose version ABI, version runtime sémantique, plateforme hôte, bits de capacité, langue courante, JSON des langues disponibles et opérations load/merge de catalogue. `EnsureCompatible()` est la porte d’entrée managée. Version ABI et version sémantique produit sont des contrats distincts et ne sont jamais combinés dans une version affichée.

`EnsureCompatible()` compare la valeur ABI complète à `SupportedAbiVersion`, actuellement `0x00000101` (0.1.1). Toute différence provoque `NotSupportedException` ; une version majeure identique ou une ABI numériquement supérieure ne suffit pas. Lors d’un changement d’ABI, synchronisez `NativeMethods.AbiVersion`, le header natif, le contrat de l’API publique et la documentation du paquet.

`LoadCatalogDirectory` remplace l’arbre runtime après validation de sa baseline `en-US`. `MergeCatalogDirectory` ajoute un arbre validé au catalogue courant et peut autoriser explicitement l’override des ID existants. L’hôte choisit délibérément remplacement ou augmentation ; il ne simule pas une fusion en copiant du JSON ou en codant une liste de cultures.

## Session et handles

`VeroScriptSession` possède handle de session natif et racines callback/objet managées. Ses opérations suivent les règles idle/running et creator-thread sous-jacentes. `VeroScriptValue` peut posséder une valeur native ou représenter une vue empruntée dans un `BorrowScope`; une valeur empruntée ne sort pas de ce scope. `VeroScriptPackage` possède un handle package, expose métadonnées et bytes signés canoniques, et suit la disponibilité de son module compilé pour chargement.

`VeroScriptCallable` enregistre un thread propriétaire lorsque demandé. Dispose pendant une invocation active est différé jusqu’à la libération du handle natif ; cela protège la durée de vie sans rendre légal l’appel inter-thread.

## Marshaling

L’adaptateur marshal explicitement entiers fixes, flottants, booléens, chaînes UTF-8/large, enums, objets hôte, buffers, arrays, maps, sets, queues, stacks, deques et channels. Les helpers UTF-8/UTF-16 et leases de buffer natif préservent la propriété et copient une vue empruntée avant libération du propriétaire. Les exceptions managées sont capturées aux frontières callback natives et converties vers le canal d’erreur ABI ; elles ne traversent jamais un frame callback C unmanaged.

## Configuration, annulation et JIT

`VeroScriptConfiguration` est appliquée avant load/run. Ses champs d’exécution sont `MaxCallDepth`, `MaxInstructions`, `TimeoutMilliseconds`, `EnableExecutionControl`, `CapabilityPolicyEnabled`, `AllowedScriptCapabilities` et `ExecutionBackend`, en plus des overlays et politiques import. Zéro instruction/timeout laisse cette limite illimitée. Lorsqu’une politique de capacité est active, les flags autorisés régissent effets fichier, environnement, console et tâche, et la VM impose une restriction avant qu’une entrée JIT ne puisse la contourner.

`RequestCancellation` marque l’objet de contrôle natif et peut être appelé depuis un thread non propriétaire. Il est coopératif et n’arrête pas un callback déjà exécuté en code natif ou managé. `SetJitPolicy` applique les réglages adaptatifs et invalide l’état compilé réutilisable ; `GetLastJitStats` donne le delta de la dernière exécution. Les `CallFunction` ordinaires admissibles réutilisent par défaut le code entre VM neuves, sans persister globales/objets de script ; `VeroScriptJitPolicy.ReuseSessionCode` à false désactive cette option. La réutilisation exige un backend effectif autre que la VM et un catalogue natif doté d’une révision. Les changements de liaisons, d’observateurs ou de génération invalident le cache. Sous gouvernance, un contexte dont les exigences correspondent reste réutilisable. Un budget de tentatives ou d’entrées nul signifie un nombre illimité ; les plafonds en octets du code natif et de la charge retenue restent applicables.

Sous gouvernance, `Mixed` utilise la VM par défaut. Activez les chemins JIT contrôlés admissibles avec `VeroScriptJitPolicy.EnableControlledMixed` après avoir mesuré la charge cible. Le choix explicite de `Jit` est indépendant de ce réglage. Les entrées non prises en charge ou préférées en VM par le modèle de coût conservent un repli VM respectant la gouvernance. `ControlledExecution` indique le support du backend ; `ControlledNativeExecutions` et `AotExecutions` comptent les invocations réelles des entrées natives contrôlées et AOT.

`SetJitCacheLimits` et `GetJitCacheLimits` règlent les plafonds de code natif et charge complète retenue, 8 MiB et 32 MiB par défaut ; zéro n’autorise aucune rétention de cette ressource. `GetJitCacheStats` retourne tentatives cumulées, octets retenus stabilisés et latch de limite pour le contexte courant, non un compteur live ou pic de processus. Ces méthodes optionnelles exigent `VeroScriptCapabilities.SessionJitCache` et lèvent `NotSupportedException` si un runtime compatible déployé séparément ne l’offre pas. L’initialisation de l’état conservé utilise la VM. Les appels admissibles de la même génération peuvent réutiliser le code compilé. `GetReloadStatus()` lit l’opération et les générations sur le thread créateur si `VeroScriptCapabilities.ReloadStatus` est disponible.

## Opérations de session

La session gère `LoadSource`, `LoadFile`, `LoadPackage`, `Reload`, `ReloadPackage`, `BuildPackageFile`, `Run`, diagnostics, input, vérifications de fonction/méthode, `CallFunction`, `CallMethod`, `CallMethodBypassingInterceptor`, `Construct`, accès champ/global et `ApplyClassDefaults`. La forme bypass ignore seulement l’intercepteur d’entrée ; les appels imbriqués du corps restent interceptables.

Les intégrations hôte comprennent enregistrement de fonction, annotation, type/membre/protocole hôte, alias de type, type hôte générique, intercepteur d’instance, handler mutation et serializer externe. Reflection et binding JSON sont des sorties d’inspection. `VeroScriptValue` fournit aussi les opérations sequence/map/set/channel et `VeroScriptCallable` retient/invoque une fonction selon ses règles owner-thread/weak-`this`.

## Diagnostics et durée de vie managée

`GetDiagnostics`, `GetDiagnosticArguments` et `FormatDiagnostic` préservent ID structuré, label de fichier, coordonnées source et arguments. `VeroScriptRunResult` rapporte succès d’exécution, état/code de sortie et nombre de diagnostics : un message imprimé ne remplace pas ce résultat. Les valeurs empruntées expirent avec `BorrowScope`, les exceptions callback sont relancées après énumération native et les buffers sont copiés avant libération de lease.

## Mapping des statistiques JIT

L’instantané `VeroScriptJitStats` distingue fast paths scalaires, kinds d’entrée trampoline et appels natifs imbriqués directs. `DirectNativeCalls` n’est pas un nombre de compilations et ne se combine pas avec `ScalarNativeFastPathCalls`. Ces valeurs décrivent le dernier snapshot, ne changent ni backend demandé ni contrat ABI/version. Les entrées non prises en charge ou protégées continuent à revenir à la VM. `CostModelVmFallbacks` est le sous-ensemble de `Fallbacks` choisi lorsqu’un chemin compilé valide est prédit plus coûteux que la VM. `NativeTypedLoopExecutions` compte les invocations de noyaux, y compris celles qui se terminent après une erreur runtime visible dans le source ; ce n’est pas un compteur de succès. Vérifiez respectivement `VeroScriptJitBackendCapabilities.NativeCode` et `VeroScriptJitBackendCapabilities.NativeTypedLoops` ; aucune de ces capacités ne peut être déduite de `HostPlatform`. `VeroScriptJitCacheStats` a une autre durée de vie : il décrit le contexte réutilisable cumulé/stabilisé et est réinitialisé quand policy, limites, backend, module ou état hôte observable pertinent invalide ce contexte.

Voir [Sessions C# et runtime — Sélection JIT explicite](../../User/fr-FR/session.md#jit-backend-capabilities).

Voir [Sessions C# et runtime — Sélection JIT explicite](../../User/fr-FR/session.md#diagnostic-translation).

## Libération des callables conservés

Voir [Sessions C# et runtime — Libération des callables conservés](../../User/fr-FR/session.md#callable-disposal).

Les appels natifs conservent les propriétaires managés des valeurs et paquets jusqu’à la fin de l’exécution et de la copie des vues natives. Les opérations bloquantes des canaux et les visiteurs de maps/sets conservent leur récepteur ; les listes d’arguments ou d’éléments conservent chaque valeur produite par leur indexeur jusqu’à la fin de son utilisation native. Cela empêche une finalisation GC prématurée ; les règles de libération explicite et de portée des callbacks restent applicables. Les tests forcent le GC pendant la réception native, les visiteurs et le transfert de listes à la demande, avec .NET 8/9 optimisé et les consommateurs NativeAOT du paquet.

<a id="managed-marshalling-maintenance"></a>

Le transfert des arguments et éléments natifs conserve un instantané des objets de valeur ayant fourni les handles. Si un indexeur de liste ou un callback managé remplace une case du tableau d’entrée, l’objet original reste vivant jusqu’à la fin de son utilisation native et la réécriture `ref`/`out` met à jour cet objet original. Les tests de régression forcent le GC après remplacement lors de la construction de maps, des appels directs et de l’invocation de callables conservés.

[Android et iOS / MAUI](../../User/fr-FR/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

La compilation mobile utilise le même projet et paquet : `VeroScriptIncludeMobile=true` inclut les cibles .NET 8/9 et .NET 10 Android/iOS avec le SDK natif vérifié.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/runtime-and-interop.md) · [fr-FR](runtime-and-interop.md) · [ja-JP](../ja-JP/runtime-and-interop.md) · [ko-KR](../ko-KR/runtime-and-interop.md) · [zh-CN](../zh-CN/runtime-and-interop.md) · [zh-TW](../zh-TW/runtime-and-interop.md)
