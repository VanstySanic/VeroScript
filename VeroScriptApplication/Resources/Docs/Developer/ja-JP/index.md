# Application サービスの保守

## 構造と境界

`VeroScriptApplication` は Core の上位サービスです。Core はコンパイル済みイメージと `.vslib` を符号化し、Application は標準 ZIP `.vsapp`、記述子、インベントリー、準備処理と内容の寿命を管理します。ZIP、暗号アダプター、GUI 統合は Core の外に置きます。`VeroScriptApplicationPackages` は HTTP を含まない `VeroScriptPackageRuntime` を明示的な凍結環境に接続します。アプリを開く処理で依存解決やインストールを行ってはいけません。

`BuildApplication` は封印済みファイル群を返し、`WriteArchive` が外側の ZIP を生成します。生イメージを受け取るオーバーロードはバイト列とメタデータを保持します。`FApplication::Open` は格納内容を検証します。`ReadEntryPackage` はプロバイダーや環境ファクトリーを読み込まずにコンパイル済みメタデータを調べます。`Prepare` は環境とバイト所有者を保持し、`FPreparedApplication::CompileSource` は所有された移植可能なソース既定値を適用します。Session の読み込みと再読み込みは共通経路を使用します。

## 整合性、所有権とポリシー

ディレクトリー、ZIP、単一ファイルでインベントリー、パス正規化、ローカル ZIP ヘッダー、モード、サイズ／展開制限、ハッシュを一致させます。余分なファイル、パストラバーサル、大文字小文字の衝突、特殊ファイルを拒否します。書き込み可能なキャッシュは所有記録と協調プロセス間リースを保持し、他者のディレクトリーを拒否します。ネイティブ内容の利用中はリースを保持します。再読み込みはコードと環境を同時に準備し、失敗時は双方を戻します。ネイティブカタログ変更時は新しいアプリ寿命が必要です。

保護サービスは Core に平文を渡す前に内容を認証します。署名の信頼と独立鍵数は利用側が決め、必須サービスがなければ平文にフォールバックせず失敗します。署名済み内容の再構成には新しい署名方針の明示が必要です。暗号モジュール所有者を保持し、秘密鍵やコールバックをシリアライズしません。内容署名とプラットフォーム署名を分け、ソース既定値に開発専用パスを混入させません。

## 統合と検証

ルートビルドがオプションモジュールを制御し、`CMakeLists.txt` は Application とパッケージアダプターを別々にエクスポートします。Core の識別、SDK エクスポート、`Resources/Schemas`、Interop／言語アダプター、CLI、ランタイム委譲を同期して保守します。ルートから有効化する `VeroScriptApplicationTests` と任意の `VeroScriptApplicationPackageServicesTests` はコンテナー／準備処理と環境統合を対象とします。破損、信頼検証失敗、所有者寿命、ロールバック、ソースコンパイル、移設、各形式の同等性を確認してください。ブートストラップ、プラットフォーム署名、実機実行の検証は別途記録し、静的文書検査の結果と混同しません。

`BuildApplication` は既に生成済みの Core アプリケーションイメージも受け付け、バイト列とホストメタデータを保持します。`FApplicationServices::PreparePackage` は環境の準備後、Session の読み込み前にコンパイル済み入口のホストサービスを設定し、イメージの検査だけでは呼ばれません。`ConfigurePackageApplicationServices` の任意の `ProvidersReady` コールバックは登録済みプロバイダー、保持された環境、スナップショットを所有するバックエンド選択関数を組み込みアダプターに渡します。選択の優先順位はホスト設定、アプリケーションのサービス設定、プロジェクト既定値で、同梱デプロイの既定値と明示的な無効化も保持します。GUI はこれらを使って標準 `.vsapp` を扱い、Core に GUI や ZIP の依存を追加しません。 `PreparePackage` はバインディングとリソースの準備だけを行い、エントリープログラムを実行してはいけません。

<!-- docs-navigation -->

---

[ユーザーガイド](../../User/ja-JP/index.md) · [開発者文書](../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
