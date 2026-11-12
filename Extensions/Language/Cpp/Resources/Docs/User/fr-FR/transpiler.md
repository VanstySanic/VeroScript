# Transpiler C++

`VeroScriptCppTranspiler` convertit un sous-ensemble C++17 strict et
préservant la sémantique en source VeroScript. Il partage l’AST et le mapping
de types du [frontend C++](frontend.md), mais reste indépendant de BindGen et
de l’ABI Interop.

## Conversion

```powershell
VeroScriptCppTranspiler Resources/Examples/Basic.cpp `
  -o build/Basic.vs `
  --source-map build/Basic.vs.map.json
```

La CLI accepte aussi des mappings répétables `--attribute-map cpp=vero`.
Les attributs des espaces `veroscript::` et `vero::` sont abaissés directement ;
les autres espaces exigent un mapping explicite. La validation de la source
générée est active par défaut ; utilisez `--no-validate` uniquement si une étape
externe prend cette vérification en charge.

Les options CLI sont `-o`/`--output` pour une sortie unique, `--source-map` pour
sa carte source, `--output-dir` pour le mode projet, `--manifest` pour le
manifeste, `--attribute-map` (répétable) pour les attributs, `--no-validate`,
`--version` et `--help`. Le mode projet émet une source et une carte par entrée.

Dans l’API C++, `FTranspileOptions` contrôle cette validation, le chemin logique
utilisé par les cartes sources et les diagnostics, les types hôtes et énumérés
connus supplémentaires, les alias de types connus et les mappings explicites
d’attributs. Les options de projet transmettent la validation et les mappings
d’attributs à la sortie du projet.

Le résultat comprend la source VeroScript et une carte source optionnelle. La
source générée est vérifiée par le compilateur VeroScript par défaut et n’est
écrite qu’après analyse, abaissement et validation réussis.

Le mode projet accepte plusieurs entrées et produit un fichier `.vs`, une
carte source par entrée et un manifeste déterministe. Les entrées sont toutes
analysées et fusionnées avant l’émission ; le manifeste déclare les imports
implicites et l’accès au projet. Les collisions de basename reçoivent un hash
de chemin déterministe et les déclarations et définitions compatibles sont
fusionnées.

Le manifeste de projet utilise des champs stables : `version`, `kind`,
`allowProjectWideAccess`, `modulePaths`, `implicitImportPaths` et `files`.
Chaque entrée de `files` contient `input`, `source` et `sourceMap`. Conservez le
manifeste, les sources générées et les cartes sources de la même exécution ; les
chemins des fichiers générés sont relatifs au projet.

Exemple de conversion de projet :

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp `
  --output-dir build/generated `
  --manifest build/generated/transpile.manifest.json
```

Exemple de structure du manifeste :

```json
{
	"version": 1,
	"kind": "veroscript.transpiled-project",
	"allowProjectWideAccess": true,
	"modulePaths": ["Types.vs"],
	"implicitImportPaths": ["Types.vs"],
	"files": [
		{
			"input": "Source/Types.cpp",
			"source": "Types.vs",
			"sourceMap": "Types.vs.map.json"
		}
	]
}
```

Le sous-ensemble couvre le code par valeur, espaces de noms, énumérations,
classes, structures, constructeurs, méthodes, globals, conteneurs,
`this->member`, `static_cast` scalaire, les alias `typedef`/`using`, les chaînes
brutes ordinaires ou `u8`, ainsi que `throw`, `try`/`catch`, le catch général et
le rethrow. Les modèles d’alias sont développés dans les déclarations typées.
Les templates de classes, structures et fonctions libres dont les paramètres
sont uniquement `typename`/`class` sont abaissés en génériques ouverts VeroScript,
avec des types par défaut finaux autorisés.
Les attributs sont abaissés en annotations VeroScript ; les attributs de
paramètres restent réservés à BindGen. Les définitions hors classe et membres
statiques correspondants sont fusionnés en mode projet.

Les opérations de propriété, pointeurs arbitraires, RTTI, coroutines, formes
avancées de templates C++, ownership partagé, allocation, globals `extern` non
résolus, champs ou globals de référence, `goto` et formes d’attribut ou de
`friend` non représentables sont refusés. Les fonctionnalités non représentables
ne sont jamais supprimées silencieusement.

Les paramètres de templates non typés, packs, spécialisations, SFINAE, concepts,
expressions de compilation dépendantes, listes de paramètres de méthode locale
et appels de fonctions avec arguments de template explicites restent hors du
sous-ensemble. Un alias générique dans un corps opaque, une séquence friend ou
un initialiseur est refusé.

Les corps des méthodes ordinaires restent dans leur module de définition, avec
une déclaration dans le module du type. Les constructeurs, destructeurs et données
statiques suivent les contraintes du noyau concernant le même fichier. La validation
compile les modules finaux avec la visibilité du manifeste et rapporte les erreurs
dans les fichiers C++ d'origine. Une fusion qui perd la portée de recherche est
refusée. Les définitions hors classe des templates de classes restent non prises en charge.

La fusion de projet n’est pas un éditeur de liens. Les définitions qualifiées d’un espace de noms restent des déclarations de cet espace. Les fonctions ou variables à liaison interne en collision produisent `cpp.merge.internal_linkage_collision` ; l’outil ne les renomme pas et ne choisit pas une définition selon l’ordre des entrées. Les sorties sont validées ensemble pour préserver l’accord entre sources générées et cartes source.

## Types numériques et littéraux entiers

Les variables locales et les cibles de `static_cast` utilisent la même correspondance de plateforme que les signatures. Les littéraux entiers choisissent leur type selon le modèle de données C++17 de la plateforme de compilation de l’outil, puis utilisent les suffixes du script ; les valeurs octales, binaires et avec séparateurs sont préservées. Les flottants hexadécimaux et les littéraux `long double` sont explicitement rejetés. Les conversions exigent une valeur dans la plage cible et signalent sinon une erreur du script ; le code dépendant d’une troncature C++ ou d’une promotion mélangeant les types signés et non signés doit être réécrit explicitement.

`std::pair<T,U>` devient le builtin souple `pair<T,U>`. Si un template utilisateur visible de même nom et arité capture ce type, la traduction échoue avec `cpp.transpile.standard_pair_shadowed`, même en mode projet et sans validation finale. Renommez le template ou fournissez un adaptateur explicite. Une autre arité ou un espace de noms sans rapport peut coexister.

Les paramètres de modèle VeroScript ne peuvent pas masquer un type ou un alias ordinaire global.
Ces déclarations C++ sont rejetées avant toute sortie avec `cpp.transpile.template_parameter_shadow_unsupported`,
y compris en mode projet et sans validation du code généré. Renommez le paramètre C++ avant la transpilation.
Les modèles d’alias sont résolus structurellement et conservent leurs propres paramètres liés.

Les déclarations structurées utilisent des noms utilisateur absolus, y compris dans les arguments
de modèle, afin de préserver leur identité dans les espaces de noms de sortie. Les types de base
conservent leur portée d’origine. La syntaxe cible ne peut pas exprimer une base globale masquée
par un homonyme lexical : `cpp.transpile.global_base_shadowed` est signalé avant toute sortie,
même sans validation du code généré.
Les bases conteneurs intégrés et les bases dépendant d’un paramètre de type sont rejetées avec
`cpp.transpile.base_type_unsupported` ; l’héritage nécessite un type classe ou structure connu.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/transpiler.md) · [fr-FR](transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
