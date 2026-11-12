# Paquets et distribution

VeroScript utilise des formats de paquet distincts pour les applications exécutables et les bibliothèques importées par d'autres scripts :

| Format | Objectif |
| --- | --- |
| `.vsapp` | Paquet d'application exécutable. |
| `.vslib` | Paquet de bibliothèque binaire importé par d'autres scripts. |
| `.vsbc` | Fichier de cache de bytecode, pas un paquet de distribution. |
| `.vspkg` / `.zip` | Archive ZIP d’installation contenant `veroscript-package.json` à la racine ; extensions insensibles à la casse. |

## Construire un paquet d'application

Utilisez `--emit-package` pour sélectionner le fichier de sortie :

```powershell
veroscript --emit-package DemoApp.vsapp main.vs
```

Un paquet d'application contient normalement son module d'entrée et ses dépendances de module. Exécutez-le avec :

```powershell
veroscript DemoApp.vsapp
```

## Construire un paquet de bibliothèque

Une source de bibliothèque doit explicitement exporter son API publique et ne fournit normalement pas de point d'entrée d'application :

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
```

D'autres scripts importent la bibliothèque avec la syntaxe `import` ordinaire :

```VeroScript
import "DemoLibrary.vslib";
```

Les déclarations sans `export` restent privées pour le module et ne font pas partie de l'API du paquet.

## Options du paquet

| Options | Objectif |
| --- | --- |
| `--package-file <file>` | Ajoute un module source au paquet ; l'option peut être répétée. |
| `--package-version <N>` | Définit la version du paquet. |
| `--strip-package-debug` | Supprime les informations du fichier source et de la table des lignes de débogage. |
| `package inspect <package>` | Inspecte un paquet `.vsapp` ou `.vslib`. |

```powershell
veroscript package inspect DemoLibrary.vslib
```

## Paquets sources et binaires

Importez les modules sources `.vs` directement pendant le développement et compilez les bibliothèques dans `.vslib` pour la distribution. Les deux formes utilisent les mêmes règles de visibilité des modules, mais un paquet binaire doit rester compatible avec le runtime et les liaisons qui le consomment.

## Installation des paquets d’extension

Le gestionnaire optionnel installe par défaut dans le projet du répertoire de lancement. `--path` choisit un autre projet ; `--root` choisit le Runtime/Core SDK vérifié du CLI, et `--root --path` un autre composant vérifié. Les chemins relatifs utilisent le répertoire de lancement. Les paquets Runtime se trouvent dans `Packages`, ceux du projet/SDK dans `.veroscript/packages`. Exécution, vérification, eval et REPL recherchent d’abord un projet, puis utilisent uniquement le composant du CLI. Il n’existe ni installation utilisateur ni fusion implicite. AOT utilise les mêmes sélecteurs dans les limites de ses capacités.

Les distributions utilisent des données ZIP standard, avec les extensions `.vspkg` ou `.zip`, sans distinction de casse. `pack` génère par défaut `<nom>-<version>.vspkg` ; une sortie `.zip` explicite reste possible. `install` et `publish` vérifient le même contenu et le fichier racine `veroscript-package.json`. Renommer une archive corrompue ou invalide ne la rend pas installable. Les URL de téléchargement ne nécessitent aucune extension ; les empreintes et le cache hors ligne reposent sur les octets réels.

```sh
veroscript pack ./library
veroscript pack ./library --output ./library.zip
veroscript pack ./library --output ./library-0.1.1.vspkg
veroscript publish ./library-0.1.1.vspkg
veroscript install ./library.vspkg
veroscript install ./library.zip --path "./My App"
veroscript install ./library.vspkg --root
veroscript install ./library.vspkg --root --path "D:/VeroScript/SDK/Core/0.1.1"
veroscript --path "./My App" "./My App/main.vs"
```

[Installer et gérer les paquets d’extension](../../../../../VeroScriptPackageManager/Resources/Docs/User/fr-FR/index.md)

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/packages.md) · [fr-FR](packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
