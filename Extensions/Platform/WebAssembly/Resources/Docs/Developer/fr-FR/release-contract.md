# ABI Web, artefacts et contrat de release

## Trois interfaces publiques

`VeroScriptWebAssembly.h` est la façade C++, `VeroScriptWebAssemblyC.h` définit l’ABI C WebAssembly, et le wrapper TypeScript avec ses déclarations forme le SDK pour navigateur. Maintenez la cohérence des champs de résultat communs, des noms de backend, des tags de langue et du comportement des entrées. L’ABI Interop native distincte ne définit pas l’ABI WebAssembly.

## Noms et compatibilité

Utilisez le module et la cible CMake `VeroScriptWebAssembly`, l’espace de noms C++ `VeroScript::WebAssembly` et l’entrée TypeScript `veroscript-webassembly.ts`. L’archive SDK est `VeroScriptWebAssemblySDK` et l’installateur `VeroScriptWebAssemblyInstaller`. Conservez les noms C `vero_web_*`, `VeroWeb*` et `VERO_WEB_*` à la version ABI `0.1.1`. Le profil `web`, le composant `web-sdk` et l’identifiant produit `dev.veroscript.web` désignent les enregistrements d’installation existants.

## Artefacts et vérification de publication

Publiez les huit fichiers : les paires `.mjs`/`.wasm` interactive et rapide, le wrapper `.ts`/`.d.ts` et les deux déclarations moteur `.d.mts`. La bibliothèque standard et les catalogues sont intégrés au système de fichiers virtuel. Vérifiez les types des bindings et déclarations, contrôlez l’inventaire complet et exercez le SDK généré dans Chromium sans interface, avec entrée interactive, Web Workers et arrêt des Workers. L’audit des artefacts applique en Release des budgets de 17 MiB pour le Wasm interactif et de 11 MiB pour le Wasm rapide. Ce sont des limites de publication, non d’ABI ; la vérification des sources seule ne prouve pas l’acceptation dans le navigateur.

La base actuelle comprend les nombres exacts et les portées des types imbriqués, locaux et génériques.

Le produit Web intègre statiquement `VeroScriptBytecodeVm`. Les vérifications de source, les extensions de compilation, les exécutions directes et les programmes réutilisables utilisent cet interpréteur explicitement sélectionné.

Les deux moteurs Wasm prennent en charge les captures initialisées, les fermetures `mutable` et les variables locales `static` via Core. Les captures par valeur sont en lecture seule par défaut ; copier une valeur de fonction partage son environnement, tandis qu'une nouvelle évaluation du lambda crée un nouvel environnement. Les variables statiques locales sont initialisées à leur première utilisation et peuvent réessayer après un échec. Chaque exécution, y compris interactive, repart d'un état neuf avec de nouvelles variables statiques locales. L'ABI Web et l'API TypeScript restent inchangées.

## ABI et localisation

La version sémantique du runtime, la version ABI WebAssembly, la version du bytecode, les capacités et la langue sélectionnée sont distinctes. Découvrez les tags BCP 47 disponibles à l’exécution. Ne traduisez ni identifiants d’API, ni champs de protocole, ni ID de diagnostic, ni versions, et ne déduisez aucun comportement d’un nombre fixe de langues.

## Capacités des programmes et des entrées

Maintenez l’accord entre exports C, déclarations TypeScript et détection des capacités. La compilation réutilisable exige les exports de compilation, d’exécution et de destruction ; l’exécution interactive exige aussi l’export interactif Asyncify. Un export additionnel absent signifie que la capacité est indisponible ; ne fabriquez jamais de handle.

Les tableaux de contexte contiennent autant de chemins que de sources UTF-8, empruntés seulement pendant l’appel. Les handles appartiennent au runtime créateur et ne se transfèrent pas entre instances. Le JSON des résultats contient diagnostics, sorties, politique de backend demandée, durées et compteurs JIT. La vérification de publication couvre réutilisation des handles, état neuf à chaque exécution, destruction, suspension interactive et sélection de langue.

<a id="web-execution-policy"></a>

Les modules publiés ne sélectionnent aucun backend JIT. `vm` interprète le bytecode ; `mixed` reste le défaut et utilise également la VM. `jit` renvoie le diagnostic structuré `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory` avant les initialisations applicatives, `main` ou une demande de saisie. Les niveaux d’exécution JIT natifs appartiennent à `Modules/Execution/VeroScriptAsmJit` ; les modules Web n’intègrent ni ne chargent dynamiquement ce backend JIT. Les appelants qui utilisaient `jit` doivent choisir `vm` ou `mixed`. Les noms des politiques et les identifiants C ABI restent stables ; `result.backend` indique la politique demandée et tous les compteurs JIT sont nuls.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/release-contract.md) · [fr-FR](release-contract.md) · [ja-JP](../ja-JP/release-contract.md) · [ko-KR](../ko-KR/release-contract.md) · [zh-CN](../zh-CN/release-contract.md) · [zh-TW](../zh-TW/release-contract.md)
