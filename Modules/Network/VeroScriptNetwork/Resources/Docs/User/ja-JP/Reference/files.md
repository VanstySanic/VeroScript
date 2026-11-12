# ファイルと原子的出力

FFileExecutor は context ごとに一つの共有 pool、後続 executor は同設定が必要です。既定 2 worker/32 task/64 KiB block/2ms poll、範囲は 1–8/1–4096、block は context 上限かつ 16 MiB 以下。超過 QueueFull、stream 待機中は worker を占有しません。

path は UTF-8、相対には絶対 BaseDirectory 必須、暗黙 cwd/親作成なし、通常 file のみです。open 時長さを固定し、その後短縮は Read、並行内容 snapshot は保証しません。Read.Body を処理中に排出してから待ちます。Write は未消費一 consumer を取得し全結果で終了、再利用/取消 source は State。Offset/Length/MaxBytes、ExpectedLength/MaxBytes、native uint64 と signed offset を検査、script は非負 int64、-1 は不明のみ。

隣接 exclusive temporary、既定 flush 後 commit、ReplaceExisting のみ上書きです。可視切替は原子的でも電源断 directory durability は保証しません。PrefixPath/Length は正確な長さの prefix を有界コピーし元を変更せず、同 target は明示上書き時のみ。ExpectedLength に prefix を含み、長短 Protocol、上限 Limit。commit 前に result/quota を準備します。

未 commit temporary は既定削除、KeepPartial 時 PartialPath。取消は system I/O/rename と競合し、commit を取り消しません。Progress は Bytes/TotalBytes/Path/PartialPath/CleanupSystemCode/PhysicalComplete と Commit=0 Uncommitted/1 Committing/2 Committed/3 Failed。PhysicalComplete を待ち Commit/error を確認します。executor.Close は自分だけ、context.Shutdown は全 worker を join し不可停止 syscall を待つ場合があります。file worker は script を実行せず、quota は OS cache/caller copy を含みません。script executor[_with_limits] と read/write options で範囲/上限/期限/長さ/prefix/commit を設定し、HostDriven 駆動と排出を続けます。

## 既定値

| API | 値 |
| --- | --- |
| `FFileReadOptions::MaxBytes` | `256 MiB` |
| `FFileWriteOptions::MaxBytes` | `256 MiB` |
| `bReplaceExisting / bKeepPartial / bFlushToDisk` | `false / false / true` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](../../zh-TW/Reference/files.md)
