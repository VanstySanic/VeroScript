# Stb

バイナリ PGM／PPM は `Maxval`（1–65535）に基づいて RGBA8 に正規化します。無効な最大値や範囲外のサンプルはエラーとなり、以前の出力を保持します。最大値 255／65535 の入力では従来の量子化とバイト順の動作を維持します。

## サービスの利用

`Stb::CreateImageService()` は `image/stb` を提供します。PNG、JPEG、BMP、TGA など有効な stb_image 形式を straight-alpha sRGB RGBA8 に変換し、GIF は先頭フレームです。HDR／浮動小数点出力は無効です。top-down BMP、16-bit PGM/PPM のバイト順、CgBI PNG を同じ契約に正規化します。失敗時は出力を保持し、明示された制限を適用します。

呼び出し中の入力を変更せず、出力とエラーを分ければ並行呼び出しできます。デコーダーの thread-local storage がないビルドでは全インスタンス間で直列化します。`VS::Image` は Image バインドが提供し、任意の `VeroScriptStbModule` はプロバイダーを提供します。Image／Stb と必要な Bindings を一緒に導入してください。

## SDK と統合

```cmake
find_package(VeroScriptStb 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Stb)
```

ネイティブ SDK は Core なしで利用できます。任意のスクリプトバインドとネイティブプロバイダーは、実際の Core ビルド、公開 ABI、ツールチェーンを一致させてください。製品バージョンだけでは不十分です。必要なネイティブ依存と Bindings コンポーネントも導入します。

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/ja-JP/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/ja-JP/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/ja-JP/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
