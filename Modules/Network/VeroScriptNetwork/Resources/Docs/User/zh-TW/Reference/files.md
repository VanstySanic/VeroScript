# 檔案與原子輸出

FFileExecutor 每 context 共用一有界池，後續執行器須同選項。預設 2 worker、32 task、64 KiB block、2ms 檢查；允許 1–8 worker、1–4096 task，block 不超 context 上限或 16 MiB。超出 QueueFull，等串流不占 worker。

UTF-8 相對路徑須明確絕對 BaseDirectory，不用 cwd、不建父目錄、僅一般檔案。讀取以開啟時長度定範圍，後續縮短為 Read，不保證並行內容快照。執行中持續排空 Read.Body 再等待。Write 接管未消費單 reader，成功/失敗/取消皆結束；重用/取消來源為 State。Offset/Length/MaxBytes、ExpectedLength/MaxBytes 以原生 uint64 及有號位移檢查，腳本非負 int64，-1 僅未知長度/總量。

目標旁獨占暫存，預設 flush 後 commit，僅 ReplaceExisting 覆寫。可見性切換原子，但不保證斷電目錄持久。PrefixPath/Length 要求來源正好該長度，分塊複製不改來源；同目標須允許替換。ExpectedLength 含前綴，長度不符 Protocol、超限 Limit；commit 前備妥結果/額度。

未 commit 預設刪除，KeepPartial 以 PartialPath 保留。取消可能與系統 I/O/rename 競爭，不能撤回已 commit。Progress 含 Bytes/TotalBytes/Path/PartialPath/CleanupSystemCode/PhysicalComplete 與 Commit=0 Uncommitted/1 Committing/2 Committed/3 Failed。等 PhysicalComplete 後合併狀態/錯誤。Close 僅本執行器，Shutdown 等全部 worker，可能等待不可中斷系統呼叫。worker 不執行腳本，OS cache/呼叫端複本不計。腳本 executor[_with_limits]、read/write 選項管理範圍/限制/期限/預期長度/前綴/提交；持續排空並推進 HostDriven。

## 預設值

| API | 值 |
| --- | --- |
| `FFileReadOptions::MaxBytes` | `256 MiB` |
| `FFileWriteOptions::MaxBytes` | `256 MiB` |
| `bReplaceExisting / bKeepPartial / bFlushToDisk` | `false / false / true` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](files.md)
