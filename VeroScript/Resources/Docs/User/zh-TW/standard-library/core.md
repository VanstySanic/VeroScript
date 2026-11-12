# 核心入口模組

模組檔案：`core.vs`

`core.vs` 是標準函式庫的聚合入口，透過 `export import` 重新匯出下列模組：

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

彙整匯入上面列出的執行階段模組：

```VeroScript
import "core.vs";
```

也可以直接匯入單一模組，以明確表達依賴並減少名稱範圍。 `core.vs` 不會新增一套不同的 API；它提供的是其他標準函式庫模組的公開宣告。

字串格式化、檔案操作、並發任務和事件委託的具體簽章分別見對應模組檔案。

- [應用程式國際化 — std::i18n](i18n.md): `std::i18n`

編譯擴充明確匯入 `compiler.vs`。`core.vs` 不重新匯出此模組，一般執行階段程式碼僅匯入它不會取得編譯權限。

[編譯擴充](compiler.md)

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/core.md) · [fr-FR](../../fr-FR/standard-library/core.md) · [ja-JP](../../ja-JP/standard-library/core.md) · [ko-KR](../../ko-KR/standard-library/core.md) · [zh-CN](../../zh-CN/standard-library/core.md) · [zh-TW](core.md)
