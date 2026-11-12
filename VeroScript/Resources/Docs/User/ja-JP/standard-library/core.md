# コアエントリーモジュール

モジュールファイル: `core.vs`

`core.vs` は、集約された標準ライブラリのエントリ ポイントです。これらのモジュールは `export import` で再エクスポートされます。

- `i18n.vs`
- `memory.vs`
- `math.vs`
- `algorithm.vs`
- `numeric.vs`
- `random.vs`
- `io.vs`
- `string.vs`
- `path.vs`
- `concurrent.vs`
- `attribute.vs`
- `callable.vs`
- `delegate.vs`

上記の実行時モジュールをまとめてインポートします。

```VeroScript
import "core.vs";
```

個々のモジュールを直接インポートして、依存関係と名前のスコープを明示することもできます。 `core.vs` では、別個の API は導入されません。他の標準ライブラリ モジュールのパブリック宣言を公開します。

書式設定、ファイル、同時実行性、およびイベント操作の署名については、対応するモジュールのページを参照してください。

- [アプリケーションの国際化 — std::i18n](i18n.md): `std::i18n`

コンパイル拡張では `compiler.vs` を明示的に import します。`core.vs` は再エクスポートせず、通常の実行コードから import してもコンパイル権限は得られません。

[コンパイル拡張](compiler.md)

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/core.md) · [fr-FR](../../fr-FR/standard-library/core.md) · [ja-JP](core.md) · [ko-KR](../../ko-KR/standard-library/core.md) · [zh-CN](../../zh-CN/standard-library/core.md) · [zh-TW](../../zh-TW/standard-library/core.md)
