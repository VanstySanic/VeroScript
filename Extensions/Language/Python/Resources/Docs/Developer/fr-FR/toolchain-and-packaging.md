# Outillage et contrat de paquet Python

## Contrat strict des outils statiques

Le frontend signale `PYF0003` lorsqu’une classe répète une déclaration de champ, valeur d’énumération, propriété ou méthode ; seul le setter unique de la propriété correspondante est autorisé. Le Transpiler n’accepte `None` que comme retour d’un callable, impose `-> None` à chaque constructeur, impose un récepteur d’instance `self` positionnel et obligatoire, et n’accepte que `@staticmethod` comme décorateur de méthode.

Avant la liaison runtime, BindGen valide aussi le Binding Catalog complet et les callables Python réels. Il rejette constructeurs dupliqués ou noms de membres visibles dupliqués, signatures champ/getter/setter incompatibles, ainsi que constructeurs, fonctions, méthodes et accesseurs de propriété dont la forme positionnelle obligatoire ne correspond pas exactement au catalogue. Ces échecs n’enregistrent pas de liaison partielle et ne remplacent pas les artefacts générés.

## Extension native

`VeroScriptPython` est une extension CPython, non une distribution Python embarquée. CMake exige un interpréteur CPython 3.11 à 3.15 et `Development.Module`, et rejette toute autre implémentation. L’extension lie `VeroScriptInterop`, utilise le suffixe ABI CPython de plateforme et place Interop près du paquet importable. Les arbres `localization/` et `stdlib/` correspondants sont aussi déployés à côté, donc tests et imports locaux ne nécessitent ni PATH ni chemin de bibliothèque global.

## Outils et composants

Frontend, BindGen, Transpiler et Python AOT s’installent comme composants d’outils Python. Les targets CMake testent avec l’interpréteur choisi et installent des chemins site-package relatifs pour garder un préfixe staged relocalisable. Le wheel runtime contient extension native, Interop, sources Python, types, bibliothèque standard et ressources Catalog.

## Vérification de release

La validation de distribution vérifie importabilité, placement de bibliothèque native, accord de version, contenu, comportement wheel/distribution source et matrice interpréteur/plateforme. Un paquet ne fonctionnant que depuis l’arbre source du dépôt n’est pas un artefact de release valide.

## Publication de la matrice de wheels

La publication complète du produit Python déduit sa matrice de `Extensions/Language/Python/pyproject.toml` : `requires-python` et la sélection `cibuildwheel` doivent correspondre, actuellement pour CPython 3.11–3.15. Préparez les cinq interpréteurs natifs correspondant au système et à l’architecture cibles ; la détection utilise les commandes versionnées et le lanceur Windows `py`. La publication complète accepte `-PythonExecutables` sous Windows et des arguments `--python-executable` répétés sous Unix. Vous pouvez aussi fournir `VEROSCRIPT_PYTHON_EXECUTABLES`, avec `;` comme séparateur sous Windows ou `:` sous Unix. Versions manquantes, builds de débogage, interpréteurs sans GIL et architectures incompatibles font échouer le contrôle préalable ; aucun interpréteur n’est installé automatiquement.

Depuis la racine du dépôt, cette commande vérifie les interpréteurs Windows x64 sans construire de paquet. Choisissez la cible correspondant à l’hôte natif ; le point d’entrée autonome accepte des arguments `--python` répétés pour les chemins explicites.

```text
python Resources/Tools/Packaging/build-veroscript-python-wheels.py --target win-x64 --check-interpreters
```

Le pipeline crée une distribution source unique et construit chaque wheel à partir de celle-ci dans des répertoires source et de compilation séparés. Chaque wheel est installée dans un environnement neuf avec l’interpréteur correspondant, puis vérifiée par toutes les suites du paquet installé sélectionnées par `Extensions/Language/Python/Resources/Tools/Packaging/test-wheel.py` et `pip check`. La publication complète exige tous les ABI de wheels, la distribution source, des tags et identités natives cohérents et les licences incluses. Le manifeste du produit enregistre la matrice dans `pythonWheelVersions` pour un audit sans dépôt ; une publication partielle de composants reste partielle. Lorsque Python est sélectionné, `-SkipBuild` ou `--skip-build` construit et vérifie toujours cette matrice.

Le sdist Python est stocké une seule fois sous `Release/Extensions/Language/Python/any/<version>/packages/`. Les wheels suivent leurs vrais tags plateforme/ABI et ne partagent un manifeste que si ces tags le justifient. Les index cibles référencent ensemble les manifestes du sdist et des wheels. Vérifiez les membres réels du sdist pour exclure les caches `.cxx`, `.gradle` et `.externalNativeBuild`, même avec un dépôt propre : `pyproject.toml` les exclut explicitement et la validation du sdist les refuse. Conservez sources, en-têtes, scripts de construction, métadonnées et notices nécessaires après extraction isolée.

Les modules natifs du SDK et de l’installateur conservent l’ABI CPython unique de la compilation principale. Les wheels Linux gardent leurs tags natifs réels ; la matrice ne garantit pas la compatibilité `manylinux` ou `musllinux`.

## Maintenance de la localisation

Chaque outil Python possède ses catalogues `Resources/Localization/<culture>/` ; seuls les messages partagés appartiennent à la zone Python. Le déploiement les réunit sans dupliquer les identifiants `python_tools.*`. Les fichiers doivent être des objets JSON UTF-8 contenant des identifiants uniques et des chaînes non vides. Une culture corrompue est ignorée et la culture correspondante suivante est essayée.

Les catalogues anglais définissent les identifiants acceptés et le contrat des arguments positionnels. Les traductions peuvent réordonner `{0}`, `{1}` et les autres arguments numérotés, mais doivent conserver l’ensemble de leurs indices ; les champs nommés et les accès aux attributs ou par indice ne sont pas pris en charge. Échappez les accolades littérales avec `{{` et `}}`. Les modèles absents ou invalides utilisent le catalogue anglais effectif, puis l’anglais intégré si le formatage échoue. Avant publication, vérifiez les tests de régression de localisation du Frontend et les contrôles de déploiement des six langues.

Un JSON mal formé, des ID dupliqués, des valeurs non textuelles ou vides et un UTF-8 invalide font rejeter la culture concernée. Les ID présents uniquement dans une traduction sont ignorés.

<a id="python-package"></a>

Le groupe Python possède le projet PEP 517 dans `Extensions/Language/Python/pyproject.toml`. Depuis la racine du dépôt, les commandes ci-dessous construisent et auditent des candidats locaux wheel/distribution source. L’installation du checkout utilise `python -m pip install ./Extensions/Language/Python`. La distribution source embarque ses entrées natives, mais exige encore les fichiers de développement CPython et la chaîne native choisis. `VEROSCRIPT_PYTHON_INSTALL_DIR` reste relatif pour permettre un staging CMake relogeable. Exécutez `cibuildwheel` depuis la racine pour la matrice configurée ; ni ces builds ni le validateur ne téléversent une version. Les métadonnées de version proviennent de `VeroScriptVersion.h` et doivent correspondre au runtime chargé.

```sh
python -m pip install build
python -m build Extensions/Language/Python --outdir .tmp/python-dist
python Extensions/Language/Python/VeroScriptPython/Resources/Tools/Packaging/validate_distribution.py .tmp/python-dist --expected-version 0.1.1
```

## Contrôle des métadonnées de distribution

Les bornes `Requires-Python` des wheels et de la distribution source doivent correspondre à toute la matrice déclarée, actuellement `>=3.11,<3.16`. Chaque artefact fourni est contrôlé, même lors d’une publication partielle. Le suffixe de l’extension Windows doit correspondre à `win32`, `win_amd64` ou `win_arm64` et à l’ABI CPython choisie. La version minimale indiquée par le tag d’un wheel macOS doit couvrir celle de l’extension et de sa bibliothèque Interop. L’assemblage croisé conserve les paramètres du compilateur, de la chaîne, de la plateforme du générateur et du déploiement, et contrôle les en-têtes CPython cibles avant compilation.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
