# Normes de code C# (provisoires)

Portée : [développement et contributions au projet VeroScript](README.md).

C# suit [GENERAL.md](GENERAL.md) et l’idée structurelle de [CPP.md](CPP.md) : relations, puis types, méthodes et données. Quand la syntaxe C# ne correspond pas, ce fichier prévaut.

## Format

- Placez `else`, `else if`, `catch`, `finally` et le `while` final de `do…while` sur de nouvelles lignes
  selon [GENERAL.md](GENERAL.md). L’accolade ouvrante d’un bloc occupe aussi une ligne distincte alignée sur sa clause.
- Utilisez une tabulation réelle par niveau logique, affichée sur quatre colonnes par défaut ; suivez les règles d’indentation de [GENERAL.md](GENERAL.md).
- Types, méthodes, propriétés, branches et `switch` à corps de bloc utilisent les accolades Allman comme en C++.
- Un membre, accesseur ou mapping local limité à une expression directe et lisible peut utiliser `=>`. N’y cachez pas plusieurs étapes, gestion de ressources, journalisation ou contrôle complexe ; utilisez un bloc Allman pour plusieurs étapes.
- Tout `if`, `else`, boucle, `using`, `lock`, `try`, `catch` ou `finally` avec corps utilise des accolades.
- Seule exception : une garde sur une ligne contenant uniquement un `return` direct ou `return <expression>`, sans chaîne `else`, journalisation, nettoyage ni autre instruction. `break`, `continue`, `throw` et les autres branches exigent des accolades.
- Tout `switch` instruction exige `default`. Un `switch` expression couvre entièrement ses entrées : ajoutez un bras `_` si les motifs précédents ne sont pas prouvés exhaustifs ; n’ajoutez pas un `_` inaccessible pour la forme si l’exhaustivité est déjà prouvée. Le traitement des inconnus doit être explicite, sans échec d’exécution obscur.
- Le corps de chaque `case`/`default` a une portée avec accolades indépendante, sans poursuite implicite. Expliquez un partage volontaire du traitement dans un commentaire voisin. Seule exception : le chemin unique termine directement la méthode par `return` ou `return <expression>`, sans journalisation, nettoyage, déclaration, condition ou autre instruction.
- Les directives comme `#if` restent au niveau logique du code entouré, sans niveau supplémentaire.

## Disposition des types

Ordre recommandé :

1. Attributs immédiatement près de leur cible ; liste de bases et interfaces près de la déclaration ;
2. membres à modificateurs d’accès dans l’ordre `public` → `protected internal` → `protected` → `internal` → `private protected` → `private`, avec si possible une région continue par niveau ;
3. `override` et implémentations d’interfaces en premier dans chaque région ; les implémentations explicites ne peuvent pas avoir de modificateur d’accès et ouvrent leur groupe, selon l’ordre d’héritage des interfaces ;
4. types imbriqués `enum` → `struct`/`record struct` → autres types → `class`/`record` ;
5. constructeurs, méthodes, propriétés et événements propres au type ;
6. champs en dernier, jamais entre méthodes, propriétés ou types imbriqués.

- Regroupez les implémentations explicites par interface ; utilisez des commentaires appariés `//~ Begin <Name> Interface` et `//~ End <Name> Interface` pour plusieurs membres ou un groupe peu évident.
- N’entrelacez pas les blocs d’interfaces, de bases et d’implémentation propre au type.
- Gardez traçable l’ordre des déclarations dans les longs fichiers d’implémentation ; les types auxiliaires locaux et méthodes statiques privées peuvent occuper la position la plus lisible.
- `file` ne concerne que les types de premier niveau locaux au fichier, pas les régions d’accès des membres. Classes et structures génériques restent dans leur propre catégorie, pas dans les « autres types ».

## Commentaires et nommage

- Commentez types, méthodes, propriétés et champs seulement si l’appelant doit connaître sémantique, propriété, threads, exceptions ou contraintes de protocole.
- Utilisez la documentation XML pour les contrats publics non évidents et `//` pour les courtes explications internes.
- Conservez le nommage .NET existant : PascalCase pour types/membres, camelCase pour paramètres/variables locales, `_camelCase` pour champs privés.
- Gardez les noms contractuels ABI natifs, JSON et protocoles ; la cohérence interne ne doit pas rompre la compatibilité externe.

## À compléter

Précisions possibles : nullable, async/annulation, libération des ressources, ViewModels Avalonia, tests et règles d’analyseurs.
