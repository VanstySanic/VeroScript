# Index des exemples

Les fichiers d'exemple valident le comportement du langage et du runtime et fournissent des références croisées exécutables pour la documentation ; il ne s'agit pas de la spécification de l'API. Pour les signatures, les conditions d’erreur et les exigences de compatibilité, utilisez les références du langage, de la bibliothèque standard et de la chaîne d’outils. Les exemples sont disponibles dans Resources ou dans le dépôt source ; remplacez les noms de fichiers ci-dessous par leurs emplacements réels lors de leur exécution.

Exécutez un exemple :

```powershell
veroscript hello.vs
veroscript --check reflection.vs
```

[Attributs et fonctions liées aux méthodes](standard-library/attribute.md)：`attribute_demo.vs`、`attribute_functions.vs`、`attribute_methods.vs`、`attribute_weak.vs`。

## Backend, ressources et autorisations

Avec un environnement de modules installé, omettre `--jit-backend` hérite de son choix JIT ; sans choix configuré, la valeur par défaut est `auto`. Sans backend utilisable sélectionné automatiquement, la stratégie `mixed` utilise VM. Pour fixer manuellement un fichier ou répertoire de backend compatible :

```powershell
veroscript --jit-backend="/absolute/path/to/backend" --backend=jit integer_aliases.vs
```

Remplacez le chemin par votre installation du backend. Utilisez `--stdlib-path` lorsque la bibliothèque standard est hors de l'organisation des ressources du Runtime. Un hôte embarqué fournit explicitement les modules ou les sources virtuelles et capture la sortie.

Commencez l'intégration embarquée ou mobile avec `portable_basics.vs` : import de `numeric.vs`, lambda capturant par valeur et sortie vérifiée `portable basics: 42`. Seule l'autorisation de sortie est nécessaire. `runtime_utilities.vs` exige la lecture de fichiers pour `std::filesystem::cwd()`, `script_tool.vs` la lecture de l'environnement, et `concurrent_demo.vs` la création de tâches et des workers VM isolés. Une attente de durée zéro peut recevoir un résultat déjà terminé ; consommez-le une seule fois. `path_helpers.vs` et `windows_path_text.vs` traitent du texte sans accéder aux fichiers. Exécutez les scripts sur un thread de travail avec un budget d'exécution adapté.

Le guide mobile C# fournit un exemple MAUI ; le guide mobile Python fournit un script de validation CPython embarqué. La compilation mobile, l'intégration à l'application et la validation sur appareil restent distinctes des tests de scripts sur ordinateur.

## Lire les exemples

Choisissez un sujet, lisez le commentaire du fichier et la référence associée, puis modifiez les entrées et relancez l’exemple. `int main()` renvoie un code de sortie ; `int main(string[] args)` reçoit aussi les arguments du script. `void main()` est pris en charge lorsqu’aucune valeur de retour n’est nécessaire.

## Prise en main et fonctionnalités combinées

| Exemple | Thème principal |
| --- | --- |
| `hello.vs` | Script minimal, `main` et sortie. |
| `portable_basics.vs` | Import numérique explicite, tableau, lambda capturant par valeur et sortie vérifiée ; autorisation de sortie uniquement. |
| `argv_echo.vs` | `int main(string[] args)` et arguments de ligne de commande. |
| `object_basics.vs` | Méthodes de structures, instance définie avec son type et références de classe nullables. |
| `language_features.vs` | Contraintes, boxing, surcharges, valeurs par défaut et variadiques. |
| `fib.vs` | Fonctions récursives et flux de contrôle conditionnel. |
| `goto_demo.vs` | `goto` et étiquettes. |
| `exceptions.vs` | Exceptions typées, gestionnaire général, relance et `finally`. |

## Valeurs, tableaux, conteneurs et références

| Exemple | Thème principal |
| --- | --- |
| `arrays.vs` | Tableaux natifs et accès aux tableaux. |
| `dynamic_array.vs` | Tableaux dynamiques. |
| `brace_initialization.vs` | Initialisation du renfort. |
| `integer_aliases.vs` | Alias entiers et types explicites. |
| `numeric_primitives.vs` | `float`/`double` distincts, suffixes numériques, littéraux flottants avec partie entière omise et entiers 8/16/32/64 bits. |
| `pair_map.vs` | `pair`, vues des entrées, décomposition et premières écritures de structures et de références faibles. |
| `multimap.vs` | Multi-cartes. |
| `multiset.vs` | Multi-ensembles et éléments en double. |
| `priority_queue.vs` | Files d'attente prioritaires. |
| `ref_foreach.vs` | Variables de référence et itération de plage. |
| `reference_returns.vs` | Alias de référence, retours de référence et reliure de classe-handle-slot. |

## Types, génériques et valeurs appelables

| Exemple | Thème principal |
| --- | --- |
| `operators.vs` | Paramètres par référence des opérateurs, références d'index modifiables/constantes, références d'affectation et copies postfixées. |
| `static_members.vs` | Membres statiques et initialisation. |
| `default_delete.vs` | Membres spéciaux par défaut et supprimés. |
| `constructor_initializers.vs` | Listes d'initialisation de constructeur, champs `const` et ordre d'initialisation. |
| `friend_access.vs` | Règles d'accès `friend`. |
| `handle_struct.vs` | Identité partagée des structures avec `shared<T>` / `handle<T>`, construction par copie et expiration de `weak`. |
| `pure_virtual_out_of_class.vs` | Définitions hors classe et syntaxe virtuelle pure. |
| `template_prefix.vs` | Syntaxe de préfixe dans les définitions de membres de modèle. |
| `method_templates.vs` | Paramètres propres aux méthodes et références callable. |
| `value_template_parameters.vs` | Valeurs contrôlées, politiques enum et dimensions fixes. |
| `constexpr.vs` | Valeurs scalaires à la compilation, fonctions pures et appels à l’exécution. |
| `template_methods_main.vs` | Définitions de méthodes entre fichiers. |
| `generic_constraints.vs` | Contraintes génériques et `where`. |
| `lambda.vs` | Lambdas, captures initialisées par valeur/référence, compteurs mutable partagés et statiques locales initialisées au premier passage. |
| `delegates.vs` | `std::delegate` / `std::multicast_delegate`, membres en minuscules, captures, réassociation et instantanés de diffusion. |
| `delegate_functions.vs` | Délégués de fonctions nommées, zéro à trois arguments ; VM et AOT C++/C#/Python. |
| `reflection.vs` | Réflexion des types, champs, méthodes, métadonnées de déclaration et champs `mutable`. |
| `reflection_executable.vs` | Lecture/écriture typée des champs, appels d'instance/statiques/d'interface par défaut, paramètres et retours par référence, isolation des paramètres par valeur. |

## Modules, prétraitement et paquets

| Exemple | Thème principal |
| --- | --- |
| `include_alias.vs` | Inclure/importer des alias. |
| `alias_exports.vs`, `alias_reexports.vs` | `export using` / `export typedef` entre fichiers, recherche dans un espace de noms et réexport par façade ; les fichiers module/façade sont destinés à l’importation. |
| `macro_scope.vs` et `macro_scope_module.vs` | Exécutez le script principal pour vérifier que les `#define` ordinaires restent privés ; le second fichier sert uniquement aux imports. |
| `macro_exports.vs`, `macro_exports_module.vs`, `macro_exports_facade.vs` | Macros publiques objet, fonction et variadiques, réexportation par façade et isolation ; exécutez `macro_exports.vs`. |
| `macro_implicit.vs` | Macros publiques implicites sans import dans le consommateur ; lancez `veroscript --implicit-import macro_exports_facade.vs macro_implicit.vs`. |
| `reexport_facade.vs` | Un module de réexportation en façade. |
| `reexport_main.vs` | Réexporter les déclarations depuis un module d’entrée. |
| `preprocessor.vs` | Conditions et macros du préprocesseur. |
| `package/package_library.vs` | Source de la bibliothèque de paquets. |
| `package/package_app.vs` | Point d’entrée de l'application en paquet. |
| Notes d'exemple de paquet | Exemple de disposition et de commandes de paquet. |

## Bibliothèque standard, runtime et outils

| Exemple | Thème principal |
| --- | --- |
| `memory_operations.vs` | Identité des références, tampons binaires, ordre des octets, vues partagées et copies indépendantes. |
| `formatting.vs` | `print`, `printf`, `format` et `formatf`. |
| `path_helpers.vs` | Bibliothèque standard de chemin. |
| `standard_library_algorithms.vs` | Algorithmes génériques, plages aléatoires déterministes et mélange reproductible. |
| `standard_namespace.vs` | Noms `std::` recommandés, valeurs `readonly` et conversions explicites. |
| `windows_path_text.vs` | Texte de chemin Windows sur toute plateforme ; aucun accès au système de fichiers. |
| `runtime_utilities.vs` | `range`, le répertoire actuel et les utilitaires de temps. |
| `concurrent_demo.vs` | Canaux, tâches callable avec fonctions, méthodes statiques et lambdas, et annulation coopérative. |
| `script_tool.vs` | Arguments de l’outil de script, variables d’environnement, chaînes, assertions et état de sortie. |

## Exemples de configuration d'hôte et de liaison

| Fichier | Thème principal |
| --- | --- |
| `workspace.analysis.sample.json` | Configuration d’analyse du serveur de langage. |
| `bindings.sample.json` | Catalogue de liaison d’hôte minimal. |

Transmettez le contenu de `workspace.analysis.sample.json` dans `initializationOptions.veroscript` ou `workspace/didChangeConfiguration.settings.veroscript`, ou appliquez les paramètres correspondants de l’IDE. Le serveur de langage ne charge pas automatiquement ce fichier. Indiquez l’emplacement réel du catalogue dans `bindingCatalogPaths` ; utilisez des chemins absolus si vous transmettez directement les paramètres.

Consultez les [notes du répertoire des exemples](../../../Scripts/examples/README.md) pour les conventions complètes. Utilisez la [référence du langage](language-reference/index.md) pour les règles de langage, la [référence de la bibliothèque standard](standard-library/index.md) pour les signatures et la [référence CLI](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md) pour les options.

- `console_demo.vs`: `std::console` (Contrôle du terminal).

[Internationalisation des applications — std::i18n](standard-library/i18n.md): `i18n_demo.vs` · `VeroScript/Resources/Examples/I18n/`.

- [Fondations de la bibliothèque standard](standard-library/foundation.md)：`standard_library_foundation.vs`

## variant

[variant — Boîte de valeur dynamique](standard-library/variant.md): `variant.vs`.

```text
veroscript variant.vs
```
<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/examples.md) · [fr-FR](examples.md) · [ja-JP](../ja-JP/examples.md) · [ko-KR](../ko-KR/examples.md) · [zh-CN](../zh-CN/examples.md) · [zh-TW](../zh-TW/examples.md)
