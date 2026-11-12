# 交換可能な VM バックエンド

<a id="vm-provider-assembly"></a>

Core はプログラム、値、論理フレーム、言語操作、実行制御とディスパッチを所有します。完全なバイトコード解釈ループは Modules/Execution/VeroScriptBytecodeVm の VeroScriptBytecodeVm に属します。ID は vs-bytecode、パッケージ名は vs-bytecode-vm です。Core は VM/JIT を暗黙に選択しません。Runtime は Vm/ 内の公式動的ライブラリを明示的に構成し、Interop、Web、モバイルの静的製品も VM を明示的に注入します。

## 静的構成

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptBytecodeVm.h"
#include "VeroScriptVm.h"

auto factory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
VeroScript::VeroScriptVm vm(output);
std::string error;
if (!vm.SetVmBackendFactory(factory, error))
{
	throw std::runtime_error(error);
}
```

## 動的構成

```cpp
if (!vm.SetVmBackendPath("./Vm", error))
{
	throw std::runtime_error(error);
}
```

```text
veroscript --vm-backend vs-bytecode app.vs
veroscript --vm-backend ./custom-vm app.vs
veroscript --vm-backend ./Vm/VeroScriptVmBytecode.dll app.vs
```

選択は実行ドメインに属し、スクリプトの実行開始後に固定されます。Session.Clear() では解除されません。各アクティブコンテキストは独立したインスタンスを使用し、同期再入では同じインスタンスを使用します。公式バックエンドは同じドメインとスレッドで完全にアイドルなインスタンスを一つ再利用できます。設定失敗は以前の選択を保持し、実行前の失敗は修正できます。

スクリプトのコンパイル処理器にも FCompileOptions::ScriptCompileExtensions.VmBackendFactory または .VmBackendPath が必要です。処理器を実行しない解析と通常のコンパイルには VM は不要です。worker と分離された破棄コンテキストは選択と既存の実行制御を継承します。

## 独立ビルドと SDK

実行契約はヘッダーのみで Core バイナリをリンクしません。バックエンドは所有元 Core が提供するサービスを呼び出します。ホストは Core と静的 VM をリンクするか、Core に動的 VM を読み込みます。組合せ SDK に両方を含めても、リンクだけで自動登録は行われません。

```text
cmake --install <build> --component VeroScriptExecutionContract --prefix <sdk>
cmake --install <build> --component VeroScriptBytecodeVmSdk --prefix <sdk>
cmake -S Modules/Execution/VeroScriptBytecodeVm -B <vm-build> -DCMAKE_PREFIX_PATH=<sdk>
```

## 実行と配布の制約

EExecutionBackend::Vm/Mixed/Jit は別の実行方針です。既定 JIT はありません。VM の欠如や能力不足はエラーとなり、引数準備の繰返しや隠れたフォールバックは行いません。制限、取消し、権限、有界クリーンアップは Core の意味を保持します。不完全な清理や無効なバックエンド状態は Session のリセットを要求します。バージョンは 0.1.1 ですが、公開ヘッダー指紋、コンパイラ、STL、CRT、プラットフォームとレイアウトも一致する必要があります。Windows は一致した /MD、iOS/Web は静的構成を使用します。デスクトップ試験はモバイル実機検証の代替にはなりません。

独立配布には展開用の ZIP/tar と、ルートにマニフェストを持つインストール用 `.vspkg` を含みます。`veroscript install ./vs-bytecode-vm-0.1.1-<target>.vspkg --path=<project>` でインストールします。`vm` サービスと `vs-bytecode` ID を記録しますが、既存の実行ドメインの選択は変更しません。展開したライブラリはファイルまたはディレクトリで直接指定できます。

## VM を装配しないコンパイル

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript)
```

```cpp
#include "VeroScriptCompile.h"

auto compiled = VeroScript::VeroScriptCompile::CompileSource(
	"int answer(){return 42;}", "answer.vs");
// compiled.bSuccess / compiled.Error / compiled.Module
```

GUI などの C++ SDK ホストは、選択した VM を `FEngineConfig` で渡します。マークアップスクリプトでは `Gui::FMarkupScriptOptions::Engine` を使用します。アダプターはホストの選択を保持し、C++ SDK 自体はバックエンドを暗黙に選択しません。リポジトリの独立モジュールテストでは `-DVEROSCRIPT_TEST_VM_LIBRARY=<一致する動的バックエンドファイル>` を明示できます。
