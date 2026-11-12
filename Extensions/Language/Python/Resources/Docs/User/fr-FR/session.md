# Sessions Python et exécution

Vider une session ou désactiver le rechargement à chaud peut exécuter des destructeurs du script et des rappels Python. Les exceptions de rappel sont immédiatement signalées par `CallbackError` ; le nettoyage ou les modifications de configuration déjà effectués restent appliqués.

Fermer une session ouverte pendant une opération ou un rappel actif lève `Error` et la laisse utilisable. Fermez-la après le retour de l’opération externe. Pendant la finalisation, `closed` vaut déjà vrai et répéter `close()` est sans effet.

## Créer une session et interpréter les résultats

Utilisez `Session(...)` comme gestionnaire de contexte et fermez la session sur
son thread de création. `load_source`, `load_file`, `load_package` et
`reload_package` renvoient un `RunResult` ; `run(RunMode.SCRIPT)` appelle
`main`, tandis que `RunMode.EVAL` utilise l'évaluation incrémentale.

Un résultat expose `success`, `exit_requested`, `exit_code` et `diagnostics`.
Un code de sortie non nul peut rester une exécution réussie. Les appels qui
renvoient une valeur lèvent `VeroScriptError` pour un résultat en échec ;
consultez `VeroScriptError.result`. Les erreurs de transport, de type, de thread
et de callback utilisent respectivement les exceptions spécialisées du module.

## Noms publics importants

Les accès d'état exacts sont `is_loaded`, `get_global(name)` et
`set_global(name, value)`. `load_source`, `load_file`, `load_package` et
`reload_package` renvoient un `RunResult`; `call`, `construct`, `call_method`,
`get_field` et `set_field` lèvent `VeroScriptError` si l'opération échoue.

`Session` regroupe les chemins d'import, les bindings, l'entrée, les diagnostics et les ressources d'une exécution. Utilisez-la depuis son thread de création et appelez `close()` ou utilisez le gestionnaire de contexte.

## Configuration

Les options comprennent `backend` (`Backend.VM`, `Backend.MIXED` ou `Backend.JIT`), `import_paths`, `implicit_import_paths`, `script_args`, `defines`, `stdlib_search_paths`, `source_overlays`, `bytecode_cache_directory`, `use_bytecode_cache`, `max_call_depth`, `max_instructions`, `timeout_milliseconds`, `enable_execution_control`, `capability_policy_enabled`, `allowed_script_capabilities`, `allow_circular_imports`, `allow_project_wide_access`, `hot_reload_enabled`, `print_callback` et `diagnostic_callback`.

`load_source`, `load_file` et `load_package` chargent le contenu ; `reload` et `reload_package` le rechargent ; `clear` réinitialise l'état ; `close` libère la session.

`max_instructions` et `timeout_milliseconds` sont des limites coopératives par exécution ; `0` signifie sans limite. L'une ou l'autre limite, ou `enable_execution_control=True`, utilise le chemin des points de sécurité de la VM, même avec un backend `MIXED` ou `JIT`. Une limite ou une demande d'annulation ne termine pas de force un rappel hôte natif déjà en cours.

Avec `enable_execution_control=True`, appelez `request_cancellation()` depuis n'importe quel thread Python pour demander l'annulation de l'exécution active. Gardez la session en vie jusqu'au retour de la demande. La demande reste active pour cette session ; créez une nouvelle `Session` avant de lancer une autre exécution.

Avec `capability_policy_enabled=True`, les effets de bord de la bibliothèque standard sont limités aux bits de `allowed_script_capabilities` : lecture/écriture/suppression de fichiers, lecture/écriture de l'environnement, entrée/sortie de console, création de tâches et changement du répertoire du processus. `chdir` exige `ENVIRONMENT_WRITE` et `PROCESS_DIRECTORY_CHANGE`. La politique ne couvre que les fonctions intégrées ; l'hôte doit autoriser ses propres rappels. Lorsqu'elle est désactivée, ces fonctions sont autorisées par défaut.

## Appels et résultats

`run` exécute le point d'entrée. `call`, `construct`, `call_method`, `get_field`, `set_field`, `get_global` et `set_global` offrent les opérations structurées ; `has_function` et `has_method` permettent de vérifier une cible.

`RunResult` fournit le succès, la demande de sortie, le code de sortie et les diagnostics structurés. Le succès décrit l'opération ABI et ne doit pas être déduit du seul code de sortie.

## Performance et concurrence

`Backend.MIXED` choisit automatiquement les niveaux d'exécution ; `Backend.JIT` les prépare plus agressivement et revient à la VM si nécessaire. `set_jit_policy` configure le seuil d'appel chaud, les tentatives, le cache et le budget de compilation ; `jit_stats` expose les compteurs correspondants. Il contient notamment `direct_native_calls`, qui compte les appels JIT à portée de génération ayant atteint une autre entrée native sans passer par le pont de valeurs de la VM.

`jit_stats` est un instantané indépendant du dernier appel. Les entrées
réellement exécutées sont comptées par `native_leaf_executions`,
`native_cfg_executions`, `bridge_executions`, `linear_executions`,
`vm_trampoline_executions`, `controlled_native_executions` et `aot_executions`,
même si elles échouent ou transfèrent le contrôle. Les compilations et les
entrées en cache sont comptées séparément ; les appels `direct_native_calls`,
les chemins scalaires et les boucles typées sont des événements imbriqués,
pas des entrées de fonction supplémentaires. En VM, ces compteurs d'entrée
restent à zéro. Chaque appel les réinitialise, même en réutilisant du code
compilé ; `clear()` réinitialise l'instantané de session. `jit_cache_stats`
conserve les totaux cumulés de compilation et de ressources du contexte réutilisable.

<a id="session-jit-cache"></a>

`JitCacheLimits` gère le code conservé entre appels ordinaires admissibles de `Session.call`/`CallFunction` natif, indépendamment du cache de bytecode. `JitPolicy.reuse_session_code` vaut `True` par défaut. Zéro pour `max_compile_attempts` ou `max_cached_entries` signifie un nombre illimité, y compris dans le contexte conservé. La réutilisation exige une fabrique JIT compatible et une stratégie effective qui autorise le JIT. Les sessions persistantes et les rappels stables peuvent réutiliser le code. Les catalogues de liaisons natives doivent fournir une révision à l’exécution. Une exécution gouvernée peut réutiliser un contexte dont les exigences de gouvernance correspondent.

`max_native_bytes` vaut par défaut 8 Mio et `max_retained_payload_bytes` 32 Mio ; zéro interdit la rétention de la ressource concernée. Utilisez `session.set_jit_cache_limits(...)`, `session.jit_cache_limits` et `session.jit_cache_stats`. Les statistiques sont cumulées et stabilisées par contexte, pas des mesures en direct ou à l’échelle du processus. Changer stratégie, limites, configuration, module, backend ou frontière hôte observable invalide le contexte ; une limite de ressources entraîne un repli sûr.

`Backend.MIXED` gouverné utilise la VM par défaut. `session.set_jit_policy(JitPolicy(enable_controlled_mixed=True))` active les entrées natives contrôlées compatibles ; `Backend.JIT` explicite est indépendant de cet indicateur. Les entrées non prises en charge se replient avant exécution. Le débogage, l’initialisation avec état conservé et le nettoyage borné utilisent la VM.

`set_input` définit l'entrée, et `set_hot_reload_enabled` contrôle le rechargement à chaud. Les conteneurs renvoient des snapshots : modifier une liste ou un dictionnaire Python ne modifie pas le conteneur du script. Les sessions et valeurs mutables ne doivent pas être partagés entre threads.


Vérifiez `CAPABILITIES & RuntimeCapabilities.RELOAD_STATUS` avant de lire `session.reload_status` sur le thread créateur. L’instantané `ReloadStatus` identifie le rechargement par `operation_id` et distingue `current_generation` de `committed_generation`. `PENDING` indique que le programme candidat attend son initialisation. `REJECTED` préserve le programme précédent et `ROLLED_BACK` confirme sa restauration. `APPLIED_WITH_ERRORS` indique que le candidat a été validé avant un échec d’exécution ou de nettoyage. `RESET_REQUIRED` bloque la poursuite de l’exécution. Recréez la session si `clear()` ne peut pas la réinitialiser. Des valeurs externes encore détenues peuvent bloquer le rechargement et préserver l’état précédent.

## Sélection de la VM

Le paquet Python standard inclut la VM bytecode statique : aucune bibliothèque VM externe n'est nécessaire pour une session ordinaire. `RuntimeCapabilities.VM_BACKEND_SELECTION` indique la prise en charge de l'API de sélection. Sur le thread créateur, lorsque la session est inactive et avant la première exécution, `set_vm_backend(path)` sélectionne un fichier ou répertoire VM compatible ; `None` ou une chaîne vide efface la sélection. Un échec conserve la VM précédente. L'exécution fige la sélection, même après `clear()` ; créez une nouvelle session pour la changer. Ce choix est indépendant de la politique `Backend.VM`, `MIXED` ou `JIT`.

`get_vm_backend_info()` renvoie un instantané autonome `VmBackendInfo` contenant `id`, `version`, `target`, `library_path`, `capabilities`, `selected` et `frozen`. Une VM statique a un `library_path` vide ; `selected` la distingue d'une absence de VM. L'instantané reste valide après remplacement ou fermeture de la session.

## Sélection JIT explicite

Stratégie d’exécution et sélection de backend sont distinctes. Sans environnement de modules installé, runtime ne fournit aucun module par défaut : mixed utilise VM sans sélection, jit forcé échoue avant exécution. Sélectionnez un fichier/dossier installé séparément et compatible avec `Session.set_jit_backend(path)`, Session inactive sur son thread propriétaire. `set_jit_backend(None)` / `set_jit_backend("")` désactive. Un remplacement échoué préserve le précédent. `get_jit_backend()` / `get_jit_backend_info()` indique identité/capacités réelles ; `backend-services` est seulement le contrat Core. Les appels admissibles dans une session persistante peuvent utiliser le backend sélectionné. Consultez le guide JIT pour découverte et ABI.

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/fr-FR/index.md)

```python
with Session(backend=Backend.MIXED) as session:
	session.set_jit_backend("/absolute/path/to/Jit")
	session.load_source("int main(){ return 0; }")
	result = session.run()
```

## Évaluation ponctuelle et capacités du backend

Dans les sessions ordinaires, `run(RunMode.EVAL_ONCE)` exécute l’initialisation du module une fois par appel, sans invoquer `main`, et permet sa compilation par le backend JIT sélectionné. `RunMode.EVAL` conserve le chemin incrémental ; `RunMode.SCRIPT` invoque aussi `main`. `RuntimeCapabilities.EVAL_ONCE` annonce ce mode. Les sessions de rechargement à chaud conservent les variables globales initialisées et utilisent la VM pour l’initialisation avec état conservé.

`get_jit_backend_info()` retourne un snapshot possédé `JitBackendInfo`. Son masque `JitBackendCapabilities` expose `NATIVE_CODE`, `PORTABLE_EXECUTION`, `NATIVE_TYPED_LOOPS` et `DIRECT_SCALAR_CALLS`. Il décrit le module sélectionné ; `CAPABILITIES` décrit les services Core. Le snapshot reste valide après remplacement du backend ou fermeture de la Session.

## Services de compilation et gravité des diagnostics

`COMPILE_CAPABILITIES` et `CompileCapabilities.ANNOTATION_PROCESSING` décrivent les services de compilation indépendamment de `CAPABILITIES` et du backend JIT sélectionné. `Diagnostic.severity` utilise `DiagnosticSeverity` : `ERROR`, `WARNING`, `INFORMATION` ou `HINT`. Consultez `RunResult.success` pour déterminer le succès ; une liste de diagnostics non vide peut contenir uniquement des messages non fatals.

`session.set_annotation_processing_service(handle)` attache un handle opaque valide créé par une extension SDK native lorsque la session est inactive. La session copie sa configuration ; le producteur peut ensuite libérer son handle. Conservez le module natif du processeur et les données de callback jusqu'à la suppression du service ou la fermeture de la session. `None` ou `0` supprime le service. Une adresse entière arbitraire n'est pas un handle valide. `register_annotation(...)` déclare le vocabulaire des annotations et n'installe pas de processeur de compilation.

## Intégration du terminal hôte

`Session(console_query=..., console_control=..., console_write=...)` propose des callbacks de terminal. La requête renvoie `ConsoleInfo` (dimension 0 : inconnue). Le contrôle reçoit `ConsoleOperation` et deux entiers : couleurs ANSI 0..15/défaut -1, ligne after/before/all=0/1/2, colonne/ligne à partir de zéro, visibilité 0/1. Renvoyez `True` après succès synchrone. Le texte exact sans saut de ligne renvoie `bool` ; sans callback, la sortie par lignes reste en tampon. Conservez/synchronisez les callbacks jusqu’à la fermeture ; les exceptions deviennent `CallbackError`. Coordonnez les séquences entre tâches.

## Environnements de modules résolus et traitement général

`Session(module_environment=handle)` accepte un handle opaque créé par `VeroScript::Interop::CreateModuleEnvironment` dans le SDK natif correspondant. La session conserve cet environnement immuable ; le producteur peut libérer son handle après la construction. L’environnement définit la propriété des sources, les imports exacts, les ressources, les services de compilation et les dépendances d’exécution. Il doit fournir les racines et la propriété de la bibliothèque standard si nécessaire ; Python n’ajoute pas sa racine intégrée dans ce mode. Créez une nouvelle session pour remplacer l’instantané. Un catalogue de liaisons détenu par l’environnement interdit les enregistrements locaux : combinez les liaisons avant publication. `None` ou `0` utilise la configuration ordinaire ; une adresse entière arbitraire est invalide. Vérifiez la disponibilité avec `RuntimeCapabilities.MODULE_ENVIRONMENT`.

`set_compile_processing_service(handle)` connecte un service de traitement général du SDK natif, avec les mêmes règles d’inactivité, de thread créateur et de durée de vie que le service d’annotations. `None` ou `0` le retire. Sa capacité est `CompileCapabilities.GENERAL_PROCESSING`. Configurez les extensions de compilation de scripts avec `set_script_compile_extensions(...)`.

## Fermetures et variables statiques locales

Les valeurs de fonction du script prennent en charge les captures initialisées et
`mutable`. `Value.clone()` et `retain_callable()` partagent l'environnement existant ;
évaluer à nouveau la lambda crée un environnement indépendant. Les captures par
référence conservent leur stockage après le retour de la fonction créatrice. La
fermeture de la session invalide les objets appelables retenus.

Les variables locales `static` appartiennent à un état d'exécution. Les appels
ordinaires à `call()` créent un nouvel état ; `hot_reload_enabled=True` conserve
l'état entre les appels. Fermer la session libère l'état statique qu'elle conserve.
La sortie [Python AOT](aot.md) autonome refuse les lambdas et les déclarations locales `static`.

## Arguments de sélection des backends de Session

`Session.set_vm_backend` / `set_jit_backend`

Ces setters Session acceptent un fichier/dossier compatible ou un ID installé si l’environnement fourni contient son résolveur. `auto` demande les candidats de l’environnement et peut utiliser le repli produit conservé pour la VM. `none` ou une chaîne native vide désactive la sélection ; `None` / `""` donnent cette requête vide. Sur une nouvelle session, omettre un choix de backend permet la sélection par l’environnement ; cela diffère d’un effacement explicite. Lors de la reconfiguration d’une session existante, omettre le choix VM conserve son domaine d’exécution. Un échec explicite conserve le backend précédent. Utilisez le thread créateur au repos ; modifiez la VM avant exécution, car Clear/reload ne libère pas son domaine. Le choix JIT reste indépendant de la politique VM/Mixed/JIT.

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/session.md) · [fr-FR](session.md) · [ja-JP](../ja-JP/session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
