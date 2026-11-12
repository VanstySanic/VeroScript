# Introduction à VeroScript

VeroScript est un langage de script multiplateforme destiné à une utilisation intégrée. Il utilise une syntaxe familière de style C++, mais possède son propre système de types, son propre système de modules et son propre environnement d'exécution. Les fichiers sources utilisent l'extension `.vs`.

Le Runtime et les SDK natifs ciblent Windows, Linux et macOS. Les navigateurs et les hôtes JavaScript utilisent le [SDK WebAssembly](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/fr-FR/index.md) ; Android et iOS utilisent les [adaptateurs mobiles expérimentaux](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/fr-FR/index.md). Les composants, moteurs d’exécution et paquets disponibles dépendent de la cible et de la version du produit ; consultez [Installation et distribution](../../../../../VeroScriptInstaller/Resources/Docs/User/fr-FR/index.md) et le guide du SDK concerné pour les conditions de déploiement.

VeroScript est utile lorsqu'une partie de la logique d'une application doit être développée, vérifiée, exécutée et rechargée indépendamment du programme hôte. Les exemples typiques incluent la logique de jeu, les outils d'édition, les tâches d'automatisation et les extensions d'application.

VeroScript n'est pas un sous-ensemble de C++, ni un interpréteur capable d'exécuter une source C++ arbitraire. Sa syntaxe et son modèle objet sont influencés par le C++, mais le langage définit sa propre sémantique de type, son modèle de mémoire et ses capacités d'exécution.

## Modèle d'exécution

Un script suit généralement ce chemin depuis la source jusqu'à l'exécution :

```text
.vs fichier source
  -> prétraitement
  -> analyse lexicale et syntaxique
  -> analyse sémantique et compilation
  -> bytecode
  -> exécution par la VM VeroScript
```

Le préprocesseur prend en charge la compilation conditionnelle, les macros de type objet et fonction, la stringification, le collage de jetons et l'inclusion de fichiers. Le compilateur effectue une vérification de type, une résolution de surcharge, une instanciation générique, des vérifications d'héritage et d'autres analyses sémantiques. Les modules sont organisés avec `import` et `export` ; le chargeur de module résout les chemins de module, les importations implicites, les dépendances de paquets et les cycles d'importation.

L'exécution régulière est basée sur la machine virtuelle et utilise le backend mixte par défaut. Sur les plateformes prises en charge, un backend d'exécution plus rapide peut être utilisé et la VM reste disponible en solution de secours. VeroScript peut également générer des caches de bytecode pour réduire le travail de compilation lors des lancements ultérieurs.

## Fonctionnalités du langage

Le tableau ci-dessous donne un aperçu rapide du langage. La syntaxe et les règles détaillées appartiennent à la référence du langage correspondant.

| Zone | Principales caractéristiques |
| --- | --- |
| Valeurs primitives | Entiers, valeurs à virgule flottante, booléens, chaînes, chaînes larges, énumérations et `nullptr` |
| Structures de données | Tableaux, cartes, ensembles, files d'attente, piles, deques, files d'attente prioritaires et canaux |
| Système de types | `struct`, `class`, `interface`, espaces de noms, alias de type et contrôle d'accès |
| Programmation orientée objet | Héritage, interfaces, méthodes virtuelles, remplacements, membres statiques, constructeurs et destructeurs |
| Génériques et surcharges | Types et fonctions génériques, contraintes `where`, paramètres de type par défaut et surcharge de fonctions/méthodes/opérateurs |
| Fonctions | Arguments par défaut, arguments variadiques, paramètres de référence, retours de référence, alias de référence, valeurs de fonction, lambdas et captures de référence |
| Flux de contrôle | Conditions, boucles, `foreach`, `switch`, sauts, étiquettes et instructions return |
| Bibliothèque standard | Chaînes, chemins, mathématiques, formatage, fichiers, entrées/sorties et concurrence |

Utilisez `struct` pour les données avec une sémantique de valeur et `class` pour les données avec une identité et une durée de vie partagées. Les interfaces, l'héritage, la répartition virtuelle et les génériques sont les principaux mécanismes permettant de créer des abstractions réutilisables dans les scripts.

Voir [Outil de ligne de commande — Sélection du backend](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md#cli-backend-policy).

Mesurez une exécution normale, avec une charge représentative, un backend et des entrées constants, et laissez le démarrage à chaud se stabiliser. Le cache de bytecode réduit le coût de compilation initiale ; il ne modifie pas le travail exécuté par le script.

## Exécuter un script

Utilisez l'outil de ligne de commande pour exécuter un script directement :

```powershell
veroscript hello.vs
```

Les commandes courantes incluent :

```powershell
# Check without executing
veroscript --check hello.vs

# Start debugging mode
veroscript --debug hello.vs

# Package as a runnable application
veroscript --emit-package app.vsapp hello.vs
```

Un script doit fournir un point d'entrée `main`. Le point d'entrée peut renvoyer `int` ou `void`. `int main` est recommandé car sa valeur de retour devient le code de sortie du processus ; `void main` reste pris en charge et est traité comme une sortie réussie. Les deux formes courantes sont :

```VeroScript
int main()
{
	print("Hello, VeroScript!");
	return 0;
}
```

```VeroScript
int main(string[] args)
{
	print(len(args));
	return 0;
}
```

Les arguments de ligne de commande sont disponibles via `args` ; le chemin du script lui-même n'est pas inclus dans `args`.

VeroScript utilise trois formats de fichiers courants : `.vsapp` est un paquet d'application exécutable, `.vslib` est un paquet de bibliothèque importé par d'autres scripts et `.vsbc` est un fichier cache de bytecode. Un paquet de bibliothèque ne peut pas être exécuté directement en tant qu'application.

## Intégration dans un hôte C++

L'un des principaux cas d'utilisation de VeroScript consiste à l'intégrer dans une application C++. L'hôte peut charger ou compiler des scripts, appeler des fonctions, des méthodes et des constructeurs de script et exposer les fonctionnalités de l'hôte aux scripts. Les liaisons disponibles incluent les fonctions natives, les types d'hôtes, les champs, les méthodes, les opérateurs, les types génériques et les alias de type.

`VeroScriptEngine` fournit des points d'entrée de haut niveau pour vérifier et exécuter des scripts. `VeroScriptSession` est destiné aux intégrations qui nécessitent un contrôle plus précis sur les modules, les liaisons et l'état d'exécution. L'hôte peut également recevoir des rappels de diagnostic, de sortie et de débogage, et inspecter ou modifier les types de script, les champs et les valeurs globales.

Les hôtes non C++ peuvent utiliser l'ABI `VeroScriptInterop` C11/C++17. Il fournit des appels multilingues, des liaisons d'hôte, des paquets, une réflexion JSON, une sérialisation et des rappels via des descripteurs de session et de valeur opaques sans nécessiter de présentations d'objets C++. Les intégrations de navigateur et WebAssembly utilisent le SDK `VeroScriptWebAssembly` distinct, qui fournit un wrapper TypeScript, une découverte de catalogue de langage, une vérification/exécution de sources en mémoire et des fichiers virtuels importés.

Le rechargement à chaud est optionnel et désactivé par défaut. Les variables globales, objets, conteneurs et fermetures compatibles peuvent migrer, y compris le `this` capturé, si l’identité de fonction ainsi que les noms et types des captures restent compatibles. Les canaux actifs, les changements de type incompatibles et les modifications des captures sont refusés. Les effets natifs de l’hôte restent hors de la transaction d’état du script.

## Outillage et AOT

Les outils linguistiques partagent les fondements du prétraitement et de l'analyse sémantique du compilateur. Ils peuvent fournir des diagnostics, des plans de documents, des jetons sémantiques, la navigation, des informations de survol, la complétion, une aide à la signature, la recherche de références, le renommage et le formatage. Le débogueur utilise DAP et exécute le chemin de la VM.

Pour les déploiements qui ne nécessitent pas de chargement dynamique, le backend AOT peut générer une source C++17 et transmettre le résultat à un compilateur natif. L'exécution d'AOT et de VM sont des choix de déploiement différents ; celui qui convient dépend des exigences de construction, de distribution et de débogage de l’application hôte.

## Notes de version

Ce document décrit les fonctionnalités publiques actuelles de VeroScript. Les scripts, les caches de bytecode, les paquets et les liaisons d'hôte doivent utiliser des versions compatibles.

VeroScript maintient délibérément sa propre limite d'exécution. La version actuelle ne fournit pas de sémantique de pointeur de script ni de syntaxe de concept C++ autonome ; `__VA_OPT__` pour les macros variadiques, les captures de références, les retours de références et la réflexion d'exécution sont des fonctionnalités publiques soumises aux propres règles de type et de durée de vie de VeroScript. Les interfaces utilisateur graphiques sont en dehors du runtime principal.

Commencez par la [page d’accueil de la documentation](../../../../../Resources/Docs/User/fr-FR/index.md). Voir [Bases du langage](language-basics/index.md) pour la syntaxe et la sémantique, [Référence de la bibliothèque standard](standard-library/index.md) pour les API de bibliothèque et [Chaîne d’outils](../../../../../Resources/Docs/User/fr-FR/toolchain/index.md) pour la ligne de commande, le débogage, l'AOT, l'intégration d'hôte, WebAssembly/WASM et les SDK Interop.

<a id="backend-selection"></a>

## Sélection des backends de service

Lorsqu’un hôte ou la CLI fournit un environnement de modules installé ou déployé, un choix omis hérite de `services.<service>.backend`. Sans choix configuré, `auto` essaie les candidats activés par ordre croissant d’identifiant et retient la première fabrique compatible qui peut être créée. Un ID, une fabrique ou un fichier/répertoire explicite est prioritaire ; son échec ne choisit pas un autre ID. L’ID `asmjit` désigne un backend ; `vs-asmjit` désigne son paquet.

Un `auto` explicite remplace le choix de l’environnement. `none`, ainsi qu’une valeur de backend explicitement vide dans la configuration, désactive le service. Une valeur absente est différente. La VM assemblée au produit sert de repli uniquement si la sélection automatique ne trouve aucun interpréteur utilisable. Sans JIT optionnel utilisable, Mixed reste sur la VM ; JIT forcé exige toujours un backend compatible. Un service obligatoire sans candidat utilisable signale un échec.

La sélection intervient à la création des fabriques et à la préparation, jamais pendant l’exécution métier. Une erreur après entrée ne rejoue aucune opération. Le domaine d’interprétation reste figé après le début d’exécution. Quotas, annulation, nettoyage borné, débogage et rechargement conservent leurs règles. Les instances gardent leurs propriétaires de module et d’environnement.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/introduction.md) · [fr-FR](introduction.md) · [ja-JP](../ja-JP/introduction.md) · [ko-KR](../ko-KR/introduction.md) · [zh-CN](../zh-CN/introduction.md) · [zh-TW](../zh-TW/introduction.md)
