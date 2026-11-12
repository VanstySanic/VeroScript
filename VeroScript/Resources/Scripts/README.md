# Core script assets

| Directory | Purpose |
|---|---|
| `stdlib/` | Standard-library source modules owned by the VeroScript core. |
| [`examples/`](examples/README.md) | Runnable language and standard-library examples. |

Native embedding and SDK consumer examples belong to each module's
`Resources/Examples/`: [Core SDK](../Examples/README.md),
[C++ SDK](../../../Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/README.md),
and [Interop SDK](../../../VeroScriptInterop/Resources/Examples/README.md).
Shared documentation and tutorials belong to `Resources/Docs`; release output belongs to `Release`.

Builds and packages copy these source assets directly. Runtime packages retain
their `stdlib/` and `examples/` directories; Core SDK packages retain
`assets/stdlib/` and `examples/scripts/`. Import names such as `std::concurrent`
are unchanged. Complete releases place these assets under `Resources/Scripts`
and documentation under `Resources/Docs`, with document links rebased for offline reading.

The module loader and tooling recognize the source standard library when run
from the repository root, `Script/`, or `VeroScript/`. Installed tools
discover their packaged assets relative to the executable.
