# Normes de code Python (provisoires)

Portée : [développement et contributions au projet VeroScript](README.md).

Python suit les principes de structure, commentaires et explications nécessaires minimales de [GENERAL.md](GENERAL.md). Sans accolades ni modificateurs d’accès, l’ordre des déclarations de modules et classes exprime les niveaux correspondants.

## Format

- Utilisez une tabulation réelle par niveau logique, affichée sur quatre colonnes par défaut ; suivez les règles d’indentation de [GENERAL.md](GENERAL.md).
- [Python interprète les tabulations aux limites de huit colonnes](https://docs.python.org/3/reference/lexical_analysis.html#indentation), indépendamment de l’affichage de l’éditeur. Utilisez-les uniformément pour les blocs ; préservez les limites des blocs lors d’une migration, sans remplacement mécanique selon les colonnes affichées.
- Indentez correctement chaque corps de `class`, `def`, condition, boucle, `try` et gestionnaire de contexte. Ne comprimez pas la logique en instructions composées sur une ligne.
- `if`, `for`, `while`, `try` et autres branches utilisent un bloc indenté indépendant. L’exception C++ de garde `return` sur une ligne ne s’applique pas.
- Avec `match` pour des branches de type switch, ajoutez `case _` comme défaut et traitez explicitement les entrées inconnues ou non prises en charge.
- Gardez imports conditionnels et branches de version dans leur région logique, sans découper un bloc sémantique complet.

## Disposition des modules et types

Ordre recommandé d’un module :

1. Docstring du module et imports `__future__` ;
2. imports de bibliothèque standard, tiers, puis du projet ;
3. constantes, alias et protocoles ;
4. `Enum` ;
5. `dataclass` ou autres types de données ;
6. autres types auxiliaires ;
7. classes ;
8. fonctions du module et entrée de ligne de commande.

Ordre recommandé d’une classe :

1. Constantes et types au niveau de la classe ;
2. méthodes exigées par héritage/protocoles ;
3. `Enum`, types de données, autres types, puis classes imbriqués ;
4. `__init__`, constructeurs alternatifs, méthodes publiques et propriétés ;
5. auxiliaires protégés et privés ;
6. variables de classe ; établissez l’état d’instance dans `__init__`, pas sporadiquement entre les méthodes.

- Regroupez les implémentations de plusieurs bases/protocoles selon l’ordre d’héritage. Utilisez au besoin `#~ Begin <Name> Interface` / `#~ End <Name> Interface`, ou de courtes explications plus idiomatiques en Python.
- Suivez le nommage Python : `snake_case` pour modules/fonctions/variables, `PascalCase` pour classes, `UPPER_SNAKE_CASE` pour constantes et soulignement initial pour l’implémentation interne.

## Commentaires

- Utilisez des docstrings pour les contrats non évidents des modules, classes et fonctions publics.
- Utilisez `#` pour les raisons locales, sans répéter les noms de variables ou fonctions suffisamment explicites.
- Les annotations décrivent les entrées/sorties publiques et structures complexes ; leur rigueur pourra être définie séparément.

## À compléter

Précisions possibles : exceptions, E/S de fichiers, chemins, frontières de sécurité, frameworks de test, formatage et typage statique.
