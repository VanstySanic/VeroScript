# 数値集約

モジュール: `numeric.vs`

`std::numeric::accumulate(values, initial)` は明示的な初期値を受け取り、空の配列にも対応します。`sum` と `average` は空でない配列を必要とし、空入力ではアサーションになります。`average` は各要素を `double` に変換します。
`average` は逐次平均を使用し、有限値の中間合計によるオーバーフローを避けます。結果には浮動小数点の丸めが適用されます。


- [標準ライブラリ基盤](foundation.md)


## 大整数と正確な有理数

`bigint.vs`、`rational.vs`、または `core.vs` をインポートします。型はコアのネイティブカタログが提供します。不変の値としてコピーされ、コンテナ、フィールド、引数、戻り値、`std::attribute<T>` で使用できます。

整数除算はゼロ方向へ丸め、剰余は被除数と同符号です。右シフトは算術シフト、ビット演算は無限長の2の補数です。有理数は常に既約で分母が正です。標準表記は `分子/分母` です。分母が1なら整数のみ、ゼロなら `0` を出力します。浮動小数点、bool、enum、通常整数からの暗黙変換は追加しません。

整数／文字列コンストラクタ、`parse`、`try_parse`、明示的な変換を使用します。整数入力は64ビットを保持します。`from_double_exact` は保存された2進数値を読み取るため 0.1 は 1/10 ではありません。`to_double`/`to_float` は目標形式へ直接偶数丸めし、`_exact` は精度損失を拒否します。分数は整数変換前に明示的に丸めます。通常の `try_` 失敗では出力を保持し、上限、キャンセル、割り当て失敗はエラーを伝播します。

`std::numeric::gcd`、`lcm`、`divmod`、`pow` は正確です。整数の指数は非負、有理数は負の指数も可能です。桁数や基数は整数のみで、縮小前に範囲を確認します。`sum`/`accumulate` は正確、`average` は double を使用する近似です。標準の書式化にも数値上限が適用されます。

`round_with_mode` と `to_decimal_with_mode` は `std::numeric::rounding_mode::{toward_zero, floor, ceil, away_from_zero, half_even, half_away_from_zero}` を受け取ります。既定は half_even です。指定した小数桁数を保持します。ネイティブ実行環境が必要で、独立した AOT 出力は現在これらを拒否します。

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

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/numeric.md) · [fr-FR](../../fr-FR/standard-library/numeric.md) · [ja-JP](numeric.md) · [ko-KR](../../ko-KR/standard-library/numeric.md) · [zh-CN](../../zh-CN/standard-library/numeric.md) · [zh-TW](../../zh-TW/standard-library/numeric.md)
