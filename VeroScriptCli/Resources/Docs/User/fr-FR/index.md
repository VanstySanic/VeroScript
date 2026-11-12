# Outil de ligne de commande

Le programme de ligne de commande s'appelle `veroscript`. Il peut exécuter des scripts, les compiler et les vérifier sans les exécuter, évaluer du texte source, ouvrir un REPL, démarrer un débogueur DAP, créer des paquets, valider des catalogues de liaisons et générer une source AOT.

## Initialiser un projet

```sh
veroscript project init
veroscript project init --path "./MyProject"
veroscript build --path "./MyProject"
```

La commande crée les fichiers manquants `veroscript-project.json` et
`veroscript-build.json`, avec la version `0.1.1`, des dépendances vides et une cible
d'application portable produisant `app.vsapp`. Une nouvelle configuration de
construction fournit aussi un `main.vs` minimal s'il manque. Les fichiers ordinaires
existants sont indiqués par `Kept` et ne sont jamais écrasés ; une configuration de
construction existante conserve son propre point d'entrée. Un répertoire ou un lien
symbolique occupant un fichier de destination provoque une erreur. Réexécuter la
commande complète la configuration manquante. `project --help` et
`project init --help` affichent l'aide sans écrire de fichiers.

Sans `--path`, le répertoire de travail courant est utilisé. Les chemins relatifs
partent du répertoire de lancement ; les chemins absolus et `--path=<répertoire>`
sont acceptés, et les répertoires manquants sont créés. Le nom du répertoire est
normalisé en nom de projet ASCII minuscule valide, avec `my-project` comme valeur de
repli, sans renommer le répertoire. L'initialisation est locale, sans installation de
dépendances ni exécution de scripts. `veroscript.lock` est créé après une installation
réussie ; la gestion des paquets crée `.veroscript/` au besoin. La construction exige
un CLI avec les services correspondants. Voir l'[exemple minimal](../../../Examples/Project/README.md).

## Formulaires de commande

```powershell
# Run a source script or application package
veroscript [options] hello.vs [args...]
veroscript [options] DemoApp.vsapp [args...]

# Check without executing
veroscript --check hello.vs

# Execute source text
veroscript -e 'print(1 + 2);'

# Enter the REPL
veroscript -i

# Generate C++17 AOT source (the default target)
veroscript aot hello.vs -o generated.cpp

# Generate standalone Python source
veroscript aot --target=python hello.vs -o generated.py

# Generate a deterministic C# source bundle
veroscript aot --target=csharp hello.vs --output-dir generated --bundle-name App

# Validate or format a binding catalog
veroscript bindings validate catalog.json
veroscript bindings format catalog.json -o formatted.json

# Inspect a package
veroscript package inspect DemoApp.vsapp
```

Un `main` avec `string[] args` reçoit des arguments après le chemin du script ; le chemin lui-même n'est pas inclus dans `args`. Un `main` sans paramètre ignore les arguments supplémentaires. Une valeur de retour `int main` devient le code de sortie du processus ; un `void main` normalement terminé renvoie le succès.

Le code de sortie du processus CLI est le résultat final du script : la valeur `int main` est renvoyée, `exit(n)` est prioritaire et `void main` ou un `int main` qui atteint son accolade fermante produit `0`. Cela diffère de la sémantique `FRunResult` de l'API d'intégration ; un hôte intégré doit utiliser `bSuccess` pour décider si le moteur s'est terminé normalement, puis inspecter `ExitCode` séparément.

## Options communes

| Options | Objectif |
| --- | --- |
| `-e <source>` / `--eval <source>` | Exécute le texte source ; ne peut pas être combiné avec `-i`, `--check` ou le mode de surveillance. |
| `-i` / `--interactive` | Entre dans le REPL. |
| `--repl-strict` | Nécessite `-i` ; sort différent de zéro lorsque la dernière entrée REPL a échoué. |
| `-I <path>` / `--import-path <path>` | Ajoute un chemin de recherche de module ; l'option peut être répétée. |
| `--stdlib-path <path>` | Ajoute une racine de recherche de bibliothèque standard. |
| `--implicit-import <file>` | Importe un fichier dans chaque source compilée ; l'option peut être répétée. Elle ne modifie pas automatiquement les déclarations d'origine ou de réexportation. |
| `--no-circular-imports` | Rejette les dépendances cycliques des modules ; les cycles sont autorisés par défaut. |
| `--allow-circular-imports` | Restaure explicitement le comportement par défaut consistant à autoriser les dépendances de module cycliques. |
| `--allow-project-wide-access` | Permet un accès à l'échelle du projet aux déclarations non exportées ; utiliser avec parcimonie. |
| `--binding-catalog <file>` | Charge des catalogues de liaison d'hôte ; l'option peut être répétée et les catalogues sont fusionnés. Elle est disponible pour les modes de vérification/exécution ordinaires, mais pas avec `-e`, `-i` ou le mode surveillance. |
| `-D name[=value]` / `--define name[=value]` | Définit une macro de type objet ou un symbole de compilation conditionnelle. |
| `--macro NAME(params)=body` | Définit une macro de fonction structurée de style hôte ; `--predefined-macro` est un alias de compatibilité. |
| `--check` | Compile et rapporte les diagnostics sans les exécuter ; ne peut pas être combiné avec `-e`, `-i` ou le mode de surveillance. |
| `--lang <tag>` | Sélectionne un langage de diagnostic résolvable à partir des répertoires de culture chargés. Les ressources par défaut sont déterminées par les répertoires de culture inclus dans la distribution ; les racines de catalogue personnalisées peuvent fournir davantage de langues. |
| `-o <path>` / `--output <path>` | Chemin de sortie pour le formatage AOT ou du catalogue de liaisons. |
| `--output-dir <path>` | Répertoire du bundle AOT multi-fichier C# ou Python. |
| `--bundle-name <name>` | Nom portable du bundle source ; le nom du fichier d'entrée est utilisé par défaut. |
| `--manifest <path>` | Chemin du manifeste d'un bundle C# ; le répertoire de sortie est utilisé par défaut. |
| `--target <name>` | Cible AOT : `cpp` (par défaut), `csharp` ou `python`. |
| `-h` / `--help` / `-?` | Affiche l'aide. |
| `--version` | Affiche les informations sur la version, la plate-forme et les fonctionnalités JIT. |

## Backends d'exécution et cache

| Options | Objectif |
| --- | --- |
| `--backend=vm` ou `--backend vm` | Utilise uniquement la machine virtuelle bytecode. |
| `--backend=mixed` ou `--backend mixed` | Utilise JIT lorsqu'il est pris en charge et revient à la VM ; c'est la valeur par défaut. |
| `--backend=jit` ou `--backend jit` | Exige un module JIT compatible, choisi explicitement ou automatiquement par l’environnement de modules ; compilation anticipée et repli VM avant l’entrée pour les corps non pris en charge. |
| `--bc-cache=off` / `--bc-cache=on` | Désactive ou active la mise en cache du bytecode `.vsbc` ; activé par défaut. |
| `--bc-cache-dir <path>` | Sélectionne un répertoire de cache de bytecode partagé ; le cache utilisateur de la plateforme est utilisé lorsqu'il est omis. |
| `--vm-backend <id\|file\|directory>` / `--vm-backend=<id\|file\|directory>` | Choisit l’interpréteur : `vs-bytecode` livré, fichier/répertoire compatible, ou ID installé avec le support des paquets. Indépendant de la politique d’exécution. |

Un Runtime complet fournit son interpréteur compatible dans `Vm/` ; conservez ce répertoire au déploiement. `--vm-backend=vs-bytecode` sélectionne celui livré. Les autres ID simples se résolvent uniquement dans l’environnement installé explicitement sélectionné avec support des paquets ; l’installation seule ne les sélectionne pas. Les chemins de fichier/répertoire partent du répertoire de lancement et doivent correspondre au Runtime. `--vm-backend` choisit l’interpréteur, `--backend=vm|mixed|jit` la politique et `--jit-backend` l’implémentation JIT. Les processeurs de compilation en script exigent aussi cet interpréteur ; une vérification ordinaire n’exécute pas le programme d’entrée.

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

Le débogage utilise la VM. `--watch` lance une nouvelle exécution selon la stratégie choisie quand un fichier surveillé change.

## Mode de surveillance

`--watch` réexécute le script lorsqu'un fichier source ou un module associé est modifié. `--watch-once` active le mode de surveillance et se ferme après le premier résultat d'exécution non nul. Le mode de vérification, l'évaluation, les fichiers REPL, de débogage et de paquet ne peuvent pas être combinés avec le mode de surveillance ; un paquet construit ne prend pas en charge `--watch`.

## Débogage

DAP sur entrée et sortie standard :

```powershell
veroscript --debug hello.vs
```

DAP sur TCP :

```powershell
veroscript --debug --dap-port=4711 hello.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 hello.vs
```

`--dap-port` accepte `1`–`65535` ; `--dap-bind` sélectionne l'adresse d'écoute, par défaut `127.0.0.1` et nécessite `--debug --dap-port`. En mode débogage, DAP `launch` ou `attach` fournit le programme et ses arguments ; un chemin de script de ligne de commande pré-amorce le programme et les arguments qui le suivent deviennent les arguments de script par défaut de cette requête. `veroscript-dap` est un alias léger pour le même hôte de débogage. Voir [Débogage](../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/debugging.md) pour connaître les limites complètes du protocole.

## Paquets, AOT et catalogues

Créez des paquets d'applications et de bibliothèques avec `--emit-package`. `--package-file` peut ajouter des modules sources à plusieurs reprises, `--package-version` définit une version de paquet non signée et `--strip-package-debug` supprime les informations sur le fichier source et la table de lignes. Ces options ne peuvent pas être combinées avec `-e`, `-i`, `--check`, le mode de surveillance ou le débogage. Voir [Paquets et distribution](packages.md).

La sous-commande `aot` nécessite un script source. La cible par défaut est C++17
et produit une unité de traduction avec `-o`/`--output`. `--target=python` produit
du Python 3.11-3.15 autonome et `--target=csharp` une source C# pour .NET 8/9.
Python et C# acceptent `--output-dir` et `--bundle-name` pour les bundles source.
Le résultat est une source, pas un exécutable lié. Voir [AOT C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/aot.md),
[AOT Python](../../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/aot.md) et [AOT C#](../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/aot.md).

`bindings validate` vérifie un catalogue, tandis que `bindings format` le normalise ; ce dernier peut utiliser `-o`/`--output` pour écrire un nouveau fichier. Voir [Liaisons d'hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/bindings.md).

## Diagnostics et variables d'environnement

```powershell
veroscript --lang en-US --check script.vs
veroscript --version
```

Les variables d'environnement courantes incluent :

| Variables | Objectif |
| --- | --- |
| `VERO_PATH` | Chemins de recherche de modules ; `;` sous Windows et `:` sous Unix. |
| `VERO_STDLIB_PATH` | Racines de recherche dans la bibliothèque standard. |
| `VERO_CACHE_DIR` | Répertoire de cache de bytecode. |
| `VERO_LANG` | Langage de diagnostic tel que toute balise BCP 47 canonique chargée depuis la racine du catalogue actuelle. |
| `VERO_LOCALIZATION_DIR` | Racine du catalogue de localisation. |
| `VERO_MAX_CALL_DEPTH` | Limite totale de profondeur d’appel de récursion. |

Les options de ligne de commande remplacent les paramètres d'environnement correspondants. Une langue système ou `VERO_LANG` non prise en charge revient à l'anglais ; une valeur `--lang` explicitement non prise en charge est rejetée comme erreur d'argument. Utilisez `veroscript --version` pour inspecter la version installée.

Les racines de localisation utilisent la disposition `localization/<culture>/<component>.json`. Voir [Localisation de diagnostic](../../../../../VeroScript/Resources/Docs/User/fr-FR/localization.md) pour la validation, l'ordre de recherche et les catalogues au niveau du module.

## Mesurer les performances

Les backends VM, mixte et JIT préféré ont les mêmes sémantiques de langage. Le backend mixte utilise le niveau optimisé pour le code éligible tout en conservant le repli vers la VM ; le backend JIT préféré demande une compilation plus précoce sans garantir que chaque fonction sera compilée.

Utilisez une exécution normale, hors débogage, pour mesurer les performances. Le débogage utilise la VM. Le cache de bytecode agit sur le temps de compilation, pas sur le coût stable du travail du script.

## Chemins et arguments Unicode

Sous Windows, le CLI reçoit des arguments Unicode et les convertit en UTF-8 avant la répartition. Les chemins de scripts, les chemins des modules importés et les arguments transmis à `main(string[] args)` conservent donc leur texte non ASCII. Les sources et les JSON de localisation utilisent UTF-8 ; les outils lisant stdout ou stderr redirigés doivent décoder ces flux en UTF-8.

## Sélection du backend

<a id="cli-backend-policy"></a>

`vm`, `mixed` et `jit` choisissent la stratégie d’exécution, indépendamment du backend JIT. Runtime ne contient pas de JIT. Avec un environnement de modules installé, omettre `--jit-backend` hérite de `services.jit.backend` ; sans choix configuré, `auto` essaie les candidats activés par ordre croissant d’identifiant et retient le premier backend compatible utilisable. `--jit-backend=auto` remplace la configuration ; `--jit-backend=none` désactive JIT. Un identifiant, fichier de bibliothèque ou répertoire peut aussi être choisi explicitement ; un choix explicite invalide produit une erreur. `auto` signale aussi les identifiants candidats invalides ou plusieurs fournisseurs actifs en conflit pour le même identifiant. La sélection automatique exige un environnement de modules. Si JIT est désactivé ou si la sélection automatique ne trouve aucun backend utilisable, `mixed` utilise VM et `jit` forcé échoue avant l’exécution. Placez les options CLI avant le chemin du script. Le débogage utilise la VM. Le code non pris en charge peut revenir à VM avant l’entrée ; un échec après l’entrée ne rejoue jamais les effets de bord.

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

```sh
veroscript --vm-backend=auto --jit-backend=auto main.vs
veroscript --jit-backend=none --backend=mixed main.vs
```

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
```

## Extensions de compilation en script

Voir [Extensions de compilation en script](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/compiler.md#source-extension-activation).

<a id="cli-compiler-options"></a>

`--compiler-config <key=value>` ajoute une entrée de configuration textuelle ; répétez cette option pour des clés différentes. Les valeurs ne sont pas analysées comme du JSON et les clés dupliquées sont refusées ; --compiler-instructions et --compiler-timeout bornent le travail ; --no-compiler-imports désactive l’activation depuis les sources tout en conservant les extensions configurées. Les extensions utilisent la VM de compilation, une annulation coopérative et des services restreints. Un import ordinaire ne donne aucune autorité de compilation. Le code généré respecte les règles normales. Les exports autonomes exécutent le résultat métier sans garder la VM d’extension. Le guide Developer décrit l’écriture des processeurs.

[Extensions de compilation en script](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/compiler.md)

Le budget total par défaut est de 1 000 000 d’instructions VM et de 2 000 ms. Zéro désactive le budget correspondant ; ces limites ne peuvent pas interrompre un rappel natif bloquant.

`install -r <file>` / `install --requirement=<file>`: [Listes de dépendances](../../../../../VeroScriptPackageManager/Resources/Docs/User/fr-FR/index.md).

## Commandes des modules natifs

Les commandes d’un paquet installé et activé déclarant le rôle natif `cli` sont disponibles directement dans son environnement de projet ou de composant. Utilisez les noms réels fournis par le paquet. Pour choisir un autre projet, placez `--path <directory>` avant les options du module.

Pour une bibliothèque locale, répétez `--cli-module <file>` si nécessaire. Les chemins relatifs partent du répertoire de démarrage ; le chargeur accepte des fichiers de bibliothèque et ne parcourt ni dossiers ni PATH. Utilisez `.dll` sous Windows et le `.so`/`.dylib` correspondant sous Linux/macOS. Placez options d’environnement/backend et sélecteurs de bibliothèque avant les options du module, puis la commande ou le script ; les arguments suivants sont transmis sans modification.

Les commandes de module priment sur les fichiers homonymes ; utilisez `./name` pour exécuter un tel fichier. Les noms intégrés et les doublons restent interdits. Une commande de module ne se combine pas avec check, eval, REPL, watch, debug/DAP ou la sortie de bytecode. `--help` liste les commandes/options ; `<command> --help` affiche son usage sans la configurer ni l’exécuter. L’enregistrement charge néanmoins le module natif. Sorties et descriptions viennent du module ; `--lang` transmet la langue sélectionnée aux callbacks sans traduire automatiquement leur texte. L’aide CLI et certains messages de validation restent en anglais.

```text
veroscript my-command arg
veroscript --path ./project --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll my-command --help
```

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
