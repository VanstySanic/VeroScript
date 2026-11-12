# Diagnostics et erreurs

Les problèmes VeroScript relèvent généralement des diagnostics de compilation et des erreurs d'exécution. Les diagnostics de compilation doivent être corrigés avant l'exécution ; les erreurs d'exécution se produisent uniquement lorsque le chemin concerné s'exécute.

## Compiler les diagnostics

La compilation vérifie la structure lexicale et syntaxique, la recherche de nom, les conversions, les surcharges, les contraintes génériques, l'implémentation de l'interface, la visibilité des modules et les règles de déclaration. Utilisez `--check` pour compiler sans exécuter le script :

```powershell
veroscript --check script.vs
```

Le mode de vérification n'appelle pas `main`, ce qui le rend adapté aux vérifications de l'éditeur en temps de sauvegarde, à la validation CI et à la vérification préalable à la distribution. Les diagnostics incluent normalement un fichier, des informations sur les lignes et les colonnes, ainsi qu'une explication. Une cause première peut produire plusieurs diagnostics associés, alors corrigez d’abord la première erreur.

## Erreurs d'exécution

Les échecs d'exécution peuvent provenir de :

- un tableau, une chaîne ou un index de conteneur hors plage ;
- un `assert` dont la condition est `false` ;
- l'appel d'un délégué non lié ou d'une autre opération d'exécution invalide ;
- une opération ayant échoué sur un fichier, un répertoire ou un répertoire de travail ;
- une opération invalide sur un canal fermé ;
- limites de profondeur de récursion, d'opération de type ou de liaison à l'hôte.

Un échec d'exécution met fin à l'exécution en cours ou est signalé via le résultat d'exécution et le flux d'erreurs de l'hôte. Un hôte d'intégration doit inspecter `FRunResult` et conserver le texte d'erreur et les emplacements de diagnostic si nécessaire.

Les exécutions VM, mixte et JIT utilisent les mêmes diagnostics de langage.
Lorsqu'un noyau JIT signale une erreur arithmétique ou de conversion, le
runtime la rattache au meilleur emplacement source disponible et conserve le
diagnostic plus précis fourni par un assistant. Les hôtes doivent consommer le
diagnostic structuré et son emplacement plutôt que comparer le texte localisé.

## Exceptions du script

`throw` lève une valeur de script qui peut se propager à travers les appels et
être traitée par `try`/`catch`. Un gestionnaire typé ou `catch (...)` peut traiter
une exception de script, mais ne peut pas intercepter une faute du moteur ou de
l'exécution, notamment un `assert` échoué. Un bloc `finally` s'exécute pendant
la propagation de l'un ou l'autre type d'échec. Voir le chapitre [Flux de
contrôle](../language-basics/control-flow.md#exceptions) pour la syntaxe et les
règles de correspondance complètes.

## Langue et sortie

L'outil de ligne de commande peut sélectionner le langage de diagnostic :

```powershell
veroscript --lang en --check script.vs
```

`VERO_LANG` peut également sélectionner la langue par défaut. La sortie standard du script et la sortie d’erreur de diagnostic sont des flux distincts et doivent rester distincts dans une intégration hôte.

## Ordre d'enquête

Les pages de référence doivent d'abord établir les déclarations, les portées, les types et la visibilité des modules. En cas d'échec réel, utilisez `--check` pour éliminer les problèmes de compilation, reproduire les échecs d'exécution avec un minimum d'entrées, puis inspecter les autorisations de l'hôte, les chemins d'importation et les chemins de recherche de bibliothèque standard.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-reference/diagnostics.md) · [fr-FR](diagnostics.md) · [ja-JP](../../ja-JP/language-reference/diagnostics.md) · [ko-KR](../../ko-KR/language-reference/diagnostics.md) · [zh-CN](../../zh-CN/language-reference/diagnostics.md) · [zh-TW](../../zh-TW/language-reference/diagnostics.md)
