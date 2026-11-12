# Numeric aggregation

Module file: `numeric.vs`

```VeroScript
import "numeric.vs";

array<int> values = {2, 3, 5};
int with_initial = std::numeric::accumulate(values, 10);
int total = std::numeric::sum(values);
double mean = std::numeric::average(values);
```

`accumulate` accepts an explicit initial value and therefore also supports empty arrays. `sum` and `average` require a non-empty array and report an assertion failure for an empty input. `average` converts each element to `double`; use these functions with numeric element types.
`average` uses a running mean to avoid overflow from intermediate sums of finite values; floating-point rounding still applies.


- [Standard library foundation](foundation.md)


## Exact integers and rational numbers

Import `bigint.vs` for integer helpers, `rational.vs` for both types, or `core.vs`. The Core native catalog supplies the value types. Values are immutable and copied by value, including in containers, fields, function arguments and `std::attribute<T>`.

Integer division truncates toward zero; the remainder has the dividend’s sign. Right shift is arithmetic; bitwise operations use infinite two’s complement. A rational is always reduced with a positive denominator; its canonical text is `numerator/denominator` for fractions, or just the numerator for integers (zero is `0`). No implicit conversion from floating point, bool, enum or ordinary integers is added.

Use exact integer/string constructors, `parse`, `try_parse` and explicit conversion methods. Integer sources retain all 64 bits. `from_double_exact` uses the stored binary value, so 0.1 is not 1/10. `to_double`/`to_float` round directly to the destination format with ties to even; `_exact` variants reject precision loss. Fractional values cannot convert to integers without explicit rounding. Failed `try_` parsing/conversion preserves the output; resource limits, cancellation and allocation failures still propagate.

`std::numeric::gcd`, `lcm`, `divmod` and `pow` provide exact operations. Integer powers require nonnegative exponents; rational powers allow negative exponents. Counts and bases require integer arguments checked before narrowing. `sum`/`accumulate` remain exact; `average` converts to double and is approximate. Formatting uses canonical numeric text and obeys numeric limits.

`round_with_mode` and `to_decimal_with_mode` accept `std::numeric::rounding_mode::{toward_zero, floor, ceil, away_from_zero, half_even, half_away_from_zero}`. `round` and `to_decimal` default to half_even. Decimal output has exactly the requested number of fractional digits. Native runtime operations are required; standalone AOT export currently rejects these operations.

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

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](numeric.md) · [fr-FR](../../fr-FR/standard-library/numeric.md) · [ja-JP](../../ja-JP/standard-library/numeric.md) · [ko-KR](../../ko-KR/standard-library/numeric.md) · [zh-CN](../../zh-CN/standard-library/numeric.md) · [zh-TW](../../zh-TW/standard-library/numeric.md)
