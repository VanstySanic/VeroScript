# Documentation technique VeroScript Web

La forme à espace unique reste également exacte : `void main(string[] args)` et `int main(string[] args)`.
Ce répertoire documente le SDK WebAssembly sans UI. Il est lié à `VeroScriptWebAssembly.h`, `VeroScriptWebAssemblyC.h`, bindings TypeScript, build Emscripten et browser tests ; il ne documente pas un éditeur, HTML application, CSS framework ou frontend product.

## Surfaces publiques

Le module a trois surfaces : API wrapper d’implémentation C++ `VeroScript::WebAssembly::FRuntime`, ABI C Web avec runtime handles opaques/result/virtual-file/language/catalog/run, et module/declaration TypeScript générés pour browser. La surface TypeScript doit rester en parity avec les contrats C/C++.

Runtime Web dépend du language core, C++ host API et des standard-library/localization assets partagés. Il ne crée pas de second language ou bytecode version contract.

## Politiques d’exécution

Voir [ABI Web, artefacts et contrat de release — Capacités des programmes et des entrées](release-contract.md#web-execution-policy).

Le baseline publié n’annonce pas script-level worker threads. Un script unbounded est arrêté par termination de son Web Worker owner. Interactive input suspend runtime pendant l’attente provider ; une seule interactive run est active par Wasm module.

## Règles artifact et binding

Standard library/localization catalog sont intégrés au virtual filesystem. Release artifact set, generated declaration et binding parity check forment un contrat unique. Modifier `VeroScriptWebAssemblyC.h`, binding TypeScript, result field, backend name, language tag ou generated file name impose de mettre à jour parity/browser tests.

Language selection utilise BCP 47 canonical tags découverts depuis embedded catalog. Ne hard-codez jamais le nombre de langues. Catalog key, API identifier, backend name, diagnostic et version value ne sont pas traduits.

## Détails API runtime

`createVeroScriptRuntime()` crée runtime Asyncify-capable pour interactive input ; `createFastVeroScriptRuntime()` est la factory low-latency lorsque input est fourni d’avance. `runSource` est synchronous après module creation ; `runSourceInteractive` retourne promise et suspend VM/script state pendant l’attente provider. Une seule interactive run est active ; provider exception rejette après sortie sûre de l’attente.

runSource accepte file label, optional source overlay, standard input, script argument et backend. Les entrées `void main(string[]
args)` et `int main(string[]
args)` sont supportées ; integer non zéro reste success et retourne `exitCode`. Embedded NUL est rejeté. Spawn n’est pas baseline browser : worker script tenté rapporte diagnostic structuré plutôt que suspendre Wasm.

Quand l’artefact annonce `supportsCompiledPrograms`, `compileSource` compile une source graph une fois et retourne owned `VeroScriptProgram`. Les `run` répétés ont fresh state en réutilisant program ; `runInteractive` n’est disponible que sur artefact Asyncify. Program est disposed avant runtime ; runtime disposal libère les handles restants. Capability compiled-program ne change pas language/bytecode version.

## Pages détaillées

- [Runtime et bindings Web](runtime-and-bindings.md)
- [ABI Web, artefacts et contrat de release](release-contract.md)
- [Contrat de build et des artefacts Web](build-and-artifacts.md)

## Handle runtime/program ABI C

Le runtime handle C est contrôlé par un caller à la fois. Le pointer d’un result-producing call appartient au runtime jusqu’à next replacing operation ou runtime destruction. Pendant Asyncify input wait, stateful getter/text operation sont indisponibles : host réessaie après fin de l’appel interactive.

vero_web_supports_threads indique si artifact est construit avec pthread-backed script concurrency. L’artefact browser default utilise plutôt Web Workers indépendants pour program concurrent. Cette capability est distincte des flags SDK supportsInteractiveInput/supportsCompiledPrograms.

Les context entry points reçoivent des virtual-file path/source pairs empruntés. Compile crée runtime-owned program handle ; run_program/run_program_interactive réutilisent compiled module avec fresh state. vero_web_destroy_program libère ce handle et doit être appelé avant abandon de runtime propriétaire si deterministic cleanup est requis.

## Source de vérité

Utilisez headers Web publics, `Bindings/`, implementation Web native, build scripts Emscripten, artifact audit, declaration parity check, smoke/browser tests. Mettez à jour ce document avec tout changement WebAssembly ABI, artifact généré, execution policy, input-provider, cancellation ou asset-packaging.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
