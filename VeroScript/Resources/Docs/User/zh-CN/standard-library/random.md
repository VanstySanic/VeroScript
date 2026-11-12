# 确定性随机流

模块文件：`random.vs`

使用 `std::random::make_engine(seed)` 创建显式的独立状态，并把同一个
`array<unsigned int>` 传给后续 API。模块没有进程级全局生成器，因此测试可复现；
零种子会映射为固定的非零状态。
状态数组是可传递的实现表示，但调用方不得直接改写元素；状态为零时
`std::random::next_uint` 会报告错误，可用 `std::random::seed` 恢复。生成器不是密码学随机源。

| API | 行为 |
| --- | --- |
| `std::random::make_engine(seed)` | 创建独立的随机状态。 |
| `std::random::seed(engine, seed)` | 重置状态；数组为空时先创建元素。 |
| `std::random::next_uint(engine)` | 推进状态并返回无符号整数。 |
| `std::random::next_bounded_uint(engine, upper_exclusive)` | 返回 `[0, upper_exclusive)` 中的无偏值；上界必须为正。 |
| `std::random::next_int(engine, minimum, maximum_exclusive)` | 返回 `[minimum, maximum_exclusive)` 中的整数；区间不能为空。 |
| `std::random::next_double(engine)` | 返回 `[0, 1)` 中的值。 |
| `std::random::next_double_range(engine, minimum, maximum_exclusive)` | 返回指定半开浮点区间中的值。 |
| `std::random::next_bool(engine)` | 返回伪随机布尔值。 |

带上界的整数生成使用拒绝采样，避免直接取余带来的分布偏差。
采样以 xorshift32 实际可达的 `2^32-1` 个非零输出为基础。
`std::random::next_double_range` 要求两个端点有限且严格递增；浮点舍入触及排他上界时会重新采样。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/random.md) · [fr-FR](../../fr-FR/standard-library/random.md) · [ja-JP](../../ja-JP/standard-library/random.md) · [ko-KR](../../ko-KR/standard-library/random.md) · [zh-CN](random.md) · [zh-TW](../../zh-TW/standard-library/random.md)
