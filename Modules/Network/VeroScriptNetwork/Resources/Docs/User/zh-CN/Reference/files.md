# 文件流与原子输出

C++ 公共入口为 `VeroScriptNetworkFiles.h`；
脚本入口属于 `Network`。依赖、注册及驱动方式见 [模块说明](README.md)。

## 文件执行器

`FFileExecutor(context, options)` 将打开、读写、关闭、临时文件回收及提交交给有界文件线程池，
网络 I/O 线程只处理有界字节流和状态。每个 context 共用一个池；后续执行器的选项必须与首个一致。
默认 2 个工作线程、最多 32 个未完成文件任务、64 KiB 分块、2 ms 后备检查间隔。
允许 1–8 个线程、1–4096 个任务；块大小不能超过 context 的单缓冲上限或 16 MiB。
超出任务数返回 `QueueFull`。等待读者或写者时不占用文件线程。

路径为 UTF-8。绝对路径直接使用；相对路径必须提供显式、绝对的 `BaseDirectory`。
不隐式使用进程当前目录，不自动创建父目录。仅处理普通文件。读取以打开时的文件长度确定范围，
之后缩短导致 `Read`；不会承诺同一文件被其他程序并发改写时仍有内容快照。

`Read` 返回任务及有界 `Body()`，先持续消费正文，再等任务结束；先等待大文件读取完成而不排空正文会被背压阻塞。
`Write` 接管一个尚未消费的 readable stream；成功接管后，失败、取消及完成都会结束该来源。
已被接管或取消的来源返回 `State`，不能由两个消费者同时读取。
读取可指定 Offset、可选 Length、MaxBytes；输出可指定 ExpectedLength 与 MaxBytes。
数量使用原生 `uint64`，文件位置还须可表示为原生有符号文件偏移。脚本数量使用非负 `int64`；
未知长度、未知进度总数使用 `-1`，不把其他负值当作有效长度。

## 原子输出与取消

写入在目标旁创建独占临时文件，默认刷入文件后提交。默认不覆盖已有目标；
显式启用 ReplaceExisting 后原子替换目标。提交提供文件可见性的原子切换，
不承诺系统断电后的目录持久性。任务的结果对象和保留额度在提交前准备，避免提交后再分配结果。

`PrefixPath/PrefixLength` 可把已有文件复制为新输出前缀；已有文件的实际长度必须等于 PrefixLength。
复制有界分块执行，不修改前缀文件；输出也可以在显式允许替换时使用同一目标。
ExpectedLength 约束的是包含前缀的最终长度。来源提前结束或超出已声明长度返回 `Protocol`；
超出 MaxBytes 返回 `Limit`。

默认清除未提交的临时文件。KeepPartial 为 true 时保留，并通过 PartialPath 返回实际路径。
取消与系统写入、重命名可能竞争；逻辑取消不保证尚在执行的系统调用已停止，也不能撤销已经提交的文件。
`Progress()` 返回 Bytes、可选 TotalBytes、Path、PartialPath、CleanupSystemCode、
PhysicalComplete 以及 Commit：Uncommitted=0、Committing=1、Committed=2、Failed=3。
检查最终文件状态时应等 PhysicalComplete，再结合 Commit 与操作错误处理；关闭/删除失败的原生码保存在 CleanupSystemCode。

`executor.Close()` 停止其所属任务，并不关闭同 context 的其他执行器。
`context.Shutdown()` 回收并等待所有文件线程结束；不可中断的系统文件调用可能延长关闭。
文件线程不运行脚本或用户完成回调。代码 Owner、缓冲与终态结果按真实保留期存活；
context 额度覆盖池队列、任务块、管道及保留结果，不包含操作系统缓存或调用方自行复制的数据。

## 脚本入口

`Network::create_file_executor(context)` 创建文件执行器，`make_file_read_options` / `make_file_write_options` 配置范围和输出。数量使用非负 `int64`，未知总数使用 `-1`。

HTTP 文件上传、下载与续传见 [HTTP 文件传输](../../../../../../VeroScriptHttp/Resources/Docs/User/zh-CN/Reference/files.md)。

## 默认值

| API | 值 |
| --- | --- |
| `FFileReadOptions::MaxBytes` | `256 MiB` |
| `FFileWriteOptions::MaxBytes` | `256 MiB` |
| `bReplaceExisting / bKeepPartial / bFlushToDisk` | `false / false / true` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](files.md) · [zh-TW](../../zh-TW/Reference/files.md)
