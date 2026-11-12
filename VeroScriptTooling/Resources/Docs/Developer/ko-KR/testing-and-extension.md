# Tooling test와 extension rule

이 page는 tooling layer extension의 compatibility rule을 기록합니다.

## feature와 protocol coverage

모든 public feature에는 result-level test가 필요하고 LSP/DAP boundary를 넘으면 serialization/transport coverage도 필요합니다. LSP test는 framing, JSON-RPC correlation, malformed input, initialization, lifecycle notification, incremental synchronization, document version, cancellation, diagnostic, configuration, completion, hover, navigation, symbol, semantic token, signature help, reference, highlight, rename, formatting, workspace symbol, shutdown, exit, unsupported-method error를 포함합니다. completion coverage에는 textual-match ranking, `sortText`, conservative `preselect` rule이, semantic token coverage에는 lexer fallback span, incomplete-buffer recovery, primitive type word의 `keyword`, container type word의 `type`이 포함됩니다. built-in type test는 localized hover, source definition 부재, rename refusal도 cover합니다.

qualified completion coverage는 마지막 이름의 `Label`과 `InsertText`가 삽입 contract를 유지하고, `LabelDescription`이 `labelDetails.description`으로 serialize되며, 선택적 `$valencePresentation`이 filtering/sorting을 바꾸지 않는 정확한 UTF-16 display span을 갖는지도 증명해야 합니다.

namespace segment test는 expression/statement completion이 replace range 뒤에 separator가 없을 때만 `::`를 추가하고,
`using namespace` 대상에는 마지막 이름만 삽입하는지도 확인해야 합니다.

workspace test는 overlay, imported-file analysis, import path, define/predefined macro, circular import, project-wide access, ordered binding catalog, duplicate definition, catalog reload, stale diagnostic clear를 포함합니다. localization test는 catalog-key/placeholder parity, fallback, stable diagnostic identifier, localized built-in type display text, non-translatable protocol/language identifier를 검증합니다. test는 runtime에서 catalog directory를 discover하고 fixed locale count를 가정하면 안 됩니다.

## 확장 경계

language semantics를 core에 먼저 추가합니다. Tooling은 public source/AST/diagnostic/module-metadata/debugger API만 consume하고 private compiler/VM state를 query하면 안 됩니다.

new feature에서는 public result/option type(source/range/version/failure semantics), direct service/workspace behavior, safe/complete할 때만 protocol serialization/capability advertisement, genuine user prose localization, focused feature/localization/transport test를 함께 update합니다.

LSP capability는 real safe handler와 일치합니다. unsupported operation을 available로 advertise하지 않으며 handler가 edit/location을 fabricate하면 안 됩니다. server는 completion resolve, type definition, implementation, selection range, document link, full/delta semantic token, document/range/on-type formatting을 advertise합니다. advertise하는 code action kind는 `source.formatDocument`, `source.organizeImports`, `refactor.rewrite`입니다. organize action은 안전하게 삭제할 수 있는 완전히 같은 import가 있을 때만, rewrite action은 type body에 생성할 method가 있을 때만 반환합니다. range formatting은 selection을 complete physical line으로 expand하고 actual replacement range를 반환합니다.

## failure와 recovery

malformed document, incomplete input, invalid request, cancellation, duplicate catalog definition, unsupported locale, stale workspace state는 expected state입니다. recovery는 declaration/range/diagnostic/edit을 fabricate하지 않고 host responsive를 유지합니다.

empty feature result는 protocol/transport failure와 다르고 language diagnostic은 framing error/invalid JSON-RPC request와 다릅니다. direct service, adapter, host는 구분을 보존합니다. tracked version보다 새롭지 않은 change는 ignore하고 clean document는 empty diagnostic list를 publish하며 new analysis에서 사라진 imported file은 previous diagnostic을 clear합니다.

## 검토 항목

publish 전에 header/implementation, range/edit coordinate, document identity/overlay/version, imported diagnostic/stale cleanup, advertised capability handler/test, localized message/placeholder, completion rank/`sortText` 및 conservative `preselect`, 모든 locale의 built-in type hover key와 불변인 identifier/no definition/rename refusal, semantic token legend/lexer recovery, exact protocol token/identifier/path/source fragment, 관련 Tooling/LSP user page를 확인합니다.

## Debug와 DAP coverage

debug test는 source/file loading, overlay, conditional breakpoint, entry/step stop reason, output/error output, script exit code와 독립적인 success, local/child/evaluate/write, call-depth limit, JIT policy snapshot, debugger attachment가 선택하는 `Vm`을 cover합니다. breakpoint test는 multiline declaration/initializer pause location, preprocessor line map, inactive/out-of-range failure, 같은 basename을 공유하는 import, compiler pause marker가 instruction budget을 소모하지 않는 것도 cover합니다.

DAP adapter test는 initialize, launch/attach, breakpoint, configuration-done, stopped, output, exited, terminated, disconnect lifecycle을 test합니다. stopped callback 한정 method는 callback에서 test하고 active run 중 disconnect는 `ConfigurationDone` 후 resume/cleanup합니다. live binding pointer/serialized catalog는 in/out-process test를 분리합니다. 또한 pre-launch pending breakpoint, stable DAP ID, relocated verified line, localized unverified message, post-load `breakpoint` event의 `reason: "changed"`를 cover합니다.

new adapter는 `VeroScriptDebugSession`, `VeroScriptDapAdapter`를 consume하며 compiler, VM inspection, breakpoint resolution, value lifetime을 duplicate하지 않습니다. transport test는 JSON/framing을 in-process DAP contract와 분리합니다. TCP는 IPv4, `Port == 0`, `OnListening`, sequential `MaxSessions`, clean termination, client disconnect, DAP request metadata/locale/script argument/stdlib root/working directory/scoped environment/initial input/collection limit을 verify합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/testing-and-extension.md) · [fr-FR](../fr-FR/testing-and-extension.md) · [ja-JP](../ja-JP/testing-and-extension.md) · [ko-KR](testing-and-extension.md) · [zh-CN](../zh-CN/testing-and-extension.md) · [zh-TW](../zh-TW/testing-and-extension.md)
