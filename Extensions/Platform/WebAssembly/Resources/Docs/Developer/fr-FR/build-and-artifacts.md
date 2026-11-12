# Contrat de build et des artefacts Web

## Modules Emscripten

La cible CMake Web lie `VeroScriptWebAssembly` au core et crée deux executables depuis le module entry point partagé : `veroscript-engine` avec support interactive input et `veroscript-engine-fast` sans celui-ci. Les deux embarquent `stdlib/` et `localization/` dans le virtual filesystem.

Les modules générés exportent allocation, requêtes de WebAssembly ABI version/thread capability, create/destroy, language selection, catalog discovery, source run/check, source option et interactive-context entry point. Cette liste exportée appartient au SDK contract généré.

## Génération des sources SDK

Le build copie TypeScript source et declaration dans WebAssembly output directory. Native public header, TypeScript declaration générée et parity test doivent concorder. Un generated file ne doit jamais être modifié à la main pour masquer une divergence.

Depuis la racine du dépôt, régénérez les déclarations avec la notice SPDX et une indentation par tabulations grâce à la commande ci-dessous. Exécutez ensuite `npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run typecheck` pour vérifier la parité.

```sh
npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run generate:declarations
```

## Vérifications de release

Artifact audit exige l’ensemble complet interactive/fast, les declarations générées et SDK source. Browser smoke test couvre headless execution, interactive input, event-loop behavior, Web Worker execution et worker termination. Size budget est une limite de release review, pas une ABI limit.

## Parité export et declaration

La liste Emscripten générée comprend thread-capability query, language/catalog query, source option/context call, compiled-program create/execute/destroy, interactive program execution et context checking. Le binding TypeScript doit feature-detect les additive compiled-program export afin que les artefacts antérieurs à cette capability restent utilisables sans inventer d’opération non supportée.

Les browser test couvrent virtual-file array emprunté, program-handle reuse/cleanup, fresh state à chaque program run, interactive input quand un program est suspendu, language discovery/selection, result-pointer lifetime et branch thread-capability. Ces vérifications ne changent aucun langage ni bytecode version contract.

## Espace de sauvegarde interactif

Le moteur interactif alloue sur le tas Wasm une pile de sauvegarde Asyncify de 1 MiB lors de la suspension, puis la libère après la restauration de l’exécution. Elle permet d’attendre une entrée dans des appels imbriqués de méthodes liées. La réentrée native profonde reste limitée par la pile d’appels de l’hôte. Le moteur rapide n’alloue aucun espace Asyncify.

Cette pile est distincte de la pile C/C++ de 16 MiB et conserve les frames natives lors des réentrées de méthodes liées dans la VM. Les contrôles de publication suspendent et restaurent deux fois une chaîne de 32 appels imbriqués, depuis le source et un programme compilé, avec `vm` et `mixed`. Ils vérifient aussi que le cœur rejette 128 réentrées imbriquées avec `diag.call_stack_overflow` avant toute demande d’entrée, puis que le même programme s’exécute avec une imbrication moindre sous Node et Chromium.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/build-and-artifacts.md) · [fr-FR](build-and-artifacts.md) · [ja-JP](../ja-JP/build-and-artifacts.md) · [ko-KR](../ko-KR/build-and-artifacts.md) · [zh-CN](../zh-CN/build-and-artifacts.md) · [zh-TW](../zh-TW/build-and-artifacts.md)
