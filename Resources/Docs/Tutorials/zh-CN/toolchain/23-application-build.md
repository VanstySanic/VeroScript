# 构建库与应用

前提：匹配且包含 Build/Application 服务的 CLI、可写目录和分发的 hello 示例。本教程仅使用脚本内容，不需要原生提供者安装或网络服务。

1. 将链接的整个 hello 目录复制到 /absolute/hello 等可写绝对路径，保留 src/package。先查看计划，再只构建 application 目标，库依赖自动构建；明确指定 release 配置。

   [hello](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)

```sh
veroscript build --path /absolute/hello --profile release --target application --plan --json
veroscript build --path /absolute/hello --profile release --target application
veroscript /absolute/hello/dist/release/portable/application/sample.vsapp
```

2. 计划显示依赖图而不执行自定义步骤。成功构建产生 dist/release/portable/application/sample.vsapp 及库，运行打印 result: 42、返回0。计划/构建失败须先修复，不能把生成文件当成运行成功。

3. 把库中的回答改为43，重新获取计划并构建，预期输出 result: 43。不要编辑生成容器，持续以源码和构建配置为输入。

[Build](../../../../../VeroScriptBuild/Resources/Docs/User/zh-CN/index.md) · [Application](../../../../../VeroScriptApplication/Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)

将 /absolute/hello 替换为实际系统的绝对路径；Windows 例如 C:/work/hello。

## 源码输入

- [veroscript-build.json](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)
- [src/main.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/main.vs)
- [src/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/library.vs)
- [package/veroscript-package.json](../../../../../VeroScriptBuild/Resources/Examples/hello/package/veroscript-package.json)
- [package/scripts/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/package/scripts/library.vs)

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)
