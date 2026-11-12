# 決定論的乱数ストリーム

モジュール: `random.vs`

`std::random::make_engine(seed)` で独立した明示的状態を作り、同じ
`array<unsigned int>` を後続の API に渡します。プロセス全体の生成器はないため、
テストは再現可能です。ゼロ seed は固定された非ゼロ状態に変換されます。

| API | 動作 |
| --- | --- |
| `std::random::make_engine(seed)` | 独立した状態を作ります。 |
| `std::random::seed(engine, seed)` | 状態をリセットします。空配列なら要素を作ります。 |
| `std::random::next_uint(engine)` | 状態を進めて符号なし整数を返します。 |
| `std::random::next_bounded_uint(engine, upper_exclusive)` | `[0, upper_exclusive)` の値を返します。上限は正でなければなりません。 |
| `std::random::next_int(engine, minimum, maximum_exclusive)` | 半開区間の整数を返します。範囲は空にできません。 |
| `std::random::next_double(engine)` | `[0, 1)` の値を返します。 |
| `std::random::next_double_range(engine, minimum, maximum_exclusive)` | 指定した半開区間の浮動小数値を返します。 |
| `std::random::next_bool(engine)` | 疑似乱数の真偽値を返します。 |

bounded 版は modulo bias を避ける rejection sampling を使用します。
サンプリングは xorshift32 が実際に生成できる `2^32-1` 個の非ゼロ出力に基づきます。状態配列を直接変更しないでください。ゼロ状態では `std::random::next_uint` がエラーになり、`std::random::seed` で復旧できます。暗号用途には使用できません。`std::random::next_double_range` には有限で昇順の端点が必要で、丸めによって排他的上限に達した場合は再サンプリングします。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/random.md) · [fr-FR](../../fr-FR/standard-library/random.md) · [ja-JP](random.md) · [ko-KR](../../ko-KR/standard-library/random.md) · [zh-CN](../../zh-CN/standard-library/random.md) · [zh-TW](../../zh-TW/standard-library/random.md)
