# Deterministic random streams

Module file: `random.vs`

```VeroScript
import "random.vs";

array<unsigned int> engine = std::random::make_engine(123u);
unsigned int bits = std::random::next_uint(engine);
double unit = std::random::next_double(engine); // [0, 1)
bool coin = std::random::next_bool(engine);
```

Each engine is explicit independent state. The module has no process-global generator, so tests remain reproducible and separate sessions do not contend over shared state. `std::random::seed` resets an existing engine. A zero seed is mapped to a fixed non-zero state because the xorshift32 algorithm's all-zero state cannot advance.

Treat the returned one-element `array<unsigned int>` as opaque state and pass the same array to subsequent calls. This representation preserves the standard library's behavior across the VM, C++ AOT, C# AOT, and Python AOT backends.
Do not modify the array element directly: `std::random::next_uint` rejects a zero state, which `std::random::seed` can repair. This generator is not suitable for cryptography.

## API

| Function | Behavior |
| --- | --- |
| `std::random::make_engine(unsigned int seed)` | Creates an independent random state. A zero seed is replaced with the documented non-zero initial state. |
| `std::random::seed(array<unsigned int> engine, unsigned int seed)` | Resets the supplied state, creating its element if the array is empty. |
| `std::random::next_uint(array<unsigned int> engine)` | Advances the state and returns an unsigned 32-bit value. |
| `std::random::next_bounded_uint(array<unsigned int> engine, unsigned int upper_exclusive)` | Returns an unbiased unsigned value in `[0, upper_exclusive)`; the bound must be positive. |
| `std::random::next_int(array<unsigned int> engine, int minimum, int maximum_exclusive)` | Returns an unbiased integer in `[minimum, maximum_exclusive)`; the range must be non-empty. |
| `std::random::next_double(array<unsigned int> engine)` | Returns a value in `[0, 1)`. |
| `std::random::next_double_range(array<unsigned int> engine, double minimum, double maximum_exclusive)` | Returns a value in `[minimum, maximum_exclusive)`; the range must be non-empty. |
| `std::random::next_bool(array<unsigned int> engine)` | Returns a pseudo-random Boolean. |

The bounded integer operation uses rejection sampling rather than a direct
remainder, so non-power-of-two bounds do not introduce modulo bias.
It samples from the `2^32-1` non-zero outputs that xorshift32 can actually produce. `std::random::next_double_range` requires finite, strictly increasing endpoints and resamples when floating-point rounding reaches the exclusive upper bound.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](random.md) · [fr-FR](../../fr-FR/standard-library/random.md) · [ja-JP](../../ja-JP/standard-library/random.md) · [ko-KR](../../ko-KR/standard-library/random.md) · [zh-CN](../../zh-CN/standard-library/random.md) · [zh-TW](../../zh-TW/standard-library/random.md)
