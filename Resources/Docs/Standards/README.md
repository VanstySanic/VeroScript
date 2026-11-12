# VeroScript — Project development and contribution standards / 本项目开发与贡献规范

These publicly readable standards apply only to development of and contributions to the VeroScript project. They are not language or SDK usage requirements or additional MIT license conditions. External projects need not adopt these coding conventions or use this project's copyright holder for their own code. Existing copyright and license notices remain governed by the applicable licenses.

本规范公开可读，仅适用于 VeroScript 本项目开发与贡献，不是语言或 SDK 的使用要求，也不构成 MIT 许可的附加条件。外部项目不必采用本项目编码风格或将本项目版权人用于自己的代码；已有版权与许可声明仍按适用许可证保留。

Standards remain in the source repository and in `All` documentation as developer material. `User` documentation and functional Runtime/SDK/extension releases and installers omit Standards; source copyright and license notices remain intact.

Standards 保留在源码仓库与 `All` 文档的开发者资料中；`User` 用户文档、Runtime/SDK/扩展功能发行包及安装器不附带 Standards。源码版权与许可声明继续保留。

- [English](en-US/README.md)
- [Français](fr-FR/README.md)
- [日本語](ja-JP/README.md)
- [한국어](ko-KR/README.md)
- [简体中文](zh-CN/README.md)
- [繁體中文](zh-TW/README.md)

Maintain the authoritative rules under `Resources/Docs/Standards/zh-CN` and keep all six languages equivalent. Rule changes originate from the project maintainer. Code, API names and English source comments retain their original spelling. Historical records remain in their original language.

规范制定正本位于 `zh-CN`，其他五种语言保持等价。规则变更以维护者的明确决定为准；代码、API 名称及英文源码注释保持原样。历史记录保留原语言。

- [Packaging and publication / 打包与发行](zh-CN/PACKAGING.md) · [English](en-US/PACKAGING.md)

- [Repository documentation / 仓库文档](../README.md)

Synchronize and verify from the repository root / 从仓库根目录同步与校验：

```powershell
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```
