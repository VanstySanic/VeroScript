# BindGen C++

`VeroScriptCppBindGen` transforme le sous-ensemble de déclarations C++ pris en
charge en catalogue de bindings VeroScript. Le catalogue est une métadonnée
pour le compilateur et les outils, pas une implémentation runtime. Les stubs
de callbacks générés ne sont pas des thunks d’appel natifs ; l’adaptateur hôte
doit enregistrer les vraies fonctions C++.

## Entrées et sorties

`GenerateBindings` accepte un AST. `GenerateBindingsFromSource`,
`GenerateBindingsFromFile`, `GenerateBindingsFromFiles` et
`GenerateBindingProject` acceptent des sources. Le mode projet produit un
`CatalogJson` de référence, des `ApiReferences` optionnelles, un
`ManifestJson` déterministe, des diagnostics et `Succeeded()`.

`FBindGenOptions::bEmitApiReference` contrôle la référence lisible. Catalogue,
références et manifeste sont écrits transactionnellement ; les collisions de
basename reçoivent un hash de chemin déterministe.

`FBindGenProjectLayout` choisit le catalogue fusionné, le dossier des références API et le manifeste du projet. Fournissez le catalogue fusionné au compilateur et à l’IDE ; les références par entrée sont des vues documentaires et ne remplacent pas cet ensemble de déclarations.

Le manifeste de projet contient les champs stables `version`, `kind`,
`catalogRole`, `catalog`, `inputs` et `apiReferences`. Chaque entrée de
`apiReferences` contient `input` et `path`. Le catalogue fusionné est émis une
seule fois ; les références sont des vues lisibles facultatives.

Le mode projet écrit le catalogue fusionné, les références API par entrée et le
manifeste comme un ensemble déterministe :

```json
{
	"version": 1,
	"kind": "veroscript.cpp-binding-project",
	"catalogRole": "merged",
	"catalog": "bindings.json",
	"inputs": ["Include/Types.hpp"],
	"apiReferences": [
		{
			"input": "Include/Types.hpp",
			"path": "api/Types.vs.txt"
		}
	]
}
```

```powershell
VeroScriptCppBindGen Resources/Examples/BindingInput.hpp Resources/Examples/BindingExtras.hpp `
  --catalog build/BindingInput.bindings.json `
  --api-reference build/BindingInput.vs.txt
```

Pour produire une référence par entrée et un manifeste de projet :

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

Les options CLI sont `--catalog <output.json>` (obligatoire),
`--api-reference <output.vs.txt>`, `--api-reference-dir <directory>`,
`--manifest <output.json>`, `--version` et `--help`. Les deux options de
référence sont mutuellement exclusives.

BindGen conserve les noms qualifiés, l’identité et le type sous-jacent des
énumérations, les genres de membres, constructeurs, opérateurs, méthodes
`const`, paramètres `ref`/`const ref`, alias de types et modèles d’alias. Les
valeurs par défaut d’alias ne sont pas représentables dans le catalogue. Les
attributs sur les types, énumérations, champs, fonctions et paramètres sont
écrits dans les annotations structurées ; les attributs sans espace et les
espaces réservés `veroscript::`/`vero::` sont acceptés, les autres doivent être
explicitement mappés. Les initialiseurs d'énumération acceptent les entiers, les
littéraux caractère portables, les énumérateurs précédents, les parenthèses et
les opérateurs unaire, arithmétique, de décalage, bit à bit, booléen, logique,
relationnel et d'égalité, avec contrôle du dépassement `int64`. Les attributs
sans valeur restent des indicateurs et les valeurs nommées sont conservées dans
le catalogue.

Il refuse notamment pointeurs, retours par référence, héritage hôte-hôte,
membres supprimés, champs de référence, globals libres, surcharges de méthodes
nommées, surcharges libres, arguments par défaut, fonctions à linkage interne
et valeurs d’énumération hors de la plage du type sous-jacent. Les expressions
d’énumération prises en charge sont évaluées avec contrôle de plage.

Voir [Bindings hôte](bindings.md) pour le format complet du catalogue et la
frontière d’enregistrement runtime.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/bindgen.md) · [fr-FR](bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
