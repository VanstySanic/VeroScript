# Core i18n example

This example registers an immutable in-memory resource resolver, then resolves
one typed message through independent English and Chinese contexts. It needs
the installed Core and matching Bytecode VM SDKs and has no ICU or GUI dependency.

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="/absolute/path/to/CoreSDK;/absolute/path/to/BytecodeVmSDK"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For scripts without resource providers, see
[`i18n_demo.vs`](../../Scripts/examples/i18n_demo.vs).
For a new format grammar implement `II18nFormatterSnapshot`; preserve the
argument kinds, enforce `FI18nWork`, and publish a fresh immutable provider
with `FI18nServices::PublishFormatter`. Pin a dynamic module through the
corresponding owner argument. C++ Engine/Session hosts may use
`FEngineConfig::I18nServices` and `I18nLimits` instead of configuring the VM.
