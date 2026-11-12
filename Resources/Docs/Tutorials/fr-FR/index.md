# Tutoriels VeroScript

Ces pages sont organisées autour de la réalisation de tâches. Chaque tutoriel commence par un petit objectif, développe le programme étape par étape et se termine par des exercices. Utilisez la [documentation](../../User/fr-FR/index.md) pour les règles du langage et les détails complets de l'API.

## Avant de commencer

Il vous faut :

1. L'outil de ligne de commande VeroScript disponible via `veroscript --version`.
2. Un répertoire de travail pour les fichiers de script.
3. Un éditeur capable d'enregistrer des fichiers de texte brut.

Les blocs indiquent leur langage ou shell. Exécutez les commandes `powershell` dans PowerShell et les commandes `sh`/`bash` dans le shell correspondant ; adaptez les chemins au système. Les blocs `text` montrent la sortie attendue. Les programmes complets utilisent généralement `int main` avec un statut explicite ; `void main` reste valable pour un exemple court sans statut à renvoyer.

`--check` valide le programme sans appeler `main`. Les [extensions script de compilation](../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/compiler.md) activées peuvent toutefois s’exécuter sous les limites et permissions du compilateur.

## Chemin recommandé

### Étape 1 : exécutez votre premier script

- [01 : Votre premier script](getting-started/01-first-script.md)
- [02 : Vérification, exécution et lecture des arguments](getting-started/02-cli-and-arguments.md)

### Étape 2 : apprendre le squelette du langage

- [03 : Valeurs, conditions et boucles](language/03-values-and-control-flow.md)
- [04 : Fonctions et collections](language/04-functions-and-collections.md)
- [05 : Structures, classes et interfaces](language/05-structs-classes-and-interfaces.md)
- [06 : Références, retours de référence et captures lambda](language/06-references-and-captures.md)
- [07 : Modules, importations et préprocesseur](language/07-modules-and-preprocessor.md)

### Étape 3 : créer des programmes utiles

- [08 : Chaînes, fichiers et chemins](standard-library/08-strings-files-and-paths.md)
- [09 : Concurrence et événements](standard-library/09-concurrency-and-events.md)
- [20 : Lire et copier un paquet binaire](standard-library/20-managed-memory.md)
- [22 : Formater un texte applicatif et capturer son état](standard-library/22-application-i18n.md)

### Étape 4 : expédier et intégrer

- [10 : Construire des paquets et distribuer une application](toolchain/10-packages-and-distribution.md)
- [11 : Débogage et génération AOT](toolchain/11-debugging-and-aot.md)
- [12 : Liaisons d'hôte et intégration de script](advanced/12-host-bindings.md)
- [13 : Réflexion d'exécution](advanced/13-reflection.md)
- [16 : Entrée interactive dans le SDK Web](toolchain/16-web-interactive-input.md)
- [17 : Valeurs optionnelles, références faibles et handles de structures partagées](advanced/17-value-utility-types.md)
- [18 : Choisir les modules de la chaîne C++](cpp/index.md)
- [Outils sources Python](python/index.md)
- [Outils sources C#](csharp/index.md)
- [Flux de travail Tooling : connecter un éditeur et le débogueur](toolchain/17-tooling-workflow.md)
- [19 : Exécuter un script dans une application mobile](toolchain/19-mobile-embedding.md)
- [21 : Installer un paquet local](toolchain/21-package-manager.md)
- [23 : Construire bibliothèque et application](toolchain/23-application-build.md)
- [24 : Collections, navigation et dialogues](toolchain/24-gui-collections.md)


### Étape 5 : standardiser un projet

- [14 : Utiliser l'espace de noms standard `std`](advanced/14-standard-namespace.md)
- [15 : Configurer un espace de travail et une localisation de diagnostic](toolchain/15-workspace-and-localization.md)

## Comment utiliser ces tutoriels

Terminez les deux premières étapes dans l’ordre. Les didacticiels de l'étape 3 sont axés sur les tâches et peuvent être sélectionnés selon les besoins ; L'étape 4 est destinée aux scripts prêts à être livrés ou intégrés.

Chaque didacticiel peut être lu indépendamment, mais n'ignorez pas `--check` : il recherche les problèmes de syntaxe, de type et d'importation avant l'exécution. Faites les exercices, même si le changement est minime ; modifier une entrée ou ajouter une branche suffit pour vérifier que vous avez compris l'exemple.

## Références

- [Bases du langage](../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/index.md) : syntaxe et sémantique.
- [Référence de la bibliothèque standard](../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/index.md) : signatures et limites des fonctions.
- [Chaîne d’outils](../../User/fr-FR/toolchain/index.md) : CLI, paquets, débogage et intégration hôte.
- [Index des exemples exécutables](../../../../VeroScript/Resources/Docs/User/fr-FR/examples.md) : les exemples distribués regroupés par sujet.

<!-- docs-navigation -->

---

[Tutoriels](index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
