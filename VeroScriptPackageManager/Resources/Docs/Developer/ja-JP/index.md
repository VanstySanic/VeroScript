# パッケージ管理の保守

クライアントが取得、ZIP、解決、受領記録、トランザクション、公開、環境合成を担当します。Core は解決済みモジュール/資源/ネイティブ契約を持ち、HTTP/アーカイブ/解決から独立します。サーバーはアカウント、認可、不変保存、方針、索引を担当します。

## ビルドと所属

デスクトップの既定は VEROSCRIPT_BUILD_PACKAGE_MANAGER=ON、embedded/mobile/Wasm/Python-only は OFF です。固定した静的 curl、miniz、nlohmann/json、PicoSHA2 とライセンスをモジュールで保持します。VEROSCRIPT_PACKAGE_MANAGER_USE_SYSTEM_CURL または既存 CURL ターゲットでは CURL 7.85+ と対応依存が必要です。同梱 curl は SHA-256 で固定します。

VeroScriptPackageManager のインストールコンポーネントはヘッダー、ライブラリ、スキーマ、依存識別情報、法的通知を出力します。サンプルと README はインストールせず、六言語文書は Resources で公開します。同梱 curl のビルドコンポーネントも導入し、Core SDK のパッケージ処理は両方を実行します。その後、公開する Core SDK バージョンと実際の一時配置 Core ライブラリの SHA-256 に依存情報を確定します。シンボル除去後も対象とし、ネイティブ製品版や以前のハッシュだけでは不十分です。外部 Core SDK 利用テストでは HTTP トランスポートも作成し、ZIP/HTTP のリンク依存を検証します。ソース API の英語診断と構造化 code はパッケージの表示文から独立して保持します。

## トランザクションと寿命

ソースと生成物を準備し、不変内容を公開してから復旧可能なジャーナルで要求/ロック/状態/所有権をコミットします。直前に計画入力と保存先識別を再検査し、外部編集を保持して古い計画は拒否します。復旧/巻戻しにキャンセル済みトークンを使わず、未完了トランザクションを読ませません。

各世代がプロセス間ファイルリースを持ちます。clean は受領記録、全対象設定、所有権、内容、リースを検査し、編集済み/外部ファイルを保持します。孤立した公開世代の回収にも完全な所有記録とリース情報が必要です。放棄した準備領域はホストが管理します。借用コールバックは同期的で同一環境へ再入せず、最終デストラクターまでスナップショット/モジュールを保持します。

再実行は書き込み前にログ全体の許可された一意の保存先、string/null データ、予算と現在のファイルを検証します。各ファイルは `old` または `new` と一致する必要があり、それ以外は `recovery-conflict` でログと外部編集を保持します。各書き込み前にも検証し、復元済みの値は省略します。ロールバックが止まる場合は `recovery-required` でトランザクションを保持します。編集を退避して記録された値に戻し、復旧後に編集を再適用します。`JournalRecoveryTests` は部分コミット、ロールバック、欠損、無効ログ、重複と処理中の編集を検証します。

## フックのコンパイル

事前検証／実行前コンパイルごとに `FLimits.HookConstantEvaluationLimits` を Core の `FCompileOptions.ConstantEvaluationLimits` に渡します。ホストの方針であり JSON やロックには保存しません。作業量、呼び出し深度、式深度、評価メモリは正数が必要で、キャッシュのみゼロを許可します。操作全体の期限、キャンセル、ホストの解析オブザーバーと元の診断を保持します。VM の実行制限は独立します。`ConstantHookTests` はソース／ライブラリのインポート、破棄するジェネリック分岐、uint64、予算、単発キャンセル、キャッシュなし再試行と削除トランザクションを検証します。

## 検証

VeroScriptPackageManagerTests をビルドし、VeroScriptPackageManagerCliTests と VeroScriptPackageManagerSdkTests を実行します。不変ソース、オフライン、対象/任意依存、名前/アーカイブ、認証範囲、言語/書式、型/秘密入力、フック予算、中断/並行編集、リース回収、解析/静的/動的登録、明示 JIT、配置閉包を検証します。SDK 消費者は異なる Core を拒否させ、プラットフォーム/端末は別途検証します。

```sh
cmake -S . -B build/package-manager -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVEROSCRIPT_BUILD_PACKAGE_MANAGER=ON
cmake --build build/package-manager --config Release --target VeroScriptPackageManagerTests VeroScriptCli
ctest --test-dir build/package-manager -C Release --output-on-failure -R VeroScriptPackageManager
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

[レジストリプロトコル](registry-protocol.md) · [ユーザー](../../User/ja-JP/index.md) · [スキーマ](../../../Schemas) · [例](../../../Examples)

## 依存一覧ファイル

`PackageRequirements.cpp` は環境を変更する前に位置引数と `FRequest.RequirementFiles` を展開します。UTF-8 検証、循環検出、元ファイルと開始行の診断、MaxManifestBytes/MaxMetadataBytes/MaxFiles/MaxDependencyDepth/MaxEdges の制限を維持します。同じソースと互換設定の場合だけ制約を結合します。入力ハッシュを保持してコミット前に再検証し、同時編集は requirements-changed とします。ファイルからフック許可やレジストリ方針を変更できません。CLI 短長オプション、C++、入れ子と引用パス、制約解決、locked/offline の再実行を検証します。プロトコルと診断コードは維持し、一覧の内容はユーザー入力として扱い、翻訳カタログにしません。

## 依存リストのサービス

`FRequest.RequirementFiles` は CLI `-r`／`--requirement` と同じインストール専用サービスです。トップレベルの相対パスには `Environment.WorkingDirectory` が必要で、入れ子パスは記述元ファイルが基準です。`Private/PackageRequirements.cpp` が管理ファイル書き込み前に解析し、UTF-8 読み込み、包含、項目、累積メタデータを `FLimits`／`FWork` で制限し、確定前にハッシュを再確認します。正規化した依存と取得元は `veroscript-project.json`／`veroscript.lock` に保存されるため、通常の固定プロジェクトに元リストは不要ですが、明示的な `-r` の再実行にはリストとローカルアーカイブが必要です。コア、レジストリエンドポイント、形式版は変更しません。ライブラリ、CLI、インストール済み SDK の検証に[リストの例](../../User/ja-JP/index.md)を含めてください。

## 環境と出力の整合性

読み取り専用の選択は、プロジェクトや状態が未作成でも中断した初回インストールを拒否します。残存する `committed` ログは、識別情報、書き込み先、全ファイルの現在のバイト列と `new` の一致を検証してから読み取れます。読み取り側はログを削除しません。最初の書き込み前に最長フェーズのログ予算を確保します。

オンライン更新では成果物の `signatures` も変更不可のロック情報です。`pack` と `deploy` は既存の出力先を置換せず、準備済み出力を原子的に公開します。ファイルは安全なハードリンクに切り替えられますが、ディレクトリにはシステムの置換禁止リネームが必要です。未対応なら失敗し、上書きには切り替えません。

組み合わせでは、コンパイル専用の依存関係を除いたデプロイも含め、スナップショットの検証済み依存エッジを使います。インストール処理結果には、自身の設定と選択した依存関係全体のコンテキストの一致が必要です。インストール処理がないネイティブローダーは、実際に参照する外部ライブラリの提供パッケージの変更不可の識別情報だけを照合します。不一致なら `combine-processing` とし、必要なグラフを同じ環境にインストールします。無関係なスクリプト依存の変更はローダーを無効にしません。

デプロイは各パッケージの設定をパッケージ値とプロジェクトのスナップショットの両方に保持し、Overlay として再利用する場合も一致させます。出力ディレクトリの末尾区切り文字に対応します。`list` と `show` は状態ファイル作成前の初回インストール中断も拒否します。コミット直前に書き込み済みと保持対象の全管理レコードを再確認します。その間はホストのコールバックを呼ばず、元の期限を引き継ぎます。ロールバックは外部編集を保持し、書き込み済みレコードとの競合時は復旧ログを残します。

## アプリケーションとビルドの接続

`VeroScriptPackageRuntime` は固定環境、配備、リソース、ネイティブ登録を扱い、HTTP、依存解決、インストール、フック、公開を含みません。`FClientOptions.ArtifactDecoder` は配布アーカイブ、`FCoreEnvironmentOptions.ArtifactDecoder` は保護ライブラリーを検証します。秘密鍵とコールバックはロックに保存せず、署名ポリシーはキャッシュにも適用します。

配備は通常 runtime、ソースアプリは runtime＋compile を保持します。利用中は `AcquireDeploymentDirectoryLease` の共有リース、置換時は排他リースを保持します。`Build::FServices` は `Deploy` と `LeaseDeployment` コールバックを提供します。

登録サーバーの URL は変更しません。任意の `signatures` は 1～16 件の空でない配列で、distribution、実際の長さ、SHA-256、アルゴリズム、鍵の識別子を結び付けます。アップロード要求、公開、照会、確認、復旧、ロック、ミラーは同じ記録を保持します。サーバーは長さとハッシュを再計算し、公開済み成果物を変更せず、同一バージョンへの追加はメタデータ一致・対象非重複・原子性・冪等性を保証します。秘密鍵は送信せず、未対応フィールドは明示的に拒否します。

`veroscript install name --application-options /absolute/trust-options.json` で信頼設定を指定します。ローカル署名は `<archive>.sig.json`。直接 URL から署名 URL を推測せず、登録メタデータまたは明示的に取得したファイルを使います。インストール ZIP とアプリコンテナーは別物です。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md) · [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
