# Tooling 測試與擴充套件規則

本文記錄擴充套件 tooling layer 的 compatibility rule。

## feature 與 protocol coverage

每個 public feature 都需要 result-level test；跨越 LSP/DAP boundary 的 feature 還需要 serialization/transport coverage。LSP test 覆蓋 framing、JSON-RPC correlation、malformed input、initialization、lifecycle notification、incremental synchronization、document version、cancellation、diagnostic、configuration、completion、hover、navigation、symbol、semantic token、signature help、reference、highlight、rename、formatting、workspace symbol、shutdown、exit、unsupported-method error。completion coverage 還要驗證 textual-match ranking、`sortText` 與 conservative `preselect` rule；semantic token coverage 要驗證 lexer fallback span、incomplete-buffer recovery、primitive type word 的 `keyword` 與 container type word 的 `type`。built-in type test 還要覆蓋 localized hover、沒有 source definition 和 rename refusal。

qualified completion coverage 還必須證明：葉節名稱 `Label` 與 `InsertText` 仍是插入 contract、`LabelDescription` 會 serialize 為 `labelDetails.description`，選用的 `$valencePresentation` 具有正確 UTF-16 display span 且不改變 filtering 或 sorting。

namespace segment 測試也要分別驗證：expression/statement 補全只在 replace range 後面沒有 separator 時追加 `::`；補全
`using namespace` 目標時只插入葉節名稱。

workspace test 覆蓋 overlay、imported-file analysis、import path、define/predefined macro、circular import、project-wide access、ordered binding catalog、duplicate definition、catalog reload、stale diagnostic clear。localization test 驗證 catalog-key/placeholder parity、fallback、stable diagnostic identifier、localized built-in type display text 與 non-translatable protocol/language identifier。test 必須在 runtime discover catalog directory，不得假設 fixed locale count。

## 擴充邊界

先將 language semantics 加入 core。Tooling 只 consume public source、AST、diagnostic、module-metadata、debugger API；tooling query 不得繞過 boundary inspect private compiler/VM state。

new feature 需同步更新 public result/option type（source/range/version/failure semantics）、direct service/workspace behavior、僅在 safe/complete 時的 protocol serialization/capability advertisement、僅真正 user-facing prose 的 localization，以及 focused feature/localization/transport test。

LSP capability 必須與 real safe handler 一致。unsupported operation 不得 advertize 為 available，handler 不得 fabricate edit/location。server advertize completion resolve、type definition、implementation、selection range、document link、full/delta semantic token，以及 document、range、on-type formatting。advertise 的 code action kind 有 `source.formatDocument`、`source.organizeImports`、`refactor.rewrite`；只有能安全移除完全重複的 import 時才返回整理 action，type body 空游標位置有可生成 method 時才返回 rewrite action。range formatting 將 selection expand 到 complete physical line，並返回 actual replacement range。

## failure 與 recovery

malformed document、incomplete input、invalid request、cancellation、duplicate catalog definition、unsupported locale、stale workspace state 都是 expected state。recovery 必須保持 host responsive，不能 fabricate declaration/range/diagnostic/edit。

empty feature result 不同於 protocol/transport failure；language diagnostic 不同於 framing error/invalid JSON-RPC request。direct service、adapter、host 必須保持這些區分。document version 不比 tracked version 新時 ignore change；document clean 時 publish empty diagnostic list；new analysis 中消失的 imported file 應 clear previous diagnostic。

## 審查清單

publish 前確認 header/implementation、range/edit coordinate、document identity/overlay/version、imported diagnostic/stale cleanup、每項 advertised capability 的 handler/test、localized message/placeholder、completion rank/`sortText` 與 conservative `preselect`、每個 locale 的 built-in type hover key 及不變的 identifier/no definition/rename refusal、semantic token legend/lexer recovery、exact protocol token/identifier/path/source fragment，以及對應 Tooling/LSP user page。

## Debug 與 DAP coverage

debug test 覆蓋 source/file loading、overlay、conditional breakpoint、entry/step stop reason、output/error output、獨立於 script exit code 的 success、local/child/evaluate/write、call-depth limit、JIT policy snapshot，以及 debugger attachment 選擇 `Vm`。breakpoint test 也要覆蓋 multiline declaration/initializer 的 pause location、preprocessor line map、inactive/out-of-range failure、共用 basename 的 import，以及 compiler pause marker 不消耗 instruction budget。

DAP adapter test 覆蓋 initialize、launch/attach、breakpoint、configuration-done、stopped、output、exited、terminated、disconnect lifecycle。僅 stopped callback 合法的 method 必須在 callback 中 test；active run 中 disconnect 應在 `ConfigurationDone` 返回後 resume/cleanup。live binding pointer/serialized catalog 需要分別做 in/out-process test。也要覆蓋 pre-launch pending breakpoint、stable DAP ID、relocated verified line、localized unverified message，以及 post-load `breakpoint` event 的 `reason: "changed"`。

new protocol adapter 應 consume `VeroScriptDebugSession` 與 `VeroScriptDapAdapter`，不得 duplicate compiler、VM inspection、breakpoint resolution、value lifetime。transport test 單獨 verify JSON/framing，不能改變 in-process DAP contract。TCP 還覆蓋 IPv4、`Port == 0`、`OnListening`、sequential `MaxSessions`、clean termination、client disconnect，以及 DAP request metadata/locale/script argument/stdlib root/working directory/scoped environment/initial input/collection limit。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/testing-and-extension.md) · [fr-FR](../fr-FR/testing-and-extension.md) · [ja-JP](../ja-JP/testing-and-extension.md) · [ko-KR](../ko-KR/testing-and-extension.md) · [zh-CN](../zh-CN/testing-and-extension.md) · [zh-TW](testing-and-extension.md)
