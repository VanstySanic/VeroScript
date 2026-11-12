# 數值彙總

模組檔案：`numeric.vs`

`std::numeric::accumulate(values, initial)` 支援明確初值與空陣列。`sum` 和 `average` 要求非空陣列；空輸入會觸發斷言。`average` 會將元素轉換為 `double`，這些 API 應用於數值元素型別。
`average` 使用遞推平均值，避免有限值的中間總和溢出；結果仍受浮點捨入影響。


- [標準函式庫基礎](foundation.md)


## 大整數與精確有理數

匯入 `bigint.vs` 使用整數輔助函式，匯入 `rational.vs` 使用兩種型別，或匯入 `core.vs`。值型別由核心 native catalog 提供。值不可變並以值複製，可用於容器、欄位、參數、傳回值與 `std::attribute<T>`。

整數除法向零取整，餘數與被除數同號。右移為算術右移，位元運算採無限位元二補數語意。有理數始終約分且分母為正，標準文字為 `分子/分母`；分母為 1 時只輸出整數，零輸出 `0`。不新增浮點、bool、列舉或普通整數到這兩種型別的隱含轉換。

使用明確的整數／字串建構、`parse`、`try_parse` 與轉換方法。整數輸入完整保留 64 位元。`from_double_exact` 精確讀取已儲存的二進位浮點值，因此 0.1 不等於 1/10。`to_double`／`to_float` 直接捨入至目標格式，中點取偶；`_exact` 版本拒絕精度損失。小數須先明確取整才能轉為整數。`try_` 解析／轉換失敗保留輸出；資源超限、取消與配置失敗仍傳播錯誤。

`std::numeric::gcd`、`lcm`、`divmod`、`pow` 提供精確運算。整數冪要求非負指數，有理數冪允許負指數。計數與進位參數僅接受整數，縮窄前檢查範圍。`sum`／`accumulate` 保持精確，`average` 轉為 double，結果為近似值。預設格式化輸出標準文字並遵守數值限制。

`round_with_mode`、`to_decimal_with_mode` 接受 `std::numeric::rounding_mode::{toward_zero, floor, ceil, away_from_zero, half_even, half_away_from_zero}`。`round` 與 `to_decimal` 預設為 half_even。十進位輸出保留指定小數位數。需要原生數值執行階段；目前獨立 AOT 匯出明確拒絕這些操作。

```VeroScript
import "rational.vs";

std::bigint n = std::bigint("18446744073709551616");
auto qr = std::numeric::divmod(n, std::bigint(3));
assert(qr.first * std::bigint(3) + qr.second == n);
std::rational r = std::rational(1, 3) + std::rational(1, 6);
assert(r.to_string() == "1/2");
assert(r.to_decimal(4) == "0.5000");
```

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/numeric.md) · [fr-FR](../../fr-FR/standard-library/numeric.md) · [ja-JP](../../ja-JP/standard-library/numeric.md) · [ko-KR](../../ko-KR/standard-library/numeric.md) · [zh-CN](../../zh-CN/standard-library/numeric.md) · [zh-TW](numeric.md)
