# 既存 Session のデバッグ

このブリッジは VM バックエンドを選択しません。実行前に[ホスト統合](embedding.md)の説明に従い、`VmDebugCapability` を持つバックエンドを明示的に設定してください。デバッガーを接続してもインタープリタは提供されません。

実行前に `VeroScriptSessionDebug.h` の `ISessionDebugBridge` を `SetDebugBridge` で設定します。`BeginExecution` は呼び出し用の共有デバッガーを返せます。これにより VM デバッグを選び、実行中の JIT 呼び出しを再実行しません。`Run`、オブジェクト構築、メソッド／関数呼び出し、保持 callable、クラス既定値が対象です。失敗や例外巻き戻しを含め、begin ごとに `EndExecution` が一度対応します。ワーカーはこのブリッジの追加コンテキストではありません。

コールバックと停止 VM の検査、評価、変更、再開は Session の制御スレッドで行います。停止コールバックはそのスレッドで要求を処理します。スレッドを越えて直接使える制御は `RequestPause()` とキャンセルだけです。共有 `FExecutionControl::RequestCancel()`、または Session の存続が保証される場合に `Session::RequestCancel()` を使います。借用プログラム、バインディング、ソース overlay、エントリー名はコールバック終了で失効します。必要なメタデータをコピーしてください。overlay は現在の構成であり固定ソースではありません。ソース／バインディング版は別途公開します。

<a id="debug-disconnect-lifetime"></a>

切断はホストポリシーに従って停止接続を解除し、それだけで実行を取り消したり Session を破棄したりしません。`Detached` は解除／置換時と破棄前に呼ばれます。同じ制御スレッドへ投入した処理を待たずにコールバックを終了させます。終了時は停止を解除し、必要なら取り消しを要求して実行と通信の終了を待ち、その後 Session を破棄します。取り消し状態のトークンは再利用前にリセットします。ブロックするネイティブコールバックは自ら協調する必要があります。

[DAP](../../../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/debugging.md)

<!-- docs-navigation -->

---

[ユーザー文書](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/session-debugging.md) · [fr-FR](../fr-FR/session-debugging.md) · [ja-JP](session-debugging.md) · [ko-KR](../ko-KR/session-debugging.md) · [zh-CN](../zh-CN/session-debugging.md) · [zh-TW](../zh-TW/session-debugging.md)
