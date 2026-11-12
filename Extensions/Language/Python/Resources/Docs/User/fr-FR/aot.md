# AOT Python

`veroscript aot --target=python` convertit un programme VeroScript vérifié en
un programme Python lisible. Le fichier généré contient son petit runtime de
compatibilité et ne charge ni la VM VeroScript, ni `VeroScriptInterop`, ni
l'extension `veroscript`.

## Limite d'exécution

Le programme généré nécessite CPython 3.11–3.15 fourni par
l'utilisateur. Il n'embarque ni interpréteur Python, ni SDK Python, ni
bindings hôte, ni bibliothèque native VeroScript. Utilisez cette cible pour
livrer du code source Python autonome ; utilisez le SDK Python pour intégrer le
runtime dans une application Python.

Les workers générés utilisent des threads du système. Les variables globales
et statiques du script sont initialisées à partir d'un instantané propre à
chaque worker ; les channels sont le mécanisme synchronisé explicite entre
workers. Les identifiants de tâches appartiennent à leur worker et ne peuvent
pas être joints ou annulés depuis un autre worker.

## Fichier unique

```powershell
veroscript aot --target=python app.vs -o app.py
python app.py
```

Répétez `-I`/`--import-path` pour ajouter les répertoires des modules de
l'application. Le fichier de sortie n'est remplacé qu'après une génération
réussie.

## Bundle source

```powershell
veroscript aot --target=python app.vs --output-dir app_python --bundle-name app
python app_python
python -m app_python
```

Le bundle contient `__main__.py`, `__init__.py`, `_veroscript_runtime.py`,
`_veroscript_program.py` et `python-aot.manifest.json`. Le graphe complet des
imports est vérifié avant l'écriture. Les fichiers sont préparés dans un
répertoire temporaire avant le remplacement, qui refuse le contenu étranger.
La destination doit être un répertoire ordinaire ; les liens symboliques et
jonctions Windows finaux sont refusés. L'ancien répertoire est déplacé pendant
le remplacement, ce qui peut rendre le chemin brièvement absent. En cas
d'échec du remplacement, sa restauration est tentée.

## Limites volontaires

La cible prend en charge les scalaires, chaînes, conteneurs, fonctions,
espaces de noms, imports, enums, classes, structs, interfaces, conversions,
exceptions, `switch`, les modules `algorithm`, `numeric`, `random` et les
wrappers de tâches concurrents.

Elle refuse notamment `ref`/`out`, les types utilisateur génériques ouverts,
les ensembles de surcharges, les destructeurs, les bases concrètes multiples,
les blocs de valeurs par défaut de classe, les noms privés aplatis en double,
`goto`/étiquettes, le fallthrough de `switch`, les bindings hôte et les
lambdas source, ainsi que les déclarations locales `static`. Une construction non prise en charge produit un diagnostic et
aucune sortie partielle.

Déclarez les valeurs de fonction avec un type explicite `fn(...) -> T` ; leur
inférence avec `auto` n'est pas prise en charge. Les noms de fonction qualifiés par
un espace de noms sont pris en charge. Les récepteurs `Super` et les valeurs de
fonction variadique sont refusés ; les appels variadiques directs restent pris en charge.

Les wrappers `shared<T>` / `handle<T>`, les points d'entrée de tâches sous
forme d'objets appelables et la réflexion exécutable sont également refusés.
Ces fonctionnalités d'exécution restent disponibles via le SDK Python.

À l’arrêt, les canaux existants sont fermés et l’attente couvre les workers ainsi que toutes les générations de tâches qu’ils créent, dans le même délai. Les canaux créés pendant l’arrêt sont fermés dès leur création.

Le programme généré utilise `VERO_JOIN_TIMEOUT_MS` pour limiter l'attente de
fermeture des workers ; la valeur par défaut est de 30000 millisecondes. Un
worker qui appelle `exit(n)` fait échouer cette tâche, sans terminer le processus
parent.

Utilisez le [SDK Python](index.md) pour la VM/JIT, le chargement dynamique, les
bindings hôte, la réflexion, le hot reload, les callables conservés ou les
opérations de paquets à l'exécution.

## Mémoire du runtime

L’AOT autonome ne prend actuellement pas en charge les requêtes, buffers, vues ou types de `std::memory`. Les appels ou l’utilisation de ces types produisent une erreur explicite de génération ; importer `memory.vs` ou `core.vs` sans utiliser ces fonctionnalités reste pris en charge.

[Runtime SDK](index.md)

Python AOT autonome prend en charge la sortie texte, les requêtes de capacités et dimensions ainsi que les commandes de couleur, de curseur et de ligne de `std::console`. Une sortie redirigée signale des capacités nulles, des dimensions nulles et des commandes non exécutées ; les noms, modes ou coordonnées invalides restent des erreurs. Les primitives console inconnues sont refusées avec un diagnostic source.

Sous Windows, les couleurs, la réinitialisation du style, l’effacement et les commandes du curseur utilisent les API natives de la console sans activer le mode ANSI. Les dimensions et coordonnées décrivent la fenêtre visible ; l’effacement préserve l’historique en dehors de cette fenêtre.

## Export des attributs

La comparaison de configuration d’attribut est une exception builtin en lecture seule : `has_same_configuration(ref other)` ne crée pas d’alias modifiable. `try_get()` est accepté, `try_get(ref output)` reste refusé. Consultez la référence attribute pour ces limites par opération.

[std::attribute](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/attribute.md)

<a id="native-bundle-api"></a>

Les clients SDK natifs incluent `VeroScriptPythonAot.h`. `FPythonAotSourceBundle` porte `Files` et `ManifestJson` ; utilisez `EmitPythonSourceBundleFromSource` / `EmitPythonSourceBundleFromFile`, puis `WritePythonSourceBundle` pour le remplacement préparé du répertoire. `BuildPythonSourceBundle` emballe du Python déjà généré. L’option `veroscript_executable` du contrôleur Python sélectionne l’exécutable CLI ou la racine/bin d’un SDK ; sinon la recherche utilise `VEROSCRIPT_EXECUTABLE`, `VEROSCRIPT_SDK`, puis PATH. Ce contrôleur de CLI externe est distinct du programme autonome produit.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/aot.md) · [fr-FR](aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
