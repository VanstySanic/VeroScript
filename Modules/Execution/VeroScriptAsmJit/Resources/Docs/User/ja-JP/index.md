# オプションの JIT バックエンド

## 選択と互換性

AsmJit は Runtime とは別に配布する任意の JIT バックエンドです。実行ポリシーとバックエンドは別々に選択します。プロジェクト設定の継承、`auto`、`none`、ID・パスの明示指定、フォールバックは [CLI バックエンドの参照](../../../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md#cli-backend-policy)を確認してください。このガイドでは AsmJit パッケージ、ネイティブ互換性、手動ロードを説明します。

互換 JIT が `MyProject` にインストールされ有効な場合、次のコマンドはそれぞれ設定の継承と明示的な自動選択を使用します。

```text
veroscript --path ./MyProject --backend=jit ./MyProject/main.vs
veroscript --path ./MyProject --jit-backend=auto --backend=mixed ./MyProject/main.vs
```

ポータブルアーカイブから手動で配置する場合：機能パッケージを `Release/Modules/Execution/Jit/<target>/<version>/archives/` から取得し、Runtime と別に展開します。独立インストーラーは提供しません。展開したライブラリまたはディレクトリを明示的に指定します。同じバージョン文字列だけでは互換性は保証されません。Core、アーキテクチャー、コンパイラー、標準ライブラリ、ランタイム構成をホストに合わせてください。ポータブルバックエンドはネイティブ機械語を生成せずに最適化する場合があります。

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
veroscript --jit-backend="/path/to/backend-library" --backend=jit main.vs
```

## パスと選択の変更

以下は明示パスのファイル／ディレクトリローダーの規則で、モジュール環境の ID 指定や自動選択とは別です。ファイル指定は一つのモジュールに固定します。ディレクトリはルートと最大二階層の子を UTF-8 辞書順で検索し、最初の利用可能な互換候補を選びます。Windows の名前は `VeroScriptJit`、Linux/macOS は `libVeroScriptJit` で始まります。候補の上限は 4096 です。読めない子エントリーは飛ばします。相対パスは現在の作業ディレクトリ基準です。環境変数、レジストリー、暗黙のパスによる選択はありません。選択はアイドル時にのみ変更し、失敗時は以前の選択を保持します。埋め込み API の空パスは無効化を意味します。

## 埋め込み API

C++ は `SetJitBackendPath`、C は `vs_interop_session_set_jit_backend` と `vs_interop_session_get_jit_backend_info`、C# は `SetJitBackend` と `GetJitBackendInfo`、Python は `set_jit_backend` と `get_jit_backend_info` を使用します。実行ポリシーは別途指定します。ファクトリー、インスタンス、ワーカー、保持実行コードが必要とする間、モジュールはロードされたままです。

公式パッケージ名と配布アーカイブの接頭辞は `vs-asmjit` です（例：`vs-asmjit-0.1.1-win-x64.zip`）。名前による選択で使うサービスのバックエンド ID は引き続き `asmjit` です。

## 実行制御付きネイティブ実行と静的 AOT

<a id="controlled-native-execution"></a>

制限、キャンセル、権限、フレーム、値は Core が管理します。未対応関数は入口前に VM へ移り、エラー後に関数を再実行しません。実行制御付き Mixed JIT は `FJitPolicy::bEnableControlledMixed` で明示有効化します（C ABI `enable_controlled_mixed`、C# `EnableControlledMixed`、Python `enable_controlled_mixed`）。初版は VM より遅い場合があります。明示 Jit は独立です。`ControlledNativeExecutions`／`AotExecutions` は実際のネイティブ入口数です。Android／iOS SDK と実機検証は未完了で、デスクトップ試験では代替できません。

[実験的なモバイル埋め込み — 実行制御付きネイティブ実行と静的 AOT](../../../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/ja-JP/index.md#mobile-jit-selection) を参照してください。

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[ユーザー文書](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
