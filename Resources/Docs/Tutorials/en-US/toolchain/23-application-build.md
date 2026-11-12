# Build a library and application

Prerequisites: a matching CLI with Build and Application services, writable storage, and the shipped hello example. This task uses script content only; no native provider installation or network service is needed.

1. Copy the entire linked hello directory to a writable absolute path such as /absolute/hello, preserving src and package. First inspect the plan; then build only the application target. The library dependency is built automatically. Keep the release profile explicit.

   [hello](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)

```sh
veroscript build --path /absolute/hello --profile release --target application --plan --json
veroscript build --path /absolute/hello --profile release --target application
veroscript /absolute/hello/dist/release/portable/application/sample.vsapp
```

2. The plan reports the dependency graph without executing custom steps. A successful build creates dist/release/portable/application/sample.vsapp and the library artifact. Running the app prints result: 42 and returns 0. A failed plan/build must be fixed before running; generation alone does not establish execution success.

3. Change the library answer to 43, obtain a fresh plan and rebuild. Expected output becomes result: 43. Do not edit the generated container; keep source and build configuration as the inputs.

[Build](../../../../../VeroScriptBuild/Resources/Docs/User/en-US/index.md) · [Application](../../../../../VeroScriptApplication/Resources/Docs/User/en-US/index.md)

[en-US](23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)

Replace /absolute/hello with an absolute path on your system; on Windows, for example, C:/work/hello.

## Source inputs

- [veroscript-build.json](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)
- [src/main.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/main.vs)
- [src/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/library.vs)
- [package/veroscript-package.json](../../../../../VeroScriptBuild/Resources/Examples/hello/package/veroscript-package.json)
- [package/scripts/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/package/scripts/library.vs)

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)
