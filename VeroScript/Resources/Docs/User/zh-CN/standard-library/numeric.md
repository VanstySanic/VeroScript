# 数值聚合

模块文件：`numeric.vs`

`std::numeric::accumulate(values, initial)` 支持显式初值与空数组。`sum` 和 `average` 要求非空数组；空输入会触发断言。`average` 会把元素转换为 `double`，这些 API 应用于数值元素类型。
`average` 使用递推均值，避免有限数值因中间求和溢出而得到无穷大；浮点结果仍受舍入精度限制。


- [标准库基础](foundation.md)


## 大整数与精确有理数

导入 `bigint.vs` 使用整数辅助函数，导入 `rational.vs` 使用两种类型，或导入 `core.vs`。值类型由核心 native catalog 提供。值不可变并按值复制，可用于容器、字段、参数、返回值与 `std::attribute<T>`。

整数除法向零取整，余数与被除数同号。右移为算术右移，位运算采用无限位二进制补码语义。有理数始终约分且分母为正，规范文本为 `分子/分母`；分母为 1 时只输出整数，零输出 `0`。不新增浮点、bool、枚举或普通整数到这两种类型的隐式转换。

使用显式整数／字符串构造、`parse`、`try_parse` 及显式转换方法。整数输入完整保留 64 位。`from_double_exact` 精确读取已存储的二进制浮点值，因此 0.1 不等于 1/10。`to_double`／`to_float` 直接舍入到目标格式，中点取偶；`_exact` 版本拒绝精度损失。带小数部分的值须先显式取整才能转换为整数。`try_` 解析／转换失败保留输出；资源超限、取消及分配失败仍传播错误。

`std::numeric::gcd`、`lcm`、`divmod`、`pow` 提供精确运算。整数幂要求非负指数，有理数幂允许负指数。计数和进制参数仅接受整数，缩窄前检查范围。`sum`／`accumulate` 保持精确，`average` 转成 double，结果为近似值。默认格式化输出规范文本并遵守数值限制。

`round_with_mode`、`to_decimal_with_mode` 接受 `std::numeric::rounding_mode::{toward_zero, floor, ceil, away_from_zero, half_even, half_away_from_zero}`。`round` 与 `to_decimal` 默认使用 half_even。十进制输出严格保留指定小数位数。需要原生数值运行时；当前独立 AOT 导出明确拒绝这些操作。

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

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/numeric.md) · [fr-FR](../../fr-FR/standard-library/numeric.md) · [ja-JP](../../ja-JP/standard-library/numeric.md) · [ko-KR](../../ko-KR/standard-library/numeric.md) · [zh-CN](numeric.md) · [zh-TW](../../zh-TW/standard-library/numeric.md)
