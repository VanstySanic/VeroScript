# Resolved module environment

This example uses the public Core API and the Bytecode VM SDK to import and execute an exact in-memory module. It needs neither an installer nor a package database. A package client or embedding host constructs the same immutable descriptor after resolving enabled packages.

Build with the installed Core and matching Bytecode VM SDKs through CMake, listing their prefixes in `CMAKE_PREFIX_PATH`; run `ModuleEnvironment` to print `42`. The source-tree test target is `VeroScriptModuleEnvironmentExample`. See the [SDK build commands](../README.md#build-an-external-core-sdk-example).

Native loading and resource-provider contracts are documented in [module environments](../../Docs/Developer/en-US/module-environments.md). Installation, dependency solving and backend selection remain services above Core.
