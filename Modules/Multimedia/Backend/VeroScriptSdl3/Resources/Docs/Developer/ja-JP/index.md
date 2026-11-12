# VeroScriptSdl3

<a id="provider-boundary"></a>

公開 Platform/Graphics/Audio 契約は SDL と Core から独立させます。具体的な SDL 実装、ランタイム、ドライバー設定、ライセンスは VeroScriptSdl3 が所有します。プロバイダー変更時は契約の一致、インストール／移設 SDK、モジュールの寿命と所有スレッドでの遅延解放を検証してください。静的ソース確認は実機やプラットフォーム検証の代わりにはなりません。

## ビルドとホスト連携

```sh
cmake -S . -B build/visual -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF -DVEROSCRIPT_BUILD_VISUAL=ON
cmake --build build/visual --config Release
cmake --install build/visual --config Release --component VeroScriptVisual --prefix out/visual-sdk
```

## 保守と検証

グラフィックスサービス登録は `VEROSCRIPT_SDL3_GRAPHICS`、各ドライバーは `VEROSCRIPT_SDL3_GPU`、`VEROSCRIPT_SDL3_D3D11`、`VEROSCRIPT_SDL3_OPENGL`、`VEROSCRIPT_SDL3_WEBGPU` で個別に制御します。WebGPU は既定で OFF です。コントローラーと音声は `VEROSCRIPT_SDL3_CONTROLLERS` と `VEROSCRIPT_SDL3_AUDIO` を使用します。ネイティブ WebGPU は `VEROSCRIPT_WEBGPU_ROOT` の固定 wgpu-native v29.0.1.1、ブラウザーは固定 Emdawn ポートを使用します。ビルド対応と実際の検証結果は分けて記録します。

<a id="sdl-audio-build"></a>

ルート音声ビルドは `VEROSCRIPT_BUILD_AUDIO=ON`、SDL 実装は加えて `VEROSCRIPT_BUILD_SDL3=ON` と `VEROSCRIPT_SDL3_AUDIO=ON` を使用します。基盤の単独ビルドも Audio に対応します。Linux の実ドライバーはダミーデバイスのテストと別に検証してください。

`VEROSCRIPT_SDL3_CONTROLLERS` は既定 ON、OFF はネイティブ入力だけを無効化します。各機器を一度だけ開き、別スレッドでの最終解放も所有スレッドで回収します。ホストの背景入力設定は変更しません。ジャイロ、パッドのタッチ面、トラックボール、高度な力覚は対象外です。音声は `VEROSCRIPT_SDL3_AUDIO` で個別に有効化でき、カメラ、SDL_Renderer、独立 Haptic は無効のままです。バージョンは変更せず、ホスト・提供者・バインディング・SDK を一緒に再ビルドしてください。

`VeroScriptSdl3ControllerTests` は SDL 仮想機器で列挙、接続変更、マッピング、独立状態、振動、範囲、破棄を検証します。メモリ提供者とスクリプトのテストも含みます。物理機器および Apple／モバイル／ブラウザのドライバーは別途実機検証が必要です。

`VeroScriptSdl3OpenGl.cpp` は `SDL_PLATFORM_ANDROID`、`SDL_PLATFORM_IOS`、`SDL_PLATFORM_TVOS` でホストウィンドウ経路を使い、OpenGL ES と互換 Platform ウィンドウ一つを要求し、非表示の補助ウィンドウは作りません。Android は保持 EGL コンテキスト数と表示サーフェスの占有を分け、代替の確定／取消まで旧デバイスを維持します。UIKit は排他的な一コンテキスト規則を維持します。Android は SDLActivity／`SDL_main` のネイティブイベントスレッド連携で初期化し、Java UI スレッドの準備完了を SDL 所有スレッド初期化と混同しないでください。他の対象はアプリケーションのメインスレッドで初期化します。ウィンドウ・デバイス・イベント処理・回収の所有スレッドを守り、SDK／端末を別途検証してください。

[README](../../../../README.md) · [User](../../User/ja-JP/index.md)

合成テストは SDL のこの経路を通りません。

`VEROSCRIPT_SDL3_GRAPHICS` は有効な描画ドライバーから算出されます。この派生値を上書きせず、各ドライバーの設定を変更してください。

SDL_GPU は退役した頂点・インデックスバッファ対を最大 128 対／8 MiB、かつ MaxGeometryBytes 以下で再利用します。転送領域も再利用し、マッピングと転送先アップロードの cycling により、待機中の GPU コマンドが参照する領域を保護します。レンダラー終了時に未使用領域を解放し、残るジオメトリは既存の所有権と所有スレッドでの退役規則を維持します。

<a id="uikit-recovery-capability"></a>

`Graphics/VeroScriptSdl3GraphicsBackend.cpp::GetCapabilities` をコンテキストの所有権と一致させます。SDL_PLATFORM_IOS／SDL_PLATFORM_TVOS では opengles のみ bDeviceRecovery を無効にします。提供者 ID や他の七つの有効な能力から復旧対応を推測してはいけません。UIKit は旧 EAGL 所有者と並行して置換を生成できず、Android の独立保持コンテキストは別の契約です。実行時の能力報告と公開の復旧文書を同時に維持します。

<a id="native-provider-maintenance"></a>

提供者ソースは SDL 固有のサービス記述子と静的問い合わせ `veroscript_sdl3_query` を所有し、ネイティブ拡張ビルドでは `veroscript_extension_query` を公開します。ビルドに含まれるサービスと描画ドライバーだけを登録し、各公開契約と実能力を別々に確認します。シェーダー入力、ドライバー依存と lease は汎用契約でなく SDL3 が所有します。一致する利用者で静的／動的探索、入力、描画、音声と所有スレッド上の破棄を検証します。合成／ダミードライバー試験は実機器の認定ではありません。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
