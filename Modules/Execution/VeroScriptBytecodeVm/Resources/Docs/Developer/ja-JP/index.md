# バイトコード VM の保守

Core は言語操作、値、論理フレーム、実行管理、クリーンアップ、ディスパッチを所有します。本モジュールは `Private/VeroScriptBytecodeVmExecution.cpp` の完全な命令ループ、メンバー実行、診断を所有します。静的／動的ターゲットはヘッダーのみの `VeroScriptExecutionContract` に依存し、別 Core バイナリーをリンクしません。`BytecodeVm::CreateFactory` に実際の所有 Core のサービスを渡します。

`FVmInvocation` は準備済み Core フレームを受け取ります。ドメイン／コンテキスト／世代、呼び出しトークン、フレーム境界を維持し、引数準備やフレーム入口を二度実行しません。最初の意味操作より前に `bEntered` を立てます。`NotEntered` は未進入時だけ有効です。継続、計測、制限、未完了クリーンアップとリセットは Core の契約に従います。

アクティブコンテキストごとにインスタンスを持ち、同期再入は実行中インスタンスを使います。公式 VM は同一ドメイン／スレッドのアイドル再バインドに対応します。交換前に旧プログラムを解除し、インスタンス存続中はファクトリー／モジュールを保持します。割り当て元ファクトリーがインスタンスを破棄し、記述子が割り当て元モジュールでのファクトリー破棄を提供します。

動的入口は `veroscript_vm_query` です。使用前に `CoreVersion`、記述子サイズ、`BuildVmModuleAbi()`、ID／バージョン、機能を検証します。製品値は major:8/minor:8/patch:16 で C ABI の符号化と異なります。生成された実装識別はソースを追跡します。同じ `0.1.1` でもヘッダー、コンパイラー／STL／CRT、プラットフォーム、レイアウトの一致が必要です。

`VS_VEROSCRIPT_VM_VERSION` は独立 VM 製品の数値形式 `major.minor.patch` の CMake キャッシュ設定で、既定値は `0.1.1` です。生成する `VEROSCRIPT_VM_VERSION`、ファクトリー・モジュールのバージョン記述子、インストール済み CMake パッケージのバージョンに反映します。完全なパッケージ処理は登録済み `Vm` 製品バージョンを渡し、古いキャッシュや不一致の成果物を拒否します。`CoreVersion`、サービス ABI、実装・公開ヘッダーのフィンガープリントとは別の値です。番号の変更で非互換バイナリーを互換にはできません。

単体 CMake はインストール済み `VeroScriptExecutionContract` を利用し、ルート統合はテストへ実 Core も供給します。`VeroScriptVmBackendStaticTests` と任意の `VeroScriptVmBackendDynamicTests`、`Resources/Tests/Contract.cpp`／`Package.py` を関連箇所として保守します。機能、進入前フォールバック、再入、制限、クリーンアップ／リセット、寿命、インストール利用側を確認してください。iOS／Web は静的組み込みで、デスクトップ結果だけでは実機や別環境を検証できません。

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/ja-JP/vm-backends.md)

<!-- docs-navigation -->

---

[Developer](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
