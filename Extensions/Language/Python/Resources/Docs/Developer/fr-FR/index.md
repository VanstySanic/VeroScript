# Documentation technique VeroScript Python


Ce répertoire documente les contrats source/binaire pour mainteneurs et auteurs de binding, non un tutoriel utilisateur. Syntaxes, API/modules, options, clés JSON et diagnostics restent exactement ceux des sources.

## Propriété des modules

`VeroScriptPython` est l’adaptateur CPython sur l’ABI C Interop stable et possède sessions, valeurs typées, containers, paquets, callbacks, réflexion, callables retenus, sérialisation et classes hôte Python. `VeroScriptPythonAot` émet source Python autonome ou paquet source multi-fichier, sans VM/Interop/extension. `VeroScriptPythonFrontend` parse statiquement le sous-ensemble de déclarations Python 3.11 typé sans importer/exécuter. `VeroScriptPythonBindGen` convertit les déclarations en Binding Catalog/références API ; `VeroScriptPythonTranspiler` lower le sous-ensemble typé et valide par le compilateur réel.

## Frontière des outils statiques

Frontend, BindGen et Transpiler n’importent, n’exécutent ni n’utilisent les effets runtime d’un module input. Feature dynamique, signature non typée, sémantique précision arbitraire non préservable, async, réflexion et héritage non supporté échouent explicitement. Le mode projet résout le graphe complet avant sortie ; layout relatif, module, import, source map, référence API et manifeste restent cohérents, et aucun échec parse/lowering/import/validation ne remplace la sortie.

## Frontière runtime et code généré

L’adaptateur CPython libère le GIL pour le travail natif permis et le reprend pour les callbacks. Restriction creator-thread, propriété callable, réentrance callback et synchronisation d’état Python partagé sont des contrats publics ; un callback worker n’appelle pas une opération session confinée. Le runtime AOT autonome préserve les sémantiques entiers/conteneurs/exceptions/modules/concurrence pour le sous-ensemble exécutable documenté. Ses workers utilisent des threads OS, isolent globales/statiques et préservent l’identité channel comme partage synchronisé explicite.

## Objets Python et artefacts

Les objets runtime principaux sont `Session`, `Value`, `Package`, handle callable retenu, `HostParameter` et objets d’enregistrement hôte. Une session est fermée explicitement sur son thread créateur. Les containers utilisent un type d’élément explicite, de sorte que containers vides/étroits ne dépendent pas d’une inférence Python ; itération et `keys()`/`values()`/`items()` retournent des snapshots possédés. Callbacks hôte et mutation ont des règles d’exception/thread distinctes et leur état s’auto-synchronise.

Le Bundle AOT contient manifeste et fichiers runtime de compatibilité. Ses fichiers sont préparés avant le remplacement, permis seulement pour un répertoire ordinaire vide ou à manifeste Python AOT VeroScript structurellement valide sans contenu étranger ; les `__pycache__` reconnus sont les seuls extras tolérés. Les liens finaux et jonctions Windows sont refusés ; le chemin peut être brièvement absent pendant le remplacement, et sa restauration est tentée en cas d’échec. Le programme généré choisit l’interpréteur Python utilisateur et ne découvre ni n’embarque de SDK Python.

## Packaging et version

Wheel et distribution source portent extension native, Interop, sources Python, types, bibliothèque standard et assets de localisation concordants. `VeroScriptVersion.h` est la source sémantique autoritaire, à laquelle métadonnées package et version runtime importée doivent correspondre. Le mode projet frontend émet un AST par input et un manifeste de projet ; BindGen émet un catalogue fusionné, une référence par input et `bindgen.manifest.json`; Transpiler préserve le layout, résout imports relatifs, émet maps et écrit `transpile.manifest.json` seulement après validation du graphe complet. Import échoué, module double, déclaration ambiguë ou vérification finale laisse la sortie précédente intacte.

## Pages détaillées

- [Runtime et outils statiques Python](runtime-and-tools.md)
- [AOT et packaging Python](aot-and-packaging.md)
- [Outillage et contrat de paquet Python](toolchain-and-packaging.md)

## Source de vérité

Sources Python, code adaptateur natif, AST frontend, tests générateur/transpiler, CMake, métadonnées package et outils de validation de distribution font foi. Tout changement de sous-ensemble supporté, règle de thread, artefact généré, layout de package ou mapping ABI met à jour cette documentation.


[Python sur Android et iOS](mobile.md)

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
