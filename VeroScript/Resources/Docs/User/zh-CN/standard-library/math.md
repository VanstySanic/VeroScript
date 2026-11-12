# 数学模块

模块文件：`math.vs`

导入方式：

```VeroScript
import "math.vs";
```

## 函数

| 函数 | 说明 |
| --- | --- |
| `std::sqrt(double x)` | 返回平方根。 |
| `std::sin(double x)` | 返回正弦值。参数使用弧度。 |
| `std::cos(double x)` | 返回余弦值。参数使用弧度。 |
| `std::tan(double x)` | 返回正切值。参数使用弧度。 |
| `std::floor(double x)` | 向负无穷取整。 |
| `std::ceil(double x)` | 向正无穷取整。 |
| `std::round(double x)` | 舍入到最近的整数值；正好处于中间时向远离零的方向舍入。 |
| `std::trunc(double x)` | 向零舍入并去除小数部分。 |
| `std::pow(double base, double exponent)` | 计算 `base` 的 `exponent` 次幂。 |
| `std::exp(double x)` | 返回 `x` 的自然指数。 |
| `std::log(double x)` | 返回 `x` 的自然对数。 |
| `std::log2(double x)` | 返回以 2 为底的 `x` 的对数。 |
| `std::asin(double x)` / `std::acos(double x)` | 返回反正弦或反余弦，单位为弧度。 |
| `std::atan2(double y, double x)` | 返回点 `(x, y)` 的角度，单位为弧度。 |
| `std::isfinite(double x)` / `std::isnan(double x)` | 判断数值是否为有限值或 NaN。 |
| `std::pi()` / `std::e()` | 返回数学常数 π 或 e。 |
| `std::clamp<T>(T x, T lo, T hi)` | 将数值类型 `T` 的值限制到 `[lo, hi]`。 |
| `std::clamp_int(int x, int lo, int hi)` | 将整数限制在 `[lo, hi]` 范围内。 |

`std::clamp<T>` 支持非 bool 的基本数值类型，相同参数类型可推断 `T`。反向或 NaN 边界会被拒绝，NaN 输入保留。`std::clamp_int` 对 int 调用同一操作。完整转换规则见[基础参考](foundation.md)。

绝对值、最小值和最大值由运行时提供的全局内置函数支持，不属于 `math.vs` 中的导出声明。


- [标准库基础](foundation.md)

<a id="floating-results"></a>

浮点定义域错误或溢出可能产生 NaN 或无穷；需要有限结果时检查 `std::isnan`／`std::isfinite`。NaN 的文本拼写及符号呈现依赖目标平台，不能用该文本作为可移植身份。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/math.md) · [fr-FR](../../fr-FR/standard-library/math.md) · [ja-JP](../../ja-JP/standard-library/math.md) · [ko-KR](../../ko-KR/standard-library/math.md) · [zh-CN](math.md) · [zh-TW](../../zh-TW/standard-library/math.md)
