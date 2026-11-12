# 決定性隨機串流

模組檔案：`random.vs`

使用 `std::random::make_engine(seed)` 建立明確的獨立狀態，並將同一個
`array<unsigned int>` 傳給後續 API。模組沒有程序級全域產生器，因此測試可重現；
零種子會對映為固定的非零狀態。

| API | 行為 |
| --- | --- |
| `std::random::make_engine(seed)` | 建立獨立的隨機狀態。 |
| `std::random::seed(engine, seed)` | 重設狀態；陣列為空時先建立元素。 |
| `std::random::next_uint(engine)` | 推進狀態並回傳無號整數。 |
| `std::random::next_bounded_uint(engine, upper_exclusive)` | 回傳 `[0, upper_exclusive)` 的無偏值；上限必須為正。 |
| `std::random::next_int(engine, minimum, maximum_exclusive)` | 回傳 `[minimum, maximum_exclusive)` 的整數；區間不能為空。 |
| `std::random::next_double(engine)` | 回傳 `[0, 1)` 的值。 |
| `std::random::next_double_range(engine, minimum, maximum_exclusive)` | 回傳指定半開浮點區間的值。 |
| `std::random::next_bool(engine)` | 回傳偽隨機布林值。 |

帶上限的整數產生使用拒絕取樣，避免直接取餘造成的分布偏差。
取樣以 xorshift32 實際可達的 `2^32-1` 個非零輸出為基礎。請勿直接改寫狀態陣列；狀態為零時 `std::random::next_uint` 會報錯，可用 `std::random::seed` 復原。此產生器不適用於密碼學用途。`std::random::next_double_range` 要求有限且嚴格遞增的端點，浮點捨入碰到排他上限時會重新取樣。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/random.md) · [fr-FR](../../fr-FR/standard-library/random.md) · [ja-JP](../../ja-JP/standard-library/random.md) · [ko-KR](../../ko-KR/standard-library/random.md) · [zh-CN](../../zh-CN/standard-library/random.md) · [zh-TW](random.md)
