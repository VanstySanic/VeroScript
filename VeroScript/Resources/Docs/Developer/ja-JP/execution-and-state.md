# コア実行、状態、package

[交換可能な VM バックエンド](vm-backends.md)

## VM 状態

[交換可能な VM バックエンド](vm-backends.md#vm-provider-assembly) を参照してください。

## bytecode metadata と control flow

bytecode は source/package behavior を保存する metadata を保持します。reference-parameter flag、closure capture name/type、declaration-only または abstract function state、interface/enum table、debug-file ownership、exception region、`finally` range、global initializer range、package component ownership が含まれます。reference opcode、checked/optional cast、`throw`、protected-region cleanup は semantic operation であり交換可能な VM optimization ではありません。serialization、hot reload、reflection、tooling は metadata と executable table の alignment を維持します。

## worker と synchronization

script global と class static field は native worker ごとに分離されます。worker task registry と task ID も worker-local です。`channel<T>` は明示的で同期された cross-worker capability であり、host callback と到達可能 native state には host-provided synchronization が必要です。worker は別 worker の task ID を自分の local ID として使えません。

## package と reload

package load は module metadata と application/library entry shape を検証します。session reload は opt-in で、commit 前に replacement を stage します。file-backed、compiled-module、package reload は異なる input を持ちます。failure は previous working module を保持し structured diagnostic を公開します。

## debug と diagnostic

debug location、stack frame、variable、breakpoint、evaluation、stop reason は Tooling と DAP が消費する public data contract です。diagnostic ID、source range、substitution argument は stable machine data であり、localized message は presentation です。

## 実行の制御と制限

`FExecutionLimits` は instruction limit と wall-clock timeout を持ち、field の zero は unlimited です。`FExecutionControl` は thread-safe shared control object です。`Run` の active 中に host は `RequestCancel` を呼べ、VM は cooperative point で request を観測します。cancellation は進行中 native/managed callback を強制停止しないため callback state は通常の synchronization を必要とします。

`FScriptCapabilities` は standard-library side-effect policy で、file、environment、console、task operation を制限できます。既定 configuration は permissive ですが host が restriction を渡すと JIT entry 前に execution path が強制します。これは optimization hint ではなく security policy です。`FEngineConfig` は limit、control、capability を source overlay と requested `EExecutionBackend` と共に保持します。

active limit、cancellation control、restricted capability は必要に応じ enforcement のため interpreter path を使用します。debugger attachment、保持された初期化区間、有界クリーンアップは effective backend を `Vm` にします。requested backend は configuration value のままであり diagnostic で effective backend として報告してはいけません。

## 再入可能なクリーンアップと例外の転送

値の解放はスクリプトのデストラクタを呼び、実行に再入してフレーム格納領域を再配置することがあります。呼び出しをまたぐ場合はフレームのインデックスを保持し、終了後に参照を取得し直します。バイトコードのオペランド読み取りは現在のフレームの `StackBase` より上に限定し、フレームをまたぐ巻き戻しでは保存したオペランドを明示的に解放します。

保留中の戻り値や例外は外側の `finally` 継続に属します。`finally` 内のループや内部で捕捉した例外は転送を保持し、領域を離れる転送は置き換えた値を通常の所有権経路で解放します。クリーンアップで未捕捉の例外が出ると伝播中のスクリプト例外を置き換えられますが、デストラクタ内で捕捉した例外は外側の例外を保持します。実行失敗時と VM 再利用時には保留中の例外オブジェクトを解放します。`VeroScriptExceptionTests` でデバッガと命令数制限を含む各ケースを確認してください。実行制御中も `throw` を個別に数え、検査します。

## 数値サービス

`VeroScriptNumerics.h` は不変の `FBigInteger`/`FRational` とトランザクション型の noexcept `Numerics::Try*` 演算を提供します。`FNumericError` は固定メタデータです。コピー、移動、交換、破棄は割り当てず、移動元はゼロになります。不変値はホストスレッド間で共有できますが、各コンテキストとコールバックはホストが同期します。

`FNumericOperationContext` は `FNumericLimits` と同期の借用コールバックを組み合わせます。ゼロは有限の既定値を選択します：65,536ビット（上限1,048,576）、入力/出力各1 MiB、scratch 16 MiB、演算ごとに16,777,216単位。scratch は一時領域と未確定出力の要求量で、VMヒープやRSSではありません。失敗時は出力を保持します。

実行前に `VeroScriptVm::SetNumericLimits`、`FEngineConfig::NumericLimits`、`VeroScriptSession::SetNumericLimits` で設定します。workerも制限を継承し、グローバル初期化を含め命令、タイムアウト、キャンセルを適用します。`VeroScriptNumericBindings.h` は検証済みFValue変換を提供します。C Interop は `VS_INTEROP_CAP_NUMERICS`、`vs_interop_numeric_parse/to_text/convert`、型識別とセッション制限を公開します。文字列長は明示的です。各言語のBigInteger/Fractionへは自動変換しません。独立AOTはこれらを拒否します。

インストールフックは初期化前に `Packages::FLimits::HookNumericLimits` を設定します。パッケージ形式、レジストリプロトコル、JSON数値は変更しません。

```cpp
#include "VeroScriptNumerics.h"

VeroScript::FNumericOperationContext context;
VeroScript::FNumericError error;
VeroScript::FBigInteger value;
bool parsed = VeroScript::Numerics::TryParse("18446744073709551616", value, context, error);
```

## variant / FValueRoot

[相互運用 SDK — variant](../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#exported-value-lifetime) を参照してください。

ボックス化前に VM が `FVariantTypeDescriptor` を検証します。`Variant::Box` と `Variant::Snapshot` は通常の複製を再利用し、書き換え可能な内部値を公開しません。`VisitValueGraph` はボックス、オブジェクト、クロージャー、コンテナを走査し、ファイナライザースレッドではスクリプトを実行しません。コールバックや複製の前に結果領域を予約し、割り当てなしで確定します。外側の処理は実行後に解放待ちの値を清掃します。コールバックから終了を要求しても、実行中の呼び出しの終了後に破棄します。

VM は全意味論を実装します。Mixed/asmjit は未対応の命令を安全に VM で実行します。可搬バイトコードとパッケージは宣言と命令を保持します。独立 C++・C#・Python AOT は出力生成前に variant 依存を明確に拒否します。デバッガーは読み取り専用の $present、$type、$value を表示し、type<variant>().kind() は "variant" を返します。
呼び出しの安全点は実行中の借用引数と受信オブジェクトを保護します。終了時には、呼び出し元がルートを登録できるまで戻り値、構築したオブジェクト、書き込み可能な引数の結果を保持し、入力専用の借用は呼び出しとともに終了します。退役値の解放に失敗した場合、成功した呼び出しも実行時エラーになります。既存の実行エラーがある場合は、それを優先します。終了処理の解放エラーはホストの診断ストリームに報告します。

## ネイティブ非同期コールバックのルート

実行中の native 呼び出しは `IScriptCallInvoker::RetainForGeneration` で関数とキャプチャを保持できます。保存するのは `FValueRoot` であり、借用した invoker、VM、Args は保存しません。別スレッドからの解放要求は可能ですが、読み取りと言語の後始末は所有スレッドで行います。通常のエクスポートと完了ルートは同じ生存グラフを保護します。

native の安全点で完了ルートを処理し、`DrainGeneration` は通常のホスト借用ルートを保持します。デストラクターは現在の実行予算、キャンセル、絶対期限に従います。永続 Session は同じ世代のルートを保持し、単独 VM の最上位実行終了、置換、再読み込み、終了で無効化します。通常のエクスポートは引き続き置換を阻止します。コンパイル失敗では旧世代を維持し、移行開始後の無効化はロールバックでも復元しません。

## 準備キャッシュと JIT 実行サービス

`VeroScriptProgramPreparation::TryGetCacheInputFingerprint` は、処理器の実行、パッケージの再適用、ホストバインディングの検証が不要な解析済み入力に限り、準備前のキャッシュ参照を許可します。ローダーの完全なソース・解決フィンガープリントおよびコンパイラのビルド識別子と組み合わせてください。対象外の場合は通常の準備と依存関係の検証を行います。ソースメタデータと実効定数評価制限も識別に含まれます。

`ICompiledFunctionServices::ExecuteBlock` は、長さを制限した命令列でネイティブと Core 間の呼び出しをまとめます。各命令の予算、期限、キャンセル、権限の確認は維持され、分岐、復帰、失敗で列を終了します。制御付き Mixed の既定方針は変わりません。

借用、非保持、独立ワーカーの JIT キャッシュは入口を先に準備し、到達した呼び出し先も引き続きコンパイルできます。保持キャッシュで完了済み準備を再利用できるのは実行要件が同じ場合だけです。未対応または不利な経路は VM を使用します。

この C++ サービス変更に伴い、Core、バックエンド、SDK 利用側を対応する同じソースから再ビルドしてください。

モジュール全体の入口では、設定済みの事前コンパイル予算を引き続き使用します。

`FJitCompiledFunction::bPreferVmForExecution` で、バックエンドは引数に依存しない VM 選択の判断を公開できます。Core はキャッシュ済みの判断をバックエンドサービスの起動前に適用し、フォールバック統計、通常の VM 検査、呼び出し先の独立した選択を維持します。

## 中断後の上限付き終了処理

[ホスト統合 — 中断後の上限付き終了処理](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/embedding.md#bounded-interruption-cleanup) を参照してください。

## 型互換性キャッシュの所有権

`VeroScriptVm::IsActiveObjectTypeAssignableTo` は、完全な正規クラス／インターフェイス名の互換性をモジュールと `ProgramGeneration` ごとにキャッシュします。キャッシュは実行コンテキストが所有し、モジュール／世代変更時または 4096 エントリーの上限到達時に消去します。所有する不変プログラムでは呼び出し外でも再利用できます。借用プログラムでは実行中だけ使い、`BeginExecutionGovernance` で呼び出し間に消去します。別名と非正規名は正規のメタデータ解決を維持します。JIT 呼び出し、値コピー、ホスト照会の最適化でも、この所有権と無効化条件を保持してください。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/execution-and-state.md) · [fr-FR](../fr-FR/execution-and-state.md) · [ja-JP](execution-and-state.md) · [ko-KR](../ko-KR/execution-and-state.md) · [zh-CN](../zh-CN/execution-and-state.md) · [zh-TW](../zh-TW/execution-and-state.md)
