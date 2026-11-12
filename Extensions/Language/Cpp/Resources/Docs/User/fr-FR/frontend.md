# Frontend C++

## Fusion de projet et limites de représentation

La fusion multi-fichier n'est pas un linker : elle ne choisit pas une
définition selon l'ordre des entrées, ne renomme pas les entités à liaison
interne et ne résout pas les règles arbitraires de possession ou de stockage.
`std::shared_ptr<T>` n'est pas réduit à `T` ; la possession partagée exige un
adaptateur explicite. `std::size_t` et `std::ptrdiff_t` dépendent de l'ABI cible,
et les qualifications cv du pointeur et du pointeur désigné font partie de
l'identité de surcharge.

`VeroScriptCppFrontend` analyse un sous-ensemble volontairement borné de C++17
dans un AST public stable. Il est partagé par [BindGen](bindgen.md) et
[Transpiler](transpiler.md). Les constructions non prises en charge produisent
un diagnostic et ne sont jamais approximées silencieusement.

## Opérations publiques

Le header public fournit `ParseSource`, `ParseFile`, `ParseFiles`,
`MergeTranslationUnits`, `MapTypeToVeroScript`, `ResolveTypeAliases`,
`EvaluateEnumValues`, `TryGetEnumValueRange`, `GetFunctionIdentity`,
`FormatDiagnostic` et `JoinTokens`. L’AST conserve les emplacements source,
espaces de noms, accès, noms qualifiés, constructeurs, champs, fonctions,
énumérations, bases, références, qualification cv des pointeurs et
qualificateurs tels que `constexpr`, `consteval`, `constinit` et `noexcept`.

`FParseOptions` contrôle les includes, `#pragma`, le rejet des directives inconnues et le traitement des corps de fonctions. `FTypeMapOptions` définit les politiques explicites pour les pointeurs bruts vers des types hôtes connus, les références rvalue, les types hôtes et énumérés connus, les paramètres de type et les alias nécessaires lorsque les includes sont ignorés. Configurez ces politiques explicitement : une analyse permissive ne garantit pas qu’une déclaration puisse être liée ou transpilée.

Le fournisseur accepte les espaces de noms, `using Name = Type` et les alias
`typedef Type Name`, ainsi que les templates de classes, fonctions et alias à
paramètres de type avec des types par défaut finaux, énumérations
scopées ou non, classes, structures, accès, champs, constructeurs, méthodes,
fonctions libres, globals, entiers de largeur fixe, chaînes standard et
conteneurs ayant un équivalent VeroScript. Les déclarations anticipées nommées
sont fusionnées avec leurs définitions. Les chaînes brutes ordinaires et `u8`
sont normalisées en littéraux UTF-8 ; les chaînes brutes larges sont refusées.
Les listes simples de déclarateurs de même type sont développées, tandis que
les listes mixtes pointeur/référence restent refusées.

Les définitions hors classe de constructeurs, destructeurs, méthodes,
opérateurs et membres statiques peuvent être appariées, y compris depuis un
autre fichier du projet. Les attributs C++17 sont conservés dans l'AST pour les
types, énumérations, énumérateurs, champs, fonctions et paramètres. L'évaluation
des énumérations accepte les expressions entières vérifiables et les caractères
portables. Elle conserve la largeur et le signe des littéraux selon l'ABI cible,
applique les promotions entières C++, autorise le rebouclage non signé et refuse
les dépassements signés ainsi que les nombres de décalage invalides. Le résultat
doit tenir dans la représentation publique `int64` et le type sous-jacent mappé.
Les décalages de valeurs signées négatives et les expressions `&&`/`||` à
court-circuit restent refusés.

Sans type sous-jacent explicite, une énumération non scoped conserve le type
d'initialisation des énumérateurs précédents ; un type sous-jacent fixe utilise
les promotions de ce type. Pour les enums qui dépendent de cette distinction,
un hôte MSVC doit activer `/Zc:enumTypes` (VS 2022 17.4 ou ultérieur), ou donner
un type sous-jacent explicite. Cette option est désactivée par défaut, même avec
`/permissive-` ; le SDK ne modifie pas les options du compilateur hôte.

## Frontière du préprocesseur et fusion

`#pragma once`, les includes, les gardes de header conventionnelles et les
blocs ou déclarations `extern "C"` sont acceptés. Les conditions générales et
les directives inconnues sont refusées. La collecte multi-fichiers fusionne les
déclarations externes compatibles, mais rejette définitions dupliquées,
chemins répétés, collisions de linkage interne et espaces de noms anonymes.

Le merge n’est pas un linker : il ne choisit pas une définition selon l’ordre
des entrées et ne change pas les noms privés.

Les paramètres de template non typés, packs, spécialisations, SFINAE, concepts,
expressions de compilation dépendantes, unions, tableaux C, espaces de noms
anonymes et directives inconnues sont refusés. Les déclarations `friend` de type,
fonction ou membre sont conservées au niveau de l'enregistrement, mais les
définitions friend inline sont refusées. Un pointeur mutable unique vers un
type hôte connu peut être autorisé par la politique, mais les pointeurs
scalaires, vers const et multi-niveaux restent exclus. `std::shared_ptr<T>`
n’est pas réduit à `T`.

La recherche de types privilégie la portée de déclaration et prend en charge les noms qualifiés relatifs et les noms absolus `::`. Les types utilisateur déclarés sont prioritaires sur les correspondances pratiques telles que `string` et `vector` ; un type qualifié non résolu produit une erreur.

Les classes, énumérations et paramètres de type liés sont prioritaires sur les alias des portées externes.
Pour résoudre les alias manuellement, utilisez la surcharge à trois arguments de `ResolveTypeAliases`
avec les options `KnownHostTypes`, `KnownEnumTypes` et `KnownTypeParameters`.

Une valeur vide de `FType::DeclarationScope` désigne la portée globale et reste inchangée
lors de la substitution des alias. `FBaseType::Type` conserve la structure des types de base,
leur portée et leurs arguments de modèle. `bQualifyNamedTypes` demande des noms utilisateur absolus
pour le code généré ; sa valeur par défaut conserve les noms canoniques du catalogue.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/frontend.md) · [fr-FR](frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
