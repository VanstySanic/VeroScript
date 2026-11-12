# バイトコード VM

`VeroScriptBytecodeVm` はバックエンド ID `vs-bytecode`、配布名 `vs-bytecode-vm` のインタープリターです。VM 選択と `Vm`／`Mixed`／`Jit` 方針は独立しています。生の Core/C++ ホストはインタープリターを暗黙選択しません。

## インストール済み Runtime

対応する Runtime 全体と `Vm/` を保持してください。自動選択は、選択したインストール済み環境の有効な候補を先に使用し、利用可能な自動 VM 候補がない場合に同梱インタープリタを製品の代替として使います。明示選択と無効化が優先し、この代替では置き換えません。次のコマンドは同梱 ID または対応する展開済みディレクトリを選びます。`.vspkg` のバックエンド ID を選ぶ前に、その環境へインストールしてください。インストールだけで固定済みの実行ドメインは変更されません。独自 ID にはパッケージ対応と選択環境が必要です。

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

## 静的 SDK の組み込み

既存 CMake ターゲット `app` で互換な両 SDK を検索し、両ターゲットをリンクします。実行前に各 VM を明示的に設定します。リンクだけでは選択されません。C++ Session/Engine は `FEngineConfig` または対応する選択 API を使います。

```cmake
find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeVmAssembly.h"
#include <sstream>

std::ostringstream output;
VeroScript::VeroScriptVm vm(output);
VeroScript::BytecodeVm::Configure(vm);
```

## 選択と互換性

実行コミット時に VM が凍結され、Session の `Clear()`／再読み込みでも保持します。設定失敗時は旧選択を保持し、凍結後の変更には新 Session を作成します。コンパイルスクリプトには明示的 VM が必要ですが、それを実行しない通常解析／コンパイルには不要です。SDK ヘッダー、Core、コンパイラー／ランタイム、アーキテクチャー、バックエンド識別を一致させてください。同じバージョン表記だけではバイナリー互換性を示しません。モバイル／Web 静的製品はデスクトップの共有ライブラリーを使わず明示的に組み込みます。

[C++](../../../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/embedding.md) · [CLI](../../../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md)

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` は製品の代替フィールドを設定します。Session 用オーバーロードは `SetFallbackVmBackendFactory` を使い、明示選択と環境候補を維持します。一方、生の `VeroScriptVm` の `Configure(vm)` は `SetVmBackendFactory` を直接呼びます。低水準 VM 自体はインストール済み候補を解決しません。この違いは上記の静的 SDK 例にも適用されます。

<!-- docs-navigation -->

---

[User](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
