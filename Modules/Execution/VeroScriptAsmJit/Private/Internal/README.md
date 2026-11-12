# Linear JIT internals

These fragments are included, in order, by `VeroScriptJitLinear.cpp`. They remain
in one translation unit so anonymous-namespace helpers, optimization ordering,
and internal linkage do not become a public or cross-module ABI.

- `VeroScriptJitLinearAnalysis.inl`: eligibility, ownership and CFG analysis.
- `VeroScriptJitLinearMetadata.inl`: scalar expression decoding and dispatch metadata.
- `VeroScriptJitLinearExecution.inl`: authoritative Linear executor and general opcode dispatch.
- `VeroScriptJitLinearLoadLocal.inl`: typed `LoadLocal` hot paths and canonical-loop reductions.

Do not include these files from another translation unit. New optimizations should
be placed according to their semantic responsibility rather than appended to the
small owning `.cpp` file.
