# VeroScriptPlatform — 基盤実装

Platform はウィンドウ、論理／ピクセル寸法、キーボード、マウス、タッチ、UTF-8 テキスト／IME、クリップボードの契約を提供します。ネイティブ契約は Core や SDL に依存せず、任意のスクリプトアダプターが `VS::Platform` を公開します。

スクリプトの機能確認、ウィンドウ寸法、入力スナップショットの取得メソッドは const で、読み取り専用の値キャプチャから呼び出せます。イベントの消費、入力状態のリセット、ウィンドウの変更には書き込み可能な受信側が必要です。そのようなコールバックでは `mutable` または書き込み可能な参照キャプチャを使用してください。

サービスはアプリのメインスレッドで作成します。共有イベントキューは一度だけ処理するか、外部モードで正規化イベントを送信します。描画サーフェスを閉じてからウィンドウを閉じます。終了前に所有スレッドで遅延解放を回収してください。

## インストール済みネイティブ SDK の使用

```cmake
find_package(VeroScriptPlatform 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Platform)
```

ウィンドウ座標は各プラットフォームに従います。`ScaleX/ScaleY` は描画ピクセルへの変換率、`DisplayScale` は推奨 UI スケールです。両者は異なる場合があります。密度非依存の入力は `guiX = windowX * ScaleX / DisplayScale`、描画は `pixelX = guiX * DisplayScale` で変換します。スクリプトでは `window.GetDisplayScale()` を使い、ピクセル寸法や表示倍率が変わったら寸法情報を更新します。

## 複数の入力デバイス

<a id="input-device-snapshots"></a>

`service.GetInputDevices()` は `int64 Count` と `Get(index)` を持つスナップショットを返します。デバイスのフィールドは `Kind`、`DeviceId`、`Name`、`Aggregate`、`Connected` です。`service.GetInputState(kind,id)` は不変スナップショットで、`Device`、`KeyCount`、`PointerCount`、`Modifiers`、`GetKey(index)`、`GetPointer(index)`、`IsKeyDown(Key)`、`IsScanCodeDown(int)` を提供します。キーは `Key/ScanCode/WindowId`、ポインターは `PointerId/WindowId/X/Y/Pressure/Buttons` を持ち、添字は範囲検証されます。能力確認は `SupportsInputDevices/SupportsInputState/SupportsDeviceEvents()` です。

識別子は `(InputDeviceKind,DeviceId)`、接触点 ID はデバイス内で有効です。種類は `Unknown/Keyboard/Mouse/Touch/Joystick/Gamepad`。ゼロ ID は集約入力を示し、物理マウスの列挙はマウス別イベントの識別を保証しません。状態はこのサービスが消費したイベントから生成されるため、問い合わせ前にイベントを処理します。修飾キーはデバイス自身で観測した押下状態のみで、システムのロック状態は含みません。接続イベントは `InputDeviceAdded/Removed`、追加フィールドは `DeviceKind`、`DeviceName`、`AggregateDevice`、`Synthetic` です。

切断、フォーカス喪失、ライフサイクル変更、ウィンドウ破棄時に状態を消去し、原因イベントの前に合成解放／キャンセルを通知します。`ClearInputState(windowId)` は指定ウィンドウ、ゼロなら全体を消去し、該当する待機中の正規化入力を除いて `InputStateReset` を通知します。ホストから送ったリセットは後続イベントの順序を保持します。合成の全体リセットは通知欠落後の再同期にも使われ、利用側のキャプチャ／押下状態を消去して再列挙します。外部サービスの状態はホスト入力ごとに独立します。ネイティブな検出方法と対応デバイスは選択したバックエンドが定義します。物理イベントの制限はそのガイドを参照してください。バージョンは変更せず、ネイティブホストと提供者を一緒に再ビルドします。

ゼロ以外のデバイスが切断された場合、物理的な発生元を識別できないため、同じ種類の集約入力の押下状態も保守的に解放します。残りの押下は後続イベントで再観測する必要があります。解放イベントの宛先やキーボード配列が変わった場合も、元の所有者に合成解放／キャンセルを通知します。

`COMPONENTS Platform` → `VeroScript::Platform`; `COMPONENTS PlatformBindings` → `VeroScript::PlatformBindings` + `VeroScript::Core`.

## ホストウィンドウ、カーソルとキャプチャ

共通スクリプト API は Window.SetCursor(Cursor,bool visible)、SetMouseCapture(bool)、HasMouseCapture()、IsBorrowed() を提供します。Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows は API の対応を示し、ドライバーは操作を拒否する場合があります。SDL キャプチャには同じウィンドウのマウスとキーボード両方のフォーカスが必要です。フォーカス喪失、非表示、最小化、バックグラウンド、終了、入力リセットで解除します。カーソル設定はマウスフォーカス中に適用し、退出時には既定の可視カーソルへ戻します。ホストは SDL のグローバルなカーソルとキャプチャの所有権を調整してください。C++ IService.AttachWindow は FBorrowedWindowOptions の HandleKind="sdl3.window"、有効な SDL_Window アドレス、非 null の共有 Owner を受け取ります。スクリプトにアドレスは公開しません。Close は登録だけを解除し、ホストのウィンドウを破棄しません。ネイティブ破棄はラッパーを無効化し、観測済み入力を消去します。ホストのウィンドウ破棄前にグラフィックス表面を閉じてください。Owner は退役記録の破棄まで保持しますが、ホストによる明示的な早期破棄を防止できません。

[VeroScriptSdl3 — 基盤実装 — ホストウィンドウ、カーソルとキャプチャ](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/ja-JP/index.md#host-window-retirement) を参照してください。

## グラフィックス API と非同期要求

FWindowOptions.GraphicsApi または Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi) で互換ウィンドウを作成します。借用ウィンドウは既に互換性を持つ必要があります。

[VeroScriptGraphics — 描画リソースと二次元描画 — グラフィックス API と非同期要求](../../../../../VeroScriptGraphics/Resources/Docs/User/ja-JP/index.md#resource-retirement) を参照してください。

## ゲームパッドと振動

<a id="controllers-and-feedback"></a>

`InputDeviceKind::Gamepad` は標準 `GamepadAxis/GamepadButton`、`Joystick` はデバイス固有のゼロ始まりの軸・ボタン・ハットを公開します。物理デバイスごとにイベント系列は一つです。マッピング変更では旧配置を解放して新配置を通知するため、再列挙してください。非ゼロ ID は接続単位です。入力はグローバル (`WindowId=0`) で、イベント処理後に `(Kind,DeviceId)` の状態を取得します。

接続情報は `AxisCount/ButtonCount/HatCount/SupportsRumble`（C++ `bRumble`）を持ちます。標準パッドのスロットは軸 6、ボタン 26 で、存在しない入力は中立です。ネイティブ状態は `Axes/ControllerButtons/Hats`、スクリプトは `GetAxis(int64)` / `GetGamepadAxis(GamepadAxis)`、`IsButtonDown(int64)` / `IsGamepadButtonDown(GamepadButton)`、`GetHat(int64)->Hat` と各件数を提供し、添字を検証します。`ControllerAxis/ControllerButtonDown/ControllerButtonUp/ControllerHat` の `Control/Value` を参照します。軸は [-1,1]、トリガーは [0,1]、デッドゾーンは自動適用しません。

`SupportsControllers()/SupportsRumble()` はサービス能力です。各デバイスの振動能力も確認してください。`Rumble(kind,id,低周波振幅,高周波振幅,int64 ミリ秒)` は有限の 0..1 と 0..65535 ms を受け付けます。新しい呼び出しで置き換え、時間ゼロまたは両振幅ゼロで停止します。未対応、切断、不正値、別スレッドの操作は失敗します。

全体リセット、バックグラウンド／終了、切断、通知あふれ、サービス破棄で状態とこのサービスの振動を停止します。管理ウィンドウのフォーカス喪失／非表示／最小化でも全体パッドを消去しますが、明示的なウィンドウ単位リセットでは消去しません。不足する解放・中立イベントは原因の前に合成し、既存スナップショットは保持します。外部モードはホストの配置とイベントを受信し、ネイティブ機器や振動を操作しません。ホスト提供者は同じ振動契約を実装できます。

`SupportsControllers()` と機器ごとのフィードバック機能を確認してください。物理的な利用可否は OS、機器、ドライバー次第です。バックグラウンド入力方針はホストが管理します。ジャイロ、コントローラーのタッチパッド、トラックボール、高度な力覚効果は対象外です。ネイティブリソースとフィードバックの遅延解放は所有スレッドで完了してからアンロードします。

<a id="window-and-events"></a>

ネイティブホストは `VeroScriptPlatformApi.h` を使い、`IService` がウィンドウとイベント、`IWindow` が状態、`FInputDevice` / `FInputState` が入力スナップショットを表します。ID は不透明な識別子で、ネイティブアドレスではありません。閉じる要求はイベントで、実際の終了時点はホストが決めます。`PollEvent` は成功とイベント有無を分け、空の結果を `None` にします。`WaitEvent` は 0..50 ミリ秒を受け付け、外部ポンプではホストが待機して `SubmitEvent` で渡します。テキスト／IME は所有された UTF-8 文字列ですが、IME 選択位置は Unicode 文字数です。`ScanCode` はキーボードページの USB HID usage、未対応ならゼロです。マウスボタンは 1=左、2=中央、3=右、4=戻る、5=進むで、マスクはビット N-1 です。入力領域はネイティブ座標、カーソルは X からの水平オフセットを使います。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
