# 數學模組

模組檔案：`math.vs`

匯入方式：

```VeroScript
import "math.vs";
```

## 函式

| 函式 | 說明 |
| --- | --- |
| `std::sqrt(double x)` | 傳回平方根。 |
| `std::sin(double x)` | 傳回正弦值。參數使用弧度。 |
| `std::cos(double x)` | 傳回餘弦值。參數使用弧度。 |
| `std::tan(double x)` | 傳回正切值。參數使用弧度。 |
| `std::floor(double x)` | 向負無限取整。 |
| `std::ceil(double x)` | 向正無窮取整。 |
| `std::round(double x)` | 四捨五入到最接近的整數值；正好在中間時向遠離零的方向取整。 |
| `std::trunc(double x)` | 向零取整並移除小數部分。 |
| `std::pow(double base, double exponent)` | 計算 `base` 的 `exponent` 次方。 |
| `std::exp(double x)` | 傳回 `x` 的自然指數。 |
| `std::log(double x)` | 傳回 `x` 的自然對數。 |
| `std::log2(double x)` | 傳回以 2 為底的 `x` 對數。 |
| `std::asin(double x)` / `std::acos(double x)` | 傳回反正弦或反餘弦，單位為弧度。 |
| `std::atan2(double y, double x)` | 傳回點 `(x, y)` 的角度，單位為弧度。 |
| `std::isfinite(double x)` / `std::isnan(double x)` | 判斷數值是否為有限值或 NaN。 |
| `std::pi()` / `std::e()` | 傳回數學常數 π 或 e。 |
| `std::clamp<T>(T x, T lo, T hi)` | 將數值型別 `T` 的值限制到 `[lo, hi]`。 |
| `std::clamp_int(int x, int lo, int hi)` | 將整數限制在 `[lo, hi]` 範圍內。 |

`std::clamp<T>` 支援非 bool 的基本數值型別，相同參數型別可推斷 `T`。反向或 NaN 邊界會被拒絕，NaN 輸入保留。`std::clamp_int` 對 int 呼叫同一操作。完整轉換規則請見[基礎參考](foundation.md)。

絕對值、最小值和最大值由執行階段提供的全域內建函式支援，不屬於 `math.vs` 中的匯出宣告。


- [標準函式庫基礎](foundation.md)

<a id="floating-results"></a>

浮點定義域錯誤或溢位可能產生 NaN 或無窮；需要有限結果時檢查 `std::isnan`／`std::isfinite`。NaN 的文字拼寫及符號呈現依賴目標平台，不能使用該文字作為可攜識別。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/math.md) · [fr-FR](../../fr-FR/standard-library/math.md) · [ja-JP](../../ja-JP/standard-library/math.md) · [ko-KR](../../ko-KR/standard-library/math.md) · [zh-CN](../../zh-CN/standard-library/math.md) · [zh-TW](math.md)
