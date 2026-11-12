# Installer 生命週期與安全性

## component 與 staging

`Core/` 負責 manifest validation、bundle archive、package metadata、path safety、platform information、retry 與 install engine。 `App/` 負責 graphical host 與 localized progress；`Tool/` 負責 bundle/build entry point。Core 可脫離 graphical app 使用。

installation 對 selected component 進行 stage、verify copied file、寫 installation record，並 commit transition。record 跟蹤 product identity、component root、owned file、manifest fingerprint、runtime PATH mutation。

變更 PATH 或 live component directory 前，engine 在每個 affected component root 旁寫 atomic transaction journal，並持有 exclusive per-parent-directory transaction lock。之後的 install/repair/uninstall/`LoadRecordAsync` 會 recover interrupted prepared transaction，或完成 durable committed transaction 的 cleanup。journal copy 按 conservative 規則 reconcile，因此 final commit marker persist 中 crash 不會 rollback completed commit。deterministically selected copy 是 durable authority，最後才 delete；post-commit backup cleanup failure 會由 later operation retry，且不會把 successful installation report 成 rollback。successful rollback 同樣在 remove journal 前記錄 terminal state，以免 partial journal delete 在 new installation 後 replay PATH change。

`LoadRecordAsync` 因而不是 read-only inspection API：它會在開啟 `.veroscript-install.json` 前 recover requested component root 旁的 transaction。GUI repair/uninstall startup 與 Tool record loading 都走同一路徑，所以 interrupted operation 期間移到 backup 的 record 能在 UI display 前 restore。active competing installer 可導致 lock error；recovery 不會 bypass lock。

installation 會在 old record load 前檢查 selected destination root，及提供時包含 `ExistingInstallRecordPath` 的 root。這支援以 different new destination retry interrupted relocation。state 為 `prepared`、`committed`、`rolledback`；terminal journal 是 cleanup marker，不得 replay PATH change。

## 安全邊界

destination root 不得與 protected payload 或彼此 overlap。managed destination/ancestor 檢查 link。relative path 在 intended root 下 resolve；traversal、absolute escape、foreign entry、ambiguous ownership 均 fail closed。retried filesystem operation 不能讓 partial operation 成為 successful。

## repair、relocation 與 uninstall

repair 要求 record 的 product version/platform/architecture/manifest fingerprint。relocation 在 move 前 validate existing/sibling record、ownership、destination safety、staged path。uninstall 先 stage all unowned file，再 remove owned PATH entry，並將所有 component 作為 one recoverable transaction commit。它只 remove recorded file/owned PATH entry，preserve unowned file。durable commit 前 failure 會 rollback；failed rollback 留下 journal/backup 供 next recovery。durable commit 後的 cleanup failure 不會將 success report 為 failure。

operation active 或等待 recovery 時，不要手動 delete transaction journal、stage directory、backup。應以 original record/root reopen 同一 product operation。recovery 再次 failure 時保留檔案，inspect reported filesystem/PATH error 後再 retry。

## 迴歸測試涵蓋範圍

test suite 覆蓋 install、update、relocation、optional-component removal、repair、uninstall、user-file preservation、PATH ownership。recovery case 包括 multi-parent journal reconciliation、stale terminal journal、GUI startup 前 record loading、透過 old record root retry relocation。依[發行驗收流程](release-validation.md)驗證候選產物並記錄結果，區分引擎測試與人工 GUI 測試。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/lifecycle-and-safety.md) · [fr-FR](../fr-FR/lifecycle-and-safety.md) · [ja-JP](../ja-JP/lifecycle-and-safety.md) · [ko-KR](../ko-KR/lifecycle-and-safety.md) · [zh-CN](../zh-CN/lifecycle-and-safety.md) · [zh-TW](lifecycle-and-safety.md)
