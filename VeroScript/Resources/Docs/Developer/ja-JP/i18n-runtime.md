# アプリケーション国際化の実行契約

コアは値、原子的な設定、不変のプロバイダースナップショット、ネイティブ呼び出し診断を管理します。診断言語は独立しています。ICU、リソースファイル、GUI 更新は拡張の責任です。

## 公開 API

- [`VeroScriptI18n.h`](../../../../Public/VeroScriptI18n.h) は `FI18nArgument`、`FI18nArguments`、`FI18nConfiguration`、`FI18nText`、`FI18nContext`、`FI18nSnapshot`、`FI18nVersion` を公開します。
- `CreateContext(config, services, out, error)` で作成し、`CaptureSnapshot()`、`TryLookup`、`TryResolve`、`TryFormat` で操作します。`ToScriptValue` / `FromScriptValue` は Core ラッパーの識別情報を検証します。
- `FI18nServices::Publish` は両方の不変ビューをアトミックに公開します。`PublishResources` / `PublishFormatter` はもう一方の最新ビューを保持し、`Acquire` は所有権のあるスナップショットを返します。リソースや設定内容を変えるときは新しいビューを使います。同じビューを所有者省略で再公開すると既存のモジュール保持が維持されます。制御ブロックを所有するヌルの別名 shared_ptr も有効な保持参照です。
- `ConfigureAndSetServices` は設定とサービス参照をアトミックに交換します。他のコンテキストと共有するサービス自体をトランザクションで変更しません。`Set*` はほかのフィールドの最新値と統合します。
- リゾルバーは呼び出しごとに正規化済みの単一の正確な言語を受け、`Found` / `NotFound` / `Failed` を返します。見つかった空のテンプレートも有効です。明示的なフォールバック列は Core だけが処理します。
- フォーマッターは不変で並行呼び出し可能な `SupportsFormat` と `Format` を持ちます。要求には元の型付き引数、要求言語、一致した言語、有効な書式ロケール、型付きオプションを渡します。`literal` / `vero.named` は Core 用に予約されています。
- `FI18nWork` は現在の操作の確認処理と出力上限を借用し、ゼロは無制限です。長い処理ではキャンセルを確認し、出力割り当て前に予算を検証します。Core はコールバック前後を確認しますが、ブロック中のネイティブ呼び出しを強制中断できません。
- 値とスナップショットはデータを所有します。コールバックのコンテキスト、VM、借用要求ビュー、`FI18nWork` を保持してはいけません。通常の単一ネイティブ値の変更は外部同期が必要です。コンテキスト/サービスのメソッドはスレッド安全で、カスタム値とプロバイダーは不変かつ並行読み取り可能である必要があります。
- プロバイダー/カスタム値のモジュール所有者はコールバックと仮想デストラクターより長く存続させます。動的コードには所有者を明示し、コンテキスト/サービス → プロバイダー → コンテキスト/サービスの強参照循環を避けて弱参照を使います。
- 同一コンテキストの `Try*` 再帰はスレッド単位で拒否します。プロバイダーから設定を読み書きできますが、コールバックが呼び出し側の値を交換・リセットしても実行中の操作は開始時のスナップショットとテキストを保持します。借用 string_view の入力は戻るまで有効にし、エラー出力に入力文字列を所有させないでください。実行中の VM へ再入してはいけません。
- バージョン等価性にはコンテキスト識別情報と設定、リソース、フォーマッターのリビジョンが含まれます。リビジョン枯渇時は失敗し、巻き戻りません。永続化やプロセス間の意味はありません。
- 構造化エラーは `FI18nError::Status`、所有する `Diagnostic`、ドメイン/キー/言語/名前を持ちます。`NotFound` はエラーなしで返し、失敗には Ok 以外の状態と診断が必要です。成功したコールバックはエラーを返しません。
- `FNativeCallContext::DiagnosticOutput` は所有する `FScriptError` の借用出力です。VM/JIT ブリッジは ID/引数を保ち、現在の呼び出し位置を設定します。`I18nServices` は現在の呼び出し用の借用サービスです。スクリプトで作成したコンテキストは所有権のあるサービス選択を保持し、VM は保持しません。
- `VeroScriptVm::SetI18nServices` または C++ Engine/Session の `FEngineConfig::I18nServices` / `I18nLimits` を設定します。ワーカーは選択と予算を継承し、独自の実行制御を使います。既存コンテキストはホスト設定が変わっても元のサービス参照を保持します。

## コンパイルと実行

パーサーとコンパイラーは共通の `StandardLibrary::GetBindingCatalog` を使い、正規のメモリエントリーを i18n より前に配置します。既存の Core 相対オペコードは内部名 `CallMemory` を維持したまま共通カタログを参照し、新しいオペコードや `FValue` の選択肢は追加しません。`Memory::UsesRuntimeMemory` は i18n エントリーを除きます。バイトコード/パッケージの読み込みも同じカタログを検証します。生きたコンテキスト、プロバイダー、カスタムペイロードは実行時の値であり、ライブラリーに直列化されません。

AsmJit は未対応の Core 相対呼び出しを実行前に拒否し、コールバックを再実行せず既存の VM フォールバックを使います。標準値には通常のホスト複製/等価性プロトコルを使い、一般的な値の等価性にキーハッシュは不要です。Map/Set キーの等価性には引き続き有効なハッシュプロトコルが必要です。

通常の名前付きホストメソッドは引数型によるオーバーロードに対応します。スクリプト API は `text::message_key(domain,key)` / `text::message(domain,key,args)` と `format` / `format_with` を維持し、型付き書式化は文字列専用の位置引数 `std::format` を暗黙に呼び出しません。

## Validation

`VeroScriptI18nTests` はスカラー精度、所有/COW データ、プロバイダー、バージョン、並行公開、再入、キャンセル/出力上限、スクリプトのジェネリック/コンテナー/Attribute、ワーカー継承、バイトコード/ライブラリー往復、VM/Mixed/JIT、診断ブリッジを検証します。JIT 部分は互換バックエンドを指定したときに実行します。`VeroScriptI18nExample` は Core と互換性のある Bytecode VM SDK をリンクします。

[i18n SDK サンプル](../../../Examples/I18n/README.md) · [ユーザー API](../../User/ja-JP/standard-library/i18n.md).

<!-- docs-navigation -->

---

[開発者文書](index.md) · [en-US](../en-US/i18n-runtime.md) · [fr-FR](../fr-FR/i18n-runtime.md) · [ja-JP](i18n-runtime.md) · [ko-KR](../ko-KR/i18n-runtime.md) · [zh-CN](../zh-CN/i18n-runtime.md) · [zh-TW](../zh-TW/i18n-runtime.md)
