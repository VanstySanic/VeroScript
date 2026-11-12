# Normes de code VeroScript (provisoires)

Portée : [développement et contributions au projet VeroScript](README.md).

La disposition des types et les groupes logiques suivent [CPP.md](CPP.md). Ce fichier définit seulement les conventions VeroScript actuellement établies ; les détails propres au langage ou au formateur pourront être ajoutés ensuite.

## Format

- Placez `else`, `else if`, `catch`, `finally` et le `while` final de `do…while` sur de nouvelles lignes
  selon [GENERAL.md](GENERAL.md). L’accolade ouvrante d’un bloc occupe aussi une ligne distincte alignée sur sa clause.
- Utilisez une tabulation réelle par niveau logique, affichée sur quatre colonnes par défaut ; suivez les règles d’indentation de [GENERAL.md](GENERAL.md).
- Chaque espace de noms, type, méthode, condition, boucle ou portée locale ajoute un niveau.
- L’accolade ouvrante occupe sa propre ligne ; la fermante s’aligne sur sa déclaration ou instruction.
- Les conditions et les boucles dotées d’un corps utilisent des accolades. Seule exception : une garde sur une ligne limitée à un `return` direct, sans chaîne `else`, journalisation, nettoyage ni autre instruction.
- Chaque `switch` contient `default`. Placez les instructions ordinaires de chaque `case`/`default` dans une portée indépendante délimitée par des accolades ; une branche ne contenant qu’un seul `return` direct qui termine la méthode courante peut omettre ce bloc.
- Un `fallthrough;` explicite doit être la dernière instruction directement rattachée au label, après son bloc délimité par des accolades. Ne l’imbriquez pas dans un bloc, une condition ou une boucle. Cette exception suit le contrat du langage ; les autres instructions restent dans la portée indépendante.
- Les déclarations `class`, `struct`, `interface` et `enum` se terminent par un point-virgule.

## Disposition des types

Ordre :

1. Relations de types et alias ;
2. régions `public`, `protected`, `private`, dans cet ordre ;
3. méthodes héritées en premier dans chaque région ;
4. types imbriqués `enum` → `struct` → autres types → `class` ;
5. méthodes propres au type ;
6. variables membres.

- Regroupez méthodes héritées/d’interfaces selon l’ordre d’héritage. Utilisez au besoin les marqueurs appariés `//~ Begin <Name> Interface` / `//~ End <Name> Interface`.
- Plusieurs types de premier niveau dans un fichier suivent aussi `enum` → `struct` → autres types → `class`, si leurs dépendances complètes le permettent.
- Aucune déclaration anticipée VeroScript n’est actuellement prescrite : sa prise en charge native n’est pas confirmée. Ne l’introduisez pas simplement parce que C++ la prescrit ; ajoutez une règle après confirmation de la syntaxe.

## Nommage et commentaires

- Types et interfaces utilisent PascalCase ; fonctions, champs, paramètres et variables locales utilisent lowerCamelCase.
- Suivez [GENERAL.md](GENERAL.md) pour expliquer caractéristiques du langage, contraintes, propriété ou objectif de l’exemple, sans répéter le code évident.
- Les noms publics de bibliothèque standard et de liaisons hôtes font partie de la compatibilité ; tout renommage exige une évaluation distincte.

## Longueurs et indices

Utilisez `int64` pour les longueurs de conteneur, indices linéaires, positions et nombres d’octets des sous-chaînes et capacités. Conservez les types déclarés des clés de map et identifiants de tâche ; un nombre devenu 64 bits n’élargit pas les autres API. Vérifiez le signe et les limites avant une conversion vers `size_t` natif ou un indice de backend plus étroit. Une conversion vers `int` doit être vérifiée ; conservez les limites d’allocation et les sentinelles documentées d’absence de résultat. Mettez à jour ensemble les déclarations de la bibliothèque standard, la documentation et les tests de régression lorsque ces contrats changent.

## À compléter

Précisions possibles : ordre des imports, espaces de noms standard, exceptions, références, concurrence, comportement du formateur et publication des exemples.
