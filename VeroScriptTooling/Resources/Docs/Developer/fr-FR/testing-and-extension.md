# Règles de test et extension Tooling

Cette page consigne les compatibility rules pour étendre tooling layer.

## Couverture feature et protocol

Chaque public feature exige result-level test ; une frontière LSP/DAP exige aussi serialization/transport coverage. La surface LSP test couvre framing, JSON-RPC correlation, malformed input, initialization, lifecycle notification, incremental synchronization, document version, cancellation, diagnostic, configuration, completion, hover, navigation, symbol, semantic token, signature help, reference, highlight, rename, formatting, workspace symbol, shutdown, exit et unsupported-method error. La couverture completion inclut textual-match ranking, `sortText` et la règle `preselect` conservative ; les semantic tokens couvrent lexer fallback spans, incomplete-buffer recovery, les types primitifs comme `keyword` et les types conteneurs comme `type`. Les tests de types intégrés couvrent aussi le survol localisé, l'absence de définition source et le refus de renommage.

La couverture des complétions qualifiées doit aussi prouver que `Label` et `InsertText` conservent le nom final comme contrat d'insertion, que `LabelDescription` se sérialise dans `labelDetails.description`, et que `$valencePresentation` facultatif porte des plages d'affichage UTF-16 exactes sans modifier filtrage ni tri.

Les tests des segments namespace doivent aussi vérifier que la complétion d'un
chemin d'expression/d'instruction n'ajoute `::` que si aucun séparateur ne suit
la plage remplacée, et que `using namespace` n'insère que le nom final.

Workspace test couvre overlay, imported-file analysis, import path, define/predefined macro, circular import, project-wide access, ordered binding catalog, duplicate catalog definition, catalog reload et stale diagnostic clearing. Localization test vérifie catalog-key/placeholder parity, fallback, diagnostic identifier stable, built-in type display text localisé et preservation des protocol/language identifier non traduisibles. Les tests découvrent catalog directory au runtime sans supposer fixed locale count.

## Limite d’extension

Ajoutez d’abord language semantics au core. Tooling consomme seulement public source, AST, diagnostic, module-metadata et debugger API ; une query tooling ne contourne pas cette limite vers private compiler/VM state.

Pour une nouvelle feature, mettez à jour ensemble public result/option type (source/range/version/failure semantics), direct service/workspace behavior, protocol serialization/capability advertisement seulement si safe et complete, localization de vraie prose utilisateur, et focused feature/localization/transport test.

Les capacités LSP annoncées doivent correspondre aux gestionnaires sûrs réellement
disponibles. Une méthode non prise en charge ne doit pas être annoncée, et un
gestionnaire ne doit pas inventer d'édition ni d'emplacement. Le serveur annonce
notamment la résolution des complétions, la définition de type, l'implémentation,
les plages de sélection, les liens de document, les jetons sémantiques complets
et incrémentaux, ainsi que le formatage du document, d'une plage et à la saisie.
Les types d'action annoncés sont `source.formatDocument`,
`source.organizeImports` et `refactor.rewrite` ; des actions rewrite sont
proposées lorsque des méthodes héritées peuvent être générées. `source.organizeImports` n'est proposée que si des lignes
d'import exactement dupliquées peuvent être identifiées et supprimées sans
risque. Le formatage d'une plage l'étend aux lignes physiques complètes et
renvoie la plage de remplacement effective.

## Échec et recovery

Malformed document, incomplete input, invalid request, cancellation, duplicate catalog definition, unsupported locale et stale workspace state sont attendus. Recovery maintient host responsive sans fabriquer declaration/range/diagnostic/edit.

Empty feature result diffère protocol/transport failure ; language diagnostic diffère framing error/invalid JSON-RPC request. Direct service, adapter et host préservent ces différences. Version non plus récente est ignorée ; document clean publie diagnostic list vide ; fichier importé disparu clear ses diagnostics précédents.

## Points à vérifier

Avant publish, confirmez headers/implementation, coordinate contract des ranges/edits, document identity/overlay/version, imported diagnostic/stale cleanup, handler/test de chaque advertised capability, localized message/placeholder, cohérence completion rank/`sortText` et `preselect` conservateur, clés de hover built-in type dans chaque locale avec identifier/absence de definition/refus de rename invariants, legend/lexer recovery des semantic tokens, token/identifier/path/source fragment exacts et document Tooling/LSP user-facing associé.

## Couverture Debug et DAP

Debug test couvre source/file loading, overlay, conditional breakpoint, entry/step stop reason, output/error output, succès indépendant script exit code, locals/children/evaluate/write, call-depth limit, JIT policy snapshot et debugger attachment qui sélectionne `Vm`. Les tests de breakpoint couvrent aussi les lignes de pause des déclarations/initialiseurs multilignes, les preprocessor line maps, les échecs inactive/hors limite, les imports qui partagent un basename, et l’absence de coût sur le budget d’instructions pour les pause markers du compilateur.

DAP adapter test couvre lifecycle initialize, launch/attach, breakpoint, configuration-done, stopped, output, exited, terminated, disconnect. Les méthodes valides seulement depuis stopped callback sont testées dans callback ; disconnect durant active run reprend puis cleanup après `ConfigurationDone`. Live binding pointer et serialized catalog demandent in-process/out-of-process test séparés. Ils couvrent également les breakpoints pending avant lancement, les ID DAP stables, les lignes vérifiées déplacées, les messages non vérifiés localisés et les événements post-load `breakpoint` avec `reason: "changed"`.

New protocol adapter consomme `VeroScriptDebugSession` et `VeroScriptDapAdapter`, sans dupliquer compiler, VM inspection, breakpoint resolution ou value lifetime. Transport test sépare JSON/framing de in-process DAP contract. TCP couvre IPv4, `Port == 0`, `OnListening`, sequential `MaxSessions`, clean termination mapping et client disconnect ; DAP request couvre initialize metadata, locale, script argument, stdlib root, working directory, scoped environment, initial input et collection-limit validation.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/testing-and-extension.md) · [fr-FR](testing-and-extension.md) · [ja-JP](../ja-JP/testing-and-extension.md) · [ko-KR](../ko-KR/testing-and-extension.md) · [zh-CN](../zh-CN/testing-and-extension.md) · [zh-TW](../zh-TW/testing-and-extension.md)
