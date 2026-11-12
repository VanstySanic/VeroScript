# VeroScriptGraphics — 描画リソースと二次元描画

<a id="graphics-service-contract"></a>

Graphics は VS::Graphics を通じてデバイス、テクスチャ、サンプラー、インデックス付きジオメトリ、Renderer2D を提供します。公開契約に SDL/Core 型は含みません。モジュールとネイティブサービス契約は同じバージョン 0.1.1 を使用します。更新時はホストとプロバイダーを一緒に再ビルドします。

スクリプトのリソース、機能、寸法の取得と完了した要求の結果取得（`GetDevice`、`GetPixels`）は const メソッドで、読み取り専用の値キャプチャから呼び出せます。描画、更新、要求のポーリングや取り消し、リソースの終了には書き込み可能な受信側が必要です。`GetError` も要求をポーリングします。これらの操作では `mutable` または書き込み可能な参照キャプチャを使用してください。

ジオメトリは一度アップロードして再利用します。頂点は X,Y,U,V,R,G,B,A の八つの double、インデックスは範囲確認付き int64 です。UpdateTexture は密に並べた array<uint8> RGBA を転送します。CreateSampler は Filter と U/V AddressMode を指定します。DrawList2D は AddGeometry 時に SetTexture、SetTint、SetTransform、SetBlendMode を保存します。PushClip は矩形を交差し、PopClip は前のクリップを復元します。

位置は視口内の描画ピクセルで Y が下向き、クリップは対象全体の絶対ピクセルです。SetTransform は列優先の十六個の double を受け取り、GUI は Platform DisplayScale でピクセルへ変換します。頂点色は線形 RGBA で既定では非乗算済みです。Tint は常に非乗算済みです。StraightAlpha は非乗算済み、PremultipliedAlpha は乗算済みテクスチャを使います。透明黒の上に描いた半透明レイヤーは乗算済みピクセルとなるため合成に PremultipliedAlpha を使います。sRGB 変換は形式に従います。

RenderTexture と RenderSurface は target,list,clear,R,G,B,A を受け取り、Viewport 付きでは x,y,width,height を追加します。消去は対象全体に適用します。以前の内容を使うテクスチャは初期化が必要で、ウィンドウ表面は消去必須です。交換画像を取得できなければ成功しても表示しません。所有スレッドで各リソースを閉じてからデバイスを閉じ、Clear でリスト参照を解放します。閉じた資源、異なるデバイス、描画先の同時読み取りは拒否します。既定上限はテクスチャと整列転送 256 MiB、ジオメトリ 64 MiB、命令とクリップ深度 65,536。同期読み戻しは C++ API を使用します。スクリプトは `Device.ReadTextureAsync` の要求をポーリングし、成功後にのみピクセルを取得します。

```veroscript
VS::Graphics::Service graphics = VS::Graphics::Service::Create("sdl3");
VS::Graphics::Device device = graphics.CreateDevice("");
VS::Graphics::Renderer2D renderer = device.CreateRenderer2D();
array<double> vertices = {0.0,0.0,0.0,0.0,1.0,0.0,0.0,1.0,
						 32.0,0.0,1.0,0.0,1.0,0.0,0.0,1.0,
						 0.0,32.0,0.0,1.0,1.0,0.0,0.0,1.0};
array<int64> indices = {0,1,2};
VS::Graphics::Geometry2D geometry = renderer.CreateGeometry(vertices,indices);
VS::Graphics::DrawList2D list = VS::Graphics::DrawList2D::Create();
list.PushClip(0,0,64,64);
list.AddGeometry(geometry);
list.PopClip();
VS::Graphics::Texture target = device.CreateTextureWithFormat(64,64,VS::Graphics::TextureFormat::Rgba8Unorm);
renderer.RenderTexture(target,list,true,0.0,0.0,0.0,0.0);
list.Clear();
geometry.Close();
target.Close();
renderer.Close();
device.Close();
```

SDL ウィンドウ表面は線形ブレンドを保つため SDR_LINEAR 合成を要求し、非対応の場合は生成に失敗します。

## インストール済みネイティブ SDK の使用

```cmake
find_package(VeroScriptGraphics 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Graphics)
```

<a id="vertex-color-mode"></a>

SetVertexColorMode は Blend と独立して VertexColorMode::Straight（既定）または Premultiplied を指定します。乗算済み頂点 RGB は補間中も乗算済みを保ち、Tint は常に非乗算で、PremultipliedAlpha 出力時にその alpha を RGB に一度だけ適用します。AddGeometry はこの状態を保存し、Clear は Straight に戻します。頂点色は引き続き線形値であり、この設定は sRGB を復号しません。

## 合成と手動デバイス復旧

Composition2D は AddDraw、BeginLayer/EndLayer、PushMask/PopMask、AddBlur、AddColorMatrix、AddShadow、AddBackdropBlur を記録します。Device.CreateCompositor2D() は一時ターゲットを再利用し、CreateCompositor2DWithLimits(bytes,depth) で予算を指定します。既定は 256 MiB と 32 階層、記録上限は 64 階層と 65,536 命令／描画です。グループ不透明度は合成後に一度だけ適用し、入れ子の逆マスクを含む alpha マスクは交差します。ぼかしは半径 0..64 ピクセル、13 サンプルの分離可能な二つのパスです。影は元の内容を保持し、背景ぼかしはマスク内だけを置換します。色行列は行優先の二十値です。合成画素は線形の乗算済み RGBA で、非乗算 ClearColor は合成器が変換します。既存内容のロードと Opaque 描画には互換性のある乗算済み画素が必要です。sRGB 変換はテクスチャ形式が処理します。不正なスコープや予算不足では最終対象を変更しません。Trim は一時対象を解放し、デバイスより先に合成器を閉じます。DrawList2D.SetAlphaMask、SetBlur、SetColorMatrix、ResetEffect は単一パス効果で、Additive は乗算済み成分を加算します。表面メソッドは composition の前に現在の描画画素 width/height を取り、Viewport 版は末尾に x,y,width,height を追加します。表示失敗時は false、GetPooledBytes は int64 です。Device.GetGeneration と SupportsDeviceRecovery は手動復旧を示します。Service.RecoverDevice(previous,driver) は置換デバイスを先に作成し、成功後にのみ旧世代を無効化します。失敗時は旧世代を変更しません。成功後は全資源を再作成／アップロードし、旧表面を閉じてから同じウィンドウを再利用し、旧資源と旧デバイスを閉じます。Invalidate 後も資源の Close は有効です。選択したドライバーは Device.Poll でデバイス喪失を報告する場合があります。復旧と資源復元はホストが明示的に行います。この API は Renderer3D、任意のユーザーシェーダー、ホストコマンド記録を提供しません。

合成器の一時テクスチャは既定で RGBA8 sRGB 保存を使い、サンプリングとブレンドは線形空間で実行します。線形 8 ビット中間画像による sRGB 暗部の損失を防ぎます。C++ では線形量子化を伴う RGBA8 UNORM を明示選択でき、二つのスクリプト作成メソッドは sRGB を使います。ColorMatrix と AlphaMask は負の UV と一を超える UV でも Clamp/Repeat/Mirror に従います。Blur と Shadow はソース矩形の外を透明として扱い、半径はソース画素単位です。

## グラフィックス API と非同期要求

[VeroScriptSdl3 — 基盤実装 — グラフィックス API と非同期要求](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/ja-JP/index.md#sdl-graphics-drivers) を参照してください。

<a id="asynchronous-requests"></a>

CreateDeviceAsync/RecoverDeviceAsync は DeviceRequest、Device.ReadTextureAsync は ReadbackRequest を返します。所有スレッドの Poll は Pending / Succeeded / Failed / Cancelled を返し、成功後に GetDevice/GetPixels を使用します。スクリプトの GetError で診断を取得します。既定実装は同期完了する場合があります。ブラウザー WebGPU の同期作成・読み戻し・WaitIdle は graphics.async_required を返すため、ポーリング間でイベントループに制御を返してください。

<a id="resource-retirement"></a>

復旧成功後に旧世代を無効化し、資源を再作成します。Cancel は結果を破棄しますが、ネイティブ処理が続く場合があります。資源・デバイス・要求を閉じて参照を解放し、所有スレッドで Service.HasPendingRetirements() が false になるまで回収します。ブラウザーではフレームを待ちます。Platform 所有サービスを終了処理まで保持してください。

<a id="graphics-capabilities"></a>

能力は選択したデバイス／ドライバーに属します。C++ では `IDevice::GetCapabilities()`、スクリプトでは `SupportsGuiBasic`、`SupportsTransforms`、`SupportsClipMasks`、`SupportsFilters`、`SupportsDeviceRecovery` を確認します。置換を要求する前に復旧能力を調べ、失敗を処理してください。提供者名だけでは判断できません。SDL3 の制限は [UIKit デバイス復旧](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/ja-JP/index.md#uikit-device-recovery)にあります。

スクリプト頂点の八つの double は有限 float に収まり、int64 インデックスはネイティブ uint32 へ変換する前に確認されます。`UpdateTexture` は指定領域の幅 × 高さ × 4 の詰めた RGBA バイトを受け取り、符号化を保持します。`ResetTexture` は単色描画に戻します。描画リストの変更と描画は所有スレッドで行い、並行実行してはいけません。ネイティブの割当例外は C++ へ伝播し得ますが、スクリプトアダプターはコールバック失敗を診断に変換します。読戻しは保存された RGBA バイトを返し、失敗時は渡された出力を保ちます。ブロックするネイティブ読戻し／WaitIdle は通常のフレームループ外で使います。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
