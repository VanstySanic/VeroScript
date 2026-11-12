# C++ グループアーキテクチャ

## 依存方向

C++ group は `VeroScript/` 上の adapter/toolchain layer です。`VeroScriptCpp` は embedding と session、`VeroScriptCppAot` は core 直接依存、`VeroScriptCppFrontend` は共有 C++17 declaration model を所有します。`VeroScriptCppBindGen` と `VeroScriptCppTranspiler` はその model を独立して利用し、自分の lowering policy のために frontend AST を変更してはいけません。

core は C++ tool header を含みません。`VeroScriptInterop` は独立した stable C ABI で、frontend/BindGen/Transpiler の implementation dependency ではありません。embedding layer は core の public runtime/binding interface を使えます。

## ソースの構成

各 native module は `Public/` から supported header を公開し、implementation/test は `Private/` に置きます。CMake は private source を再帰検出し、focused test source を library target から除外して別 executable を作ります。public header test は private include path を必要としてはいけません。

## 共通の出力モデル

frontend diagnostic は code、message、source location、token/declaration data を持ち、BindGen/Transpiler result がこれを保持します。project operation は全 input を集めてから catalog、reference、source map、manifest を生成します。同一 file path を拒否し、complete operation 成功後だけ output を置換します。

## 変更規則

public struct/enum、source location、generated format、CMake target、supported-subset rule の変更には focused test と該当 topic page の更新が必要です。README は command を説明できますが、public header と test が動作の根拠です。

## ネイティブデバッグの境界

`VeroScriptSessionDebug.h` はホストまたは Tooling の実行ドメインが使用する C++ Session ブリッジを定義します。Session は登録を管理し、実際に実行する VM の範囲で共有デバッガーを保持します。プログラムの置換、ロールバックと破棄をブリッジへ通知し、後処理用 VM にはデバッガーを引き継ぎません。プログラム、バインディング、ソースと実行コンテキストのビューは借用です。ホストは同期済みメタデータをコピーし、安全な停止点にある実行スレッドで待機中のデバッグ要求を処理します。

Cpp は Tooling に依存しません。Tooling はドメイン登録、DAP と通信を、UE 統合はエディター／PIE、オブジェクト寿命とゲームスレッドの停止方針を担当します。この公開境界は Session の C++ レイアウトを変更するため、対応する SDK でネイティブホストを再コンパイルしてください。

`VeroScriptCppSessionDebugTests` は引数変更、オブジェクト呼び出し、保持コールバック、インタープリター選択、リロードのロールバック、例外処理とブリッジの交換／解除を扱います。`VeroScriptCppJitContractTests` は独立した公開ファクトリーでバックエンド遷移を検証します。SDK 利用例はエクスポート済みヘッダーとライブラリを使います。TCP の検証は待機と終了を制限した子プロセスで行い、ローカル Session を借用するサーバースレッドを切り離さないでください。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/architecture.md) · [fr-FR](../fr-FR/architecture.md) · [ja-JP](architecture.md) · [ko-KR](../ko-KR/architecture.md) · [zh-CN](../zh-CN/architecture.md) · [zh-TW](../zh-TW/architecture.md)
