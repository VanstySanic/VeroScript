# Tooling test と extension rule

この page は tooling layer extension の compatibility rule を記録します。

## feature と protocol coverage

全 public feature は result-level test が必要で LSP/DAP boundary を越える feature は serialization/transport coverage も必要です。LSP test は framing、JSON-RPC correlation、malformed input、initialization、lifecycle notification、incremental synchronization、document version、cancellation、diagnostic、configuration、completion、hover、navigation、symbol、semantic token、signature help、reference、highlight、rename、formatting、workspace symbol、shutdown、exit、unsupported-method error を含みます。completion coverage は textual-match ranking、`sortText`、conservative `preselect` rule を、semantic token coverage は lexer fallback span、incomplete-buffer recovery、primitive type word の `keyword` と container type word の `type` を含みます。built-in type test は localized hover、source definition がないこと、rename refusal も cover します。

qualified completion の coverage では、末尾名の `Label` と `InsertText` が挿入 contract のままであること、`LabelDescription` が `labelDetails.description` に serialize されること、任意の `$valencePresentation` が filtering/sorting を変えず正しい UTF-16 display span を持つことも証明します。

namespace segment の test では、expression/statement completion が置換範囲の後ろに separator がない場合だけ
`::` を追加することと、`using namespace` の target には末尾名だけを挿入することも確認します。

workspace test は overlay、imported-file analysis、import path、define/predefined macro、circular import、project-wide access、ordered binding catalog、duplicate definition、catalog reload、stale diagnostic clear を含みます。localization test は catalog-key/placeholder parity、fallback、stable diagnostic identifier、localized built-in type display text、non-translatable protocol/language identifier を検証します。test は runtime で catalog directory を discover し fixed locale count を仮定しません。

## 拡張の境界

language semantics は core に先に追加します。Tooling は public source/AST/diagnostic/module-metadata/debugger API だけを消費し private compiler/VM state を query しません。

新 feature では public result/option type（source/range/version/failure semantics）、direct service/workspace behavior、safe/complete な場合だけの protocol serialization/capability advertisement、genuine user prose の localization、focused feature/localization/transport test を同時に更新します。

LSP capability は real safe handler と一致します。unsupported operation を available と表示せず handler が edit/location を fabricate しません。server は completion resolve、type definition、implementation、selection range、document link、full/delta semantic token、document/range/on-type formatting を advertise します。advertise する code action kind は `source.formatDocument`、`source.organizeImports`、`refactor.rewrite` です。organize action は安全に削除できる完全一致の重複 import がある場合だけ、rewrite action は type body に生成可能な method がある場合だけ返します。range formatting は selection を complete physical line に広げ actual replacement range を返します。

## failure と recovery

malformed document、incomplete input、invalid request、cancellation、duplicate catalog definition、unsupported locale、stale workspace state は expected state です。recovery は declaration/range/diagnostic/edit を fabricate せず host responsive を維持します。

empty feature result は protocol/transport failure と異なり、language diagnostic は framing error/invalid JSON-RPC request と異なります。direct service、adapter、host は区別を保ちます。tracked version より新しくない change は ignore、clean document は empty diagnostic list、new analysis で消えた imported file は previous diagnostic を clear します。

## レビュー項目

publish 前に header/implementation、range/edit coordinate、document identity/overlay/version、imported diagnostic/stale cleanup、advertised capability の handler/test、localized message/placeholder、completion rank/`sortText` と conservative `preselect`、全 locale の built-in type hover key と不変の identifier/no definition/rename refusal、semantic token legend/lexer recovery、exact protocol token/identifier/path/source fragment、関連 Tooling/LSP user page を確認します。

## Debug と DAP coverage

debug test は source/file loading、overlay、conditional breakpoint、entry/step stop reason、output/error output、script exit code と独立した success、local/child/evaluate/write、call-depth limit、JIT policy snapshot、debugger attachment の `Vm` 選択を cover します。breakpoint test は multiline declaration/initializer の pause location、preprocessor line map、inactive/out-of-range failure、同じ basename を共有する import、compiler pause marker の instruction budget が増えないことも cover します。

DAP adapter test は initialize、launch/attach、breakpoint、configuration-done、stopped、output、exited、terminated、disconnect lifecycle を test します。stopped callback 限定 method は callback 内で test し active run 中 disconnect は `ConfigurationDone` 後 resume/cleanup します。live binding pointer/serialized catalog は in/out-process test を分離します。さらに pre-launch pending breakpoint、stable DAP ID、relocated verified line、localized unverified message、post-load `breakpoint` event の `reason: "changed"` を cover します。

new adapter は `VeroScriptDebugSession` と `VeroScriptDapAdapter` を consume し compiler、VM inspection、breakpoint resolution、value lifetime を duplicate しません。transport test は JSON/framing を in-process DAP contract と分離します。TCP は IPv4、`Port == 0`、`OnListening`、sequential `MaxSessions`、clean termination、client disconnect、DAP request metadata/locale/script argument/stdlib root/working directory/scoped environment/initial input/collection limit を verify します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/testing-and-extension.md) · [fr-FR](../fr-FR/testing-and-extension.md) · [ja-JP](testing-and-extension.md) · [ko-KR](../ko-KR/testing-and-extension.md) · [zh-CN](../zh-CN/testing-and-extension.md) · [zh-TW](../zh-TW/testing-and-extension.md)
