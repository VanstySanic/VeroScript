# SDK d'interopérabilité

## Compatibilité des statistiques JIT

Pour `vs_interop_session_get_last_jit_stats`, un `struct_size` nul est la forme héritée et n’écrit que le préfixe original des statistiques ; les appelants actuels passent `sizeof(vs_interop_jit_stats)` pour recevoir les compteurs ajoutés. `native_typed_loop_executions` compte les invocations de noyaux natifs de boucles typées, y compris une invocation qui se termine après avoir signalé une erreur runtime visible dans le source. Ce n’est pas un compteur de succès.

`native_leaf_executions`, `native_cfg_executions`, `bridge_executions`, `linear_executions` et `vm_trampoline_executions` comptent les entrées effectives dans les exécutables ordinaires, y compris celles qui se terminent par une erreur ou un transfert de contrôle. Les champs `*_entries` correspondants comptent les artefacts compilés. Ces compteurs concernent la dernière exécution ; un accès au cache peut exécuter du code sans compilation. `scalar_native_fast_path_calls` compte uniquement les appels natifs rapides de l’hôte. Les appels directs et les boucles typées sont des événements imbriqués et ne doivent pas être additionnés comme des appels de fonction indépendants.

L’API C expose les statistiques agrégées. Les instantanés de diagnostic par fonction, les motifs de repli et les mesures de temps sont actuellement disponibles via Core/C++ ; la politique JIT C n’active pas cette collecte.

`VeroScriptInterop` est un ABI de processus indépendant du langage pour les adaptateurs C, Python,
C#, Rust et autres. Il expose les sessions, valeurs, appels, liaisons et paquets de VeroScript
sans dépendre de la disposition des objets C++ ni des types STL. Pour une intégration C++ directe,
consultez d'abord [Intégration hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/embedding.md). Pour une frontière interlangage ou une
bibliothèque dynamique stable, utilisez le SDK décrit ici.

Les signatures des fonctions, la disposition des structures, les constantes et la disponibilité
des fonctionnalités sont définies par l'en-tête public fourni. Cette page explique les contrats et
les modes d'utilisation ; consultez cet en-tête pour obtenir la liste complète des déclarations.

## ABI et installation

Le SDK fournit un en-tête public `VeroScriptInterop.h` valide en C11 et C++17 :

```c
#define VS_INTEROP_ABI_VERSION UINT32_C(0x00000101)
```

La version de référence est l'ABI 0.1.1. Les 16 bits supérieurs codent la version majeure,
les 8 bits suivants la version mineure et les 8 bits inférieurs le correctif.
Les bindings exigent une version ABI exactement identique. Les descripteurs et les structures de sortie
commencent par `struct_size` ; les révisions compatibles ajoutent des champs à la fin. Les vues de
chaînes et les paires clé/valeur n'ont intentionnellement pas de `struct_size`, car leur disposition
est fixée par l'en-tête.

Le SDK fournit la bibliothèque d'exécution, l'en-tête C11 et la cible d'importation CMake.
Un adaptateur n'a pas besoin des en-têtes C++ de VeroScript ni d'un ABI STL.

```cmake
find_package(VeroScriptInterop CONFIG REQUIRED)
target_link_libraries(MyAdapter PRIVATE VeroScript::VeroScriptInterop)
```

Sous Windows, déployez les DLL du runtime SDK, les ressources de localisation et les notices légales du runtime à côté de l’exécutable hôte. Conservez-les dans votre application distribuée ; copier uniquement `VeroScriptInterop.dll` omet des dépendances nécessaires. Obtenez séparément les exemples d’intégration via les références documentaires ou sources.

## Modèle objet opaque

L'interface utilise des poignées opaques. Les adaptateurs ne doivent pas inspecter leur disposition ou
mémoire libre appartenant au module avec l'allocateur système.

| Poignée | Objectif |
| --- | --- |
| `vs_interop_session` | Chargement, compilation, exécution et session d'appel isolés. |
| `vs_interop_value` | Valeur de script scalaire, objet, conteneur, fonction ou objet hôte. |
| `vs_interop_callable` | Script appelable conservé au-delà du rappel natif actuel. |
| `vs_interop_call_context` | Lit les arguments de rappel natifs, écrit les arguments de référence et effectue des appels imbriqués de rappel local. |
| `vs_interop_buffer` | Texte d'erreur appartenant au module, octets du paquet ou sortie de sérialisation ; libération avec `vs_interop_buffer_destroy`. |
| `vs_interop_package` | Paquet `.vsapp`/`.vslib` validé en mémoire. |
| `vs_interop_intercept_context` | Récepteur, cible résolue et arguments pour un intercepteur d'instance. |
| `vs_interop_generic_type_context` | Définit un type de valeur d'hôte générique concret, des membres et des protocoles. |
| `vs_interop_external_serialize_context` / `deserialize_context` | Contextes pour les rappels de sérialisation et de désérialisation d'objets externes. |

Le plus petit workflow C est « créer une session → appliquer la configuration → charger
source ou un fichier → exécuter → lire les diagnostics → détruire » :

```c
#include "VeroScriptInterop.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static vs_interop_string_view view(const char* text)
{
	vs_interop_string_view result;
	result.data = text;
	result.size = (uint64_t)strlen(text);
	return result;
}

int main(void)
{
	vs_interop_session* session = NULL;
	vs_interop_session_options options = {0};
	options.struct_size = sizeof(options);

	if (vs_interop_session_create(&options, &session) != VS_INTEROP_STATUS_OK)
	{
		return 1;
	}

	const vs_interop_string_view source = view(
		"int main()\n"
		"{\n"
		"\tprint(\"hello\");\n"
		"\treturn 0;\n"
		"}");
	vs_interop_run_result load = {0};
	load.struct_size = sizeof(load);

	const vs_interop_status load_status = vs_interop_session_load_source(
		session,
		source,
		view("main.vs"),
		&load);

	if (load_status != VS_INTEROP_STATUS_OK || !load.success)
	{
		vs_interop_session_destroy(session);
		return 2;
	}

	vs_interop_run_result run = {0};
	run.struct_size = sizeof(run);
	const vs_interop_status run_status = vs_interop_session_run(
		session,
		VS_INTEROP_RUN_SCRIPT,
		&run);

	const int result = run_status == VS_INTEROP_STATUS_OK && run.success ? 0 : 3;
	vs_interop_session_destroy(session);
	return result;
}
```

Le script est transmis via l'échappement de chaîne C. Les vrais scripts devraient suivre le
convention de projet d'indentation avec des tabulations, d'accolades sur leurs propres lignes et d'un
Point d’entrée `int main`.

## Statut, diagnostics et sémantique de sortie

Tous les appels renvoient des valeurs `vs_interop_status` explicites. Les statuts courants incluent :

| Constante | Signification |
| --- | --- |
| `VS_INTEROP_STATUS_OK` | L'appel est terminé. |
| `VS_INTEROP_STATUS_INVALID_ARGUMENT` | Un argument de handle, de pointeur, de vue chaîne ou de tableau n’est pas valide. |
| `VS_INTEROP_STATUS_WRONG_THREAD` | Une règle de session ou de thread d'objet a été violée. |
| `VS_INTEROP_STATUS_ENGINE_ERROR` | Le chargement ou l'exécution a échoué. |
| `VS_INTEROP_STATUS_TYPE_MISMATCH` | Un type de valeur ou un type d'hôte ne correspond pas. |
| `VS_INTEROP_STATUS_CALLBACK_ERROR` | Un rappel de l'hôte a signalé un échec. |
| `VS_INTEROP_STATUS_UNSUPPORTED` | La version actuelle ne fournit pas cette fonctionnalité. |
| `VS_INTEROP_STATUS_OUT_OF_MEMORY` / `BUFFER_TOO_SMALL` / `INTERNAL_ERROR` | Ressource, tampon de sortie ou panne interne. |

`vs_interop_run_result` contient `struct_size`, `success`, `exit_requested`,
`exit_code` et `diagnostic_count` :

- `success` signale une compilation/exécution normale, et non si `exit_code` est nul ;
- un `int main` réussi peut renvoyer un code de sortie non nul ;
- `exit_requested` est défini uniquement par un `exit(n)` explicite, y compris `exit(0)` ;
- un retour `int main` ordinaire ne positionne pas `exit_requested` ;
- en cas d'échec, `exit_code` est un code d'erreur du moteur ou de l'API plutôt qu'un code de sortie de script normal.

Utilisez `vs_interop_session_diagnostic_count` et
`vs_interop_session_get_diagnostic` pour lire les diagnostics. Chaque diagnostic a un
`message`, `message_id`, `file_label`, `line` et `column` ; les arguments peuvent être
lu et formaté avec l'argument de diagnostic et `format_diagnostic`
fonctions. Les vues de chaîne renvoyées ont la durée de vie documentée pour le propriétaire
fonctionnement des séances.

## Localisation

La localisation d'interopérabilité est maintenue par le noyau comme un seul état à l'échelle du processus, et
les opérations de localisation elles-mêmes sont thread-safe. Ce n'est pas un
réglage indépendant sur chaque `vs_interop_session`. Un changement de langue affecte
diagnostics formatés par le noyau après le changement, y compris
`vs_interop_session_format_diagnostic`.

| Fonction | Objectif |
| --- | --- |
| `vs_interop_locale_set_language` | Sélectionne une langue chargée à partir d'une balise BCP 47 canonique ou normalisable ; renvoie `VS_INTEROP_STATUS_OK` en cas de succès. |
| `vs_interop_locale_get_language` | Renvoie la balise de culture canonique qui correspond réellement, telle que `en-US` ou `zh-CN`. |
| `vs_interop_locale_translate` | Renvoie le texte UTF-8 traduit dans un tampon du module ; messages inconnus copiés inchangés. Libérez via `vs_interop_buffer_destroy`. |
| `vs_interop_locale_get_available_languages` | Renvoie un tampon JSON UTF-8 appartenant au module contenant les balises de culture canoniques actuellement chargées. |
| `vs_interop_locale_load_catalog_directory` | Valide et remplace l’arborescence de localisation complète à l’échelle du processus. |
| `vs_interop_locale_merge_catalog_directory` | Valide une arborescence et la fusionne dans les catalogues actifs ; `override_existing != 0` remplace les ID de message existants. |

Le répertoire doit utiliser la disposition `<culture>/<component>.json` décrite dans
[Localisation du diagnostic](../../../../../VeroScript/Resources/Docs/User/fr-FR/localization.md) et doit inclure un
Ligne de base `en-US`. Lorsque la validation ou le chargement du catalogue échoue,
`load_catalog_directory` et `merge_catalog_directory` renvoient
`VS_INTEROP_STATUS_ENGINE_ERROR` et écrivez un tampon d'erreur appartenant au module dans
`out_error` ; arguments invalides, échecs d'allocation et exceptions internes
utiliser leurs statuts correspondants. Libérez le tampon d'erreur et les fichiers disponibles.
tampon JSON du langage avec `vs_interop_buffer_destroy`. La vue chaîne de
`vs_interop_locale_get_language` n’appartient pas à l’adaptateur ; copie-le quand il
doit être retenu.

```c
/* The view(...) helper from the minimal workflow above is reused here. */
int ConfigureLocale(void)
{
	vs_interop_buffer* error = NULL;
	if (vs_interop_locale_load_catalog_directory(
			view("localization"),
			&error) != VS_INTEROP_STATUS_OK)
	{
		if (error != NULL)
		{
			vs_interop_buffer_destroy(error);
		}
		return 1;
	}

	if (vs_interop_locale_set_language(view("zh-CN")) != VS_INTEROP_STATUS_OK)
	{
		return 2;
	}
	return 0;
}
```

`vs_interop_locale_get_available_languages` renvoie un tableau JSON contenant
les balises de culture canoniques actuellement chargées. Un adaptateur doit utiliser ce tableau pour remplir un sélecteur de langue
sans analyser à nouveau le système de fichiers hôte.

Les adaptateurs doivent vérifier que `vs_interop_get_capabilities()` contient
`VS_INTEROP_CAP_LOCALIZATION` avant d’appeler ces exportations facultatives. Catalogues
peut être chargé avant la création des sessions ou pendant l'exécution du processus, mais
un hôte partageant un processus entre des threads ou des sessions doit définir quand
les changements de langue sont autorisés afin qu’un lot d’interface utilisateur ne mélange pas les langues.

## Configuration des sessions

`vs_interop_session_apply_config` accepte `vs_interop_config` :

| Champ | Objectif |
| --- | --- |
| `import_paths` | Chemins de recherche de modules ordinaires. |
| `implicit_import_paths` | Fichiers implicitement importés dans chaque module source ; cela ne modifie ni ne réexporte les déclarations de source. |
| `script_args` | Arguments transmis à `main(string[] args)`. |
| `defines` | Macros de type objet ou définitions de compilation conditionnelle. |
| `stdlib_search_paths` | Racines de recherche dans la bibliothèque standard. |
| `source_overlays` | Remplacements de chemin/source en mémoire. |
| `bytecode_cache_directory`, `use_bytecode_cache` | Emplacement et commutateur du cache de bytecode. |
| `max_call_depth` | Limite de récursivité/profondeur d'appel par exécution ; `0` utilise l'environnement ou la valeur par défaut. |
| `max_instructions` | Nombre maximal d'instructions VM par exécution ; `0` signifie sans limite. |
| `timeout_milliseconds` | Limite de temps réel par exécution ; `0` signifie sans limite. |
| `execution_backend` | `VM = 0`, `Mixed = 1`, `JIT = 2`. |
| `predefined_macros` | Macros structurées de type fonction avec nom, paramètres, corps, nom de source et indicateur variadique. |
| `allow_circular_imports` | Si les importations cycliques sont autorisées ; activé par défaut. |
| `allow_project_wide_access` | Si les déclarations non exportées peuvent être partagées à l'échelle du projet ; désactivé par défaut. |
| `enable_execution_control` | Installe le jeton d'annulation coopérative ; une configuration appliquée avec succès réinitialise une demande précédente. |
| `capability_policy_enabled` | Active la politique des effets de bord de la bibliothèque standard. |
| `allowed_script_capabilities` | Masque de bits `FILE_READ`, `FILE_WRITE`, `FILE_DELETE`, `ENVIRONMENT_READ`, `ENVIRONMENT_WRITE`, `CONSOLE_INPUT`, `CONSOLE_OUTPUT`, `TASK_SPAWN` et `PROCESS_DIRECTORY_CHANGE`. Utilisé seulement lorsque la politique est active. `chdir` exige `ENVIRONMENT_WRITE` et `PROCESS_DIRECTORY_CHANGE`. |

`max_instructions` et `timeout_milliseconds` sont des limites coopératives par exécution ; zéro signifie sans limite. Une limite non nulle ou `enable_execution_control` exige les contrôles coopératifs sur la VM ou un chemin JIT contrôlé admissible. Mixed contrôlé utilise la VM par défaut ; activez `vs_interop_jit_policy.enable_controlled_mixed` après mesure de la charge cible. La sélection explicite JIT est indépendante. Vérifiez `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION` du backend choisi et le compteur réel `controlled_native_executions`. Une entrée non prise en charge ou moins coûteuse sur VM conserve les contrôles via le repli VM. `vs_interop_session_request_cancel` est thread-safe ; sans contrôle activé, il renvoie `VS_INTEROP_STATUS_INVALID_ARGUMENT`. Il ne termine pas de force un rappel natif en cours. Gardez la session valide jusqu’au retour.

Lorsque `capability_policy_enabled` est non nul, seules les capacités de `allowed_script_capabilities` sont accordées aux fonctions intégrées de la bibliothèque standard. Les rappels hôtes ne relèvent pas de cette politique et doivent appliquer leur propre autorisation. Vérifiez `VS_INTEROP_CAP_EXECUTION_GOVERNANCE` avant de dépendre de ces fonctions facultatives.

`vs_interop_jit_policy` contrôle les seuils de hotness des backends mixtes, maximum
tentatives de compilation, capacité du cache et budget de compilation impatient.
`vs_interop_session_get_last_jit_stats` renvoie la compilation la plus récente,
les compteurs de cache, de répartition, de secours, de saut de budget, de chemin
rapide et `direct_native_calls`. Ce dernier compte les appels JIT à portée de
génération qui atteignent une autre entrée native sans passer par le pont de
valeur de la VM.

### Cache de code réutilisable de session

Vérifiez d’abord `VS_INTEROP_CAP_SESSION_JIT_CACHE`. Cette capacité facultative expose `vs_interop_jit_cache_limits`, `vs_interop_jit_cache_stats` ainsi que les fonctions de session pour régler/lire les limites et les statistiques. Initialisez `struct_size` pour chaque structure extensible fournie ou reçue. Les limites par défaut sont 8 Mio de code natif et 32 Mio de charge conservée totale ; une limite à zéro désactive la conservation de cette ressource. Ce cache est distinct du cache de bytecode et ne conserve du code compilé que pour des contextes `Run`/`EvalOnce`/`CallFunction` ordinaires admissibles. Ses statistiques sont des valeurs cumulées stabilisées du contexte, non des mesures mémoire en direct ou à l’échelle du processus. Une reconfiguration ou une invalidation du contexte efface le code retenu ; une limite de ressource déclenche un repli sûr vers la VM sans modifier la sémantique du script.

La réutilisation exige `vs_interop_jit_policy.reuse_session_code` non nul (valeur par défaut) et un backend effectif autre que VM. Un budget `max_compile_attempts` ou `max_cached_entries` nul signifie sans limite pour ce compteur ; les plafonds de code natif et de charge conservée restent applicables. Une révision stable des liaisons est requise. Les sessions persistantes et le rechargement à chaud peuvent réutiliser le code associé à leur génération. Toute modification des liaisons natives, des intercepteurs ou des rappels de mutation invalide le code conservé. Le débogage, l’initialisation de l’état persistant et le nettoyage borné utilisent la VM. Une exécution contrôlée admissible peut conserver un cache compatible et son identité de gouvernance ; les appels répétés ne réinitialisent pas le quota cumulé de compilation.

### Macros publiques et paquets

`defines` et `predefined_macros` initialisent le prétraitement de chaque module
source. `#define` reste privé ; `#define_export` publie la définition finale du
module. Un `import` explicite active les macros publiques après sa chaîne de
chemin ; `export import` et `#include` les réexportent aussi.
`implicit_import_paths` importe uniquement les déclarations. Ces règles valent
aussi pour `source_overlays`. Les macros publiques de type fonction se développent
dans l'environnement appelant sans capturer les macros auxiliaires privées.
Deux origines différentes portant le même nom entrent en conflit ; utilisez
`#undef` avant un remplacement volontaire. Une fermeture de dépendances source
cyclique contenant un `#define_export`, même inactif, est rejetée indépendamment
de `allow_circular_imports`.

Les paquets bibliothèque construits par l'ABI C conservent les macros publiques
pour les deux valeurs de `strip_debug_info`. Les exports conditionnels sont fixés
à la construction et ne sont pas réévalués avec les définitions du consommateur.
Les paquets application ne portent pas d'interface de macros publiques. Le format
du bytecode utilise l'identifiant fixe `VSBC` et une version de format séparée,
qui doit correspondre exactement au chargement ; reconstruisez les paquets et
caches contenant `VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4`. L'ABI C publique reste 0.1.1. Reconstruisez
aussi les backends JIT indépendants avec le cœur actuel ; les empreintes d'en-têtes
ou dispositions incompatibles sont rejetées au chargement.

## Types et valeurs

`vs_interop_value_create_array` canonicalise le type d’élément et vérifie chaque valeur. Une case objet accepte un objet script dérivé de la classe de base déclarée ou implémentant l’interface déclarée, selon les métadonnées du module de la valeur source. Les déclarations de conteneurs restent invariantes : cela ne rend pas `array<Derived>` affectable à `array<Base>` et n’ajoute aucune conversion implicite numérique ou de conteneur.

`vs_interop_native_type_desc` décrit de manière récursive les types de retour, les paramètres,
et des arguments de type conteneur pour les fonctions hôtes et les membres. Public natif
les types incluent :

| Catégorie | Identificateurs de types |
| --- | --- |
| Scalaires | `VOID`, `BOOL`, `INT32`, `UINT32`, `INT64`, `UINT64`, `FLOAT`, `DOUBLE`, `INT8`, `UINT8`, `INT16`, `UINT16`, `STRING`. |
| Hôte et énumération | `HOST_OBJECT`, `ENUM` ; le champ de nom contient un type d'hôte, un nom d'énumération ou une signature de fonction. |
| Conteneurs | `ARRAY`, `MAP`, `SET`, `QUEUE`, `STACK`, `ORDERED_MAP`, `DEQUE`, `CHANNEL`, `MULTI_MAP`, `ORDERED_MULTI_MAP`, `PRIORITY_QUEUE`, `MULTI_SET`, `ORDERED_MULTI_SET`. |
| Appelable | `FUNCTION`. |

`vs_interop_value_kind` distingue en outre `OBJECT`, `NATIVE_ARRAY`,
`WSTRING`, `FUNCTION` et `HOST_OBJECT`. Les valeurs numériques à largeur fixe sont
types distincts : `FLOAT` est binaire32, `DOUBLE` est binaire64 et `INT8`/`UINT8`/
`INT16`/`UINT16` ne doit pas être réduit à `INT32` ou à un C `int` non signé par un
adaptateur.

Utilisez les fonctions `vs_interop_value_set_*`/`get_*` appariées pour les scalaires. Texte UTF-8
utilise le `vs_interop_string_view` délimité par la longueur ; les chaînes larges utilisent UTF-16
unités de code plus une longueur explicite. Les valeurs peuvent être créées, clonées, réinitialisées et
détruit. `clone` copie la sémantique de référence du wrapper de valeur ; ce n'est pas le cas
copie automatiquement en profondeur la charge utile d'un conteneur ou d'un objet.
`vs_interop_value_assign(destination, source)` copie une valeur runtime complète
dans une destination modifiable en conservant l'identité des conteneurs et des
objets. Une destination empruntée en lecture seule est refusée.

Les tableaux, les files d'attente, les piles, les deques, les cartes, les ensembles et les canaux ont des créations dédiées,
compter, lire, contenir, envoyer, recevoir et fermer les opérations. Cartographier et définir les clés
doivent être des types exacts pris en charge par le langage : entiers signés/non signés, UTF-8/UTF-16
chaînes, énumérations de portée ou valeurs d'hôte. Les valeurs d'hôte utilisent l'identité par défaut, ou
un protocole d'égalité/timbre de hachage enregistré. Une clé de carte manquante renvoie le succès
avec `out_found == 0` et n'écrase pas la valeur de sortie. Recevoir de
un canal ouvert peut bloquer ; la fermeture d'une chaîne réveille les serveurs.

## Liaisons et appels natifs

Les adaptateurs peuvent enregistrer :

- `vs_interop_session_register_native` pour une fonction native compacte ;
- `vs_interop_session_register_host_function` pour les types de retour structurés, les paramètres, la documentation, les noms de paramètres, les annotations et les métadonnées génériques ;
- `vs_interop_session_register_host_type`, `vs_interop_session_register_host_member` et `vs_interop_session_register_host_protocol` pour les énumérations, les structures, les classes, les constructeurs, les méthodes, les propriétés, les champs, les membres statiques, les opérateurs, le clonage, l'égalité, le hachage et les usines de sous-classes.

Les indicateurs de paramètres sont des indicateurs binaires : `VS_INTEROP_PARAM_REF = 0x01` et
`VS_INTEROP_PARAM_CONST_REF = 0x02`. `const ref` doit inclure le bit de référence.
Les rappels de l'hôte peuvent lire les arguments, accéder aux arguments mutables, obtenir
`host_this` et la génération du programme, et effectuer un appel de rappel local
d'une fonction de script ou conservée appelable via `vs_interop_call_context`.

Les fonctions de script, les méthodes, les constructeurs, les champs et les appels globaux utilisent :

| Capacité | Fonctions représentatives |
| --- | --- |
| Fonctions et méthodes libres | `vs_interop_session_call_function`, `vs_interop_session_call_method`. |
| Contournement de l'intercepteur | `vs_interop_session_call_method_bypassing_interceptor`. |
| Requêtes | `vs_interop_session_has_function`, `vs_interop_session_has_method`. |
| Objets et membres | `vs_interop_session_construct_object`, `vs_interop_session_get_field`, `vs_interop_session_set_field`, `vs_interop_session_get_global`, `vs_interop_session_set_global`. |
| Appelables conservés | `vs_interop_session_retain_callable`, `vs_interop_callable_invoke`, `vs_interop_callable_destroy`, `vs_interop_callable_is_valid`. |

Les descripteurs d’argument doivent être accessibles en écriture car les appels réussis valident le `ref`/ final
`out` renvoie les valeurs à ces handles. Les arguments Value et `const ref` ne sont pas
réécrit. Les arguments sont validés avant l'exécution du script, donc invalides
les arguments ne peuvent pas échouer après avoir produit des effets secondaires de script. Une valeur en lecture seule
emprunté à un contexte de rappel natif doit être cloné avant d'être conservé ou
transmis à un appel imbriqué.

## Points de réflexion, d'interception et d'extension

Les sessions peuvent exporter deux vues JSON :

- `vs_interop_session_export_bindings_json` contient des fonctions, des types d'hôtes,
  membres, métadonnées du conteneur, indicateurs de paramètres, noms de paramètres, documentation,
  et des annotations pour les adaptateurs IDE, LSP et de vérification statique ;
- `vs_interop_session_export_reflection_json` contient des types de modules chargés,
  champs, méthodes, variables globales et métadonnées de déclaration pour les outils d'exécution.

Un vocabulaire d'annotation décrit les noms et les arguments pris en charge ; les
instances d'annotation décrivent quelles déclarations portent réellement ces annotations.
Il s'agit de deux concepts de données distincts.

La séance peut également inscrire :

- `vs_interop_session_set_instance_call_interceptor`, renvoyant `UNHANDLED`, `HANDLED` ou `FAILED` ; `proceed` utilise la cible et la surcharge déjà résolues ;
- `vs_interop_session_set_mutation_callback` pour les écritures de champs, les ajouts de conteneurs, les suppressions, les ensembles et les effacements, avec type d'objet, champ, valeur et génération de mutation ;
- `vs_interop_session_apply_class_defaults` pour les valeurs par défaut des classes fournies par l'hôte ;
- `vs_interop_session_register_generic_host_type_template` et `vs_interop_session_instantiate_generic_host_type` pour les types de valeur d'hôte créés par rappel synchrone ;
- `vs_interop_session_register_type_alias` pour les alias d'hôtes transparents ;
- `vs_interop_session_register_external_serializer`, `vs_interop_session_serialize_object`, `vs_interop_session_deserialize_object` et `vs_interop_session_enumerate_object_references` pour l'encodage externe, le décodage et le parcours de référence récursif.

Un désérialiseur externe peut ajouter des affectations de champs nommés à une transaction
temporaire, mais il ne peut pas accéder directement au stockage des objets de script. Les
métadonnées de paquet inconnues de l'hôte sont conservées octet par octet.

## Paquets et rechargement à chaud

<a id="package-container-format"></a>

Un `.vslib` est une image de bibliothèque Core. Une image applicative Core brute constitue la charge compilée d’un conteneur ZIP `.vsapp` standard. Avec le support Application, les API de paquets en mémoire acceptent aussi les ZIP applicatifs ordinaires et produisent des conteneurs ZIP pour les applications ; sans ce support, elles produisent des images Core brutes. Pour configurer explicitement environnement, signatures ou protection, utilisez l’API de chargement Application. Les octets canoniques à signer sont ceux du conteneur original pour une application conservée, ou ceux de Core pour une image brute. Analyser un paquet ne garantit pas la confiance envers son éditeur.

Interop peut lire, valider, construire et exécuter en mémoire des paquets `.vsapp` et `.vslib` :

- `vs_interop_package_read` effectue la validation du format, de la version et de la structure de base ;
- `vs_interop_package_get_kind`, `vs_interop_package_get_name`, `vs_interop_package_get_version`, `vs_interop_package_metadata_count` et `vs_interop_package_get_metadata` lisent les informations sur le paquet ;
- `vs_interop_package_get_canonical_signed_bytes` renvoie les octets empruntés stables sur la plate-forme ; l'hôte doit vérifier les signatures avant de franchir une limite de confiance ;
- `vs_interop_session_load_package` charge un paquet d'application ou de bibliothèque ;
- `vs_interop_session_reload_package` recharge de manière transactionnelle et consomme le module uniquement en cas de succès ;
- `vs_interop_session_build_package_file` construit les octets d'un paquet en mémoire.

Les paquets et caches de bytecode produits avec l'ancien format de développement
doivent être reconstruits depuis les sources avant utilisation avec l'exécution
actuelle ; `vs_interop_package_read` rejette cet ancien format.

Activez le rechargement à chaud des sources avec
`vs_interop_session_set_hot_reload_enabled`. `vs_interop_session_reload` et
`vs_interop_session_reload_package` peuvent préparer un remplacement dont
l’initialisation aura lieu à la prochaine exécution. Si
`VS_INTEROP_CAP_RELOAD_STATUS` est disponible, initialisez
`vs_interop_reload_status.struct_size` et appelez
`vs_interop_session_get_reload_status` sur le thread créateur. Avec `PENDING`,
le candidat est préparé et la génération précédente reste validée.
`ROLLED_BACK` restaure la génération précédente. `APPLIED_WITH_ERRORS`
conserve le candidat validé après une erreur ultérieure d’exécution ou de
nettoyage. `RESET_REQUIRED` impose de vider la session et de recharger le programme.
Si `vs_interop_session_clear` signale une annulation inachevée, détruisez la
session et recréez-la. Utilisez `operation_id` pour suivre la même opération.

Les variables globales, les champs et l’identité des objets compatibles peuvent
migrer. Les changements de type incompatibles, les canaux actifs et les
fermetures avec captures peuvent empêcher la migration. Libérez les valeurs C
possédées liées au module avant de le remplacer. Les effets de bord natifs
restent hors de la transaction du script.

## Threads, emprunts et destruction

Les adaptateurs doivent respecter ces limites :

- la création de session, les API de session ordinaires et la destruction finale sont confinées au thread créateur ;
- la destruction d'un mauvais thread renvoie `VS_INTEROP_STATUS_WRONG_THREAD` sans consommer le handle ;
- un appelable conservé avec `owner_thread_only == 0` peut être invoqué depuis un autre thread uniquement lorsque l'hôte sérialise chaque accès à la session ; la session elle-même n'est toujours pas thread-safe ;
- les rappels déclenchés par l'exécution du script peuvent provenir des threads de travail de la VM et peuvent se chevaucher, de sorte que les environnements d'exécution étrangers doivent acquérir leur propre verrou d'exécution et synchroniser l'état partagé ;
- les contextes de rappel, les vues de chaînes, les vues d'éléments de conteneur et les arguments de métadonnées de l'hôte sont empruntés pour la durée de vie du rappel ou de l'opération documentée ; copier des chaînes ou cloner des valeurs avant de les conserver ;
- les chaînes, octets et vues appartenant au module ne doivent pas être libérés par l'adaptateur ; les objets `vs_interop_buffer` appartenant au module ne sont libérés qu'avec `vs_interop_buffer_destroy` ;
- les rappels étrangers ne doivent pas annuler les exceptions dans le C ABI ; stocker l'erreur dans le contexte et renvoyer `VS_INTEROP_CALLBACK_ERROR` ;
- les durées de vie des valeurs, des objets et des conteneurs suivent la sémantique de référence VeroScript ; copier une structure C n’est pas une opération de copie complète.

## Négociation des capacités et compatibilité

`vs_interop_get_capabilities()` signale les bits de capacité facultatifs pour les paquets, le
rechargement à chaud, les liaisons d'hôte, les liaisons JSON, la réflexion JSON, l'interception
d'instances, les rappels de mutation, les valeurs de conteneur, les appelables conservés, les
types d'hôtes génériques, la sérialisation externe, les diagnostics JIT, la localisation, les
types numériques distincts, l'énumération des conteneurs, la gouvernance de l'exécution, la mise en cache réutilisable du code de session, la sélection explicite du backend et l’initialisation unique. Vérifiez d'abord l'ABI, puis les capacités avant d'appeler des
fonctions optionnelles. La détection des capacités ne modifie pas `VS_INTEROP_ABI_VERSION`.

`vs_interop_get_capabilities()` annonce les fonctions de l’API du processus, dont
`VS_INTEROP_CAP_JIT_BACKEND_SELECTION`. Le résultat global ne définit ni
`VS_INTEROP_CAP_NATIVE_JIT` ni `VS_INTEROP_CAP_NATIVE_TYPED_LOOPS` : aucun backend
n’est implicite. Interrogez `vs_interop_session_get_jit_backend_info`, puis
`native_code`, `VS_INTEROP_JIT_CAP_NATIVE_CODE` et
`VS_INTEROP_JIT_CAP_NATIVE_TYPED_LOOPS` pour le backend choisi par la session.
La chaîne de plateforme ne prouve pas la capacité native. Les champs ABI 0.1.1 ajoutés `cost_model_vm_fallbacks` et
`native_typed_loop_executions` de `vs_interop_jit_stats` ne sont visibles que si l’appelant fournit
un `struct_size` assez grand ; les anciens appelants conservent leur préfixe précédent en sûreté.

Les règles de compatibilité ne modifient ni les signatures existantes, ni les valeurs
d'énumération, ni les dispositions publiées des structures. Les nouveaux champs de structure
sont ajoutés en fin de structure et leur visibilité est contrôlée par `struct_size`. Les
adaptateurs peuvent enregistrer `vs_interop_get_runtime_version` et
`vs_interop_get_host_platform` pour les diagnostics au lieu de supposer une disposition liée à
la plate-forme.

Le SDK ne prescrit pas de stratégie propre à Python, C#, Rust, Unreal ou AOT et n'intègre pas
leurs environnements d'exécution. Il dépend de la surface d'intégration publique
`VeroScriptCpp` sans modifier le cœur du langage. Pour les points d'entrée en mémoire adaptés
au navigateur, consultez [WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/fr-FR/index.md). Pour le format du catalogue de liaisons
au niveau des champs, consultez [Liaisons d'hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/bindings.md).

## Types de valeurs de l'ABI

L'ABI 0.1.1 inclut les kinds `NULL`, `MULTI_MAP`, `ORDERED_MULTI_MAP`,
`PRIORITY_QUEUE`, `MULTI_SET` et `ORDERED_MULTI_SET`. Utilisez
`vs_interop_value_set_null` et `vs_interop_value_is_null` pour représenter
`nullptr` de VeroScript ; null est une vraie valeur, et non un handle invalide.

`vs_interop_value_create_sequence` accepte aussi `PRIORITY_QUEUE`.
`vs_interop_value_create_map_kind` crée les maps ordinaires et multi-maps ; une
multi-map conserve toutes les associations répétées, alors qu'une map
ordinaire remplace une clé répétée. `vs_interop_value_create_set_kind` crée
les ensembles ordinaires et multi-ensembles ; les multi-ensembles conservent
les doublons.

`vs_interop_value_set_host_object_ex` accepte le callback
`vs_interop_host_object_is_type` pour vérifier la compatibilité d'un sous-type
d'objet hôte. Le callback ne doit ni lever d'exception ni conserver la vue du
nom de type empruntée.

## Modes d’exécution

`VS_INTEROP_RUN_EVAL` conserve l’évaluation incrémentale.
`VS_INTEROP_RUN_EVAL_ONCE`, annoncé par `VS_INTEROP_CAP_EVAL_ONCE`, exécute une
fois l’initialiseur du module sans appeler `main` et permet au backend JIT choisi
d’exécuter cette initialisation. `VS_INTEROP_RUN_SCRIPT` appelle aussi `main`.
La politique d’exécution est distincte ; sans backend choisi, Mixed utilise la VM
et le mode JIT forcé échoue avant l’exécution.

Dans une session ordinaire, chaque exécution initialise un nouvel état ; EvalOnce
ne supprime pas l’initialisation des exécutions suivantes. Les sessions avec
rechargement à chaud conservent les variables globales déjà initialisées selon
les règles de l’état persistant. L’initialisation conservée utilise la VM.
Les exécutions admissibles utilisent le backend sélectionné.

## Sélection JIT explicite

Stratégie d’exécution et sélection de backend sont distinctes. Sans environnement de modules installé, runtime ne fournit aucun module par défaut : mixed utilise VM sans sélection, jit forcé échoue avant exécution. Sélectionnez un fichier/dossier installé séparément et compatible avec `vs_interop_session_set_jit_backend`, Session inactive sur son thread propriétaire. `vs_interop_session_set_jit_backend(session, (vs_interop_string_view){NULL, 0})` désactive. Un remplacement échoué préserve le précédent. `vs_interop_session_get_jit_backend` / `vs_interop_session_get_jit_backend_info` indique identité/capacités réelles ; `backend-services` est seulement le contrat Core. Le débogage, l’initialisation conservée et le nettoyage borné utilisent la VM. Mixed avec contrôle utilise la VM par défaut, sauf activation de `enable_controlled_mixed` ; le mode JIT explicite est indépendant de cette option. Les entrées contrôlées admissibles peuvent utiliser JIT ; les entrées non prises en charge ou privilégiant la VM conservent les vérifications sur le chemin VM. Consultez le guide JIT pour découverte et ABI.

Initialisez struct_size avant la requête. Les vues texte sont empruntées à la fabrique ; copiez-les avant changement de backend ou destruction de Session.

[JIT](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/fr-FR/index.md)

## Mémoire gérée

API hôte : `vs_interop_memory_*` / `vs_interop_session_set_memory_limits`. La référence [`std::memory`](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md) décrit la propriété, les quotas, l'expiration des vues et les emprunts.

L’extension additive se découvre avec `VS_INTEROP_CAP_MEMORY` ; les structures existantes et la version ABI restent inchangées. Fournissez exactement un contexte : `session` inactive sur son thread propriétaire, ou `call` pendant son callback natif. Une réentrée de session renvoie `BUSY` ; le callback utilise la gouvernance de la VM active. Initialisez `struct_size` pour limites, statistiques et leases. Une limite nulle est illimitée. Configuration, Run et Clear préservent les charges des régions retenues par l’hôte.

`from_array` exige des éléments uint8 ; `to_array` et memory `clone` copient les octets. Avec une politique activée, les adresses exigent `VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS`, contrairement aux identités et accès bornés. Une lease d’écriture exclut les accès concurrents aux octets ; toute lease empêche redimensionnement et invalidation, et une lease de lecture exclut la mutation des octets. Synchronisez le travail natif avant de libérer le token exactement une fois, même depuis un autre thread ou après destruction de la session. Une inscription externe réussie adopte `owner` et appelle son callback une fois sur le thread de libération finale ; un échec laisse cette responsabilité à l’hôte. Conservez le callback et ses données jusqu’à cette libération.

## Annotations de compilation et gravité des diagnostics

Vérifiez `vs_interop_get_compile_capabilities()` et
`VS_INTEROP_COMPILE_CAP_ANNOTATION_PROCESSING`. Un hôte C++ natif crée un service
opaque avec `CreateAnnotationProcessingService` de `VeroScriptInteropAnnotation.h`
et un registre gelé, en utilisant le SDK Core et l'ABI compilateur/STL correspondants.
Un hôte C11 transmet uniquement le handle. `vs_interop_annotation_service_retain`
retourne un autre handle possédé ; libérez chacun avec `vs_interop_annotation_service_release`.
Attachez le service avec `vs_interop_session_set_annotation_service` sur le thread
créateur inactif. La session copie la configuration : les handles fournis peuvent
être libérés immédiatement. ApplyConfig la conserve ; NULL l'efface explicitement.
La réentrée de configuration depuis les callbacks de compilation ou de libération
est rejetée. Gardez le code natif et les données vivants jusqu'à la libération de
tous les handles et sessions associés. Les handles ne sont pas sérialisables.

La disposition des diagnostics et l'ABI C 0.1.1 restent compatibles. Interrogez
`vs_interop_session_get_diagnostic_severity` : les suffixes `ERROR`, `WARNING`,
`INFORMATION`, `HINT` du préfixe `VS_INTEROP_DIAGNOSTIC_SEVERITY_` valent 0, 1, 2, 3.
Un index invalide ou un mauvais thread laisse la sortie inchangée. Une compilation
réussie peut porter des notes ; vérifiez status et success. Le texte last-error
choisit l'erreur principale lorsqu'un échec suit des notes.

## variant

<a id="exported-value-lifetime"></a>

Les valeurs exportées dépendant du script sont des racines de session. Lecture et utilisation sur le thread créateur ; une libération sur un autre thread programme le nettoyage au prochain point sûr. Libérer les instantanés avant remplacement/rechargement/effacement du module. La fermeture invalide ces enveloppes et exécute le nettoyage du langage. Les boîtes primitives indépendantes restent utilisables. Aucune correspondance automatique avec C# object/dynamic ou tout objet Python.

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

Une destination variant connue permet le boxing implicite. La lecture utilise cast<T> ou try_cast<T> ; variant_is<T> vérifie le type exact sans conversion. optional<T> et try_cast<T> sont intégrés à Core et ne nécessitent pas core.vs. cast<T> échoue sur un variant vide ; try_cast<T> renvoie un optional vide pour un variant vide ou une conversion ordinairement incompatible. Les échecs de copie, annulations et limites restent des erreurs.

Les valeurs restent HOST_OBJECT ; numeric_kind vérifie 0 autre/indisponible, 1 bigint, 2 rational et rend 0 pour un objet de script fermé ou consulté hors thread. Vérifiez VS_INTEROP_CAP_NUMERICS. numeric_parse/to_text/convert exigent exactement un contexte : Session inactive sur son thread propriétaire ou contexte natif actif emprunté. Texte UTF-8 avec longueur ; to_text produit une valeur string. convert cible les tags entiers/float/double VS_INTEROP_NATIVE_*, exact_float vaut 0/1 et l’échec conserve les sorties. Initialisez struct_size, reserved=0 ; les limites nulles choisissent des défauts Core finis, pas un cap du heap processus.

`vs_interop_session_set_numeric_limits`
`vs_interop_value_numeric_kind`
`vs_interop_numeric_parse`
`vs_interop_numeric_to_text`
`vs_interop_numeric_convert`

## Propriété des valeurs liées au module

<a id="value-domain-confinement"></a>

Posséder un handle ne détache pas sa valeur de la session. Les objets/fonctions de script et conteneurs mutables dont les cases acceptent des valeurs de module restent sur le thread créateur et deviennent indisponibles à la fermeture, même vides. Le stockage créé par l’hôte se lie lors de sa première utilisation par une session ; alias partagés et clones conservent cette propriété. Les rappels des workers VM exportent seulement des valeurs possédées indépendantes ; un conteneur vide avec cases variant/fonction ne l’est pas. Libérez les handles après usage, même après fermeture.

## Arguments de sélection des backends de Session

`vs_interop_session_set_vm_backend` / `vs_interop_session_set_jit_backend`

Ces setters Session acceptent un fichier/dossier compatible ou un ID installé si l’environnement fourni contient son résolveur. `auto` demande les candidats de l’environnement et peut utiliser le repli produit conservé pour la VM. `none` ou une chaîne native vide désactive la sélection ; `{NULL, 0}` donnent cette requête vide. Sur une nouvelle session, omettre un choix de backend permet la sélection par l’environnement ; cela diffère d’un effacement explicite. Lors de la reconfiguration d’une session existante, omettre le choix VM conserve son domaine d’exécution. Un échec explicite conserve le backend précédent. Utilisez le thread créateur au repos ; modifiez la VM avant exécution, car Clear/reload ne libère pas son domaine. Le choix JIT reste indépendant de la politique VM/Mixed/JIT.

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

<a id="host-memory-ownership"></a>

## Propriété mémoire commune aux hôtes

Créez des tampons, vues bornées et instantanés avec l’API mémoire de la Session. Utilisez son thread propriétaire pendant qu’elle est inactive ; dans un callback natif, utilisez son contexte mémoire actif. Copier un handle ne transfère pas son domaine d’accès.

Configurez les quotas avant l’allocation. Zéro signifie illimité. Le quota par tampon porte sur la capacité ; le total inclut les capacités possédées et octets externes, ainsi que le pic de croissance. Une réduction sous l’utilisation retenue échoue sans changer la configuration. Run, Clear et rechargement ne libèrent pas les régions retenues par l’hôte et ne réinitialisent pas leur comptabilité. Ces quotas ne bornent pas tout le heap et ne constituent pas un sandbox.

Un prêt en écriture exclut les autres accès aux octets ; tous les prêts empêchent redimensionnement et invalidation, les prêts en lecture excluent les écritures. Terminez le travail natif avant libération. Celle-ci est possible sur un autre thread et après destruction de la Session, sans permettre de nouvelles opérations sur une Session détruite. Le stockage externe reste retenu jusqu’au dernier alias ou prêt ; son invalidation expire tous les alias. Une adresse brute ne conserve pas le propriétaire.

Les ID d’objet conservent une identité uint64 sans prolonger la durée de vie. Les adresses nécessitent la capacité hôte correspondante quand la politique est active. Interrogez une référence d’argument originale uniquement dans son callback natif ; une copie par valeur n’a pas cette adresse. Ne conservez pas les contextes au-delà du callback.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
