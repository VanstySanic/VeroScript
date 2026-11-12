# Runtime performance workbench

This module-owned directory contains performance tools, fixed inputs and
historical baselines. CMake targets require `BUILD_TESTING=ON` and are built
explicitly (`EXCLUDE_FROM_ALL`). Concrete backend harnesses now live in
`Modules/Execution/VeroScriptAsmJit/Resources/Benchmarks` and require
`VEROSCRIPT_BUILD_ASMJIT_BACKEND=ON`; each explicitly injects its factory.
Release packages exclude these development
resources. Write local measurements to `.tmp/` or the ignored `Results/` directory.
One-off assessment reports are temporary outputs and belong in those directories;
documentation must not depend on them. `ReviewBaseline/*/REPORT.md` is also ignored.

## Warm calls and the performance corrections

`VeroScriptInvocationBenchmarks` links only Core and loads an explicitly supplied
backend for Mixed/JIT. It keeps the compiled program and VM alive, reports source
compilation, backend setup and the first invocation separately, then warms for at
least 200 ms and three calls. It calibrates batches toward 30 ms (at most 2,048
calls) and emits every sample plus actual JIT/control counters as JSON. It does
not replace the end-to-end runtime workbench below.

```powershell
cmake --build build/windows-msvc-vs2026 --config Release --target VeroScriptInvocationBenchmarks
build/windows-msvc-vs2026/bin/Release/VeroScriptInvocationBenchmarks.exe VeroScript/Resources/Benchmarks/Inputs/performance-int64-recurrence.vs path/to/VeroScriptJitAsmJit.dll jit plain 200000 17 7 101486 VeroScript/Resources/Scripts/stdlib run
```

Arguments are `source backend mode profile n seed samples expected stdlib action`.
The source supplies one `work(int n)` or `work(int n, int seed)` returning a
numeric checksum. Modes are `vm`, `mixed`, `jit`; VM ignores the backend path.
Profiles are `plain`, `quota` (one billion instructions), and `combined` (the same
quota, a 30-second deadline, a live uncancelled control token and restricted
standard-library capabilities). `once` reports the first call without warm
samples; `compile` reports source compilation only and does not require `work`.
The deadline and instruction counter reset at each ordinary host invocation.

| Input under `Inputs/` | n | seed | Expected |
| --- | ---: | ---: | ---: |
| `performance-int64-recurrence.vs` | 200000 | 17 | 101486 |
| `performance-literal-append.vs` | 12000 | 17 | 48016 |
| `performance-local-string-length.vs` | 20000 | 65536 | 716070 |
| `performance-generated-string-map.vs` | 30000 | 17 | 15244632 |
| `performance-int64-heap-inline.vs` | 3000 | 17 | 98194 |
| `performance-int64-heap-helper.vs` | 3000 | 17 | 98194 |
| `performance-scalar-capture.vs` | 100000 | 17 | 20541 |
| `performance-nonlocal-string-length.vs` | 20000 | 65536 | 716070 |

These inputs also have `main()` and can run as ordinary scripts. Sorting timing
uses a small checksum; the separate `VeroScriptPerformanceRegressionTests` compares
the entire array with an independently sorted host vector, including duplicate,
negative, odd-length and empty inputs. That test also compares errors, source
positions, controlled counts and writes preceding a failure on VM/Mixed/JIT.
The follow-up regressions also cover scalar capture snapshots, rebinding/escape
fallbacks, virtual receiver selection, low call-depth limits and expanded native
local storage. Compare nonlocal text at several sizes: one field read must not
copy the complete text merely to consume its length. Governed and debugger paths
retain their ordinary instruction sequence.

For new comparisons, alternate baseline and candidate processes with matching
headers/libraries/backends, use at least three sessions and seven warm samples,
and keep builds and other measurements out of the timing window. Record source
and artifact hashes, CPU affinity, compiler flags, backend capabilities and all
samples. Explicit controlled JIT must retain real controlled-native entries;
uncontrolled cost fallbacks must be reported as fallbacks. Do not compare a new
warm-call result directly against historical end-to-end timing.

`ReviewBaseline/<baseline>` preserves the original review data and source snapshots.
These are historical measurements, not results of the current checkout. The old
standalone harnesses are evidence, not supported build entry points; some assume
Windows or the original temporary directory. Generated AOT sources, executables,
and all temporary build files are intentionally not archived here.

Build from the repository root using the existing CMake configuration:

```powershell
cmake --build build/windows-msvc-vs2026 --config Release --target VeroScriptRuntimeBenchmarks --parallel 4
build/windows-msvc-vs2026/bin/Release/VeroScriptRuntimeBenchmarks.exe matrix all 7
build/windows-msvc-vs2026/bin/Release/VeroScriptRuntimeBenchmarks.exe source VeroScript/Resources/Benchmarks/ReviewBaseline/<baseline>/cross/escaping.vs 19083680 5 7
```

The executable uses the current runtime target, but the 74-case matrix is frozen
to the archived corpus for reproducible comparisons. Unknown cases, missing input,
invalid options, compilation/runtime errors and wrong results return failure.
Each backend gets one excluded warmup, then rotates position across samples.
Repeat the complete command in at least three independent measurement sessions.

CSV separates parsing/loading, compilation, and execution. Execution includes VM
creation, JIT compilation, initialization and destruction; it is NOT pure warmed
machine-code time. Only `execute_per_work_ms` divides by `work_batch`, which must
match the work repetitions already written in the source. No historical gate or
baseline.json is overwritten. Result files should include binary/source hashes,
build flags and machine metadata. Run performance commands serially without builds
in parallel. Allocation/heap and true cross-call object tests remain separate from
the local `escaping` algorithm; see the R2 plan.

`VeroScriptRuntimeBenchmarks inspect <matrix-case>` is a separate, non-timing
diagnostic: it prints function fallback/scalar-set flags and typed-loop fusion,
native-lowering presence and bytecode headers. It does not change or execute the
program. It also compares cached stack-capacity hints with a separately rebuilt
original scalar plan, marking whether that original analysis was complete.
Incomplete plan capacities must not be treated as authoritative bounds.
Numeric fused-kind values are private implementation details; the
`direct_affine_set` boolean identifies the investigated shape explicitly.

`VeroScriptRuntimeBenchmarks layout` prints non-timing ABI diagnostics: pointer,
policy, VM and cache sizes plus policy budget-member offsets. Compare only builds
of the same architecture/compiler ABI. Restoring a few x64 sizes/offsets does not
make old and new host binaries compatible, nor prove a microarchitectural cause.

`node verify-inputs.mjs RUNTIME_BENCH.exe NEW_RESULT.json` serially checks exact
full output for all four owned-object inputs and both held-out Set inputs on
VM/Mixed/JIT. It records source/executable hashes and CSV, but the one-sample
timings are correctness diagnostics, not statistical performance claims.

## Serial runs and host boundaries

From the repository root (output directories must already exist):

```powershell
cmake --build build/windows-msvc-vs2026 --config Release --target VeroScriptRuntimeBenchmarks VeroScriptHostBenchmarks --parallel 4
node VeroScript/Resources/Benchmarks/run.mjs build/windows-msvc-vs2026/bin/Release/VeroScriptRuntimeBenchmarks.exe .tmp/matrix.json matrix all 7
node VeroScript/Resources/Benchmarks/run.mjs build/windows-msvc-vs2026/bin/Release/VeroScriptHostBenchmarks.exe .tmp/host.json host 7 50000
node VeroScript/Resources/Benchmarks/analyze.mjs compare .tmp/before.json .tmp/after.json
node VeroScript/Resources/Benchmarks/analyze.mjs archive .tmp/host.json VeroScript/Resources/Benchmarks/Results/my-host-run
```

`run.mjs` executes three independent processes serially, records executable/corpus
hashes and all CSV samples, and refuses to overwrite existing results. It checks
sample coverage, duplicates and numeric fields. Node.js 22+ is required for
`Map.groupBy`; the measured environment used Node.js 24.14.1. `analyze.mjs` reports
medians, median absolute deviations and per-process medians, or exports recorded
samples without resampling. Neither tool updates `baseline.json`.

On Windows, `run.mjs` also accepts a nonzero hexadecimal child-process affinity
mask through `VEROSCRIPT_BENCH_AFFINITY` (for example `100` on the measured machine).
Use `windows-cpu-topology.ps1` first; the mask is machine-specific. It is recorded
in the output and does not control frequencies, SMT siblings or background work.
Do not compare a pinned result with an unpinned historical result as if their
execution environments were identical.

```powershell
$env:VEROSCRIPT_BENCH_AFFINITY = '100'
node VeroScript/Resources/Benchmarks/run.mjs build/windows-msvc-vs2026/bin/Release/VeroScriptHostBenchmarks.exe .tmp/host-pinned.json host 7 50000
Remove-Item Env:VEROSCRIPT_BENCH_AFFINITY
```

Host cases distinguish raw `vm_api`, ordinary `session`, `persistent_session`
(hot reload enabled, effective VM), and the same ordinary API with session code
reuse disabled (`session_no_reuse`). Backend labels record the requested backend;
JIT dispatch/compile counters expose forced-VM execution. Other host diagnostic
columns and `load_ms`/`compile_ms` are unused zeros, not measured zero costs. Session loading is outside
host timing; VM setup, argument/return validation, stats collection and cleanup
inside each call are included. `execute_per_work_ms * 1000` is microseconds/call.
Each host combination has one excluded full-batch warmup. Host API and backend order
rotate between the three processes via `VEROSCRIPT_BENCH_ROTATION`; runtime backend
order rotates within each sample. These are not p99 latency measurements.

The new object inputs have the following `source` arguments:

| Input | Expected output | Work batch |
|---|---:|---:|
| `Inputs/owned-reverse-1000.vs` | 19194400 | 100 |
| `Inputs/owned-reverse-40000.vs` | 38173120 | 5 |
| `Inputs/owned-forward-heldout.vs` | 9895005 | 5 |

The forward input was added after fixing the classifier and is not used to tune
it. All three remain local object algorithms, not O2/O3 allocation measurements.

`Inputs/set-heldout.json` supplies two independently checked inputs to the optional
fifth argument of `compare-owned-pinned.mjs`: a sparse set with modulus 100003
(expected 200010, five work calls) and an empty set with negative state
(expected 0, five calls). They were fixed before measuring the read-only probe
change. The sparse oracle is a JavaScript Set populated with `i = 0; i < 100003;
i += 5`, queried 200000 times using `state = (state * 7 + 3) % 100003`, starting
at 1; its 40002 hits are multiplied by five. Empty membership is always false.
Use the runtime `source` mode separately for exact full-output checks. The optional
manifest rows are `[name, path-relative-to-Benchmarks, batch, expected-int32]`.

The empty held-out source contains a negative-increment expression that can lower
through a separate unary opcode. It is a broader workload/regression control,
not proof that the outlined `DirectAffineSetContains` negative-arithmetic path ran.
Dedicated differential tests separately require fused-plan admission and actual
JIT dispatch. With `VEROSCRIPT_ENABLE_JIT_TEST_HOOKS=ON`, they additionally observe
the exact executing function and loop header at the production affine-Set kernel
call; eight inputs must each be observed under Mixed and JIT. The option defaults
to OFF and must remain OFF for timing. In these observer builds, a test-only thin
adapter directly invokes the file-local production kernel for 8,401 comparisons
of complete/partial states and first-failure locations with an independent
reference. Neither this adapter nor the observer is present in timing builds.
Two negative-divisor tests use explicitly constructed constant-pool
fixtures before any cache is prepared. Their results must not be described as
unchanged source-parser coverage or instruction-by-instruction kernel tracing.

`VeroScriptSessionCacheResources` is a separate opt-in target. Run it as
`VeroScriptSessionCacheResources.exe mixed 500 200` (or `jit`) for a cold sweep
followed by 100,000 warm calls. It checks each value and rejects warm compilation.
Windows process Private Bytes and Peak Working Set are printed after loading,
cold/warm sweeps, Clear and session destruction. Other platforms print zero for
these unavailable OS counters. This diagnostic does not measure net runtime heap
and should not run alongside timing jobs. V7 also reports the independent session
context compile-attempt count, retained payload bytes, native reserved bytes and
resource-limit latch. Tiny-limit SessionJitCache tests, not process Private Bytes,
verify both pre-allocation rejection and post-compilation limit enforcement.

Ordinary reusable session calls default to 8 MiB native retention and 32 MiB total
retained payload. `SetJitCacheLimits` configures these separately from `FJitPolicy`;
zero disallows retention of that resource (not unlimited). Resource overflow stops
compilation for that context and falls back to VM on subsequent calls. Runtime
failure discards code but does not refund the context's compile-attempt budget.
V8 also covers C++ exceptions escaping a host input stream: unwind preserves the
consumed budget and resource-limit latch, clears retained-memory gauges, and
propagates the original exception. The regression tests exercise cold, warm and
over-limit contexts; they are not exhaustive allocation-failure injection.
The payload estimate includes owning tables, functions, plans and native pages,
but is not a process/allocator/compilation-peak cap. Policy/limit/module changes
reset the context. See [current Session cache maintenance](../../../Extensions/Language/Cpp/Resources/Docs/Developer/en-US/session.md) for exclusions,
reset boundaries and ABI/rebuild requirements.

## Windows hybrid-CPU replay

Run `windows-cpu-topology.ps1` to inspect CPU-set topology without changing system
settings. `compare-pinned.mjs` launches only its benchmark children with a chosen
hexadecimal affinity mask using `start /b /wait /affinity`. Do not assume a given
logical processor number is a performance core on another machine. Background
work, SMT sibling contention and frequency are still uncontrolled.

```powershell
cmake --build build/windows-msvc-vs2026 --config Release --target VeroScriptReviewReplay --parallel 4
node VeroScript/Resources/Benchmarks/compare-pinned.mjs OLD_REVIEW.exe build/windows-msvc-vs2026/bin/Release/VeroScriptReviewReplay.exe .tmp/pinned.json 100 review-review all
```

`OLD_REVIEW.exe` must be a preserved static binary from the intended baseline,
not a rebuild accidentally linked to the current runtime. `review-review` uses
the identical archived review harness for both binaries; `review` compares an old
review harness with the new runtime harness and has small timing-boundary
differences. `runtime` compares two copies of the new runtime harness. The runner
alternates binaries between measurement processes/cases and preserves every CSV.
Each backend has seven valid samples per process, three processes per binary.
The historical `export` and `sizes` commands are rejected by this replay tool.

The first build of `VeroScriptReviewReplay` followed project-default host settings
(including Release debug symbols), unlike the original standalone `review.exe`.
The subsequently corrected target matches the historical Release wrapper/link
settings; the runtime library still follows project defaults. Identify which
binary was used by the measurement hash. Results made before this correction are
broad regression diagnostics, not isolation of sub-percent layout effects or proof
of zero overhead. Investigate persistent regressions with matched wrapper/link
settings before accepting them.

For the four owned-object inputs, `VeroScriptCrossReplay` instead preserves the
historical `cross.cpp` Release wrapper/link settings. This separate replay rotates
backend order and alternates binaries, records three processes with seven valid
samples per backend/workload/binary, and keeps excluded warmup rows:

```powershell
cmake --build build/windows-msvc-vs2026 --config Release --target VeroScriptCrossReplay --parallel 4
node VeroScript/Resources/Benchmarks/compare-owned-pinned.mjs OLD_CROSS.exe build/windows-msvc-vs2026/bin/Release/VeroScriptCrossReplay.exe .tmp/owned-pinned.json 100
```

The historical cross harness checks the integer output using `stoll`, not strict
full-output equality; the new runtime harness and correctness suites additionally
check complete output. Module loading/compilation occurs once per cross process
outside execution timing. Every timed sample still creates and destroys a VM.
The historical cross CSV column named `execute_ms` is already divided by
`work_batch`; its `medianMs` summaries therefore mean ms/work. Do not divide those
values again. ReviewReplay's `execute_ms`, by contrast, is total execution time,
and RuntimeBenchmarks exports total and per-work values in separate columns.

## Memory-safety validation

An independent Windows x64 Release build with `/fsanitize=address /EHsc`, asmjit
ON and IPO/PGO OFF ran PublicHeaders, JitBackend, BackendDifferential,
SessionJitCache and SessionApi tests. V7 introduced the test observer in this build
and the independent asmjit-off build. V8-C rebuilt both and reran all five suites,
including the exception regressions and native-query entry assertions; see
`Results/snapshot/tests-v8c/README.md` for final-code reports. `tests-v8` belongs
to candidate A and `tests-v7` to V7; neither certifies the current candidate.
The query observer checks 500,000 actual helper calls in each of four ASan
plain/expression and Mixed/Jit cases, and zero in asmjit-off. Timing builds
contain neither this counter nor its observation branch.
Use a separate build directory and make the matching compiler ASan runtime DLL
available on the test process PATH. Do not use this instrumented build for timing.
C++ ASan instrumentation does not automatically instrument dynamically emitted
JIT machine code; a passing subset is not exhaustive memory-safety validation.
