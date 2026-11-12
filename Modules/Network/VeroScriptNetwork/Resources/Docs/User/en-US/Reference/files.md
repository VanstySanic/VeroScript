# Files and atomic output

VeroScriptNetworkFiles.h supplies FFileExecutor(context,options), one bounded shared pool per context; later executors must use identical pool options. Defaults: 2 workers, 32 pending tasks, 64 KiB blocks, 2 ms fallback poll. Ranges: 1–8 workers, 1–4096 tasks, blocks at most the context buffer limit and 16 MiB. Excess tasks report QueueFull; blocked streams do not occupy a file worker.

Paths are UTF-8; relative paths require an explicit absolute BaseDirectory, never implicit cwd. Parents are not created; only regular files are supported. Reads fix the range from size at open; later truncation is Read failure, not a concurrent-content snapshot. Drain Read’s Body while running before waiting. Write claims an unconsumed single-reader stream and closes it on success/failure/cancel; reused/cancelled sources report State. Read supports Offset/Length/MaxBytes, write ExpectedLength/MaxBytes. Native counts are uint64 with signed native offset checks; script uses nonnegative int64, -1 only for unknown length/total.

Output uses an exclusive sibling temporary file, flushes before commit by default, and does not overwrite unless ReplaceExisting. Commit switches visible content atomically, without a power-loss directory durability promise. PrefixPath/PrefixLength copies an unchanged exact-length prefix in bounded blocks, possibly to the same target only with replacement authorized. ExpectedLength includes the prefix. Short/excess declared lengths report Protocol, MaxBytes excess Limit. Results/quota are prepared before commit.

Uncommitted temporaries are removed unless KeepPartial, then PartialPath identifies them. Logical cancellation may race system I/O/rename and cannot undo committed files. Progress includes Bytes/optional TotalBytes/Path/PartialPath/CleanupSystemCode/PhysicalComplete and Commit 0 Uncommitted, 1 Committing, 2 Committed, 3 Failed. Wait for PhysicalComplete and inspect Commit/error; cleanup failure preserves the native code. Executor.Close stops only its tasks, context.Shutdown joins all file workers and may wait for uninterruptible system calls. Workers do not execute scripts. Managed quotas exclude OS caches/caller copies.

Script create_file_executor or create_file_executor_with_limits configures the pool. make_file_read_options/make_file_write_options set path/base, ranges, byte/time limits; write options add expected length/prefix/commit policy. Keep draining/polling rather than blocking HostDriven work.

## Defaults

| API | Value |
| --- | --- |
| `FFileReadOptions::MaxBytes` | `256 MiB` |
| `FFileWriteOptions::MaxBytes` | `256 MiB` |
| `bReplaceExisting / bKeepPartial / bFlushToDisk` | `false / false / true` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](../../zh-TW/Reference/files.md)
