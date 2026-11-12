# Sessions C# et runtime

## Modes d’exécution

`Run(VeroScriptRunMode.Script)` initialise le module et appelle `main` ; `Eval`
conserve l’évaluation incrémentale. `EvalOnce` exécute l’initialisation sans `main`
et autorise son exécution par le backend JIT sélectionné. Une session ordinaire
initialise un nouvel état à chaque exécution. Le rechargement à chaud conserve
les variables globales initialisées et utilise la VM pour l’initialisation de l’état conservé. Ce mode exige
`VeroScriptCapabilities.EvalOnce` ; un ancien runtime lève `NotSupportedException`
avant l’exécution. La politique d’exécution et le backend JIT se configurent séparément.

## Statistique de boucle typée

`NativeTypedLoopExecutions` compte les invocations de noyaux natifs de boucles typées, y compris une invocation qui se termine après avoir signalé une erreur runtime visible dans le source. C’est un compteur d’observabilité d’exécution, pas un compteur de succès.

## Capacités et compteurs JIT natifs

`NativeLeafExecutions`, `NativeCfgExecutions`, `BridgeExecutions`, `LinearExecutions`
et `VmTrampolineExecutions` comptent les entrées effectives dans les exécutables
ordinaires de la dernière exécution, y compris les sorties sur erreur ou transfert
de contrôle. Les propriétés `*Entries` correspondantes comptent les artefacts générés.
Un cache chaud peut exécuter sans compiler ; `GetJitCacheStats().CompileAttempts`
reste cumulatif pour le contexte de cache conservé. `ScalarNativeFastPathCalls`
compte les appels natifs de l’hôte ; les appels directs et les noyaux de boucles typées
sont des événements imbriqués, à ne pas additionner aux entrées ordinaires. Les anciens
runtimes compatibles renvoient zéro pour les compteurs absents. Ces statistiques sont
agrégées ; les diagnostics JIT facultatifs par fonction restent dans l’API native Core/C++.

`CostModelVmFallbacks` est le sous-ensemble de `Fallbacks` choisi lorsque le
chemin compilé valide est prédit plus coûteux que la VM : ce n’est ni un échec
de compilation ni un changement de sémantique. `NativeTypedLoopExecutions`
compte les invocations de noyaux de boucles typées du backend natif, y compris
celles qui signalent une erreur runtime visible dans le source ; ce n’est pas un
compteur de succès. Testez séparément `session.GetJitBackendInfo().CapabilityFlags` avec
`VeroScriptJitBackendCapabilities.NativeCode` et `VeroScriptJitBackendCapabilities.NativeTypedLoops`.
Ne déduisez aucune de ces capacités de `HostPlatform` ni d’un compteur non nul.

## Déploiement et métadonnées du runtime

Référencez `VeroScript.CSharp` et déployez une bibliothèque native
`VeroScriptInterop` correspondant au système d'exploitation et à l'architecture
du processus. Le package managé fournit des assets .NET 8 et .NET 9, mais
n'installe ni runtime .NET ni assets natifs RID. Si la bibliothèque ne se trouve
pas dans un répertoire de chargement sûr, définissez
`VEROSCRIPT_INTEROP_LIBRARY` avec un chemin absolu ; les chemins relatifs sont
refusés.
Définissez ce chemin avant le premier appel au SDK. Les appels suivants réutilisent la bibliothèque native sélectionnée.

`VeroScriptRuntime` expose `AbiVersion`, `RuntimeVersion`, `HostPlatform`,
`Capabilities`, `CurrentLanguage` et les catalogues de langues disponibles.
Utilisez ces informations pour vérifier la compatibilité et non des hypothèses
sur la plateforme. Les codes et arguments de diagnostic restent stables quand
la langue d'affichage change.

`VeroScriptSession` possède la configuration, les valeurs, les bindings et les diagnostics. Il est lié à son thread de création et doit être libéré par `Dispose`.

`VeroScriptConfiguration` comprend `ImportPaths`, `ImplicitImportPaths`, `ScriptArguments`, `Defines`, `PredefinedMacros`, `StandardLibrarySearchPaths`, `SourceOverlays`, `BytecodeCacheDirectory`, `UseBytecodeCache`, `MaxCallDepth`, `MaxInstructions`, `TimeoutMilliseconds`, `EnableExecutionControl`, `CapabilityPolicyEnabled`, `AllowedScriptCapabilities`, `ExecutionBackend`, `AllowCircularImports` et `AllowProjectWideAccess`.

`LoadSource`, `LoadFile`, `LoadPackage`, `Reload`, `ReloadPackage`, `BuildPackageFile` et `Clear` gèrent le contenu. `Run`, `CallFunction`, `CallMethod`, `Construct`, `GetField`, `SetField`, `GetGlobal` et `SetGlobal` exécutent ou interrogent le script.

`SetInput` fournit le tampon utilisé par les fonctions d'entrée de la bibliothèque standard lors de l'exécution suivante.

Utilisez `HasFunction` et `HasMethod` avant les appels facultatifs. Activez
explicitement le rechargement de source conservant l'état avant `Reload` :

```CSharp
session.HotReloadEnabled = true;
var reloaded = session.Reload();
```

Appelez `GetReloadStatus()` sur le thread créateur après `Reload`, `ReloadPackage` ou l’exécution qui initialise le remplacement. `Pending` attend l’initialisation, `RolledBack` rétablit la génération précédente et `AppliedWithErrors` indique que le remplacement a été validé avant une erreur ultérieure. `CommittedGeneration` et `OperationId` identifient le résultat. `ResetRequired` impose d’appeler `Clear` et de recharger le programme. Recréez la session si elle ne peut pas être vidée. La lecture préserve les diagnostics et exige `VeroScriptCapabilities.ReloadStatus`.

Si un résolveur de backend refuse `ApplyConfiguration`, la méthode lève `VeroScriptException` avec la cause native et conserve la politique d’annulation précédente. Une modification refusée de `HotReloadEnabled` lève aussi une exception. Consultez `GetReloadStatus()` et suivez les instructions de récupération indiquées. Un échec d’annulation signale l’opération d’annulation et son statut natif. Lorsque le contrôle d’exécution est désactivé, l’erreur native précédente reste consultable depuis le thread créateur.

`VeroScriptRunResult` sépare `Success`, `ExitRequested`, `ExitCode` et les diagnostics. Utilisez `GetDiagnostics`, `GetDiagnosticArguments` et `FormatDiagnostic` plutôt que d'analyser un texte d'erreur.

`MaxInstructions` et `TimeoutMilliseconds` sont des limites coopératives par exécution ; zéro signifie sans limite. La VM et les chemins JIT contrôlés admissibles conservent le même décompte des instructions, les limites, l'annulation et les restrictions de capacités. Une limite ou une demande d'annulation ne termine pas de force un rappel hôte natif ou managé déjà en cours.

Sous gouvernance, `Mixed` utilise la VM par défaut. Activez les chemins JIT contrôlés admissibles avec `VeroScriptJitPolicy.EnableControlledMixed` après avoir mesuré la charge cible. Le choix explicite de `Jit` est indépendant de ce réglage. Les entrées non prises en charge ou préférées en VM par le modèle de coût conservent un repli VM respectant la gouvernance. `ControlledExecution` indique le support du backend ; `ControlledNativeExecutions` et `AotExecutions` comptent les invocations réelles des entrées natives contrôlées et AOT.

Lorsque `EnableExecutionControl` vaut `true`, appelez `RequestCancellation()` pour demander l'annulation de l'exécution active. Cette méthode est thread-safe et constitue la seule opération de session appelable depuis un thread autre que le thread propriétaire. Gardez la session en vie jusqu'à son retour ; l'annulation est observée coopérativement aux points de sécurité.
Une nouvelle application réussie de la configuration réinitialise le jeton d'annulation de la session ; la session peut donc être préparée pour une autre exécution après une annulation.

Lorsque `CapabilityPolicyEnabled` vaut `true`, les effets de bord de la bibliothèque standard sont limités aux indicateurs de `AllowedScriptCapabilities` : `FileRead`, `FileWrite`, `FileDelete`, `EnvironmentRead`, `EnvironmentWrite`, `ConsoleInput`, `ConsoleOutput`, `TaskSpawn` et `ProcessDirectoryChange`. `chdir` exige `EnvironmentWrite` et `ProcessDirectoryChange`. La politique ne couvre que les fonctions intégrées ; l'hôte doit autoriser ses propres rappels. Lorsqu'elle est désactivée, ces fonctions sont autorisées par défaut.

`SetJitPolicy` règle seuil chaud, tentatives, cache et budget d'avance ; `GetLastJitStats` expose les compteurs, notamment `DirectNativeCalls`, qui compte les appels JIT à portée de génération contournant le pont de valeurs de la VM. VM, Mixed et JIT conservent la même sémantique. Le cache de code réutilisable de session est distinct du cache de bytecode. Si `VeroScriptRuntime.Capabilities` contient `SessionJitCache`, `SetJitCacheLimits`, `GetJitCacheLimits` et `GetJitCacheStats` règlent et observent le code retenu entre exécutions admissibles. `VeroScriptJitPolicy.ReuseSessionCode` vaut `true` par défaut. Un budget `MaxCompileAttempts` ou `MaxCachedEntries` nul signifie un nombre illimité ; les plafonds en octets du code natif et de la charge retenue restent applicables. La réutilisation exige un backend effectif autre que la VM et une révision stable des liaisons natives. Une modification des liaisons ou des callbacks invalide le cache. La gouvernance d’exécution permet la réutilisation d’un contexte dont les exigences correspondent. Les limites par défaut sont 8 Mio de code natif et 32 Mio de charge conservée totale ; zéro interdit la conservation de la ressource concernée. Les statistiques sont des valeurs cumulées stabilisées pour le contexte de cache courant, non des compteurs mémoire en direct ou à l’échelle du processus. Changer la stratégie, les limites, le backend, la configuration, la génération de module, les liaisons ou l’état de rechargement à chaud/observabilité invalide ce contexte ; une limite atteinte déclenche un repli sûr, sans changement de sémantique. Les sessions, valeurs et callables ne doivent pas traverser leur thread de création.

## Résultats d'exécution

`VeroScriptRunResult.Success` indique si la compilation ou l'exécution a réussi ; un code de sortie script non nul reste un succès. Les échecs de compilation et d'exécution sont normalement renvoyés dans le résultat avec `Success == false`. Un rejet de propriété natif peut aussi produire un résultat en échec avec des diagnostics structurés : vérifiez le résultat et `GetDiagnostics()`. Les autres échecs de statut natif lèvent `VeroScriptException` ; les contrôles managés d'arguments, de durée de vie et de compatibilité ABI lèvent leurs exceptions managées correspondantes.

## Backends et callbacks

`Vm` utilise uniquement l'interpréteur. `Mixed`, le backend par défaut, utilise le JIT pour les fonctions compatibles et revient à la VM pour les autres. `Jit` demande une compilation anticipée, mais n'interdit pas le repli vers la VM. Un débogueur attaché impose la VM. Le rechargement à chaud utilise la VM pour l’initialisation de l’état conservé. Les appels admissibles peuvent utiliser le backend JIT sélectionné.

Les sessions sont liées à leur thread créateur. Les callbacks script peuvent s'exécuter simultanément sur des workers VM ; synchronisez donc l'état managé partagé. Les exceptions des callbacks de sortie, de diagnostic et de mutation sont contenues. Les exceptions des fonctions hôte, intercepteurs, fabriques génériques et sérialiseurs deviennent des échecs de callback.

## Résumé de configuration

| Propriété | Rôle |
| --- | --- |
| `ImportPaths`, `ImplicitImportPaths` | Chemins de recherche d'import et imports implicites. |
| `ScriptArguments` | Arguments de `main(string[] args)`. |
| `Defines`, `PredefinedMacros` | Macros et définitions de compilation conditionnelle. |
| `StandardLibrarySearchPaths`, `SourceOverlays` | Racines de bibliothèque standard et sources en mémoire prioritaires. |
| `BytecodeCacheDirectory`, `UseBytecodeCache` | Répertoire et activation du cache de bytecode. |
| `MaxCallDepth` | Limite de récursion ; zéro utilise la valeur par défaut du runtime. |
| `MaxInstructions`, `TimeoutMilliseconds` | Limites coopératives par exécution ; zéro les désactive. |
| `EnableExecutionControl` | Active le mécanisme d'annulation coopérative. |
| `CapabilityPolicyEnabled`, `AllowedScriptCapabilities` | Active et renseigne les autorisations d'effets de bord de la bibliothèque standard. |
| `ExecutionBackend` | Choix entre `Vm`, `Mixed` et `Jit`. |
| `AllowCircularImports`, `AllowProjectWideAccess` | Règles d'import cyclique et d'accès au projet. |

## Sélection indépendante de la VM

Le produit Interop standard assemble explicitement la VM bytecode. Ce choix est
indépendant de `ExecutionBackend` et du JIT. Sur le thread créateur inactif,
`SetVmBackend(path)` sélectionne une bibliothèque compatible ou son répertoire ;
null ou une chaîne vide efface la sélection. Un remplacement échoué conserve le
choix précédent. `GetVmBackendInfo()` copie identité, version, cible, chemin,
capacités VM brutes et états de sélection/gel. Le chemin est vide pour une VM statique.
Les bits VM indiquent bytecode (1), gouvernance (2), débogage (4), rechargement à chaud (8)
et nettoyage (16), indépendamment des capacités du processus.

Vérifiez `VeroScriptCapabilities.VmBackendSelection` ; un ancien runtime de même ABI
sans cette fonction lève `NotSupportedException` avant l'appel des exports optionnels.
L'exécution engagée gèle le domaine, y compris le JIT et les extensions de compilation.
`Clear` et le rechargement ne le dégèlent pas : créez une nouvelle session pour changer
un choix gelé. Effacer ne rétablit pas la VM du produit et peut laisser l'exécution sans
interpréteur. Les instantanés managés restent lisibles après remplacement ou libération.

## Sélection JIT explicite

Stratégie d’exécution et sélection de backend sont distinctes. Sans environnement de modules installé, runtime ne fournit aucun module par défaut : mixed utilise VM sans sélection, jit forcé échoue avant exécution. Sélectionnez un fichier/dossier installé séparément et compatible avec `VeroScriptSession.SetJitBackend(path)`, Session inactive sur son thread propriétaire. `SetJitBackend(null)` / `SetJitBackend("")` désactive. Un remplacement échoué préserve le précédent. `GetJitBackend()` / `GetJitBackendInfo()` indique identité/capacités réelles ; `backend-services` est seulement le contrat Core. La gouvernance suit la stratégie JIT contrôlée décrite plus haut. Consultez le guide JIT pour découverte et ABI.

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/fr-FR/index.md)

```CSharp
using var session = new VeroScriptSession();
session.SetJitBackend("/absolute/path/to/Jit");
session.LoadSource("int main(){ return 0; }");
var result = session.Run();
```

<a id="jit-backend-capabilities"></a>

Interrogez `session.GetJitBackendInfo().CapabilityFlags` avec `VeroScriptJitBackendCapabilities` pour le backend sélectionné. `Capabilities` conserve le masque natif brut, y compris les bits inconnus. L’instantané possède ses chaînes et reste lisible après remplacement du backend ou libération de la session. Sans backend sélectionné, les indicateurs valent `None`. `VeroScriptRuntime.Capabilities` décrit les services Core, pas les fonctions natives du backend sélectionné.

<a id="diagnostic-translation"></a>

`VeroScriptRuntime.Translate(englishMessage)` traduit un diagnostic avec la langue et les catalogues actuels du processus. Les messages inconnus, y compris les caractères NUL intégrés, restent inchangés ; une entrée vide produit une chaîne vide. Les entrées nulles et le UTF-16 invalide sont rejetés. La liaison copie le tampon UTF-8 appartenant au module, renvoyé par `vs_interop_locale_translate`, dans une chaîne managée et le libère avec `vs_interop_buffer_destroy`, en cas de succès comme en cas d’échec.

## Libération des callables conservés

<a id="callable-disposal"></a>

`VeroScriptCallable.Dispose()` invalide immédiatement le wrapper managé. Pendant une invocation, la destruction native est différée jusqu’au retour de la dernière invocation active. La libération n’attend pas la fin de l’exécution native : un callback peut attendre qu’un autre thread libère ce wrapper. Les invocations d’un même wrapper restent sérialisées. Cette règle de durée de vie ne supprime ni les vérifications du thread propriétaire ni la sérialisation externe des accès à la session.

## Services d’annotations et sévérité des diagnostics

`VeroScriptRuntime.CompileCapabilities` indique les services de compilation indépendamment des capacités d’exécution et JIT. `AnnotationProcessing` permet d’utiliser `SetAnnotationProcessingService(nint)` avec un handle opaque valide fourni par une extension native. Configurez ou effacez le service sur le thread créateur lorsque la session est inactive ; `0` l’efface. La session copie la configuration : l’appelant peut libérer son handle après l’attachement. `ApplyConfiguration` conserve le service. Le code du processeur et les données des callbacks doivent rester valides jusqu’à leur dernière utilisation. Les anciens runtimes de même ABI renvoient `None` et rejettent cette opération avec `NotSupportedException`.

`GetDiagnostics()` conserve `Severity` : `Error`, `Warning`, `Information` ou `Hint`. Un nombre de diagnostics non nul ne signifie pas un échec de compilation : avertissements, informations et indications peuvent accompagner `Success == true`. Les diagnostics des anciens runtimes sans requête de sévérité restent lisibles avec `Error`.

## Rappels de console de l’hôte

`VeroScriptSessionOptions.ConsoleQuery` renvoie `VeroScriptConsoleInfo` : terminal de sortie, prise en charge des couleurs/du curseur/de l’effacement et dimensions visibles en colonnes/lignes. Les dimensions non positives sont inconnues. Les contrôles nécessitent la requête et `ConsoleControl`. Ce dernier reçoit `VeroScriptConsoleOperation` et deux entiers : couleurs ANSI premier plan/arrière-plan `0..15` ou défaut `-1` ; effacement after/before/all `0/1/2` ; colonne/ligne à partir de zéro ; visibilité `0/1` dans le premier argument. Les arguments inutilisés valent zéro.

`ConsoleWrite` reçoit le texte exact de `std::console::write`, y compris NUL, sans saut de ligne ajouté. Son absence conserve la sortie tamponnée `Print`. Renvoyer `true` uniquement après une réussite synchrone. Une exception de requête désactive les capacités ; une exception de contrôle indique un échec ; `false` ou une exception d’écriture fait échouer le script.

Les rappels restent référencés jusqu’à la destruction de la session et peuvent être concurrents sur les workers VM : synchroniser l’état partagé. Le runtime peut réinitialiser le style et afficher le curseur à la fin de l’exécution, même après échec ou annulation. Ne pas libérer la session ni rappeler ses opérations ordinaires depuis ces rappels. L’entrée terminal est indépendante de la sortie. Ces rappels exigent un runtime natif correspondant ; les anciens runtimes du même ABI conservent la sortie par lignes mais ignorent les rappels de console ajoutés.

Les scripts chargés via le SDK C# peuvent utiliser des alias primitifs dans la réflexion exécutable et les signatures de fonctions, y compris les conteneurs de valeurs imbriqués et les références. Ainsi, `array<long>` peut être demandé comme `array<int64>`. Cela ne relâche pas les contrôles de signe, largeur, identité d’objet ou référence constante. Le source et les packages sérialisés utilisent les mêmes contrôles Core.

Les rappels de protocole hôte (`Clone`, `Equals`, `Hash`, fabriques de sous-classes) restent référencés jusqu’à la libération de la dernière valeur native portant le protocole, même après la destruction de session et sur un autre thread. Ne pas accéder à une session détruite. Les rappels gérés exigent l’export natif facultatif de rétention ; les anciens runtimes du même ABI lèvent `NotSupportedException` avant enregistrement. Un échec de rétention après enregistrement ferme la session pour protéger les données. L’annulation d’une fabrique générique libère chaque référence transférée ou gérée une seule fois.

## Services de compilation généraux et environnements de modules

`SetCompileProcessingService(nint)` attache un pipeline du SDK natif et exige `CompileCapabilities.GeneralProcessing` ; `0` le supprime. Appelez-le sur le thread créateur d'une session inactive avec un handle opaque valide. La session copie la configuration ; l'appelant peut ensuite libérer son handle. `ApplyConfiguration` ordinaire conserve le service. Le code des processeurs et les données des callbacks doivent rester valides jusqu'à leur dernière utilisation.

`ApplyConfiguration(configuration, moduleEnvironment)` sélectionne explicitement un instantané natif immuable et exige `Capabilities.ModuleEnvironment`. `0` le supprime ; la surcharge à un argument conserve la sélection. Appelez-la sur le thread créateur d'une session inactive. Après succès, la session conserve l'instantané et l'appelant peut libérer son handle. Appelez `Clear()` avant de changer d'instantané. Un catalogue appartenant à l'environnement interdit les enregistrements locaux et les imports de catalogues ; préparez un instantané natif combiné. Les conflits avec les liaisons locales sont rejetés avant toute modification de configuration ou d'annulation. Gardez les modules natifs des fournisseurs, liaisons et processeurs chargés jusqu'à leur dernière utilisation. Les anciens runtimes incompatibles lèvent `NotSupportedException`.

## Arguments de sélection des backends de Session

`VeroScriptSession.SetVmBackend` / `SetJitBackend`

Ces setters Session acceptent un fichier/dossier compatible ou un ID installé si l’environnement fourni contient son résolveur. `auto` demande les candidats de l’environnement et peut utiliser le repli produit conservé pour la VM. `none` ou une chaîne native vide désactive la sélection ; `null` / `""` donnent cette requête vide. Sur une nouvelle session, omettre un choix de backend permet la sélection par l’environnement ; cela diffère d’un effacement explicite. Lors de la reconfiguration d’une session existante, omettre le choix VM conserve son domaine d’exécution. Un échec explicite conserve le backend précédent. Utilisez le thread créateur au repos ; modifiez la VM avant exécution, car Clear/reload ne libère pas son domaine. Le choix JIT reste indépendant de la politique VM/Mixed/JIT.

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

`VeroScriptRuntime.CompileCapabilities` utilise `VeroScriptCompileCapabilities`, séparément des capacités d’exécution. Un ancien runtime de même ABI sans cette requête renvoie `VeroScriptCompileCapabilities.None`. `GetJitBackendInfo().CapabilityFlags` utilise `VeroScriptJitBackendCapabilities`, dont `DirectScalarCalls` ; ces indicateurs décrivent la prise en charge du backend sans prouver une entrée native. `VeroScriptDiagnostic.Severity` classe chaque diagnostic structuré.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/session.md) · [fr-FR](session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
