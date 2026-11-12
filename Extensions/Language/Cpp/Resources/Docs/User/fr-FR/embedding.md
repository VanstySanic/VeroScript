# Intégration hôte

VeroScript peut être intégré dans un hôte C++. Les hôtes utilisent généralement `VeroScriptEngine` pour la vérification ou l'exécution ponctuelle et `VeroScriptSession` pour les modules de longue durée, les liaisons, l'état d'exécution et le rechargement à chaud.

## Sélection explicite de la VM

Core ne fournit pas d’interpréteur implicite. Les exemples ci-dessous utilisent la VM de bytecode statique distribuée séparément : recherchez les deux SDK, liez les deux cibles et configurez explicitement chaque configuration moteur ou Session. L’édition de liens seule ne sélectionne aucun backend.

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Runtime)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScriptCpp VeroScript::VeroScriptBytecodeVm)
```

Pour le chargement dynamique, définissez `FEngineConfig::VmBackendPath` ou appelez `Session::SetVmBackendPath` avec le fichier ou répertoire VM déployé. Fabrique et chemin sont exclusifs. Le choix devient fixe au début de l’exécution ; `Clear()` ne le déverrouille pas. Les scripts de compilation héritent de la VM choisie sauf si `ScriptCompileExtensions` indique sa propre fabrique ou son propre chemin. Une compilation ordinaire sans ces scripts ne nécessite aucune VM.

Pour une Session existante, un `FEngineConfig` sans fabrique ni chemin VM conserve le choix courant. `bClearVmBackend=true` demande un effacement explicite avant le gel ; fabrique, chemin et effacement sont exclusifs. `Clear()` réinitialise le programme chargé, mais pas le domaine VM figé après exécution. Créez une nouvelle Session pour changer ensuite d’interpréteur.

## Points d'entrée du moteur de haut niveau

Incluez l’en-tête public `VeroScriptEngine.h` fourni par le SDK. `VeroScriptEngine` fournit des points d'entrée de tampon source et de fichier :

- `CheckSource` / `CheckFile` effectue une vérification lexicale, syntaxique et sémantique ;
- `RunSource` / `RunFile` charge et exécute un script.

Ces points d'entrée peuvent recevoir des chemins d'importation, des arguments de script, des définitions, des chemins de recherche de bibliothèque standard, des répertoires de cache de bytecode, des backends d'exécution et des catalogues de liaison.

`FEngineConfig` expose également des macros prédéfinies structurées, des chemins d'importation implicites,
superpositions de sources en mémoire, profondeur d'appel maximale, importation circulaire et
des commutateurs d'accès à l'échelle du projet et un flux d'entrée. Valeurs par défaut `RunFile` intégrées
pour désactiver la mise en cache du bytecode ; activez-le explicitement avec `bUseBytecodeCache` et
`BytecodeCacheDirectory` le cas échéant. Le backend d'exécution est `Vm`,
`Mixed` ou `Jit` et la valeur par défaut est `Mixed`.

Le cache de bytecode inclut les identités de l’implémentation Core et de son API publique.
Une modification du code Core invalide les anciens caches même si les versions du produit
et du bytecode restent identiques ; les exécutions du même build les réutilisent.

`ExecutionLimits.MaxInstructions` et `ExecutionLimits.TimeoutMilliseconds` sont des limites coopératives par exécution ; zéro signifie sans limite. `ExecutionControl` reçoit un `FExecutionControl` partagé ; appelez `RequestCancel()` depuis un autre thread pour demander l’annulation. Core impose les mêmes contrôles d’instructions, d’annulation et de permissions aux chemins interprétés et natifs pris en charge. `Mixed` gouverné utilise la VM par défaut ; une Session peut activer le natif dynamique avec `SetJitPolicy` et `FJitPolicy::bEnableControlledMixed=true`. `Jit` explicite et Core AOT statique sélectionné sont indépendants de ce commutateur. Des capacités manquantes entraînent un repli vers la VM avant l’entrée native. Un rappel natif en cours n’est pas arrêté de force. Le jeton ne se réinitialise pas automatiquement ; appelez `Reset()` avant sa réutilisation.

`Capabilities` contrôle les effets de bord de la bibliothèque standard : lecture/écriture/suppression de fichiers, lecture/écriture de l'environnement, entrée/sortie de console et création de tâches. Tout est autorisé par défaut ; les restrictions ne concernent que les fonctions intégrées, et l'hôte autorise ses propres rappels.
`bAllowProcessDirectoryChange` permet d’interdire séparément à `chdir` de modifier le répertoire du processus hôte ; l’autorisation d’écriture de l’environnement reste nécessaire pour `chdir`.

Les points d'entrée renvoient `FRunResult`. `bSuccess` signale que c'est normal
compilation/exécution ; un `int main` ou `exit(n)` réussi peut toujours produire un
`ExitCode` différent de zéro. `bExitRequested` est défini uniquement par un `exit(n)` explicite,
y compris `exit(0)` ; un retour `int main` ordinaire ne le définit pas. Lire `Error`
et `Diagnostics` pour les détails de l'échec.

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptEngine.h"

#include <iostream>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	config.ScriptArgs = {"alpha", "beta"};

	const VeroScript::FRunResult result = VeroScript::VeroScriptEngine::RunFile(
		"main.vs",
		host,
		config);

	if (!result.bSuccess)
	{
		return result.ExitCode;
	}
	return result.ExitCode;
}
```

`ERunMode` est indépendant du backend : `Eval` utilise l'incrémental/REPL
chemin d'initialisation, `EvalOnce` évalue la source une fois et `Script` appelle
`main` après chargement. Le débogage, les plages d’initialisation conservées et le nettoyage borné utilisent la VM ; les fonctions persistantes peuvent utiliser le JIT sélectionné.

## Débogueur VM intégré

`VeroScriptDebugger` est le débogueur synchrone intégré pour un hôte qui
possède directement un `VeroScriptVm`. Il est distinct de DAP : il n'ouvre pas
de connexion filaire et n'encode pas de JSON. Incluez `VeroScriptCompile.h`,
`VeroScriptDebugger.h` et `VeroScriptVm.h`, compilez une source ou un fichier
vers un `FBytecodeModule`, attachez un débogueur non possédé avec
`SetDebugger`, puis exécutez le module.

Pour utiliser l'interpréteur de bytecode, incluez `VeroScriptBytecodeVmAssembly.h`
et appelez `VeroScript::BytecodeVm::Configure(vm)` avant l'exécution.

`SetBreakpoint` utilise des lignes source commençant à un. Un libellé de fichier
vide correspond à tous les fichiers ; un libellé non vide correspond au libellé
complet ou à son nom de base. Un point d'arrêt conditionnel évalue son
expression dans la trame supérieure. Les erreurs d'évaluation et les
conditions fausses ignorent l'arrêt ; les valeurs booléennes, les nombres non
nuls et les autres résultats non vides sont considérés comme vrais.

Le rappel d'arrêt est exécuté de manière synchrone pendant que la VM est en
pause. Utilisez `GetStackTrace`, `GetLocals` et `GetChildren` pour inspecter
l'état. `Evaluate` est en lecture seule et évalue dans la trame choisie ;
utilisez `SetLocal` ou `SetChild` pour écrire via une variable de débogage.
Appelez `SetRunMode` depuis le rappel pour choisir `Continue`, `StepIn`,
`StepOver` ou `StepOut`.

`RequestPause()` est une demande coopérative thread-safe, observée aux points
de sécurité de la VM et signalée par `EDebugStopReason::Pause`. Elle
n'interrompt pas de force un rappel natif. Appelez `ResetPauseRequest()` avant
de réutiliser un débogueur si une demande précédente peut encore être en
attente. `SetDebugger` ne transfère pas la propriété : le débogueur doit rester
valide pendant l'exécution de la VM et la VM doit rester valide pendant tout
rappel qui l'utilise. L'attachement force le backend effectif à `Vm` et
invalide le cache JIT.

Pour une intégration asynchrone avec un éditeur, utilisez
`VeroScriptDebugSession` ou les adaptateurs DAP décrits dans [Tooling](../../../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/index.md)
et [Débogage](../../../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/debugging.md).

## Localisation des diagnostics

Les points d’entrée du moteur initialisent automatiquement la localisation principale. Ils préfèrent
`VERO_LOCALIZATION_DIR`, puis utilisez l'ordre de recherche de déploiement décrit dans
[Localisation du diagnostic](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/localization.md) et sélectionnez une culture parmi
`VERO_LANG` ou la langue de l'interface utilisateur du système. Un hôte n'a pas besoin de supplément
appel d'initialisation juste pour recevoir les diagnostics en anglais.

Pour un contrôle explicite du catalogue et de la langue, utilisez directement `VeroScript::Locale` :

```cpp
#include "VeroScriptLocale.h"

#include <iostream>
#include <string>

int main()
{
	std::string error;
	if (!VeroScript::Locale::LoadCatalogDirectory("localization", &error))
	{
		std::cerr << error << '\n';
		return 1;
	}

	if (!VeroScript::Locale::TrySetLanguageFromTag("zh-CN"))
	{
		return 2;
	}

	const std::string language = VeroScript::Locale::GetLanguageTag();
	std::cout << language << '\n';
	return 0;
}
```

`LoadCatalogDirectory` remplace l’arborescence du catalogue à l’échelle du processus. Une bibliothèque ou
la racine de la bibliothèque standard peut utiliser `MergeCatalogDirectory` pour ajouter un frère ou une sœur
Arbre `localization/` ; les ID de message existants sont conservés sauf si le remplacement est
demandé. Les opérations du langage de base et du catalogue sont thread-safe, mais
`VeroScriptSession` lui-même n’est pas thread-safe en interne. Les hôtes doivent définir
lorsque les changements de langue sont autorisés afin que les processus partagés ne réécrivent pas le
afficher la langue au milieu d’une requête. Voir [Localisation du diagnostic](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/localization.md)
pour les ID de message, les espaces réservés et les règles de secours en anglais.

## Sessions de longue durée

`VeroScriptSession` est destiné à un éditeur, un jeu ou un outil hôte qui possède des scripts sur plusieurs exécutions. Une séance peut :

- charger, exécuter, recharger et effacer les modules ;
- enregistrer les fonctions natives, les types d'hôtes, les champs, les propriétés, les méthodes, les opérateurs et les modèles génériques de type d'hôte ;
- installer des rappels de sortie, de diagnostic, d'intercepteur d'appel et de mutation ;
- sélectionnez l'exécution VM, mixte ou JIT et configurez la mise en cache du bytecode ;
- définissez des limites coopératives d'instructions/de temps, l'annulation et les capacités de la bibliothèque standard ;
- optez explicitement pour le rechargement à chaud préservant l'état.

Les appels de session ne sont pas synchronisés en interne. L'hôte doit utiliser un thread de contrôle pour `Load`, `Run`, `Reload` et `Clear`, et demander des rechargements lorsque l'exécution est inactive.

Une session typique de longue durée est :

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptSession.h"

#include <iostream>
#include <string>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::VeroScriptSession session(host);

	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	std::string configError;
	if (!session.ApplyConfig(config, &configError))
	{
		std::cerr << configError << '\n';
		return 1;
	}

	const VeroScript::FRunResult load = session.LoadFile("main.vs");
	if (!load.bSuccess)
	{
		return load.ExitCode;
	}

	const VeroScript::FRunResult run = session.Run();
	return run.ExitCode;
}
```

<a id="apply-config-result"></a>

`ApplyConfig` renvoie `false` si la configuration est refusée et peut fournir la raison via `OutError`. Vérifiez ce résultat avant de charger ou d’exécuter. Les refus de validation précèdent l’application de la configuration ; les exceptions d’allocation ou de copie de callbacks peuvent survenir après des changements partiels et ne garantissent pas une restauration.

`IEngineHost` doit survivre à la session et reçoit `print` et la ligne de diagnostic
rappels. Complétez `RegisterNative`, `RegisterHostType` et
Enregistrement `RegisterHostMember` avant `Load` afin que le compilateur puisse résoudre
déclarations d'hôte. Le registre survit à `Reload` et `Clear` ; lisez-le entièrement
`GetBindingCatalog`, ou exportez-le avec `ExportBindingsToFile` pour IDE, LSP et
Consommateurs de DAP.

La session expose également `CompileFile`/`CommitCompiledFile`,
`ReloadCompiledModule`, `LoadPackage` et `ReloadPackage`. Ceux-ci permettent aux éditeurs,
les chargeurs de paquets et les systèmes de mise à jour à chaud séparent la compilation de l'engagement.
Vérifiez le `CanonicalSignedBytes` d’un paquet avant de franchir une limite de confiance.

`BuildPackage`/`ReadPackage` et `LoadPackage`/`ReloadPackage` de Session traitent les images Core (`.vslib` ou image applicative compilée brute). Un ZIP `.vsapp` standard est un conteneur externe : utilisez le service Application et `LoadApplication`/`ReloadApplication`. Enregistrer une image Core brute avec le suffixe `.vsapp` ne crée pas un conteneur standard.

## Limite de liaison

Les scripts ne peuvent appeler que les fonctions ou les membres de l'hôte que l'hôte a enregistrés et que le compilateur peut résoudre. Les descriptions de liaison doivent fournir des informations sur les paramètres et les retours, ainsi que des informations sur le type d'hôte, l'élément de conteneur ou le type clé/valeur. Les noms en double, les rappels nuls et les conflits avec les éléments intégrés sont rejetés.

Les copies de valeurs de structure hôte nécessitent un rappel de clone approprié. Les descripteurs d’objet hôte suivent les règles de durée de vie et de propriété établies par l’enregistrement de l’hôte. L'API de liaison complète est définie par les en-têtes publics `VeroScriptCpp`.

La surface d’appel public et de prolongation comprend également :

- `CallFunction`, `CallMethod`, `CallMethodBypassingInterceptor`, `ConstructObject`, `GetField`/`SetField` et accès global ;
- réécriture des arguments mutables `ref`, sans réécriture en cas d'échec ou de sortie ;
- `RetainCallable`/`InvokeCallable` pour conserver la valeur d'une fonction de script au-delà d'un rappel natif ;
- `SetInstanceCallInterceptor`, `SetMutationCallback` et `ApplyClassDefaults` ;
- `RegisterGenericHostTypeTemplate`/`InstantiateGenericHostType`, alias de type, sérialisation externe et énumération de référence d'objet ;
- `SetJitPolicy`, `SetJitCacheLimits`, `GetJitCacheStats`, `GetLastJitStats`: La réutilisation exige une fabrique sélectionnée, une stratégie effective autre que VM et `FJitPolicy::bReuseSessionCode`. Les invocations admissibles `Run`, `CallFunction`, `CallMethod` et de callable conservé partagent le cache et le quota de compilation d’une génération. Zéro pour `MaxCompileAttempts` ou `MaxCachedEntries` signifie illimité, pas désactivé. Une exécution persistante et des rappels stables permettent la réutilisation ; les catalogues natifs publient leur révision via `IHostBindingCatalog::GetRevision()`. Une exécution native gouvernée peut réutiliser le code si le backend fournit toutes les capacités requises ; la Session conserve ce contexte entre VM. Une modification de configuration, stratégie, backend, module ou liaison invalide le code conservé ; un rechargement échoué préserve le programme précédent. Les valeurs par défaut sont 8 Mio exécutables et 32 Mio de charge conservée. `GetJitCacheStats` décrit une génération stabilisée ; `GetLastJitStats` décrit la dernière invocation, pas la mémoire du processus.

Les corps de méthodes interceptées pris en charge suivent aussi la négociation des capacités du backend. Les appels imbriqués via `Proceed` conservent interception, écriture des références et contrôles d’exécution. Sans emplacement fourni par l’hôte, les erreurs d’interception indiquent le site d’appel ; les emplacements explicites et les erreurs du corps sont conservés.

Transmettre directement le résultat original de `Proceed` conserve sa frontière de copie par valeur, y compris pour les structures, les types valeur natifs et les tableaux fixes, sans répéter les constructeurs de copie. Un résultat substitué ou emprunté reçoit la validation de type et la copie ordinaires. Les méthodes déclarées avec un retour par référence conservent leur sémantique.

`VeroScriptSession` n’est pas thread-safe en interne. Effectuez les appels ordinaires et la destruction sur le thread qui a créé la session. Les callbacks peuvent s’exécuter sur des threads de travail de la VM ; synchronisez donc l’état externe. Par défaut, `RetainCallable` utilise `bOwnerThreadOnly = true` : un appel depuis un autre thread échoue, même si l’hôte sérialise les accès. Pour autoriser cet appel, passez explicitement `false` comme deuxième argument de `RetainCallable` et sérialisez tous les accès à la session, y compris l’exécution, la configuration, le rechargement et la destruction.

Le nettoyage du script est aussi une opération active de la Session : effacer ou remplacer le programme, désactiver le rechargement à chaud, libérer une fonction conservée ou détruire la Session peut appeler des destructeurs et des callbacks hôtes. Ces callbacks ne doivent pas réentrer dans les API qui exécutent ou modifient la même Session. `RetainCallable` refuse aussi les nouvelles racines pendant le nettoyage. Planifiez les opérations suivantes après son retour ; les callbacks d’exécution ordinaires peuvent toujours conserver des fonctions.

## Observabilité du JIT natif

`FJitStats::CostModelVmFallbacks` est le sous-ensemble de `Fallbacks` laissé
volontairement dans la VM lorsqu’un chemin compilé valide est prédit plus lent.
Ce n’est ni un échec de compilation ni un changement de sémantique.
`FJitStats::NativeTypedLoopExecutions` compte les invocations de noyaux de boucles
typées du backend natif, y compris celles qui signalent une erreur runtime visible
dans le source ; ce n’est pas un compteur de succès. Ces deux valeurs observent la
dernière exécution ; zéro ne prouve pas l’indisponibilité d’une cible ou capacité.

<a id="threads-and-hot-reload"></a>

## Threads et rechargement à chaud

L'hôte est responsable de la coordination des limites des threads entre l'exécution de la VM, le contrôle de session et les rappels natifs. Le rechargement à chaud est facultatif. Les fermetures compatibles, y compris leur `this` capturé, peuvent migrer si l'identité de la fonction ainsi que les noms et types des captures restent compatibles. L'ajout ou la suppression de captures, un changement de leur type, une fonction impossible à remapper, les canaux actifs et les changements de types incompatibles sont refusés. Les handles de callable conservés par l'hôte expirent lorsque leur génération d'enregistrement est remplacée avec succès ; le nettoyage préserve les graphes partagés avec des variables globales ou des racines de valeurs encore actives. Les effets secondaires natifs restent en dehors de la transaction d'état du script.

Lorsque le rechargement à chaud préserve l’état, les variables globales compatibles, les objets et l’identité des conteneurs peuvent être migrés. Les champs ajoutés reçoivent les valeurs par défaut du langage. Le rechargement compile, valide et migre l’état avant de remplacer le module ; si l’initialisation échoue, le dernier module valide reste disponible. Désactiver le rechargement à chaud ou appeler `Clear` libère normalement les valeurs conservées, y compris les cycles de références. Le rechargement à chaud ignore le cache de bytecode : `Reload` recompile donc les sources actuelles.

Pour l’ABI C multilingue stable, voir [Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md). Pour
navigateur et points d’entrée en mémoire WebAssembly, voir [SDK WebAssembly/WASM](../../../../../../Platform/WebAssembly/Resources/Docs/User/fr-FR/index.md).

## Déboguer une Session existante

[Déboguer une Session existante](session-debugging.md). `SetDebugBridge`, `ISessionDebugBridge`, `SetDebugBridge(nullptr)`.

## Quotas de rétention en octets

Zéro `MaxNativeBytes` ou `MaxRetainedPayloadBytes` interdit la rétention correspondante ; ces limites en octets diffèrent des budgets de stratégie zéro illimités.

## Sélection explicite du backend JIT

Sans environnement de modules installé, Core et le SDK C++ ne choisissent pas automatiquement une implémentation JIT.
Chargez un fichier ou un répertoire avec `LoadJitBackend` de
`VeroScriptJitBackendLoader.h`, puis affectez la fabrique partagée à
`FEngineConfig::JitBackendFactory`. Une Session accepte `SetJitBackendFactory` ou
`SetJitBackendPath` : un chemin vide retire le backend ; un échec conserve le
backend et le cache actuels.

Les surcharges de `RunSource` / `RunFile` avec flux peuvent recevoir une fabrique JIT puis une fabrique VM ; les surcharges plus courtes ne fournissent pas d’interpréteur. Utilisez `FStreamEngineHost` avec `FEngineConfig` pour les chemins ou les valeurs de l’environnement de modules, selon la [sélection des backends de service](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection). `Vm` et les parties interprétées de `Mixed` exigent une fabrique VM disponible ; `Jit` explicite exige aussi une fabrique JIT. Un hôte configuré peut les résoudre dans son environnement ou recevoir des fabriques explicites. Débogage et rechargement préservant l’état exigent une VM adaptée. Core AOT statique peut utiliser `Mixed` sans fabrique JIT dynamique, mais l’initialisation interprétée et le repli exigent toujours une VM.

Installer un pont de débogage libère le code JIT conservé par la Session. Si le
pont ne renvoie ensuite aucun débogueur, les appels suivants peuvent compiler
avec la fabrique sélectionnée. Déconnecter n’annule pas le script et ne détruit
pas la Session. Les structures publiques Core et C++ ont changé : reconstruisez
les consommateurs natifs avec des SDK correspondants.

## Annulation coopérative

Configurez un `FExecutionControl` partagé avec `ApplyConfig` ou
`SetExecutionControl`. Le thread du protocole peut appeler
`Session::RequestCancel()`, même lorsque le propriétaire remplace le contrôle
pendant une période inactive. L'appel annule un instantané partagé du contrôle
actuel ; un pointeur nul ne fait rien et l’annulation ne se propage pas à un autre contrôle. `GetExecutionControl()` renvoie une référence empruntée réservée au
thread propriétaire. L'annulation est coopérative et ne peut pas interrompre un
callback natif. Réinitialisez le jeton avant de le réutiliser. Attendez la fin des
appels d'annulation et de l'exécution avant de détruire la Session ; la déconnexion
du débogueur reste une opération distincte.

Les Sessions partageant le même jeton `FExecutionControl` partagent son état
d’annulation. Attendez la fin des appels d’annulation précédents avant `Reset()`
et réutilisation, pour éviter qu’une ancienne requête annule l’exécution suivante
qui réutilise ce jeton.

## Mémoire gérée dans un hôte C++

Incluez `VeroScriptMemory.h` pour utiliser les fonctions Core `Memory::*`.
`FEngineConfig::MemoryLimits` s'applique à `RunSource` et `RunFile` avec
configuration. `MaxBufferBytes` limite la capacité d'un tampon possédé ;
`MaxRetainedBytes` limite le budget partagé des régions possédées et empruntées.
La limite par région s'applique aussi à la longueur d'une région externe.
Zéro signifie illimité pour ces deux limites.

Une Session conserve un contexte mémoire entre ses VM d'exécution. Utilisez
`GetMemoryContext()`, `SetMemoryLimits()` et `GetMemoryStats()` sur le thread
propriétaire. La modification des quotas exige une exécution inactive. Une
réduction refusée conserve les quotas précédents ; un refus dans `ApplyConfig`
conserve aussi les autres réglages. Les workers partagent le budget avec des
domaines de thread distincts. Dans un callback natif, utilisez les services
empruntés `FNativeCallContext::MemoryServices` pour le contexte actif, les
contrôles de travail, le catalogue et les adresses des arguments. Ne conservez
pas ces services après le callback. `Capabilities.bAllowMemoryAddress`
contrôle l'accès aux adresses ; une adresse brute ne retient pas les octets.
Avant d'exposer une adresse par un callback natif personnalisé, vérifiez
`MemoryAddressesAllowed()` ; les appels directs de l'hôte aux fonctions Core
restent sous sa responsabilité.

`Clear` et le rechargement conservent le contexte. Un rechargement compatible
conserve l'identité des régions ; un échec d'initialisation restaure le programme
précédent. La destruction de la Session exécute le nettoyage du script avant
de fermer le domaine. Ne fermez pas manuellement le contexte emprunté et ne
conservez pas sa référence après destruction. Un bail acquis retient les octets
jusqu'à `Memory::ReleaseLease`, même après destruction. La libération est sûre
entre threads ; terminez ou synchronisez les accès natifs avant de libérer.
Le propriétaire externe et les données du callback de libération doivent vivre
plus longtemps que tous les alias et baux ; ce callback ne doit pas lancer d'exception.

Voir l'exemple SDK `CppMinimal/memory.cpp` et la
[référence mémoire](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md).
Les opérations mémoire et types de région du runtime sont refusés par l'AOT
C++ autonome. Un import inutilisé de `memory.vs` ou `core.vs` reste accepté.

Passer `false` à `RetainCallable` ne transfère pas le domaine mémoire de la
Session. Même avec des accès sérialisés, les opérations sur les régions exigent
le thread propriétaire ; un callable qui accède à une région sur un autre
thread échoue avec `diag.memory_wrong_thread`. Les workers du script utilisent
leurs propres domaines mémoire.

## Macros publiques des modules

`Defines` et `PredefinedMacros` initialisent chaque module séparément. Engine et Session utilisent le chargeur du cœur : les imports explicites exposent les macros `#define_export`, et `export import` les réexporte. `#define` reste privé ; `ImplicitImportPaths` expose uniquement les déclarations. `SourceOverlays` suit les mêmes règles. Voir la [référence du préprocesseur](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/preprocessor.md).

Après une modification d'une dépendance de macros, appelez `Reload()` pour une Session de fichier puis exécutez-la pour terminer l'initialisation, ou chargez la source mise à jour avec `LoadSource()`. Si le rechargement à chaud est activé, `Reload()` migre l'état global compatible ; `LoadSource()` remplace le module et réinitialise son état. L'interface publique des macros modifie le format du bytecode : reconstruisez les fichiers `.vslib`/`.vsapp` déployés avec ce cœur et recompilez les consommateurs natifs avec les SDK Core et C++ correspondants.

## Traitement des annotations à la compilation

Configurez Engine et Session avec `FEngineConfig::AnnotationProcessing` ; Session expose aussi `SetAnnotationProcessing`/`GetAnnotationProcessing`. Appelez `Freeze()` sur le registre et gardez-le immuable. Les fabriques créent des processeurs indépendants pour chaque préparation des déclarations. Gardez leurs bibliothèques natives et les données capturées par les callbacks en vie. Pour C++ AOT, configurez `FCppCodegenBackend` via `FCodegenOptions::AnnotationProcessing`.

Les processeurs s'exécutent de manière synchrone sur le thread qui compile. Un callback de préparation ne doit pas exécuter ou modifier la même Session ; ces opérations réentrantes sont refusées. L'annulation propage l'exception du cœur `FAnalysisCancelled`. Une annulation ou un échec de compilation conserve le programme actif. `FRunResult::Diagnostics` conserve Warning/Information/Hint et le flux d'erreurs les affiche aussi ; ces diagnostics non fatals ne rendent pas un résultat réussi invalide. Les résultats de `RunSource`/`RunFile` incluent les diagnostics de compilation et d'exécution.

Utilisez `ReadDependency` pour les entrées externes qui influencent la génération. Même sur un succès du cache, les déclarations sont préparées ; identité, configuration, contenu généré et dépendances participent à la clé. Définissez `Cacheable=false` pour un processeur dont les résultats ne sont pas reproductibles. Les budgets personnalisés s'appliquent aussi aux bibliothèques importées contenant des déclarations générées. Consultez les en-têtes Core `VeroScriptAnnotationProcessing.h` et `VeroScriptProgramPreparation.h` ainsi que l'exemple SDK compilable `CppMinimal/main.cpp`.

## Intégration du terminal hôte

`IEngineHost` peut implémenter `QueryConsole`/`ExecuteConsole` avec `VeroScriptConsole.h`. Les contrôles restent indisponibles par défaut. `HasConsoleTextOutput`/`WriteConsoleText` fournissent le texte exact sans saut de ligne, et `FlushConsoleText` le vide. `FStreamEngineHost` transmet au flux sous-jacent. Synchronisez les callbacks de travail et gardez l’hôte vivant jusqu’à la fin du nettoyage. La VM restaure style/visibilité. L’état du terminal étant partagé, coordonnez les séquences complètes entre tâches.

L’interface de terminal ajoute des méthodes virtuelles. Lors de la mise à niveau, recompiler l’hôte et les extensions natives concernées avec les bibliothèques Core/C++ SDK de la même compilation. Si JIT est utilisé, déployer également le backend correspondant.

## Traitement général de compilation

Engine et Session transmettent `FEngineConfig::CompileProcessing` ; Session propose aussi `SetCompileProcessing`/`GetCompileProcessing`. C++ AOT utilise `FCodegenOptions::CompileProcessing`. Figez le `FCompileProcessorRegistry` avant la compilation. Chaque compilation conserve les mêmes instances entre `RawSource`, `ActiveSource`, `Types`, `Members` et `Bodies`. L’émission AOT d’un programme préparé réutilise la génération acceptée. Les rappels sont synchrones sur le thread de compilation et ne peuvent pas réentrer dans la même Session.

Utilisez `ReadDependency` pour les entrées de génération et `Cacheable=false` pour les processeurs non déterministes. La génération, les dépendances, les définitions d’annotations, la conservation des commentaires et les budgets participent à l’identité du cache ; le traitement précède toute lecture du cache. Un échec ou une annulation conserve le programme actif. `CompileProcessing` et l’ancien registre `AnnotationProcessing` partagent les transactions et budgets du Core. Pour un hôte utilisant seulement les annotations, augmenter l’ancien budget d’opérations ou de source augmente aussi la valeur générale par défaut inchangée ; un registre ou budget général explicitement configuré reste prioritaire.

Les noms d’annotations respectent la casse par défaut. Pour accepter les variantes ASCII, passez `false` comme dernier argument de `RegisterAnnotation` ou définissez `FAnnotationDefinition::CaseSensitive=false` dans `CompileProcessing.AnnotationDefinitions`. Session utilise son registre de liaisons, un environnement de modules immuable son catalogue publié, et C++ AOT reçoit le catalogue via `FCodegenOptions::Bindings`. La règle s’applique aux abonnements des processeurs et à la réflexion à l’exécution ; `annotation_info.matches_name()` et `annotation_definition_info.case_sensitive()` l’exposent. L’orthographe source est conservée. Les noms d’arguments et les clés de métadonnées restent exacts. `@` est significatif pour `matches_name()` et `find_annotations()` ; `has_annotation()` accepte aussi son omission. Les inscriptions dont les règles se chevauchent sont rejetées ; une modification de règle invalide le cache de bytecode. Ces requêtes de réflexion exigent le runtime Core (VM, Mixed ou JIT hébergé) ; C++ AOT autonome les rejette.

`CommentRetention` contrôle les commentaires conservés ; la VM permet les requêtes d’annotations et de commentaires à l’exécution. Après cette modification du format des métadonnées, recompilez les consommateurs natifs et les paquets binaires avec des bibliothèques Core/C++ correspondantes. Voir `VeroScriptCompileProcessing.h`, `VeroScriptSourceMetadata.h` et l’exemple SDK `CppMinimal/main.cpp`.

Après une mise à niveau des SDK Core/C++, utilisez un nouveau répertoire de compilation ou recompilez tous les objets natifs. Avec MSVC localisé et Ninja sous Windows, exécutez `chcp 65001` dans l’invite de commandes développeur avant la configuration CMake et les compilations : le préfixe des inclusions détecté et la sortie du compilateur doivent utiliser le même encodage, sinon Ninja peut ignorer des modifications d’en-têtes.

## Extensions de compilation en script

`FEngineConfig::ScriptCompileExtensions` configure les scripts de compilation ; Session expose `SetScriptCompileExtensions`/`GetScriptCompileExtensions`. Définissez `Roots`, l'éventuel `RegistrationEntry`, `Configuration`, les sources en mémoire et les chemins de bibliothèque avant le chargement. `import compiler "builder.vs";` est autorisé par défaut ; `bAllowSourceImports=false` désactive cette entrée. Déployez le fichier `compiler.vs` correspondant au SDK.

Les extensions s'exécutent dans une VM isolée sur le thread de compilation, avec un état conservé entre les phases. Le code métier généré utilise le backend choisi. Les liaisons métier ne sont pas automatiquement accessibles : fournissez explicitement les `Bindings` de compilation et maintenez-les valides pendant la compilation. Les callbacks de compilation ne doivent ni exécuter ni modifier la même Session. L'autorisation des callbacks natifs reste à la charge de l'hôte.

Un `ExecutionControl` d'extension nul hérite du contrôleur d'exécution Engine/Session. Dans Session, il suit `SetExecutionControl` ; un contrôleur explicite reste indépendant, même s'il était initialement identique. Les setters concernent les compilations et rechargements suivants. `MaxInstructions`, `TimeoutMilliseconds` et les limites de chemins et de transfert sont distincts des limites métier. L'annulation coopérative ne peut pas interrompre un callback natif.

L'identité, la configuration, les sources, les résultats générés et les dépendances déclarées participent à la validation du cache. Lisez les entrées via les services de dépendances du compilateur. Les liaisons natives supplémentaires interdisent la réutilisation, sauf déclaration explicite de déterminisme avec un `BindingConfiguration` stable. Un échec de compilation/rechargement conserve le programme actif. Après une mise à niveau, recompilez intégralement les consommateurs natifs et paquets binaires avec les bibliothèques Core/C++ correspondantes. `CppMinimal` vérifie les extensions dans Session et AOT.

`GetScriptCompileExtensions` renvoie les options configurées en conservant un contrôleur hérité nul. Lire les options, modifier d'autres champs puis les réappliquer préserve l'héritage.

## Environnements de modules et extensions natives

Définissez `FEngineConfig::ModuleEnvironment` avec un instantané immuable créé par `FModuleEnvironment::Create` avant toute vérification, compilation ou exécution. Engine et Session utilisent ses modules, portées de dépendances, ressources, politiques de compilation et contrats natifs. Un module en mémoire peut être passé à `CheckFile`/`RunFile`/`LoadFile` par son chemin canonique `veroscript-module:/...`. Les imports sont autorisés par l'instantané ; installer ne signifie pas importer les déclarations.

Si l'environnement fournit `Bindings`, `GetBindingCatalog`, l'export et la vue de débogage Session exposent ce catalogue. Assemblez les bindings avant publication dans un même registre temporaire (ou le registre de base de `FNativeExtensionRegistry`) pour préserver les IDs de types. Session refuse les bindings locaux conflictuels et les nouveaux enregistrements ; `GetBindingRegistry` lance `std::logic_error` si le catalogue appartient à l'environnement publié. Sans catalogue, les enregistrements locaux Session restent disponibles.

L'environnement ne peut pas être remplacé tant qu'un programme est chargé. Terminez l'exécution, appelez `Clear`, puis appliquez la configuration et rechargez. Une configuration refusée par validation conserve le programme et les paramètres précédents ; un échec de compilation conserve le programme actif. Les exceptions d’`ApplyConfig` suivent son [contrat de résultat](#apply-config-result). Les callbacks et objets natifs échappés retiennent le code de l'extension ; libérez tâches, handles appelables et objets avant les ressources hôtes. Chargez les bibliothèques avec l'API explicite `VeroScriptNativeExtensionLoader.h` ; recompilez consommateurs et modules avec le même Core/C++ SDK et le CRT partagé. JIT garde son protocole distinct.

La compilation avec environnement utilise directement la préparation Core et ne consulte pas le cache persistant de bytecode Session/Engine. L'AOT de script pur prend en charge cet instantané ; les contrats natifs d'exécution sont refusés en attendant un adaptateur pour le runtime cible.

`GetModuleEnvironment` retient l'instantané courant ; le `ModuleEnvironment` emprunté de la vue de débogage expose sources en mémoire et contrats des fournisseurs. Les overlays explicites sont prioritaires. Ces données décrivent les entrées de compilation, pas une copie figée de la source principale chargée ; l'hôte publie les révisions réelles de `LoadSource`.

## API numériques exactes

SetNumericLimits/GetNumericLimits de Session configure FNumericLimits. VeroScriptNumerics.h fournit FBigInteger/FRational immuables et Numerics::Try* contrôlés avec FNumericOperationContext (Limits/Work). Zéro choisit des défauts finis ; travail/annulation/erreurs/sorties conservées sont explicites. AOT autonome refuse actuellement ces opérations exactes Core.

## Nettoyage borné après interruption

<a id="bounded-interruption-cleanup"></a>

Une limite d’instructions, un délai dépassé ou une annulation arrête l’exécution ordinaire. Core exécute les blocs `finally` des portées entrées et les destructeurs nécessaires avec une réserve distincte partagée de **16 384 instructions logiques et 50 ms**. Les appels synchrones imbriqués et les VM temporaires partagent cette réserve. La limite initiale et le jeton d’annulation ne refusent plus immédiatement le nettoyage ; les autorisations restent applicables. Le nettoyage ne peut pas créer de tâches, attendre une saisie console ni remplacer l’erreur initiale par `exit`.

La première cause reste l’erreur principale. Les erreurs secondaires figurent dans `FRunResult.Diagnostics` et `VeroScriptVm::GetCleanupDiagnostics()`. Après un nettoyage réussi, l’hôte peut lever les limites ou réinitialiser le jeton avant un nouvel appel. Les modifications métier ne sont pas annulées.

Un nettoyage incomplet, une erreur non interceptée ou l’épuisement de la réserve rend `VeroScriptVm::RequiresReset()` et `VeroScriptSession::RequiresReset()` vrais. Si l’interruption survient dans un finally ou un destructeur déjà en cours, le nettoyage incomplet n’est pas rejoué et impose une réinitialisation. Les nouvelles exécutions et opérations sur l’état conservé échouent avec `diag.execution_state_reset_required`. Appelez `Session.Clear()`, puis chargez un nouveau programme. Clear abandonne le nettoyage de script inachevé de cet état inutilisable et libère les ressources natives ; il ne réinitialise pas le jeton de l’hôte. Une VM autonome et son état Core conservé doivent être abandonnés avant réutilisation. Le délai reste coopératif et ne peut pas interrompre de force un rappel hôte bloquant. VM, Mixed, JIT et les appels AOT applicables partagent ce contrat Core. Si la restauration d’un rechargement reste incomplète ou si des racines externes empêchent `Clear()`, vérifiez `RequiresReset()` et `IsLoaded()` après Clear. Recréez la Session si elle ne peut être réinitialisée et ne reprenez pas l’exécution de l’instance inutilisable.

## Arguments de sélection des backends de Session

`VeroScriptSession::SetVmBackendPath` / `SetJitBackendPath`

Ces setters Session acceptent un fichier/dossier compatible ou un ID installé si l’environnement fourni contient son résolveur. `auto` demande les candidats de l’environnement et peut utiliser le repli produit conservé pour la VM. `none` ou une chaîne native vide désactive la sélection ; `""` donnent cette requête vide. Sur une nouvelle session, omettre un choix de backend permet la sélection par l’environnement ; cela diffère d’un effacement explicite. Lors de la reconfiguration d’une session existante, omettre le choix VM conserve son domaine d’exécution. Un échec explicite conserve le backend précédent. Utilisez le thread créateur au repos ; modifiez la VM avant exécution, car Clear/reload ne libère pas son domaine. Le choix JIT reste indépendant de la politique VM/Mixed/JIT.

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` fournit `FallbackVmBackendFactory` ; `VmBackendFactory` / `VmBackendPath` explicites sont prioritaires. Session conserve ce repli pour permettre `SetVmBackendPath("auto")` avant l’exécution.

## Exécution persistante et règlement du rechargement

Les fonctions ordinaires d’une Session persistante peuvent utiliser le backend Mixed/JIT sélectionné. Les plages d’initialisation conservées, le débogage et le nettoyage borné restent sur la VM. Les opérations natives non prises en charge se replient avant leur entrée ; les règles d’instructions, délai, annulation, permissions et profondeur restent identiques.

Des liaisons natives et rappels stables peuvent réutiliser le code d’une génération. `IHostBindingCatalog::GetRevision()` publie une révision d’exécution ; zéro exclut les catalogues personnalisés contenant des rappels natifs. `FHostBindingRegistry` la met à jour automatiquement. Appelez `InvalidateRuntimeBindings()` lorsqu’un changement externe affecte le code ou les propriétaires de durée de vie. Les données métier normalement lues par un rappel ne demandent pas de révision. Ne modifiez pas le catalogue pendant l’exécution. Les méthodes de configuration des rappels Session invalident le code conservé ; chaque appel reçoit les services actuels.

`RetainMigratableValue(value, root, error)` inscrit explicitement le graphe détenu par l’hôte dans la migration des variables globales. Objets, conteneurs, alias, cycles et fermetures partagent les mêmes règles de compatibilité. `root.Read()` renvoie null pendant l’initialisation en attente, puis redevient disponible après validation ou restauration terminée. N’utilisez pas les handles bruts copiés pendant cette attente : ils ne sont pas automatiquement relocalisés. Les racines ordinaires bloquent toujours le remplacement, les racines de génération expirent, et Load/Clear exigent la libération des racines migrables. Le rechargement des sources exige `SetHotReloadEnabled(true)` ; les rechargements transactionnels de paquet/application utilisent aussi ce service de migration.

Lisez `GetReloadStatus()` sur le thread propriétaire. `OperationId` est indépendant de `PreviousGeneration`, `CandidateGeneration`, `CurrentGeneration` et `CommittedGeneration` : un numéro candidat peut être réutilisé après annulation. `Rejected` signifie non appliqué ; `Pending`, préparé ; `RolledBack`, restauration terminée ; `Applied`, initialisation validée ; `AppliedWithErrors`, échec ultérieur du traitement métier ou du nettoyage des valeurs retirées. `ResetRequired` concorde avec `RequiresReset()` et interdit de poursuivre ; recréez la Session si la restauration ou le nettoyage ne peuvent aboutir. Le résultat décrit la dernière demande de rechargement acceptée. Les effets natifs de l’hôte ne sont pas annulés.

Core, l’intégration C++ et Application prennent en charge les chemins UTF-8 C++17/C++20 via `PathUtil::FromUtf8` / `ToUtf8`, sans désactiver `char8_t`. Même si les versions produit sont identiques, reconstruisez les backends et modules natifs avec la même implémentation Core.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/embedding.md) · [fr-FR](embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](../zh-TW/embedding.md)
