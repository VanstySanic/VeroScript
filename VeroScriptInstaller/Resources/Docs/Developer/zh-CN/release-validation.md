# 发行验收流程

本页遵循唯一的[打包与发行规范](../../../../../Resources/Docs/Standards/zh-CN/PACKAGING.md)。不兼容、不转换旧布局；旧 Release 和无用途候选清理后按新规则全量重建。产品版本以发行登记为准。

## 验收阶段

先固定预期源码、配置与目标发行配置，生产器前置条件见[构建与捆绑](build-and-bundle.md)。核对候选库存后，在目标环境完成必需的消费者、应用和安装生命周期验收。交叉编译成功不等于运行通过；签名及其他改变字节的步骤结束后，将结果绑定到最终产物摘要。

## 库存与当前正本核验

下例从仓库根目录核验已有集合；将 `win-x64` 换成实际消费目标。仅核验 `any`／`wasm32` 公共索引不能证明原生或移动整套发行完整。

```text
python Resources/Tools/Packaging/verify-veroscript-release.py --release-directory Release --target win-x64 --require-complete --check-source
```

`--require-complete` 核对该目标的必需业务集合并拒绝 partial 业务，不执行或认证目标运行、设备、图形、安装生命周期、签名或公证验收。`--check-source` 额外将验证器覆盖的公开载荷与其当前源码正本比较，不证明二进制已由该源码重新构建；仅审计较早的有效新格式集合所记录的契约时才省略。`--version` 要求每条选中业务引用具有该真实版本，不代表集合统一版本。

```powershell
./Resources/Tools/Packaging/verify-veroscript-release.ps1 -ReleaseDirectory Release -Target win-x64 -RequireComplete -CheckSource -RequireSignedInstallers
```

## 验收证据与最终复核

以 `passed`、`failed`、`not-run` 或 `not-applicable` 记录结果、原因、源码身份、工具链、配置、目标／设备身份与最终摘要。交叉链接、模拟器、真机、C# AOT／裁剪、Python 应用以及安装／修复／卸载分别记账。缺失环境和未签名产物保留明确缺口。Windows 包装器可用 `-RequireSignedInstallers` 强制核对 Authenticode，但不会签名，也不能替代安装生命周期验收。

分发前须完成真实构建，并通过包体、目标身份、依赖、清单及摘要检查。具备环境时执行目标运行与安装生命周期验收，失败或未执行均如实记录；这些缺口不再阻止已构建且有效的产物一并分发。通过[库存与验收记录](release-layout.md#release-qualification)区分 `complete` 和 `qualified`。分发后复核最终库存与摘要，输入改变时使受影响证据失效。仅修改规范时，只运行下列文档校验，不启动构建或产品打包。

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

<a id="installer-download-validation"></a>

## 安装器下载文件的静态校验

`veroscript_installer.py` 核对下载文件名和目标架构，读取 .NET 单文件 bundle，将嵌入的内容分片与程序集记录的完整 bundle SHA-256 及分片数量比较。跨目标组包还会与 staging 阶段核验的 ZIP 摘要比较。Linux 校验 `.tar.gz` 内唯一可执行程序及其权限，macOS 校验 `.dmg` 内的应用和嵌入载荷。其他操作系统上检查 DMG 需要通过 `VEROSCRIPT_7ZIP` 或 PATH 提供 7-Zip。目标运行及安装生命周期检查分别记录。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/release-validation.md) · [fr-FR](../fr-FR/release-validation.md) · [ja-JP](../ja-JP/release-validation.md) · [ko-KR](../ko-KR/release-validation.md) · [zh-CN](release-validation.md) · [zh-TW](../zh-TW/release-validation.md)
