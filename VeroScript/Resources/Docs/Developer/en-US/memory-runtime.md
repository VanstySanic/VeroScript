# Managed memory implementation contract

Core owns the memory services in `Public/VeroScriptMemory.h` and `Private/Memory/`. Compiler lowering is in `Private/Compiler/VeroScriptCompilerMemory.cpp`, execution dispatch in `Private/ExecutionSupport/VeroScriptVmMemory.cpp`. `stdlib/memory.vs` is a module entry, not a second implementation. Adapters must share these services and must not create independent identity registries or weaken ownership checks.

## Invariants and verification

Validate trusted handle provenance before accessing a payload. Object identity uses weak ownership information and must neither prolong lifetime nor confuse a reused address with the old object. Regions keep a shared owner; view descriptors include offset, length, generation and read-only kind. `FMemoryContext` owns an access domain; worker contexts share quota accounting and have distinct owner threads. A retained callable allowed on another thread still cannot transfer that domain.

Every operation, including a zero-length range, validates domain, generation, bounds and write permission. Owned growth accounts for both allocations before committing. Bulk operations check governance every 64 KiB and charge ceil(bytes/64); scalar writes check before touching bytes. Leases prevent structural changes and conflicting byte access. Release callbacks execute outside core locks, once, and must not throw.

## Host integration and limits

Use `VeroScriptMemory.h` with a persistent `FMemoryContext`, or a C++ Session’s `GetMemoryContext()`. The C ABI uses `vs_interop_memory_*` with exactly one Session or active call context. Existing `vs_interop_buffer` remains a separate serialization-result buffer.

External registration transfers release responsibility only on success. Every alias shares invalidation. Read/write leases exclude conflicting access and structural change with Busy. Release the opaque token exactly once after native work completes; release works on any thread and after Session destruction. The final external callback runs outside core locks and must not throw. A pointer obtained without a lease is only an immediate observation.

MaxBufferBytes caps each capacity/external length; MaxRetainedBytes counts all owned capacity plus borrowed bytes, including the old and new allocations during growth. Zero means unlimited. OwnedBytes, BorrowedBytes and PeakOwnedBytes exclude VM/object metadata. Run, Clear and reload do not reset retained accounting. Worker contexts share the budget but have separate access domains. Regions cannot pass through task captures, arguments, returns or channels.

C#: `session.Memory`, `SetMemoryLimits`, `using var lease = memory.AcquireLease(bytes, true)`. Python: `session.memory`, `set_memory_limits`, `with memory.acquire_lease(bytes, write=True) as lease`; release exported memoryviews before closing the lease. Both bindings pin external buffers until the final region/lease release. Native callbacks query original ref/const-ref targets through `vs_interop_memory_call_argument_address`, C# `context.GetArgumentAddress`, or Python `current_memory().argument_address`. By-value marshaling copies return Unsupported.

## Backends and debugging

VM and Mixed are supported. AsmJit rejects address-sensitive or core memory-member functions before entry and uses the existing VM fallback; actual memory builtins use core services. Execution governance keeps its existing VM policy. Standalone C++/C#/Python AOT rejects actual memory use; an unused memory/core import is allowed. Bytecode and packages store operations, never native pointers, lease tokens or identity registries. Default object snapshots reject nested region handles; persist explicit byte snapshots or use an explicit serializer.

Paused debugging displays core memory metadata. DAP variablesReference remains a protocol identifier; arbitrary readMemory/writeMemory are not enabled. Android JNI and iOS runners reuse Core and disable native addresses by default. Direct C#/Python Sessions require explicit host quota and capability configuration. Wasm uses bigint IDs/lengths/limits and instance-owned regions; lease callbacks rebuild linear-memory views after heap growth. Wasm host-region quotas are distinct from script Session quotas (16 MiB per region / 32 MiB retained by default).

Keep `memory.json` in the six Core locale directories, with stable IDs and equal key sets. Test owned/external storage, identity, view expiry, limits, busy leases, callback ownership, cross-thread release, Session teardown, reload, snapshots, worker restrictions and backend rejection. Run the Core/Cpp/Interop memory tests, binding memory contracts, locale deployment tests and documentation checks. Web tests must cover heap growth and interactive-input exclusion; mobile device acceptance remains separate.


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](memory-runtime.md) · [fr-FR](../fr-FR/memory-runtime.md) · [ja-JP](../ja-JP/memory-runtime.md) · [ko-KR](../ko-KR/memory-runtime.md) · [zh-CN](../zh-CN/memory-runtime.md) · [zh-TW](../zh-TW/memory-runtime.md)
