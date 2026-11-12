# Installer lifecycle と safety

## component と staging

`Core/` は manifest validation、bundle archive、package metadata、path safety、platform information、retry、install engine を所有します。`App/` は graphical host/localized progress、`Tool/` は bundle/build entry point を所有し Core は graphical app なしで使用可能です。

installation は selected component を stage し copied file を verify、installation record を書き transition を commit します。record は product identity、component root、owned file、manifest fingerprint、runtime PATH mutation を追跡します。

PATH/live component directory 変更前に engine は affected component root ごとに atomic transaction journal を書き exclusive per-parent-directory transaction lock を保持します。後の install/repair/uninstall/`LoadRecordAsync` は interrupted prepared transaction を recover 又は durable committed transaction の cleanup を完了します。journal copy は conservative に reconcile され final commit marker persist 中の crash でも completed commit を rollback しません。deterministically selected copy が durable authority で最後に delete され、post-commit backup cleanup failure は後で retry し successful installation を rollback と report しません。successful rollback も journal remove 前の terminal state で、partial journal delete が new installation 後 PATH change を replay しません。

`LoadRecordAsync` は read-only inspection API ではなく requested component root の隣の transaction を recover してから `.veroscript-install.json` を開きます。GUI repair/uninstall startup と Tool record loading は同じ path を使い interrupted operation 中 backup に移った record を UI 前に restore できます。active competing installer は lock error を起こせ、recovery は lock を bypass しません。

installation は selected destination root と、指定時は `ExistingInstallRecordPath` を含む root の両方を old record load 前に check します。different new destination で interrupted relocation retry を扱います。state は `prepared`、`committed`、`rolledback`。terminal journal は cleanup marker で PATH change を replay しません。

## 安全性の境界

destination root は protected payload/互いに overlap しません。managed destination/ancestor は link を検査します。relative path は intended root の下で resolve し traversal、absolute escape、foreign entry、ambiguous ownership は fail closed。retry filesystem operation は partial operation を successful にしません。

## repair、relocation、uninstall

repair は record の product version/platform/architecture/manifest fingerprint を必要とします。relocation は existing/sibling record、ownership、destination safety、staged path を validate してから move します。uninstall は all unowned file を先に stage、owned PATH entry を remove、全 component を one recoverable transaction で commit します。recorded file/owned PATH entry のみを remove し unowned file を保存します。durable commit 前 failure は rollback、failed rollback は next recovery 用 journal/backup を残します。durable commit 後の cleanup failure は success を failure と report しません。

active operation/recovery 待ちの transaction journal、stage directory、backup を手動 delete しません。original record/root で同じ product operation を reopen します。recovery がまだ failure なら file を保持し reported filesystem/PATH error を inspect してから retry します。

## 回帰テストの範囲

test suite は install/update/relocation/optional-component removal/repair/uninstall、user-file preservation、PATH ownership を cover します。recovery は multi-parent journal reconciliation、stale terminal journal、GUI startup 前 record loading、old record root 経由 relocation retry を含みます。[公開の検証手順](release-validation.md)に従って候補を検証し、結果を記録してください。エンジンのテストと GUI の手動検証は区別します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/lifecycle-and-safety.md) · [fr-FR](../fr-FR/lifecycle-and-safety.md) · [ja-JP](lifecycle-and-safety.md) · [ko-KR](../ko-KR/lifecycle-and-safety.md) · [zh-CN](../zh-CN/lifecycle-and-safety.md) · [zh-TW](../zh-TW/lifecycle-and-safety.md)
