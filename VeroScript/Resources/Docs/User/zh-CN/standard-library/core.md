# 核心入口模块

模块文件：`core.vs`

`core.vs` 是标准库的聚合入口，通过 `export import` 重新导出以下模块：

- `i18n.vs`
- `memory.vs`
- `math.vs`
- `algorithm.vs`
- `numeric.vs`
- `random.vs`
- `io.vs`
- `string.vs`
- `path.vs`
- `concurrent.vs`
- `attribute.vs`
- `callable.vs`
- `delegate.vs`

聚合导入上面列出的运行时模块：

```VeroScript
import "core.vs";
```

也可以直接导入单个模块，以明确表达依赖并减少名称范围。`core.vs` 不会新增一套不同的 API；它提供的是其他标准库模块的公开声明。

字符串格式化、文件操作、并发任务和事件委托的具体签名分别见对应模块文档。

- [应用国际化 — std::i18n](i18n.md): `std::i18n`

编译扩展显式导入 `compiler.vs`。`core.vs` 不重导出此模块，普通运行时代码仅导入它不会获得编译权限。

[编译扩展](compiler.md)

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/core.md) · [fr-FR](../../fr-FR/standard-library/core.md) · [ja-JP](../../ja-JP/standard-library/core.md) · [ko-KR](../../ko-KR/standard-library/core.md) · [zh-CN](core.md) · [zh-TW](../../zh-TW/standard-library/core.md)
