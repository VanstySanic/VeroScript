# Math module

Module file: `math.vs`

Import it with:

```VeroScript
import "math.vs";
```

## Functions

| Function | Description |
| --- | --- |
| `std::sqrt(double x)` | Returns the square root. |
| `std::sin(double x)` | Returns the sine. The argument is in radians. |
| `std::cos(double x)` | Returns the cosine. The argument is in radians. |
| `std::tan(double x)` | Returns the tangent. The argument is in radians. |
| `std::floor(double x)` | Rounds toward negative infinity. |
| `std::ceil(double x)` | Rounds toward positive infinity. |
| `std::round(double x)` | Rounds to the nearest integer value; halfway cases are rounded away from zero. |
| `std::trunc(double x)` | Removes the fractional part by rounding toward zero. |
| `std::pow(double base, double exponent)` | Raises `base` to `exponent`. |
| `std::exp(double x)` | Returns the natural exponential of `x`. |
| `std::log(double x)` | Returns the natural logarithm of `x`. |
| `std::log2(double x)` | Returns the base-2 logarithm of `x`. |
| `std::asin(double x)` / `std::acos(double x)` | Returns the inverse sine or cosine in radians. |
| `std::atan2(double y, double x)` | Returns the angle of `(x, y)` in radians. |
| `std::isfinite(double x)` / `std::isnan(double x)` | Tests whether a value is finite or NaN. |
| `std::pi()` / `std::e()` | Returns the mathematical constants π or e. |
| `std::clamp<T>(T x, T lo, T hi)` | Clamps a value of numeric type `T` to `[lo, hi]`. |
| `std::clamp_int(int x, int lo, int hi)` | Clamps an integer to `[lo, hi]`. |

`std::clamp<T>` supports non-bool primitive numeric types; matching argument types can infer `T`. Reversed or NaN bounds are rejected. A NaN input is preserved. `std::clamp_int` calls the same operation for int. Full conversion rules are in [the foundation reference](foundation.md).

Absolute value, minimum, and maximum are runtime-provided global built-ins rather than exports from `math.vs`.


- [Standard library foundation](foundation.md)

<a id="floating-results"></a>

Floating domain errors or overflow can produce NaN or infinities; check `std::isnan` / `std::isfinite` when a finite result is required. The text spelling and sign presentation of NaN depend on the target platform; do not use that text as a portable identity.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](math.md) · [fr-FR](../../fr-FR/standard-library/math.md) · [ja-JP](../../ja-JP/standard-library/math.md) · [ko-KR](../../ko-KR/standard-library/math.md) · [zh-CN](../../zh-CN/standard-library/math.md) · [zh-TW](../../zh-TW/standard-library/math.md)
