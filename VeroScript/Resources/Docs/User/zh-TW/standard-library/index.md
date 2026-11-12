# 標準庫參考

VeroScript 標準函式庫以模組形式提供常用的字串、數學、檔案、路徑、並發和事件能力。腳本可以匯入 `core.vs` 使用彙整的執行階段模組，也可以只匯入需要的模組。

呼叫模組所屬函式時，使用完整公開命名空間，例如 `std::strings::join` 或 `std::concurrent::spawn`；未限定的全域寫法會被拒絕。`array<T>`、`print` 等基礎型別與直接提供的函式仍可使用原有標準名稱。命名空間別名不能替代所需的模組匯入。 [std](../language-basics/standard-namespace.md).

- [受管理記憶體](memory.md)：`std::memory`

- [屬性與方法函式值](attribute.md)：`std::attribute<T>` · `std::bind_method<F>`

## 模組目錄

- [內建函式](builtins.md)：無需匯入的輸出、轉換、數值與環境入口；輸入介面見 io.vs。
- [核心入口](core.md)：重新匯出該頁列出的執行階段模組；compiler.vs 需要明確匯入。
- [字串](string.md)：尋找、截取、分割、連接、修剪、大小寫、取代和格式化。
- [數學](math.md)：三角函式、平方根、取整和範圍限制。
- [演算法](algorithm.md)：泛型陣列排序、反轉、搜尋與計數。
- [數值彙總](numeric.md)：累加、總和與平均值。
- [隨機數](random.md)：使用明確獨立狀態的決定性偽隨機串流。
- [輸入輸出](io.md)：檔案、目錄、標準輸入和工作目錄。
- [路徑](path.md)：檔案名稱、目錄名稱、副檔名和路徑拼接。
- [並發](concurrent.md)：通道、隔離任務、任務等待和協作取消。
- [事件](events.md)：單播委託與多重廣播委託。

## 匯入約定

完整匯入：

```VeroScript
import "core.vs";
```

按需匯入：

```VeroScript
import "string.vs";
import "path.vs";
```

標準庫函式通常是公開匯出的宣告。函式的參數和傳回類型以對應模組中的實作為準；涉及檔案、執行緒或宿主環境的操作也可能受到執行階段權限和宿主設定影響。

- [應用程式國際化 — std::i18n](i18n.md): `std::i18n`

編譯擴充明確匯入 `compiler.vs`。`core.vs` 不重新匯出此模組，一般執行階段程式碼僅匯入它不會取得編譯權限。

[編譯擴充](compiler.md)


- [標準函式庫基礎](foundation.md)

- [variant — 動態值盒](variant.md)
<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/index.md) · [fr-FR](../../fr-FR/standard-library/index.md) · [ja-JP](../../ja-JP/standard-library/index.md) · [ko-KR](../../ko-KR/standard-library/index.md) · [zh-CN](../../zh-CN/standard-library/index.md) · [zh-TW](index.md)
