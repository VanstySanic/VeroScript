# 按业务发布与独立版本

本页遵循唯一的[打包与发行规范](../../../../../Resources/Docs/Standards/zh-CN/PACKAGING.md)。不兼容、不转换旧布局；旧 Release 和无用途候选清理后按新规则全量重建。产品版本以发行登记为准。

业务优先：`Release/<business-route>/<target>/<version>/`。版本内按用途使用 `archives/`、`sdks/`、`packages/`、`installers/`、`symbols/`；公共包归 `any`，WebAssembly SDK 归 `wasm32`，原生程序保留真实 OS/架构。根入口 `release-index.json` 指向 `indexes/<target>.json`；目标业务可以引用公共与原生多份清单。完整字段、职责、载荷和引用清理规则以规范为准。

统一注册表与发行脚本实现分类路由、格式校验和验收记账。分发前须完成真实构建并通过包体／库存审计；目标运行和安装生命周期结果分别记录，包括失败或未执行的检查。纯规划和静态检查本身不构成构建或运行验收证据。

清理仅处理核实归属且无进程使用的旧产物和可再生临时数据，保护源码、凭据、仍有用途的工具链和磁盘。格式变化不迁移旧集合；有效新集合的更新仍要求锁、可恢复提交、引用闭包及摘要校验。仅修改规范时运行文档检查，不启动实际打包。

```text
Release/
  release-index.json
  indexes/<target>.json
  <business-route>/<target>/<version>/
    archives/
    sdks/
    packages/
    installers/
    symbols/
    release-manifest.json
    checksums.txt
```

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

支持范围分开记录：`supportedTargets` 描述实际载荷目标，`consumerTargets` 描述允许引用的发行目标；例如 wasm32 SDK 可被原生宿主发行索引引用。原生业务与公共分发组合时保留真实生态版本，不混合不匹配版本。

## 规划与实现入口

从仓库根目录运行。`configuration` 读取注册版本、路由、产品覆盖和格式版本；规划与生产器共用路由函数，不构建、暂存或发布产物。重复 `--target` 和 `--business` 可缩小范围。`indexSelections` 可以同时引用原生与公共清单；可选条目和 wheel 标签仍须由真实生产器输出确认。

源码输入与构建命令见[构建与捆绑](build-and-bundle.md)，最终库存核验及其限制见[发行验收](release-validation.md)。以下命令只检查元数据；仅修改规范时，完成上面的文档检查即可。

```text
python Resources/Tools/Packaging/veroscript_business_release.py configuration
python Resources/Tools/Packaging/veroscript_release_plan.py --target win-x64 --business Python --business Resources
```

<a id="release-qualification"></a>

## 库存与验收记录

目标索引的 `complete` 表示必需业务库存齐全；`qualificationStatus` 汇总已记录的验收，`qualified` 还要求库存完整且必要检查满足。真实构建及完整性核验通过的产物，可以在运行／设备检查失败或未执行时分发；声明目标可用性前须查阅记录，缺失记录为 `not-recorded`。

业务条目可包含版本为 1 的 `qualification`，绑定 `manifests[]` 的路径及 SHA-256。`--records` 输入 JSON 对象，将已发布业务 ID 映射到含 `checks` 的对象。每项检查具有真实的 `status` 和非空 `reason`；`required` 默认为 true，`evidenceSha256` 可选。必须记录成功的 `build` 及明确为必需的 `target-runtime` 检查；`failed`、`not-run` 如实保留，载荷改变后旧记录失效。记录工具在集合锁内更新所选目标索引及根摘要。

```text
python Resources/Tools/Packaging/veroscript_qualification.py --repo-root . --release-directory Release --target win-x64 --records .tmp/acceptance-records.json
```

## 既有集合的发布预检

发布前，现有目标索引中的 `requiredBusinesses` 必须与当前产品注册表定义完全一致。如果业务集合已变化，请在 `.tmp` 或 `dist` 下选择新的空专用输出目录，从源码重建集合。默认 `Release` 输出执行相同预检。规划预览默认包含所有已注册桌面与移动目标，包括 `win-x86`，可用 `--target` 缩小范围。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/release-layout.md) · [fr-FR](../fr-FR/release-layout.md) · [ja-JP](../ja-JP/release-layout.md) · [ko-KR](../ko-KR/release-layout.md) · [zh-CN](release-layout.md) · [zh-TW](../zh-TW/release-layout.md)
