# エディタ統合の保守

このガイドはエディタクライアントとネイティブ実行・プロトコルホストの共通境界を管理します。クライアント実装とリソース形式は以下のモジュールガイドに置きます。

エディタは設定、転送、IDE 表示を担当し、意味解析と LSP/DAP 契約はネイティブ Tooling/Core が管理します。カタログはメタデータで実行バインディングではありません。domain テストはプロトコル fixture であり VM 安全点やアプリ/UE ホストを検証しません。実行前に Session ブリッジを設置し、ホストが単一コンテキストの直列化と取消を管理します。`jitCapability` はサービス契約です。通常 Run は互換モジュールのパスを明示指定するか、CLI モジュール環境の選択を継承できます（未設定なら既定は auto）。LSP/Check/ローカル VM デバッグは JIT をロードしません。

UI と文書の六言語を一緒に保守し、コマンド設定、メッセージ ID、プロトコル値を保持します。実際のクライアントの書式処理でプレースホルダーを検証します。User は操作、Developer は構造、ビルド、公開を扱います。SDK、Runtime、プラットフォームの互換性は選択バージョンと実際の検証記録で確認します。

## クライアント固有の実装

- [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/ja-JP/index.md)
- [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/ja-JP/index.md)
- [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/ja-JP/index.md)
- [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/ja-JP/index.md)

## 共通ネイティブ契約

- [Tooling / LSP / DAP](../../../../VeroScriptTooling/Resources/Docs/Developer/ja-JP/workspace-and-debug.md)
- [C++ Session / DAP](../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/session-debugging.md)

<a id="acceptance-evidence"></a>

検証ごとに OS／アーキテクチャ、エディターと Runtime のバージョン、ソース全体の識別情報、パッケージハッシュ、実際の結果と未実施項目を記録します。ネイティブ実行には対応する Runtime を使った個別の検証が必要です。過去の結果は、記録された成果物と環境にのみ適用されます。

<a id="editor-icon-inputs"></a>

## 共有アイコン入力

共通の輪郭と出力ファミリーは `Resources/Tools/Assets/generate-icons.cjs` で管理します。製品画像は `#60A5FA`、ファイルのマークは `#5D75AE` を使い、エディターのグリフは各テーマの色と配置を維持します。全体の再生成は製品とファイル用の SVG、PNG、ICO、ICNS、およびエディターの SVGを更新します。`--brand-only` は製品ファミリーを更新します。Node.js、`sharp`、`--sharp-module` の使い方は[アセット再生成手順](../../../Assets/icons/README.md#regeneration)を参照してください。Runtime のパッケージ化は管理済みファイルを使用します。

VS Code の各拡張ビルドは製品 PNG から表示用 PNG を更新します。JetBrains の V を含む SVG は共有ジェネレーターが管理し、Gradle はプラグイン一覧のアイコンを `META-INF`、その他のアイコンを `Icons` に配置します。UI の言語間でパス、テーマ接尾辞、マニフェストの参照を維持します。コピーとパッケージ化の詳細は各モジュールのガイドで管理します。

VS Code はモジュールルートの README を拡張の詳細ページとして収録し、JetBrains はプラグイン記述子の説明とベンダーフィールドを使います。製品紹介は簡潔に保ち、6 言語の文書選択ページへリンクします。公開マニュアルと保守手順は既存の正本と公開上の役割を維持します。

<!-- docs-navigation -->

---

[文書の保守](index.md)

[en-US](../en-US/editors.md) · [fr-FR](../fr-FR/editors.md) · [ja-JP](editors.md) · [ko-KR](../ko-KR/editors.md) · [zh-CN](../zh-CN/editors.md) · [zh-TW](../zh-TW/editors.md)
