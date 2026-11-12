# 数学モジュール

モジュールファイル: `math.vs`

次のようにインポートします。

```VeroScript
import "math.vs";
```

## 関数

|機能 |説明 |
| --- | --- |
| `std::sqrt(double x)` |平方根を返します。 |
| `std::sin(double x)` |正弦を返します。引数はラジアン単位です。 |
| `std::cos(double x)` |コサインを返します。引数はラジアン単位です。 |
| `std::tan(double x)` |接線を返します。引数はラジアン単位です。 |
| `std::floor(double x)` |負の無限大に向かって丸めます。 |
| `std::ceil(double x)` |正の無限大に向かって丸めます。 |
| `std::round(double x)` |最も近い整数値に丸めます。ちょうど中間の場合はゼロから遠ざかる方向です。 |
| `std::trunc(double x)` |ゼロ方向に丸めて小数部分を削除します。 |
| `std::pow(double base, double exponent)` |`base` を `exponent` 乗します。 |
| `std::exp(double x)` |`x` の自然指数を返します。 |
| `std::log(double x)` |`x` の自然対数を返します。 |
| `std::log2(double x)` |`x` の 2 を底とする対数を返します。 |
| `std::asin(double x)` / `std::acos(double x)` |逆正弦または逆余弦をラジアンで返します。 |
| `std::atan2(double y, double x)` |`(x, y)` の角度をラジアンで返します。 |
| `std::isfinite(double x)` / `std::isnan(double x)` |値が有限または NaN かどうかを判定します。 |
| `std::pi()` / `std::e()` |数学定数 π または e を返します。 |
| `std::clamp<T>(T x, T lo, T hi)` | 数値型 `T` の値を `[lo, hi]` に制限します。 |
| `std::clamp_int(int x, int lo, int hi)` |整数を `[lo, hi]` にクランプします。 |

`std::clamp<T>` は bool 以外のプリミティブ数値型を扱い、引数の型が一致すれば `T` を推論できます。逆順または NaN の境界は拒否し、NaN 入力は保持します。`std::clamp_int` は int 用に同じ操作を呼びます。変換規則は [基礎リファレンス](foundation.md)を参照してください。

絶対値、最小値、および最大値は、`math.vs` からのエクスポートではなく、ランタイムによって提供されるグローバル組み込みです。


- [標準ライブラリ基盤](foundation.md)

<a id="floating-results"></a>

浮動小数点の定義域エラーやオーバーフローは NaN／無限大になり得ます。有限値が必要なら `std::isnan`／`std::isfinite` で確認してください。NaN の文字列表記と符号の表示は対象プラットフォームに依存するため、移植可能な識別子に使ってはいけません。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/math.md) · [fr-FR](../../fr-FR/standard-library/math.md) · [ja-JP](math.md) · [ko-KR](../../ko-KR/standard-library/math.md) · [zh-CN](../../zh-CN/standard-library/math.md) · [zh-TW](../../zh-TW/standard-library/math.md)
