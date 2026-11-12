# Documentation technique VeroScript Tooling

`VeroScriptTooling` est la couche publique d’analyse, de service de langage, de workspace et d’intégration debugger partagée par CLI, IDE, LSP et DAP. Cette documentation décrit des developer contracts, et non une utilisation d’éditeur ou de ligne de commande.

## Points d’entrée publics

Services de query C++ : `VeroScriptAnalyzer`, `VeroScriptCallHelp`, `VeroScriptComplete`, `VeroScriptHover`, `VeroScriptNavigation`, `VeroScriptOutline`, `VeroScriptFindReferences`, `VeroScriptDocumentHighlight`, `VeroScriptRename`, `VeroScriptFormat`, `VeroScriptWorkspaceSymbols`. Les entrées state/protocol sont `VeroScriptWorkspace`, `VeroScriptLspStdio`, `VeroScriptDebugger`; celles de debug sont `VeroScriptDebugSession`, `VeroScriptDapAdapter`, `VeroScriptDapStdio`, `VeroScriptDapTcp`. `VeroScriptToolingTypes` et `VeroScriptDapTypes` définissent les données communes.

Types de résultat/option : `FTextPosition`, `FTextRange`, `FDiagnostic`, `FTextEdit`, `FDocumentOverlay`, `FWorkspaceOptions`, `FAnalyzeOptions`, `FCompletionResult`, `FHoverResult`, `FReferencesResult`, `FRenameResult`, `FWorkspaceSymbolsResult`. Les headers publics font autorité sur fields/defaults ; cette page explique leurs contrats entre services.

## Couches

Core possède parsing, semantic analysis, module loading, diagnostic, runtime metadata et debugger semantics. `VeroScriptTooling` consomme uniquement ces contrats publics, sans inspection de l’état compiler/VM privé par C++ embedding.

`VeroScriptWorkspace` possède document overlays, contextes projet/module, résolution source/import, contexte préprocesseur, binding catalogs et état cross-file. Un service interroge une source, un fichier ou un workspace explicite, sans lire global IDE state ni écrire user file. Les adapters LSP/DAP sérialisent les résultats et possèdent framing, correlation, capability, cancellation, protocol error et event order ; ils ne refont pas parser, resolver, formatter, bytecode evaluation, breakpoint ni object lifetime.

## Coordonnées et edits

L’API C++ emploie coordonnées source one-based ; LSP les convertit en positions UTF-16 zero-based. Host ne fait jamais une deuxième conversion indépendante. Result et edit sont associés au document identity/source state ; host applique `FTextEdit` seulement si version/overlay correspondent encore. Tooling ne modifie jamais le document lui-même.

## Localisation et diagnostics

Diagnostic est un record structuré avec ID stable, source location, severity et substitution arguments. Catalog localise display message et fallback English. Diagnostic ID/argument, protocol method, JSON field, language tag, path, URI, symbol name et source/code fragment sont des données techniques non traduites.

LSP `initialize.locale` est une préférence BCP 47 ; si elle n’est pas supportée, process conserve environment-selected catalog et les descriptions standard-namespace suivent le même fallback. Binding catalog identifier/type/member/signature/documentation key reste un identifiant exact, non une ressource localisée.

## Pages détaillées

- [Architecture et protocoles Tooling](architecture-and-protocols.md)
- [Workspace, LSP et DAP](workspace-and-debug.md)
- [Règles de test et extension Tooling](testing-and-extension.md)

## Session Debug et surface DAP

`VeroScriptDebugSession` est un debugger synchronous protocol-neutral. `LoadSource` et `LoadFile` reçoivent `FDebugLoadOptions` : import paths, defines, overlays, optional `IHostBindingCatalog`, implicit imports et policies. Breakpoint : file label, ligne one-based, condition optionnelle. Session expose run mode, requested backend, call depth, JIT policy, dernier `FJitStats`; arrêté, host inspecte `FDebugStackFrame` et `FDebugVariable`, évalue et écrit local/child. `FDebugRunResult` sépare output, error output et diagnostic ; success/exit code sont indépendants.

`VeroScriptDapAdapter` est un adapter DAP-shaped in-process sans JSON/framing/transport. Lifecycle : `Initialize` -> `Launch` ou `Attach` -> `SetBreakpoints` -> `ConfigurationDone`; ce dernier est synchronous et émet stopped/output/exited/terminated. Les appels continue, step, stack, scope, variable, evaluate et set-variable sont valides dans stopped-event callback. `Attach` a les load semantics de `Launch`, n’est pas mid-run VM attach et force backend effectif `Vm`. Live binding vient de `FDapLaunchArgs::Bindings`; processus externe utilise catalog data. Capability/event name vient exactement de `FDapCapabilities` et `EDapEventKind`.

## Source de vérité

Mettez ensemble à jour `Public/`, `Private/`, focused feature/LSP/DAP tests et pages. Chaque public contract exige review de result shape, coordonnée, document version, workspace invalidation, protocol capability, diagnostic/localisation/cancellation/debugger.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
