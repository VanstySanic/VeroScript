# Frontend C++, BindGen et Transpiler

## AST frontend

`VeroScriptCppFrontend` analyse un sous-ensemble C++17 borné en modèle stable de tokens, positions, attributs, types, paramètres, modèles, records, enums, variables, fonctions, alias `typedef`/`using`, namespaces et corps. Lorsqu’ils sont représentables, noms qualifiés, linkage C, qualificateurs, constructeurs/destructeurs, friends et définitions hors classe du même fichier sont préservés. `FParseOptions` contrôle include/pragma/directives/corps ; `FTypeMapOptions` contrôle pointeurs hôte bruts, références rvalue, types connus, paramètres et alias. Ce sont des options explicites.

Le collecteur fusionne déclarations compatibles/anticipées et peut associer une définition de membre conventionnelle à un record complet d’un autre input. Il rejette doublons, chemins répétés, collision de linkage interne, namespace anonyme, modèles non pris en charge et construction dont propriété ou flot de contrôle n’est pas préservable.

## Génération de Binding Catalog

BindGen émet Binding Catalog JSON, références API optionnelles et manifeste. Le catalogue fusionné est autoritaire ; les références sont des artefacts de revue et ne compilent jamais. Le projet résout toutes les déclarations, désambiguïse les basenames de façon déterministe et remplace les sorties en une transaction.

## Transpileur de source

Transpiler ne lower que les constructions ayant un sens VeroScript direct, émet source et source maps de déclaration, puis valide par le compilateur réel. Propriété, RTTI, pointeurs, modèles avancés, éléments `std` et attributs non représentables échouent explicitement.

Les membres `operator()` et `operator[]` conservent leurs surcharges et paramètres par référence. La conversion source utilise `T&` / `const T&` pour les signatures par référence, y compris les retours ; les arguments des opérateurs se lient implicitement aux références selon les règles du cœur. BindGen conserve les indicateurs d’opérateur et de paramètre par référence, mais refuse les retours natifs par référence, dont le stockage et la durée de vie ne sont pas représentables dans le Binding Catalog actuel.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/frontend-and-tools.md) · [fr-FR](frontend-and-tools.md) · [ja-JP](../ja-JP/frontend-and-tools.md) · [ko-KR](../ko-KR/frontend-and-tools.md) · [zh-CN](../zh-CN/frontend-and-tools.md) · [zh-TW](../zh-TW/frontend-and-tools.md)
