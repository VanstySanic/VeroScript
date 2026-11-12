# Embedding et session C++

## Engine sans état

`VeroScriptEngine` expose `CheckSource`, `CheckFile`, `RunSource`, `RunFile` aux appelants sans état persistant. Les opérations fichier peuvent utiliser le cache bytecode ; label source, flux input/output/error, politique d’import circulaire et d’accès projet sont des paramètres explicites. Le résultat contient succès/échec et diagnostics structurés : une ligne du flux erreur ne le remplace pas.

## Session avec état

`VeroScriptSession` possède module chargé, enregistrements hôte, ponts I/O, configuration runtime et état hot reload optionnel. Le flux normal construit une session avec `IEngineHost`, applique configuration/enregistre fonctions natives ou types hôte, charge source/fichier/paquet validé ou compile/commit un module, exécute/appelle/inspecte, puis clear/reload seulement selon les préconditions d’état. Configuration, load, compilation, commit, reload, clear et mutation de binding sont refusés pendant un run. `LoadFile` enregistre le chemin reload fichier ; `LoadSource` n’en crée pas. Le reload valide remplacement et type de paquet avant commit.

## Évaluation et valeurs

`Run(ERunMode::Eval)` initialise le module sans appeler `main` ; les globales persistent entre appels uniquement avec `SetHotReloadEnabled(true)`. Appels fonction/méthode peuvent retourner `FValue`, construire un objet, accéder champs/globales et retenir un callable. Un callable retenu est libéré explicitement et peut être limité au thread propriétaire. Les serializers externes utilisent type runtime enregistré/version encodée ; sérialisation, désérialisation et énumération de références sont des opérations distinctes.

## Bindings hôte et métadonnées

L’enregistrement couvre fonctions natives, types hôte, modèles génériques de type hôte, membres, opérateurs, callbacks clone, types héritables, enums scoped et factories de sous-classe. Les erreurs d’enregistrement sont signalées par ID diagnostic stable. La réflexion énumère types, champs, méthodes, fonctions, globales, enums et interfaces chargés et reste un accès métadonnée en lecture seule.

## JIT et clear

<a id="session-jit-cache"></a>

La réutilisation exige une fabrique sélectionnée, une stratégie effective autre que VM et `FJitPolicy::bReuseSessionCode`. Les invocations admissibles `Run`, `CallFunction`, `CallMethod` et de callable conservé partagent le cache et le quota de compilation d’une génération. Zéro pour `MaxCompileAttempts` ou `MaxCachedEntries` signifie illimité, pas désactivé. Une exécution persistante et des rappels stables permettent la réutilisation ; les catalogues natifs publient leur révision via `IHostBindingCatalog::GetRevision()`. Une exécution native gouvernée peut réutiliser le code si le backend fournit toutes les capacités requises ; la Session conserve ce contexte entre VM. Une modification de configuration, stratégie, backend, module ou liaison invalide le code conservé ; un rechargement échoué préserve le programme précédent. Les valeurs par défaut sont 8 Mio exécutables et 32 Mio de charge conservée. `GetJitCacheStats` décrit une génération stabilisée ; `GetLastJitStats` décrit la dernière invocation, pas la mémoire du processus.

## Gouvernance d’exécution

`SetExecutionLimits` fixe les limites d’instructions et de durée ; zéro signifie illimité. `SetExecutionControl` installe un contrôle partagé et `RequestCancel` peut être appelé pendant `Run`. L’annulation reste coopérative et n’arrête pas un rappel natif en cours. `SetCapabilities` contrôle les effets de la bibliothèque standard ; Core impose ces permissions aux chemins interprétés et natifs pris en charge. `bAllowProcessDirectoryChange=false` interdit `chdir`, qui exige aussi l’écriture de l’environnement. `SetExecutionBackend` choisit `Vm`, `Mixed` ou `Jit`. `Mixed` gouverné utilise la VM par défaut ; `SetJitPolicy` avec `FJitPolicy::bEnableControlledMixed=true` active le natif dynamique. `Jit` explicite et Core AOT statique sélectionné en `Mixed` sont indépendants du commutateur. Des capacités manquantes entraînent un repli avant l’entrée native ; le débogage, les plages d’initialisation conservées et le nettoyage borné utilisent la VM.

## Appels, interception et persistance

La surface publique inclut `HasFreeFunction`, `HasInstanceMethod`, `CallFunction`, `CallMethod`, `CallMethodBypassingInterceptor`, `ConstructObject`, `RetainCallable`, `InvokeCallable`, accès champ/global, `ApplyClassDefaults` et réflexion. Le bypass saute seulement l’intercepteur d’entrée sélectionné, les appels imbriqués suivent toujours le dispatch normal. Propriété callable retenu, mode owner-thread, capture faible `this` et invalidation de génération restent des règles de durée de vie explicites.

`RegisterExternalSerializer`, `SerializeObject`, `DeserializeObject`, `EnumerateObjectReferences` suivent sérialisation Interop et référence empruntée. `Reload`, `ReloadCompiledModule`, `ReloadPackage` préparent/valident transactionnellement un remplacement ; une initialisation échouée garde le dernier module fonctionnel et retourne un diagnostic structuré.

## Quotas de rétention en octets

Zéro `MaxNativeBytes` ou `MaxRetainedPayloadBytes` interdit la rétention correspondante ; ces limites en octets diffèrent des budgets de stratégie zéro illimités.

## Annulation et livraison à l’hôte

<a id="session-control-and-streams"></a>

`RequestCancel()` prend atomiquement un instantané partagé du contrôle actuel,
même si le propriétaire le remplace pendant une période inactive. Null ne fait
rien ; l’annulation ne se propage pas à un autre contrôle. `GetExecutionControl()` est une
référence empruntée réservée au propriétaire. Attendez les appels d’annulation et
l’exécution avant la destruction. Les vues de lignes expirent après le callback.
Un callback qui lève une exception met le flux en échec sans rejouer l’événement
à la réutilisation ou destruction. `ResetForReuse` restaure l’état du flux ; la
destruction supprime les exceptions des callbacks.

Les Sessions partageant le même jeton `FExecutionControl` partagent son état
d’annulation. Attendez la fin des appels d’annulation précédents avant `Reset()`
et réutilisation, pour éviter qu’une ancienne requête annule l’exécution suivante
qui réutilise ce jeton.

## Responsabilités d’implémentation

<a id="session-debug-implementation"></a>

`VeroScriptSessionDebug.h` définit le pont de débogage Session et les vues empruntées ; leur implémentation réside actuellement dans `VeroScriptSession.cpp`. `VeroScriptLiveDebugDomain.h` définit le contrat du domaine fourni par l’hôte, consommé par `VeroScriptDapAdapter.cpp`. Les fichiers `.cpp` homonymes ne contiennent actuellement que des inclusions d’en-têtes. Leur nom ne détermine pas la responsabilité de l’implémentation. Respectez les contrats de thread de contrôle et de durée de vie ci-dessus.

## Diagnostics JIT facultatifs

`FJitPolicy::Diagnostics.MaxFunctions` vaut zéro par défaut : aucune collecte par fonction. Modifiez une copie de la stratégie existante avec les bornes de nombre/profondeur et `bMeasureTime` facultatif, puis appelez `SetJitPolicy`.

`GetLastJitDiagnostics()` renvoie un instantané possédé de la dernière exécution, conservé après destruction de la VM temporaire. Copiez-le pour le garder après d’autres appels. Run, débogage, appels fonction/méthode, construction et Callable retenu capturent cet état. L’exécution suivante avec collecte désactivée, ou `Clear()`, l’efface. `DroppedRecords` indique une collecte partielle ; sans mesure, les temps restent nuls.

`GetLastJitStats()` expose les entrées réellement exécutées : `NativeLeafExecutions`, `NativeCfgExecutions`, `BridgeExecutions`, `LinearExecutions`, `VmTrampolineExecutions`. Compilation et exécution sont distinctes. Appels directs/scalaires et boucles typées peuvent être imbriqués ; ne les additionnez pas aux entrées de fonction, ni les temps inclusifs entre fonctions.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/session.md) · [fr-FR](session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
