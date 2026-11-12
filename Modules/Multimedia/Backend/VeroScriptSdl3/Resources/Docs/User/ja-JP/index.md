# VeroScriptSdl3 — 基盤実装

スクリプトホストは VM バックエンドを明示的に選択します。Platform、Graphics、Audio のバインディング登録だけでは実行器を選択しません。ソースのサンプル実行器は `VeroScriptVisualExample --vm-backend <VMライブラリまたはディレクトリ> program.vs` で起動します。`--check program.vs` は VM なしで通常のスクリプトを検証します。VM を指定した場合は同じファクトリーをコンパイルと実行に渡すため、スクリプトのコンパイル処理器も選択済みバックエンドを使います。ネイティブサービス SDK は特定の VM 実装に依存しません。

SDL3 バックエンドは SDL 3.4.18 とプロバイダー ID `sdl3` を使用します。`platform/sdl3` に加え、インストール済みパッケージが対応する場合は `graphics/sdl3` と `audio/sdl3` を提供します。Platform、Graphics、Audio は SDL ランタイムを共有します。

登録だけでは SDL は初期化されません。サービスの作成と操作はアプリケーションのメイン／所有スレッドで行います。オフスクリーン描画にも有効なビデオ／グラフィックスドライバーが必要です。保持中のリソースはプロバイダーの寿命を延ばします。終了前に所有スレッドで遅延解放を完了してください。利用可否は導入した SDK、OS、機器に依存します。

## インストール済みネイティブ SDK の使用

```cmake
find_package(VeroScriptSdl3 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Sdl3)
```

`VeroScript::Platform`, `VeroScript::Graphics`, `VeroScript::Sdl3`; `VeroScript::PlatformBindings` / `VeroScript::GraphicsBindings` → `VeroScript::Core`.

[VeroScriptGraphics — 描画リソースと二次元描画](../../../../../../VeroScriptGraphics/Resources/Docs/User/ja-JP/index.md#graphics-service-contract) を参照してください。

[VeroScriptGraphics — 描画リソースと二次元描画 — インストール済みネイティブ SDK の使用](../../../../../../VeroScriptGraphics/Resources/Docs/User/ja-JP/index.md#vertex-color-mode) を参照してください。

## 入力デバイス

[Platform: 複数の入力デバイス](../../../../../../VeroScriptPlatform/Resources/Docs/User/ja-JP/index.md)

[VeroScriptPlatform — 基盤実装 — 複数の入力デバイス](../../../../../../VeroScriptPlatform/Resources/Docs/User/ja-JP/index.md#input-device-snapshots) を参照してください。

切断、フォーカス喪失、ライフサイクル変更、ウィンドウ破棄時に状態を消去し、原因イベントの前に合成解放／キャンセルを通知します。`ClearInputState(windowId)` は指定ウィンドウ、ゼロなら全体を消去し、該当する待機中の正規化入力を除いて `InputStateReset` を通知します。ホストから送ったリセットは後続イベントの順序を保持します。合成の全体リセットは通知欠落後の再同期にも使われ、利用側のキャプチャ／押下状態を消去して再列挙します。外部サービスの状態はホスト入力ごとに独立します。SDL はイベント処理／列挙時に 250 ms 間隔でタッチ構成を照合し、キーボード／マウスは順序付き接続イベントに従います。専用ペンは対象外です。バージョンは変更せず、ネイティブホストと提供者を一緒に再ビルドします。

[VeroScriptPlatform — 基盤実装 — 複数の入力デバイス](../../../../../../VeroScriptPlatform/Resources/Docs/User/ja-JP/index.md#input-device-snapshots) を参照してください。

SDL は全体のスキャンコード状態で遷移を重複排除します。複数の物理キーボードで同じキーを重ねて押すと、二度目の押下がリピートになり、その後の解放が欠落する場合があります。ID や raw モードだけでは物理キーボード別の完全な状態を保証できません。スナップショットは受信イベントを追跡します。完全な物理状態が必要な場合は、全遷移を送る外部ホストを使用してください。

## ホストウィンドウ、カーソルとキャプチャ

共通スクリプト API は Window.SetCursor(Cursor,bool visible)、SetMouseCapture(bool)、HasMouseCapture()、IsBorrowed() を提供します。Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows は API の対応を示し、ドライバーは操作を拒否する場合があります。SDL キャプチャには同じウィンドウのマウスとキーボード両方のフォーカスが必要です。フォーカス喪失、非表示、最小化、バックグラウンド、終了、入力リセットで解除します。カーソル設定はマウスフォーカスまたはキャプチャ中に適用します。ドラッグ中は退出してもカーソルを保持し、それ以外は既定の可視カーソルへ戻します。ホストは SDL のグローバルなカーソルとキャプチャの所有権を調整してください。C++ IService.AttachWindow は FBorrowedWindowOptions の HandleKind="sdl3.window"、有効な SDL_Window アドレス、非 null の共有 Owner を受け取ります。スクリプトにアドレスは公開しません。Close は登録だけを解除し、ホストのウィンドウを破棄しません。ネイティブ破棄はラッパーを無効化し、観測済み入力を消去します。ホストのウィンドウ破棄前にグラフィックス表面を閉じてください。Owner は退役記録の破棄まで保持しますが、ホストによる明示的な早期破棄を防止できません。

システムカーソルにはネイティブの既定カーソルが必要です。同梱 SDL の明示的キャプチャは windows、cocoa、x11 のみで公開します。他のドライバーでは未対応の SDL キャプチャを要求せず GUI の論理キャプチャを維持します。

<a id="host-window-retirement"></a>

SDL バックエンドはネイティブ破棄イベント中に GPU ウィンドウの占有を解除し、別スレッドで退役待ちの表面も処理します。後の Close は冪等です。この検知には SDL_EVENT_WINDOW_DESTROYED が有効でフィルター除外されないことが必要です。イベントを抑制するホストはネイティブ破棄前に表面とラッパーを閉じてください。

## グラフィックス API と非同期要求

<a id="sdl-graphics-drivers"></a>

導入したプロバイダーで使用可能なドライバーを選択します。Windows は `direct3d11` / `direct3d11-warp`、ほかに `opengl`（3.3 core）、`opengles`（ES 3.0）、任意の `webgpu` があります。SDL_GPU は対応環境で Direct3D 12、Vulkan、Metal を提供します。選択したサービスに問い合わせ、利用できないドライバーはエラーとして扱います。

FWindowOptions.GraphicsApi または Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi) で互換ウィンドウを作成します。借用ウィンドウは既に互換性を持つ必要があります。

Android とオプションの iOS/iPadOS/tvOS `opengles` ドライバーでは、デバイス作成前に `GraphicsApi="opengles"` の互換 Platform ウィンドウを正確に一つ作成します。Android EGL コンテキストは個別にそのウィンドウを保持でき、旧デバイスを生存させたまま復旧用の代替を準備できます。同時に表示を所有できるサーフェスは一つです。UIKit はウィンドウに一つのコンテキストを保持します。サーフェスは保持したウィンドウだけを使います。デバイス／ウィンドウより先にサーフェスとリソースを閉じ、ウィンドウの終了で関連デバイスは無効になります。既定の SDL_GPU は別のホスト経路です。

[VeroScriptGraphics — 描画リソースと二次元描画 — グラフィックス API と非同期要求](../../../../../../VeroScriptGraphics/Resources/Docs/User/ja-JP/index.md#asynchronous-requests) を参照してください。

[VeroScriptGraphics — 描画リソースと二次元描画 — グラフィックス API と非同期要求](../../../../../../VeroScriptGraphics/Resources/Docs/User/ja-JP/index.md#resource-retirement) を参照してください。

WebGPU には対応するネイティブランタイムまたはブラウザーポートと元のライセンス通知が必要です。ブラウザーでは非同期作成とフレーム間の制御返却が必要です。ネイティブとブラウザーの利用可否は SDK とターゲットごとに確認してください。

CreateDevice と CreateDeviceAsync は既定 API の選択を共有します。ネイティブで WebGPU のみを有効にした場合、ドライバー名が空でも非同期要求を使用します。DX11 の Device.Poll はデバイス喪失を検出し資源を無効化しますが、復旧後の資源再作成は必要です。WebGPU は完了前・完了後・繰り返しのキャンセルに対応し、GetPixels の失敗時は出力を変更しません。

## ゲームパッドと振動

[VeroScriptPlatform — 基盤実装 — ゲームパッドと振動](../../../../../../VeroScriptPlatform/Resources/Docs/User/ja-JP/index.md#controllers-and-feedback) を参照してください。

## 音声

音声対応が含まれる場合、SDL は `audio/sdl3`（`veroscript.audio`、0.1.1）を提供します。`CreateAudio` は共有 SDL ランタイムで音声だけを初期化します。`VS::Audio` は再生／録音ストリーム、機器スナップショット、集約可能な接続変更、有界 PCM キュー、音量／一時停止を提供します。SDL がサンプルレートとチャンネルを変換し、公共 Audio モジュールは機器なしで非圧縮 PCM／浮動小数点 RIFF/WAVE を復号します。

ストリームは一時停止状態で開始し、それぞれ独立しています。録音のオーバーフローは未読データを破棄してフラグを保持し、音声スレッドでスクリプトコールバックは実行しません。ストリームを閉じ、所有スレッドで遅延解放を完了します。Linux の配布 SDK には ALSA/PulseAudio などの実ドライバーが必要です。マイク権限とブラウザーのユーザー操作はホストの責任であり、機器の利用可否は実ターゲットで確認します。

<a id="audio-event-pumping"></a>

所有スレッドのアプリケーションループで `PollEvent()` または `GetDevices()` を定期的に呼び出してください。SDL3 アダプターはホストの SDL イベントを取り出さずに、メインスレッドのコールバックとネイティブ通知を処理します。ウィンドウのない音声専用アプリケーションでもデバイス障害や既定ルートの移行を処理できます。接続状態は現在のデバイス列挙に基づき、古いストリームが保持する形式だけでは接続中とは判断しません。

<a id="uikit-device-recovery"></a>

iOS／iPadOS／tvOS の `opengles` デバイスは `bDeviceRecovery == false`（スクリプトでは `SupportsDeviceRecovery()`）を返します。ウィンドウが保持できる EAGL コンテキストは一つなので、旧デバイスを残したまま RecoverDevice／RecoverDeviceAsync で置換を準備してはいけません。旧サーフェス・資源・デバイスを閉じて参照を解放し、所有スレッドの遅延解放を完了してから新規デバイスを作り、資源を再アップロードします。他のドライバーは個別の能力を持ち、Android EGL の置換規則は UIKit の復旧対応を意味しません。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
