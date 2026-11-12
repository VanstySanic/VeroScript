# Large test suites

The directories below contain ordered fragments included by their corresponding
test executable. This keeps each established executable, shared fixture and test
order intact while making the source reviewable by domain.

- `VeroScriptM3`: language, runtime, container, session and standard-library coverage.
- `VeroScriptJitBackend`: kernel, scalar CFG, dispatch/object and integration coverage.
- `VeroScriptHostBinding`: catalog, host values, containers, metadata and generic ABI coverage.

Each fragment is owned by exactly one `.cpp` file. It must not define an independent
test entry point or be included elsewhere.
