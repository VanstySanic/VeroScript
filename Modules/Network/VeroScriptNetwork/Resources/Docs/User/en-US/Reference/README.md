# Network — Network reference

Shared frontend installation and backend-selection rules are specified in [Network](../index.md#backend-selection).

`vs-network` supplies contexts, operations, URLs, DNS, TCP/UDP, byte pipes, file execution and shared TLS; Http and WebSocket remain independent modules. Native registration ID is `network`, script namespace `Network`, CMake target `VeroScript::Network`. Bindings require the matching Core; a native SDK does not require Core. Http/WebSocket pin the matching Network package; for offline installation declare its local archive explicitly.

FUrl::TryResolve follows RFC 3986 section 5 for authority-based network URLs. Only literal path dot segments normalize; repeated slashes, percent escapes and query/fragment dots remain. Empty query differs from absent query; an empty reference inherits the query and removes the old fragment. An authority with empty path becomes `/`; opaque URIs such as g:h are rejected. Failure preserves output. Percent coding is once per byte; form mode explicitly maps plus/space and strings preserve NUL.

Background mode drives one native I/O thread per context; HostDriven requires bounded PollIo on the creator thread. PollIo never dispatches callbacks and cannot recurse. Dispatch runs callbacks on the creator thread in stable batches; one completion registration and dispatch per operation. Terminal publication can precede queue insertion, so keep driving/dispatching. Failed completion registration can be retried if queue allocation failed. Dispatch allocation failure preserves queue/quota and reports 0/Allocation. Callback failures retain a structured code even if text allocation fails.

Wait observes only: 0 Completed, 1 WaitTimedOut (no cancel), 2 RequiresPoll for unfinished HostDriven work. Timeouts are milliseconds, -1 unlimited, 0 immediate; operation deadlines include queueing from submission. Cancellation/completion publish one terminal state. Result<T> must match the completed result; pending/failed/wrong-type access fails. Shutdown cancels work and closes registered resources; retained results/code Owners can outlive the public context but cannot resume I/O.

MaxRetainedBytes defaults to 256 MiB for managed application buffers, pipes, queues, protocol bodies and retained results. Logical completion/Shutdown does not release externally held data early. MemoryUsage exposes retained/peak/limit (script retained_bytes/peak_retained_bytes/memory_limit). It is not an RSS/OS/backend/allocator/caller-copy/temporary-reallocation hard cap. Executor Allocation/Internal failure closes the context and wakes work; still Shutdown/destroy normally. Cleanup retires without requiring a new operation list or error-string allocation. Native DNS/file calls may outlive logical cancellation and delay physical shutdown; delayed close inside PollIo may wait for native work.

TCP permits one active read plus a bounded serial write queue; MaxQueuedWrites and MaxBufferBytes limit count/bytes. A queued write cancel removes only it, an active system read/write cancel closes the socket and other work. FTcpListener supports async accept and idempotent close. UDP preserves empty datagrams; receive reports copied/original sizes and truncation. SetBroadcast and JoinMulticast/LeaveMulticast select interfaces explicitly; actual delivery depends on platform/network.

Script: parse_url/resolve_url return scheme/host/port/path/query/fragment/has_query/has_fragment/explicit_port/ipv6/authority/target/to_string/without_fragment. percent_encode/decode take explicit form mode. create_context(host_driven), resolve, connect, listen, bind_udp return typed handles/operations; sockets expose async send/receive/accept and close. operation supplies done/state/cancel/error/error_code/message_id/wait and typed results; count is checked int64. All handles retain context/code through actual resource lifetime. RetainCodeOwner deduplicates shared ownership (including null alias handles), not raw address; bare pointers without a control block are invalid Owners.

Load Network before the selected protocol modules and declare their package dependencies. Deploy the matching `VeroScriptBoostRuntime` with Boost providers, beside Windows hosts or in the native-module layout. iOS uses static registration in the same host image. These native APIs do not provide a browser/Node transport or language-level await/future.

- [Completion and program lifetime](completion.md)
- [Diagnostics and localization](diagnostics.md)
- [Files and atomic output](files.md)
- [Bounded byte streams](streaming.md)
- [TLS configuration](tls.md)

## Defaults

| API | Value |
| --- | --- |
| `FContextOptions::MaxOperations` | `1024` |
| `MaxCallbacks` | `1024` |
| `MaxConnections` | `256` |
| `MaxQueuedWrites` | `64` |
| `MaxBufferBytes` | `16 MiB` |
| `MaxRetainedBytes` | `256 MiB` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
