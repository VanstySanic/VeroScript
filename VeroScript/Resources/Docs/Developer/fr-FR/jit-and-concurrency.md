# Services JIT du cœur et concurrence

[Backends VM remplaçables](vm-backends.md)

<a id="native-abi-version"></a>

`VEROSCRIPT_VERSION_CODE` encode la version produit `VEROSCRIPT_VERSION` sur major:8/minor:8/patch:16 bits. La négociation des modules JIT et les champs JIT/AOT `CoreVersion` partagent cette valeur ; le champ du descripteur C AOT se nomme `core_version`. Aucun compteur de version de contrat ou d’API JIT distinct n’est maintenu. La compatibilité binaire reste vérifiée par les empreintes des en-têtes publics, la disposition des descripteurs, la chaîne de compilation et la plateforme ; AOT vérifie aussi l’identité de l’implémentation Core. Les consommateurs C++ statiques et les binaires des backends doivent être reconstruits avec les en-têtes et bibliothèques correspondants. Les versions de publication des paquets restent des métadonnées indépendantes.

## Propriété et sélection

Voir [Backends VM remplaçables](vm-backends.md#vm-provider-assembly).

[Le module AsmJit optionnel](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/fr-FR/index.md)
possède IR, lowering, émission native, exécution Linear portable, adaptateurs ABI,
cibles directes et stockage temporaire. Ses en-têtes privés et macros ne font pas
partie du SDK Core. Le cœur annonce `backend-services` ; la description de la
fabrique sélectionnée indique la cible et les capacités natives réelles.

## Compilation et appel

`EJitCompilationResult` distingue `Ready`, `Unsupported`, `Deferred`, `Failed`.
Code non pris en charge et report de ressources permettent le repli VM ; un échec
de compilation produit une erreur. Propriété, services et ressources sont validés
avant publication. L’identité inclut les générations du module et des liaisons natives.

`EJitInvocationResult` distingue `NotEntered`, `Returned`, `ControlTransfer`,
`Failed`. Seul `NotEntered` permet de recommencer dans la VM. Après entrée,
erreurs, throw, exit et annulation se propagent sans répéter les effets de bord.
Le cœur valide les retours. Les exécutables gardent code et propriétaire du module.
Contextes, jetons de frame et vues sont empruntés ; réacquérir le stockage des
objets/conteneurs après appel imbriqué ou réentrée de l’hôte.

Les services publics sont `VeroScriptJitRuntimeServices.h`,
`VeroScriptJitFrameServices.h`, `VeroScriptJitValueServices.h`,
`VeroScriptJitCallServices.h`, `VeroScriptJitScalarServices.h`. Copie, références,
const, dispatch, notifications, erreurs et profondeur gardent la sémantique du cœur.

Le propriétaire fourni à `SetJitProgramOwner` conserve une génération de module
immuable : ne pas modifier son contenu via un autre alias mutable. Remplacer les
modules à l’état inactif. Une exécution directe de `VeroScriptVm` ne change pas
automatiquement la génération ou le propriétaire configurés. Si des valeurs
survivent à un changement de programme, l’hôte doit mettre à jour cette identité
et conserver le module définissant ces valeurs jusqu’au nettoyage ou à une
migration prise en charge. Les remplacements et rechargements Session gèrent
cette identité.

## Politique et réutilisation Session

`FJitPolicy` contrôle chaleur, tentatives, décisions en cache et compilation
anticipée. Zéro signifie illimité pour ces budgets. `Vm` évite le backend ;
`Mixed` attend la chaleur ; `Jit` préfère compiler selon la politique. La gouvernance
et les capacités du backend sélectionnent un JIT contrôlé validé ou la VM.
Le débogage et les chemins non pris en charge conservent la VM.

`bReuseSessionCode` est actif par défaut. Les appels admissibles `Run`,
`CallFunction`, `CallMethod` et callable conservé louent le même cache générationnel
et quota cumulatif, même avec budgets illimités. Une fabrique injectée et un mode
non VM sont nécessaires ; l’exécution persistante et les rappels stables permettent la réutilisation. Les catalogues natifs publient leur révision via `IHostBindingCatalog::GetRevision()`. Politique, configuration,
Reload et Clear invalident le cache ; un Reload échoué préserve l’ancien programme.

`FSessionJitCacheLimits` autorise par défaut 8 MiB exécutables et 32 MiB de charge
conservée, scratch inclus. Zéro interdit la conservation de cette catégorie.
Le refus de ressources permet le repli sans rembourser les tentatives consommées.
`FSessionJitCacheStats` donne le bilan générationnel ; `FJitStats` décrit la dernière
exécution. `NativeTypedLoopExecutions`, `DirectNativeCalls`, `CostModelVmFallbacks`
restent distincts et reflètent les chemins réels. Le repli de coût n’est pas une erreur.

## Concurrence et validation

Le cœur gère tâches, jointures, délais, annulation coopérative, canaux, mutex,
sémaphores et valeurs synchronisées. Les canaux bloqués observent la fermeture.
Les workers partagent fabrique immuable et programme possédé ; instances,
statistiques, scratch et erreurs restent locaux. La fin libère les racines du langage.

Tester contrats, différentiel, durée de vie, débogage et concurrence. Construire
module actif, absent et sans émission native ; une copie sans répertoire backend
doit construire Core et consommateurs C++. Garder les seuils de performance et
vérifier l’injection explicite dans le SDK.

<a id="construction-dispatch"></a>

Pendant la construction ou la destruction, les cibles virtuelles doivent être résolues par Core selon la phase de vie courante. `bConstructionIncomplete` et `bDestroying` imposent cette vérification ; `GetTypeName` et `RuntimeTypeIndex` décrivent toujours le type réel et la disposition des champs. Une optimisation qui ne préserve pas cette répartition doit se retirer avant tout effet de bord et ne doit pas réutiliser le cache virtuel ordinaire du type réel. Les appels non virtuels liés statiquement et les entrées de constructeurs natifs dont Core a déjà choisi la cible conservent leurs chemins existants.

## Diagnostics d’exécution

`PlanReason`/`PlannedKind` donnent le plan en cache au moment de la lecture ; `Fallbacks` conserve les rejets historiques de cette exécution, dont la chauffe avant compilation réussie. La collecte détaillée utilise le dispatch instrumenté de la VM officielle, peut réduire la fusion et révéler davantage de frontières Core. Ses temps/comptes ne mesurent pas le chemin sans instrumentation ; les compteurs agrégés d’entrées restent accessibles sans elle.

`NativeLeafEntries`/`NativeCfgEntries` comptent les artefacts compilés, `NativeLeafExecutions`/`NativeCfgExecutions` les entrées réelles, erreurs comprises. Entrées contrôlées, boucles typées, appels directs, Linear/Bridge/VM trampoline gardent leurs compteurs. `ScalarNativeFastPathCalls` concerne seulement le chemin scalaire natif hôte. Ces événements se recouvrent et ne sont pas des appels indépendants.

Configurez `FJitPolicy::Diagnostics` avant l’exécution : par exemple `MaxFunctions=128`, `MaxDepth=256`, `bMeasureTime` facultatif. Lisez `VeroScriptVm::GetJitDiagnostics()` ou `VeroScriptSession::GetLastJitDiagnostics()`. Les enregistrements portent identité générationnelle, module, genre d’entrée, résultats, histogrammes de repli et nanosecondes inclusive/self. Seules les frontières d’appel Core sont enregistrées : pas d’entrée séparée pour une opération inline ou une récursion native directe. Les temps inclusifs se recouvrent ; self ne retire que les enfants enregistrés. L’exécution externe réinitialise le snapshot, réentrance/nettoyage y appartiennent ; `ResetJitStats()` l’efface aussi.

Par défaut `MaxFunctions=0` évite allocation, horloge et logs. Bornes : 4096 fonctions, profondeur 32768, 4096 octets par nom/raison. `DroppedRecords` indique une couverture partielle. Désactivez le timing pour compter seulement ; mesurez la performance collecte désactivée et faites une passe diagnostic séparée.

`GetFunctionReason()`/`GetFunctionFallbackReason()` exposent décision compilée et dernier refus runtime, y compris un plan VM réussi. Les contraintes exception/ref/propriété sont distinctes du coût ; une restriction runtime inconnue utilise `RuntimeGuard`. `CostModelVmFallbacks` exclut le repli obligatoire finally/ref. Finally garde le déroulement Core et le nettoyage borné. Une expression scalaire bornée peut modifier une unique capture ref simple et writable sans callback/gouvernance ; aliases, captures multiples, readonly, origines de mutation et probes échoués gardent l’exécution canonique. Les caches de frame vérifient fonction/module/génération ; ordre et nombre des getters/bindings restent inchangés.

Les agrégats sont propagés aux API C Interop/C++/C#/Python, les snapshots détaillés à Core/C++. Après changement de disposition/service public, reconstruisez Core, VM, JIT, Interop et consommateurs natifs appariés même si les versions produit sont identiques.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/jit-and-concurrency.md) · [fr-FR](jit-and-concurrency.md) · [ja-JP](../ja-JP/jit-and-concurrency.md) · [ko-KR](../ko-KR/jit-and-concurrency.md) · [zh-CN](../zh-CN/jit-and-concurrency.md) · [zh-TW](../zh-TW/jit-and-concurrency.md)
