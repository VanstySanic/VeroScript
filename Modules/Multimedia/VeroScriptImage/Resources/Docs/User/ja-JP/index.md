# Image

## サービスの利用

`Image::IService::Decode` は密な straight-alpha sRGB RGBA8 を返し、失敗時は以前の結果を保持します。`Image::Validate` は寸法とバッファを検証します。`FDecodeOptions` の既定値は各辺 16,384、16,777,216 ピクセル、入力 64 MiB です。リソース検索、キャッシュ、GPU 転送は呼び出し側が担当します。契約は `image` / `veroscript.image` / `0.1.0` です。

`stb` などのプロバイダーを用意します。Image バインド登録後は `VS::Image::Service::Create("stb").Load("image.png")` または `Decode(bytes)` を使えます。スクリプトのサイズ／添字は `int64`、制限は巨大文字列のコピー前にも適用されます。注入した reader がリソースを解決し、未設定時のファイルパスはプロセス作業ディレクトリ基準です。

## SDK と統合

```cmake
find_package(VeroScriptImage 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Image)
```

ネイティブ SDK は Core なしで利用できます。任意のスクリプトバインドとネイティブプロバイダーは、実際の Core ビルド、公開 ABI、ツールチェーンを一致させてください。製品バージョンだけでは不十分です。必要なネイティブ依存と Bindings コンポーネントも導入します。

- [Image](index.md) · [Font](../../../../../VeroScriptFont/Resources/Docs/User/ja-JP/index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/ja-JP/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
