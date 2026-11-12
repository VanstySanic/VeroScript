# Normes TypeScript et JavaScript (provisoires)

Portée : [développement et contributions au projet VeroScript](README.md).

TypeScript/JavaScript suivent [GENERAL.md](GENERAL.md) pour les groupes, commentaires et explications nécessaires minimales. La règle de retour à la ligne des clauses ci-dessous est obligatoire ; les autres accolades suivent provisoirement le style existant du module.

## Règles provisoires actuelles

- Placez `else`, `else if`, `catch`, `finally` et le `while` final de `do…while` sur de nouvelles lignes
  selon [GENERAL.md](GENERAL.md). L’accolade ouvrante d’un bloc occupe aussi une ligne distincte alignée sur sa clause.
- Utilisez une tabulation réelle par niveau logique, affichée sur quatre colonnes par défaut ; suivez les règles d’indentation de [GENERAL.md](GENERAL.md).
- Regroupez types, fonctions, conditions, boucles et `switch` par sens ; évitez une compression sur une ligne sans intérêt.
- Tout `switch` exige `default`. Le contrôle d’exhaustivité TypeScript peut s’y trouver, mais ne permet pas de l’omettre ; traitez explicitement les inconnus dans ce bloc.
- Chaque corps `case`/`default` utilise une portée indépendante avec accolades, sans poursuite implicite. Expliquez le partage volontaire du traitement près du bloc. Seule exception : le chemin unique termine directement la fonction par `return`, sans autre instruction.
- Toutes les branches de contrôle utilisent des accolades. Seule exception : une garde sur une ligne limitée à un `return` direct ou `return <expression>`, sans `else`, journalisation, nettoyage ni autre instruction.
- Les types au niveau du module TypeScript suivent `enum` → `interface` → `type`/autres alias → `class`. Interfaces et classes génériques restent dans leur catégorie. Quand JavaScript n’a pas ces déclarations, gardez la même clarté par les relations d’export et dépendances.
- Une classe suit l’ordre global C++ : alias/constantes associés, implémentations héritées ou d’interfaces, types imbriqués, méthodes et champs ; les champs terminent leur région d’accès sémantique.
- Utilisez au besoin les marqueurs appariés `//~ Begin <Name> Interface` / `//~ End <Name> Interface` pour plusieurs interfaces ou groupes d’adaptateurs.
- Types exportés, interfaces et classes utilisent PascalCase ; fonctions, champs, paramètres et variables locales utilisent camelCase. Les champs de protocole externe conservent leur nom contractuel.
- Exprimez par les types ou commentaires nécessaires la sémantique des données publiques, erreurs et libérations de ressources.

## À compléter

Décisions possibles : style d’accolades, guillemets/points-virgules/virgules finales, `readonly`, annulation asynchrone, cycles de vie Wasm et exigences de vérification de types.
