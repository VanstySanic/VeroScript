# 配布パッケージの作成

`pack` は `veroscript-package.json` を優先し、なければ `veroscript-project.json` の `packageManager.distribution`（オブジェクトまたはプロジェクト相対 JSON ファイル）を読みます。プロジェクトからの配布では独立した `veroscript-package.json` を生成し、元のプロジェクト設定を除外します。ファイルフィルターには生成マニフェストと全エントリーを含めてください。

## マニフェストと依存

ZIP ルートに veroscript-package.json を置きます。名前は小文字英数字と単一ハイフンの 2–64 文字で、予約デバイス名は不可です。版は major.minor.patch、任意のプレリリースを許可しビルドメタデータは不可です。要求は厳密版または空白区切り比較の積集合です。実行時にはパス、対象、同一性、完全な依存グラフも検査し、スキーマだけでは判定しません。

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

用途は runtime/compile/development。--development が開発依存を選び、任意依存は `packages.<name>.enabled=true` が必要です。プレリリースは明示的要求か --prerelease が必要です。環境内の正規名は一つの版とソースだけを持ちます。配置は適用される runtime ルートを選び、間接導入は別の宣言へ直接アクセス権を与えません。

## 資源、入力、表示文言

entries.modules は公開 import 名、resources は名前付き資源群を定義します。パスは相対かつ可搬で、逸脱、リンク、デバイス別名は不可です。任意資源の不存在と、受領記録にあるファイルの削除/変更は区別し、整合性エラーを通常の不存在として扱いません。

ローカライズのルートは正規化 BCP 47 ディレクトリと en-US JSON 基準を持ちます。翻訳はキー省略を許しますが追加、ファイル間重複、formatId 変更は不可です。文字列は波括弧も含めリテラル。書式には pattern と formatId=vero.named を明示します。標準キーは package.displayName、package.description、install.before/install.after、`inputs.<name>.description` で、package.name と package.version を受け取ります。診断言語は変更しません。

```json
{
	"package.displayName": "Demo Math",
	"install.after": {
		"pattern": "{package.name} {package.version}",
		"formatId": "vero.named"
	}
}
```

入力の string/boolean/integer/enum を保持します。通常は `--input=name=<JSON>`、秘密文字列は --secret-env=name=ENV_NAME を使います。秘密は操作内だけに保持し宣言的テンプレートには渡しません。優先順位は `<package>@<version>.<input>`、`<package>.<input>`、単独名。CLI は既定で非対話です。

## インストール処理と制限

copy/template は新しい generated 領域だけに書き込みます。スクリプトフックには --allow-hooks と公開の非ジェネリック void entry(std::package::install_context) が必要です。許可資源と生成先以外のファイル、環境、コンソール、タスク、アドレス、時計、パッケージのネイティブ登録は使えません。キャンセルと VM 予算は協調的で OS サンドボックスではありません。

既定はフック 1,000,000 命令、2,000 ms、資源テキスト操作 16 MiB、バッファ 64 MiB、表示 65,536 バイトです。全ヒープの制限ではありません。入力は型付きで表示スナップショットは独立します。信頼する独自プロセッサーは版と実装識別を持つホストサービスです。生成に影響する設定変更後は再導入が必要です。uninstall --skip-hooks は警告を出す明示的復旧手段です。

フックのコンパイルには Core の `constexpr`、`if constexpr`、ジェネリック、ソース／`.vslib` インポートを使います。各コンパイルで共有する定数評価予算の既定は、作業量 1,000,000、呼び出し深度 64、式深度 256、評価メモリ 16 MiB、キャッシュ 8 MiB です。ホストは `FLimits.HookConstantEvaluationLimits` で設定し、キャッシュを無効にする `MaxCacheBytes` のみゼロにできます。事前検証、実行前コンパイル、アンインストールにも同じ方針を適用します。コンパイルは操作全体の期限とキャンセルに従い、2,000 ms の VM 制限は実行段階に別途適用します。数値とバッファの制限も実行時の方針です。

## レジストリと公開

プロジェクト設定とロックファイルのどちらにも公式ソースがない場合、CLI は https://veroscript.org/api を既定で使用します。--registry-api はこの代替アドレスを変更し、packageManager.registries.official はプロジェクトのソースを設定します。埋め込みホストは引き続き FClientOptions.OfficialApi またはプロジェクトのソースを明示的に設定します。HTTPS は証明書を検証し、--allow-http は明示的な開発用途です。--credential-env は Authorization の値（例：Bearer YOUR_TOKEN、フィールド名 Authorization: は含めない）を読み取ります。--credential-scope を省略すると認証情報は --registry-api または組み込みの公式 API に限定され、そのオリジンとパスの範囲内の URL だけに認証情報を送信します。範囲外のプロジェクト API やミラーには --credential-scope を明示してください。認証情報をロックやソースマニフェストに保存しません。

設定済みミラーには trustedMirror=true が必要で、--mirror は当該操作の公式ソースだけを許可します。allowOriginFallback の既定値は false です。オリジンや範囲が変わるリダイレクトでは機密ヘッダーを削除し、HTTPS を降格しません。

## 対応プラットフォームを追加する

publish でバージョンを作成したり、同じバージョンに別のプラットフォーム向け配布物を追加したりできます。指定するすべてのアーカイブは、パッケージ名、バージョン、依存関係、競合条件が一致する必要があります。マニフェストの対象は相互にも公開済み配布物とも重複できません。OS やアーキテクチャの異なる制約は対象を区別できますが、最低バージョンや必要機能の違いだけでは区別できません。ポータブル配布物はすべての対象と重複します。

公開済み配布物の ID、内容、メタデータは変更できません。同一の配布物は再アップロードせず再利用します。既存対象の内容を変更する場合は、新しいパッケージバージョンが必要です。アーカイブ名を変えるだけでは別の対象になりません。通信結果が不明な場合は元のアーカイブと公開キャッシュを保持し、同じファイルで再試行してください。クライアントはレジストリを確認してから成功を報告します。

1 回のコマンドに同じアーカイブを複数指定しないでください。ファイル名が異なっても内容のバイト列が同じ入力は拒否されます。

```text
veroscript install example-math@0.1.1
veroscript publish ./library.vspkg
veroscript publish ./gui-0.1.1-windows-x64.vspkg ./gui-0.1.1-linux-x64.vspkg
veroscript publish ./gui-0.1.1-macos-arm64.vspkg
veroscript install example-math@0.1.1 --registry-api=https://your-host/api/v1
```

[パッケージスキーマ](../../../Schemas/veroscript-package.schema.json) · [ローカライズスキーマ](../../../Schemas/veroscript-package-localization.schema.json) · [マニフェストの例](../../../Examples/Local/library/veroscript-package.json)

## ネイティブ項目と CLI パッケージ

`entries.native` では `id`、`path`、`linkage`、`roles` が必須です。任意の `componentVersion` はネイティブ記述子のバージョンを検証します。bindings/compiler 項目の解析には `componentVersion` と `catalog` の両方が必要です。対応ロールは `bindings`、`compiler`、`provider`、`cli` で、集合が問い合わせたライブラリー記述子と一致する必要があります。ネイティブパッケージは OS、アーキテクチャー、環境、Core ビルド、`abis.native-extension` が一致した対象識別情報を必要とします。値は対応する `veroscript env --json` から取得し、パッケージバージョンから推測しないでください。

`cli` 項目は CLI ホストサービスへコマンドとオプションを登録します。選択環境でインストールして有効化すると登録名で直接使え、ライブラリー選択は不要です。無効化したパッケージはコマンド探索に寄与しません。CLI 専用項目は解析とサービスを持たないホストでは省略します。bindings/compiler を兼ねる場合は通常どおり解析カタログとコンポーネントバージョンが必要です。インストールでは通常の不変成果物とローダーメタデータを検証します。

[コマンドの使用方法](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md)

VeroScript の公式インストールパッケージ名は `vs-` 接頭辞を使用します。例は `vs-asmjit` で、JIT バックエンド ID は引き続き `asmjit` です。公式レジストリはこの接頭辞を承認された公式発行者用に予約します。ローカルまたはプライベートソースの同じ接頭辞は公式である証明にはならず、取得元と成果物の検証が必要です。

ネイティブ ID は小文字 ASCII 英字と数字を単一ハイフンで区切り、先頭と末尾にハイフンを置きません。スキーマは 2～64 文字を許可しますが、ネイティブ記述子の 64 バイトには NUL 終端を含むため、実モジュールでは最大 63 文字にしてください。宣言 ID は記述子と期待 ID に一致させます。`entries.backends[].module` は宣言済みのネイティブプロバイダー ID を参照し、ホストサービス ID ではありません。

## 公式バックエンドと GUI の配布物

JIT およびスクリプト対応の基盤モジュールの配布物には、製品ディレクトリを含むポータブル ZIP/tar に加えて、インストール可能な `.vspkg` があります。インストールできるのは、ルートに `veroscript-package.json` を持つアーカイブだけです。既定のインストール先は現在のプロジェクトです。`--path` は相対または絶対プロジェクトパス、`--root` は Runtime/SDK 環境を選択します。`vs-asmjit` の JIT バックエンド ID は `asmjit` です。

```text
veroscript install ./vs-asmjit-0.1.1-win-x64.vspkg --path ./MyProject
veroscript --path ./MyProject --jit-backend asmjit --backend jit ./MyProject/main.vs
```

GUI アダプターは既存のホストサービス `veroscript.gui.tools` を使います。対応する GUI 有効の CLI/Runtime、または明示的に GuiScript を組み込んだホストを使用してください。インストールだけで Core に GUI が組み込まれることはありません。SDL3、RmlUi、Stb、FontBackend はそれぞれインストールして選択する提供者です。オフラインでは依存アーカイブをプロジェクトの依存設定またはコマンド引数に明示してください。隣接ファイルは自動で依存元にはなりません。

## バックエンドエントリーと予約選択値

`entries.backends[]` には `service` と `id` が必要で、パッケージ ID の文法（小文字 ASCII 英数字を単一ハイフンで区切る 2–64 文字）に従います。`auto` と `none` は選択用の予約値で、バックエンド ID には使えません。形式は排他的です。`module` は宣言済みの `provider` ロールのネイティブエントリーを参照し、`path` と `contract` を指定しません。または `path` と `contract` で専用 VM/JIT ライブラリを指定し、`module` を指定せず、`service` と `contract` を両方 `vm` または両方 `jit` にします。ネイティブターゲットと ABI の検査も適用されます。プロジェクトの `services` で `backend` を省略すると自動選択を継承し、明示的な空値または `none` は無効化、`auto` は自動候補を要求します。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md) · [en-US](../en-US/authoring.md) · [fr-FR](../fr-FR/authoring.md) · [ja-JP](authoring.md) · [ko-KR](../ko-KR/authoring.md) · [zh-CN](../zh-CN/authoring.md) · [zh-TW](../zh-TW/authoring.md)
