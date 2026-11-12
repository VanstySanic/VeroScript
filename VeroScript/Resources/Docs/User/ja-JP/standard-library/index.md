# 標準ライブラリリファレンス

VeroScript 標準ライブラリは、一般的な文字列、数学、ファイル、パス、同時実行、およびイベント操作用のモジュールを提供します。スクリプトは、実行時モジュールをまとめた `core.vs` をインポートすることも、必要なモジュールのみをインポートすることもできます。

モジュールに属する関数は `std::strings::join` や `std::concurrent::spawn` のように公開名前空間を含む完全名で呼び出します。未修飾のグローバル名は拒否されます。`array<T>` や `print` などの基本型・直接提供関数は従来の正規名も使用できます。名前空間エイリアスは必要なモジュールのインポートを置き換えません。 [std](../language-basics/standard-namespace.md).

- [管理されたメモリ](memory.md)：`std::memory`

- [属性とメソッド関数値](attribute.md)：`std::attribute<T>` · `std::bind_method<F>`

## モジュールインデックス

- [組み込み関数](builtins.md)：出力、変換、数値、環境のランタイム入口。入力ラッパーには io.vs が必要です。
- [コア エントリ ポイント](core.md): 参照先に示す実行時モジュールを再エクスポートします。compiler.vs は明示的にインポートします。
- [文字列](string.md): 検索、スライス、分割、結合、トリミング、大文字と小文字の変換、置換、および書式設定。
- [数学](math.md): 三角法、平方根、丸め、およびクランプ。
- [アルゴリズム](algorithm.md): 配列の汎用ソート、反転、検索、カウント。
- [数値集約](numeric.md): 累積、合計、平均。
- [乱数](random.md): 明示的な状態を持つ決定論的疑似乱数ストリーム。
- [I/O](io.md): ファイル、ディレクトリ、標準入力、および作業ディレクトリ。
- [パス](path.md): ファイル名、ディレクトリ名、拡張子、および結合するパス。
- [同時実行](concurrent.md): チャネル、孤立したタスク、待機中のタスク、および協調キャンセル。
- [イベント](events.md): ユニキャストおよびマルチキャストのデリゲート。

## インポート規則

完全なライブラリをインポートします。

```VeroScript
import "core.vs";
```

または、個々のモジュールをインポートします。

```VeroScript
import "string.vs";
import "path.vs";
```

標準ライブラリ関数は通常、エクスポートされた宣言です。それらのパラメーターと戻り値の型は、対応するモジュール実装によって定義されます。ファイル、スレッド、またはホスト環境に関係する操作は、実行時の権限とホスト構成に依存する場合もあります。

- [アプリケーションの国際化 — std::i18n](i18n.md): `std::i18n`

コンパイル拡張では `compiler.vs` を明示的に import します。`core.vs` は再エクスポートせず、通常の実行コードから import してもコンパイル権限は得られません。

[コンパイル拡張](compiler.md)


- [標準ライブラリ基盤](foundation.md)

- [variant — 動的な値ボックス](variant.md)
<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/index.md) · [fr-FR](../../fr-FR/standard-library/index.md) · [ja-JP](index.md) · [ko-KR](../../ko-KR/standard-library/index.md) · [zh-CN](../../zh-CN/standard-library/index.md) · [zh-TW](../../zh-TW/standard-library/index.md)
