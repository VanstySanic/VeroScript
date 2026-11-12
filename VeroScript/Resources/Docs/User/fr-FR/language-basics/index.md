# Bases du langage

Cette section décrit les règles de base du langage VeroScript par sujet. Chaque page couvre un sujet relativement indépendant et utilise de petits exemples pour définir la syntaxe et le comportement. Les API complètes et les mécanismes d'exécution sont documentés séparément.

## Source, modules et portée

- [Syntaxe de base](basic-syntax.md) : fichiers sources, instructions, blocs, identifiants et mots-clés.
- [Commentaires](comments.md) : commentaires sur une seule ligne et bloc.
- [Portée](scope.md) : portées du module, de l'espace de noms, de la fonction et du bloc.
- [Espaces de noms](namespaces.md) : déclarations d'espaces de noms, noms qualifiés et `using namespace`.
- [L'espace de noms `std`](standard-namespace.md) : la liste complète des types intégrés, des fonctions d'exécution et des alias de compatibilité.
- [Modules](modules.md) : importations, exportations, réexportations de modules et importations de paquets de bibliothèque.
- [Préprocesseur](preprocessor.md) : macros, compilation conditionnelle, stringification, collage de jetons et inclusion de modules.

## Valeurs et types

- [Littéraux](literals.md) : entiers, valeurs à virgule flottante, chaînes, chaînes larges, booléens et valeurs nulles.
- [Types de données](data-types.md) : types intégrés, entiers à largeur fixe, conteneurs et inférence de type.
- [Types utilitaires de valeurs](value-utility-types.md) : optionnels, références faibles, handles de structures partagées et paires d’entrées de carte.
- [Variables](variables.md) : déclarations, initialisation, affectation et variables déduites.
- [Constantes](constants.md) : variables `const` et membres constants.
- [Déclarations](declarations.md) : `using`, `typedef`, `= default` et `= delete`.
- [Structs](structs.md) : types de valeurs, champs, méthodes, construction et sémantique de copie de structure.
- [Classes](classes.md) : identité partagée, accès des membres, construction, héritage et méthodes virtuelles.
- [Interfaces](interfaces.md) : contrats de méthode, implémentations par défaut, héritage d'interface et conversions.
- [Enums](enums.md) : énumérateurs, portée, types entiers sous-jacents et valeurs d'énumération.
- [Arrays](arrays.md) : tableaux dynamiques, tableaux natifs de taille fixe, indexation et itération de plage.
- [Conteneurs](containers.md) : cartes, ensembles, files d'attente, piles, deques et canaux.
- [Strings](strings.md) : `string`, `wstring`, opérations sur les chaînes et assistants de bibliothèque standard.
- [Références](references.md) : paramètres de référence, références en lecture seule et itération de la plage de référence.
- [Réflexion](reflection.md) : `type<T>`, informations sur le champ et la méthode, informations sur l'énumération et métadonnées de déclaration.
- [Membres statiques](static-members.md) : types utilitaires statiques, champs de niveau type, méthodes statiques et stockage indépendant dans les types génériques.

## Expressions et opérateurs

- [Opérateurs](operators.md) : arithmétique, comparaison, logique, au niveau du bit, affectation et surcharge d'opérateurs.

## Génériques, fonctions et flux de contrôle

- [Génériques](generics.md) : types génériques, fonctions génériques, arguments de type par défaut et contraintes `where`.
- [Fonctions](functions.md) : déclarations, paramètres, valeurs de retour, valeurs par défaut et surcharges.
- [Lambdas et valeurs de fonction](lambdas.md) : types de fonctions, expressions lambda, captures et paramètres de référence.
- [Control Flow](control-flow.md) : conditions, boucles, `switch`, sauts et retours.

[Annotations et métadonnées source](annotations.md)

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/index.md) · [fr-FR](index.md) · [ja-JP](../../ja-JP/language-basics/index.md) · [ko-KR](../../ko-KR/language-basics/index.md) · [zh-CN](../../zh-CN/language-basics/index.md) · [zh-TW](../../zh-TW/language-basics/index.md)
