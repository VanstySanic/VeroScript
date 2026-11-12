# コンパイル処理とソースメタデータ

Core は `VeroScriptCompileProcessing.h` でネイティブ処理器を提供します。ソース変換、型とメンバーの生成、宣言と本体の置換、注釈とコメントの追加が可能です。生成コードには通常の可視性、型、ジェネリックの検査が適用されます。業務規則は拡張側で定義します。

## 登録と寿命

FCompileProcessorRegistry に FCompileProcessorDesc を登録し、Freeze() 後に FCompileOptions::CompileProcessing.Registry へ渡します。識別子、版、構成キー、順序、段階、購読、許可操作、ファクトリを宣言します。各タスクは専用インスタンスを作成し、段階間で再利用します。コールバック内のコンテキストと AST ビューは呼び出し中のみ有効です。ネイティブモジュールなどの所有者はファクトリで保持します。

同じイベントに複数の購読が一致しても各処理器は一度だけ呼ばれます。順序は Order、ProcessorId です。順序付けは操作の競合を許可しません。未登録の注釈は通常のメタデータです。FAnnotationProcessorRegistry も同じトランザクションを使い、Members でメソッドを追加し、ユーザー本体の結合後に Bodies で既定本体を補完します。

## 段階

| 段階 | 入力と操作 |
| --- | --- |
| RawSource | 元テキスト。EditSource による独自構文と import の変換 |
| ActiveSource | 前処理後テキスト。import と前処理境界を保持 |
| Types | 宣言概要。型、interface、enum、別名の追加と置換 |
| Members | 登録済み型。メンバー、関数、グローバルの追加と置換 |
| Bodies | 結合済み本体。CompleteBody、ReplaceBody、SetMetadata |

Types 後に別名と継承を結合し、Members 後にファイル間実装を結合して本体を解析します。Bodies はレイアウトやシグネチャを変更しません。生成宣言は処理器を再帰的に起動せず、後続段階から照会できます。

AddDeclaration はモジュール、namespace、任意の OwnerId、現在の Snapshot を持つ FCompileScope を使い、TargetId を持ちません。置換は宣言種別、名前、型引数数を保持します。CompleteBody は欠けた本体のみ補完します。純粋仮想メソッドと deleted 宣言には本体を追加できません。ただし既存の純粋仮想デストラクタ規則は維持します。古いターゲットと競合する変更は適用前に拒否します。

FCompileMetadata は未指定と明示的な空集合を区別します。置換は変更対象外の説明を保持し、古い子メタデータを除去します。EditSource は UTF-8 バイト範囲と由来マッピングを使います。ActiveSource は前処理指令や解決済み import 列を変更できません。

## メタデータ、キャッシュ、パッケージ

ソースメタデータの符号化では、繰り返すパスと識別子をスナップショット内の文字列参照で共有します。値、文書とノードの識別子、位置、ソースマッピングは保持し、符号化サイズの上限は 64 MiB のままで、展開後の文字列データには別途 512 MiB の上限があります。デコーダーは従来の文字列を直接保存する形式も読み取れます。準備結果のフィンガープリント、バイトコード、パッケージは同じコーデックを使用するため、実装識別子が変わった場合は対応する Core とネイティブバックエンドを再ビルドしてください。

VeroScriptSourceMetadata.h は不変の文書、宣言、コメント、注釈引数ツリーを所有します。フラグ、値種別、入れ子、重複、原文、位置を保存します。RegisterAnnotation は処理機能なしで説明だけを登録できます。リフレクションによる情報照会は実行権限を付与しません。

CommentRetention は Full、Documentation、NoComments を選択します。NoComments でも注釈と登録説明を保持します。Unavailable はソース情報なしを意味します。ホットリロード後も古い不変スナップショットは読めます。

PreparedProgram は宣言、操作、由来、依存指紋を保持します。MatchesHost、MatchesProcessing、MatchesDependencies で再検査し、ソース、オーバーレイ、マクロ、import、通常のオプションもホストのキーへ含めます。キャッシュ不可の処理器はタスク間で再利用しません。AST だけからソース変換や処理器状態を再現することはできません。

開放ジェネリックのパッケージは変換済みテンプレート、操作、生成元メタデータを保存します。消費側は生成元処理器をロードせず Types、Members、Bodies を再生します。閉じたコードはバイトコード、型代入は通常の規則を使います。0.1.1 の契約変更時には古い開発成果物を再生成します。

キャンセルは協調式です。長い処理は CheckCancelled、依存読込は ReadDependency を使い、不在も記録します。イベントや生成量には上限があります。ブロックするネイティブ呼出しはホストの協力が必要です。Interop、ツール、AOT は同じ準備結果を使い、JIT は生成済み業務コードを実行します。

例：[ネイティブ処理器](../../../Examples/CompileProcessing/main.cpp)、[スクリプト拡張](script-compile-extensions.md)。

## 注釈名の照合

名前は既定で大文字と小文字を区別します。`annotation_definition.case_sensitive = false` を設定すると `@GuiWidget`、`@guiWidget`、`@GUIWIDGET` が同じ定義を参照します。処理器の購読、`has_annotation`、`find_annotations`、`annotation_info.matches_name(name)` はこの規則に従います。`name()` は元の表記、`definition().name()` は登録名、`definition().case_sensitive()` は照合規則を返します。引数名とメタデータのキーは区別を維持します。`find_annotations` と `matches_name` には `@` が必要ですが、`has_annotation` では省略できます。未登録の注釈は大文字と小文字を区別します。

`FAnnotationDefinition::CaseSensitive`, `FHostBindingRegistry::RegisterAnnotation(..., false)`, JSON `caseSensitive`, C Interop `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE` (`name_flags`), C# `CaseSensitive`, Python `case_sensitive`. ASCII; ロケールに依存しません。重複する登録を拒否し、指紋、バイトコード、ライブラリ、ジェネリックで規則を保持します。

## 統合の責任範囲

`FCompileResult::PreparedProgram` は共有の準備結果です。C++ Session は `SetCompileProcessing` を公開し、コード生成ホストは `FCodegenOptions::CompileProcessing` を渡し、Tooling の解析・プロトコルオプションも同じ設定を保持します。ネイティブ Interop ホストは `VeroScriptInteropCompileProcessing.h` の `CreateCompileProcessingService` で不透明なサービスを作成できます。これらは既存の統合点です。

レジストリとネイティブ所有者を明示的に渡します。通常の CLI は任意のネイティブプロセッサーファクトリーを自動検出しません。スクリプト拡張と明示的に選んだネイティブモジュールは設定済みのローダーパスを使用します。準備済みプログラムのプロセッサーを再実行したり、コメント解析から別の宣言を構築したりしないでください。JIT は処理後の業務コードを実行します。各プラットフォームとアプリケーションの受け入れ検証は別途必要です。


## スコープ宣言とブロック生成

宣言ビューは `ScopeId`、`ParentScopeId`、`bLocal`、`bNested`、意味上の所有者を提供します。`FCompileProcessingContext::Blocks` はコールバック中のみ有効な読み取り専用の借用ビューです。Types 段階で `FCompileScope::ScopeId` と `InsertionBeforeId` を指定してローカル型やエイリアスを追加します。空ブロックや末尾への追加には終了アンカーを使用し、module／owner の意味は維持します。

スクリプト側は `context.blocks()`、`context.block_scope(scope_id, before_id)` を使用します。ブロックとアンカーは `scope_id()`、`statements()`、`id()`、`range()` などの getter メソッドを提供し、ソース範囲は通常の値フィールドを保持します。scope や任意のオフセットを自作してスナップショット検査を回避できません。

同じアンカーへの操作順序は決定的で、宣言は挿入位置から可視になります。親の置換と古い子アンカーの操作は競合します。本体の補完・置換ではローカル宣言を再構築します。生成された子宣言は通常検査のみを受け、プロセッサーを再実行しません。型の具体化も準備済み結果を代入します。識別、生成記録、子宣言一覧、所属は VSBC とライブラリーで保持されます。0.1.1 のレイアウト変更時は開発成果物を再生成してください。

## 宣言追加バッチと所有者の参照

`Private/Processing/VeroScriptDeclarationProcessing.cpp` の Members 段階の `DeclarationAppendsOnly` は、全操作が `AddDeclaration` で、`TargetId` と `ScopeId` が空であることを要求します。`OwnerId` のない名前空間ヘルパーと、検証済み宣言スナップショットにある class/struct/interface のメンバーを交互に追加できます。段階、スナップショット、書込み可能モジュール、競合の検査は維持され、新たに生成した所有者を古いスナップショットの対象にはできません。

`ReuseOwners` は `P.Classes`、`P.Structs`、`P.Interfaces` の格納先アドレスが変わらない間だけそのスナップショットを借用します。生成したヘルパーの本体はローカル型を追加してコンテナーを拡張できるため、格納先が変わった後は `Collect(P)` で参照を再構築します。この拡張をまたいで宣言／AST ポインターを保持しないでください。ヘルパーとメンバーの混在、ジェネリック所有者、仮想／インターフェース呼出し、ローカル宣言を確認します。`Private/Tests/VeroScriptCompileProcessingTests.cpp` の `FInterleavedAppendGenerator` はソース上のシナリオであり、テスト合格を意味しません。

<!-- docs-navigation -->

---

[開発者ドキュメント](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

Language: [en-US](../en-US/compile-processing.md) · [fr-FR](../fr-FR/compile-processing.md) · [ja-JP](compile-processing.md) · [ko-KR](../ko-KR/compile-processing.md) · [zh-CN](../zh-CN/compile-processing.md) · [zh-TW](../zh-TW/compile-processing.md)
