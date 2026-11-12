# コアアーキテクチャとコンパイルパイプライン

[交換可能な VM バックエンド](vm-backends.md)

バイトコード識別子は固定の `VSBC` です。形式バージョンは製品バージョンに従い、現在は `0.1.1` です。以前の開発ビルドの成果物は再生成してください。

コアは言語実装を所有します。これには lexer、parser、AST、preprocessor、compiler、bytecode、VM、code generation、JIT、module、package、debugger、host binding、reflection、serialization、concurrency、mutation、localization が含まれます。

## パイプライン

コンパイルは source、overlay、import、package の解決から始まります。字句解析と前処理は source location を保持し、public AST、semantic/type/generic/visibility 検査、debug 情報付き bytecode、module または package、VM/JIT 実行へ進みます。各段階は code、message、source location、関連 token/declaration を持つ構造化 diagnostic を返します。表示されたエラー行は result object の代替ではありません。

## module、import、macro

`VeroScriptModuleLoader` は program を merge する前に main module、ordinary import、implicit import、stdlib module、package/template module graph を解決します。import path と stdlib root は正規化して重複を除きます。source overlay は正規化 path により disk read を置換するため、editor や Web host は file を書かずに in-memory graph を compile できます。

preprocessor の `-D` definition と structured predefined macro は source fingerprint に影響する前に正規化されます。host type name は parse 前に渡されるため、`Counter c = Counter(1);` のような declaration は host binding catalog で分類されます。circular-import と project-wide-access policy は hidden global compiler state ではなく explicit loader setting です。

stdlib root または module resource を導入すると、loader は隣接 `localization/` tree を merge できます。locale merge は canonical catalog ID を key とし `en-US` fallback を保持するため、resource package は stdlib と localization directory を一緒に deploy します。

## 境界と意味論

`Public/` は supported header、`Private/` は compiler、parser、VM、JIT と internal data structure を含みます。public-header test は installed header が存在しない private file に依存しないことを保証します。外部 tooling と language adapter は public contract を使います。compiler AST または runtime internal が本当に必要な source-tree target は CMake で private include path を明示的に受け取り、SDK consumer ではなく review 対象の friend dependency として扱います。

<a id="core-vm-ownership"></a>

Core はプログラム、値、論理フレーム、言語操作、実行制御とディスパッチを所有します。完全なバイトコード解釈ループは Modules/Execution/VeroScriptBytecodeVm の VeroScriptBytecodeVm に属します。ID は vs-bytecode、パッケージ名は vs-bytecode-vm です。Core は VM/JIT を暗黙に選択しません。Runtime は Vm/ 内の公式動的ライブラリを明示的に構成し、Interop、Web、モバイルの静的製品も VM を明示的に注入します。 JIT: `Modules/Execution/VeroScriptAsmJit`; Core: `Private/JitSupport`.

## static declaration、generic、capture

static field は module-owned global として登録され module initialization で初期化されます。static access は type receiver または評価済み object receiver を使えます。後者も backing global を読む前に side effect のため receiver を評価します。generic type instantiation は field type を substitute し、static storage を独立して登録します。

compiler は lambda capture と generic bound を AST に記録し、source location を bytecode diagnostic へ持ちます。static type restriction、capture validity、type-argument bound、inheritance rule は compiler semantics です。frontend、AOT emitter、tooling consumer は記録された declaration identity を使い、display name から再構成してはいけません。

## スコープ付き解析制御と依存関係のメタデータ

`FAnalysisControlScope` は、呼び出し元スレッドでの同期解析用のキャンセルと計測コールバックを所有します。入れ子のスコープは以前の制御を復元します。キャプチャしたオブジェクトはスコープの間有効である必要があり、各 `FAnalysisStage` は制御スコープより先に終了する必要があります。述語が true を返すと `CheckAnalysisCancellation()` は `FAnalysisCancelled` を送出します。構文診断とは別に扱い、VM の実行はキャンセルしません。キャンセルされた lexer は不完全なトークン列を保持せず、再度字句解析できます。

`FAnalysisStage` は同じスレッドで経過ミリ秒を報告します。名前はステージの間有効である必要があります。計測コールバックの例外は無視され、スタック巻き戻し中のキャンセルや解析例外を置き換えません。入れ子のステージ時間は重なるため、独立した処理時間として合算できません。

`VeroScriptModuleLoader::GetLoadedFileStamps()` は、実ファイルのソースやパッケージを読む前に記録した更新時刻とサイズを公開します。メモリ入力とオーバーレイにはディスク記録がありません。返された参照は次回の読み込みまたはローダーの破棄まで有効です。キャッシュ利用側は `bValid == false` を拒否し、AST の再利用前にディスクのメタデータを再確認して、解析中の変更も検出します。これはバイトのハッシュではありません。文書バージョン、オプション、オーバーレイには別途無効化が必要です。

## 長さとインデックス

コレクションの長さと線形インデックスは `int64` を使用します。ネイティブ格納は `size_t` を維持し、縮小変換前にスクリプトとプラットフォームの両方の範囲を検査します。C# コンテナには CLR の制限が適用されます。この意味論のバイトコード識別子は `0x43425356`（`VSBC`）です。32 ビット長を返す古いキャッシュはソースから再コンパイルしてください。C++ ホストと AOT 出力も更新済みヘッダーと標準ライブラリで再ビルドする必要があります。

## 公開マクロ

[プリプロセッサ — 公開マクロ](../../User/ja-JP/language-basics/preprocessor.md#exported-macro-contract) を参照してください。

VSBC 0.1.1 は検証済み公開マクロ表も末尾に保存します。キャッシュと `.vslib` は表を保持し、`.vsapp` は空の表を保存します。古いバイナリパッケージも再生成してください。最終出力のインポート位置で前処理を一時停止し、対象インターフェイスの準備後に再開します。モジュール準備には上限のあるヒープ上のスタックを使います。

## コンパイル時のアノテーション処理

凍結済みのネイティブ処理器レジストリは `FCompileOptions.AnnotationProcessing`、C++ Engine/Session、Tooling または `FCodegenOptions` から渡します。`@Name` は `@Name()` と同じで、順序付きフラグ、名前付きスカラー引数、入れ子のグループを保持します。未登録の注釈はメタデータとして残ります。単一の宣言段階でユーザー定義を結合した後、メソッド本体と補助メソッドを生成し、通常の検査、仮想ディスパッチ、ジェネリック置換を行います。別ファイルの定義には宣言と実装の両モジュールの読み込みが必要です。パッケージは生成メンバーとオープンテンプレートを保持し、利用時に生成元の処理器は不要です。Tooling は読み取り専用の `GeneratedDocuments` を公開し、組み込みツールが仮想文書を表示します。独立ツールはネイティブ処理器を自動ロードせず、ホストが登録します。C# / Python は Interop 経由でネイティブサービスハンドルを接続します。VSBC 0.1.1 は構造化注釈と生成操作を保存するため、旧キャッシュとバイナリパッケージを再生成してください。ネイティブ例は `VeroScript/Resources/Examples/AnnotationProcessing` にあります。

CLI 回帰テストは二つのライブラリの特殊化とデバッグ情報を除いたアプリケーションを検証します。

## ホストのオーバーロード

[ホストバインディングとバインディングカタログ — ホストのオーバーロード](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/bindings.md#host-overload-resolution) を参照してください。

## 共通テンプレートサービス

`VeroScriptTemplate.h` は順序付き Type/Value スキーマ、正確な型付き定数、制限付き純粋評価、引数の符号化、宣言／実体のスナップショットを提供します。コンパイラーは入力 AST を変更せず具体化結果を所有します。既定値は宣言の文脈で解決し、実体の識別には外側と独自の引数を含めます。VM、JIT、Core AOT は確定した命令と配置を使います。

`ModuleMeta::EnumerateTemplateDeclarations` は本体を実行せず、データを所有する開いた宣言を返します。関数と型の `TemplateInstance` は独自／外側の引数を分離します。型は正規名、値は正確な `@value:` 表現を使います。接頭辞を除去して `DecodeTemplateArgument` で復号し、double を経由しません。スナップショットは返却オブジェクトと同じ寿命、処理器ビューはコールバック中のみ有効です。

ネイティブ `TemplateParameters` とスクリプト `declaration_info.template_parameters` は種類、既定値、外側フラグを共有します。ライブラリは通常の公開型の開いたメソッドと必要な非公開依存を保持します。閉じたアプリのロードでは処理器を実行しません。内部レイアウト／ビルド識別子は更新し、製品と VSBC は 0.1.1 のままです。AST 消費モジュールは同じ Core で再ビルドしてください。独立ソース出力は未対応構造を出力前に診断します。

`VeroScriptTemplateQuery.h` の `FTemplateCallTypeQuery` は、準備済みの不変プログラムについて、Core の推論・既定値・オーバーロード選択を用いてシグネチャを照会します。本体のコンパイル、プロセッサやネイティブ呼び出しの実行は行いません。可視性とアクセスの絞り込みは呼び出し側の責任です。返す型表記は検索用で、表示やシリアライズ識別には使えません。プログラムとバインディングは照会より長く保持し、各インスタンスは単一の解析スレッドで使用します。キャンセルは既存の契約に従って伝播します。

## 実行時最適化の境界

有界な整数呼び出し計画は、デバッグ・ホットリロード・実行制御のない実行でのみ使用し、呼び出し深度制限を維持します。引数と値キャプチャは int に限定します。整数フィールドの試算に失敗した場合は書き込まず通常の VM に戻し、エラー以前の部分的な副作用を維持します。変更オブザーバーがある場合はフィールド計画を使用しません。計画はモジュールやプログラム世代をまたがず、借用モジュールのキャッシュは実行境界で消去します。BigInt のハッシュは不変ペイロードに必要時に原子的にキャッシュし、数値コピーは独立したラッパーとカスタムプロトコルを維持します。

FScriptObject のホスト基底は GetHostBase()/SetHostBase() で扱います。通常のオブジェクトはホスト／コピー構築の補助記録を割り当てず、コピーした記録は変更前に分離します。C++ SDK 利用側と JIT バックエンドは一致するヘッダーと Core 身元で再ビルドしてください。C ABI は変更しません。

オブジェクトの型名は `FScriptObject::GetTypeName()/SetTypeName()` で扱います。上限付きの名前キャッシュで不変の名前を共有し、VM やモジュールの解放後も保持します。借用した名前参照は、そのオブジェクトの型名変更時に無効になります。名前変更は実行時レイアウトのヒントも無効にします。呼び出し署名キャッシュはモジュール、世代、実際の署名を検証し、再入中も不変の記録を保持します。引数、参照、戻り値の検査規則は変わりません。

## フィールド既定値と静的依存の準備

`RegisterStaticMethodDeclarations` は通常フィールドの配置前に呼び出しシグネチャを公開します。閉じたジェネリック struct/class/interface は静的ストレージ、所有者記述子、メソッドを公開してから `CompileOwnFieldDefaults` でインスタンス既定値を束縛します。名前空間、アクセス、型／値置換、フィールドメタデータを保持し、テーブルが拡大するコールバック後は添字から再取得します。自己型の解決は字句宣言 ID を使い、明示的な外側引数、ルート修飾、値束縛を維持します。

`FStaticInitializationDeferral` はモジュール初期化コードがインスタンス用バッファーへ入るのを防ぎます。`bStaticFieldInitializationStarted` は初期の型配置と静的初期化段階を分け、`PendingGenericStaticFieldInits` は必要になるまで所有者を保留します。`FlushPendingGenericStaticFieldInitializers(RequiredType)` は指定所有者だけを取り除き、他の順序を保ちます。初期化コードは格納先グローバルごとに一度だけ出力し、各フィールド既定値への埋め込みや実行時の静的読取りごとの再出力は行いません。

`Private/Compiler/VeroScriptStaticInitializationDependencies.cpp` は切り離した初期化子の低水準バイトコードから依存を準備します。`PrepareStaticInitializationDependencies` は選択済み呼び出し／コンストラクター、フィールド既定値、コピー、ローカル破棄を追跡し、callable の由来、キャプチャー、参照書込みを状態リビジョンで管理します。保存したクロージャー、保持された割当て、null を保持できるフィールド、空コンテナーに、宣言型だけから実行／破棄依存を追加してはいけません。未知のコールバック先は完全な実行グラフではなく、解析はネイティブ副作用を実行しません。

利用側の式を一時バッファーにコンパイルし、依存を準備してから対応するデバッグ位置とグローバル初期化範囲で追加します。必要なジェネリック初期化は短絡分岐やフィールドバッファーへ入れません。解析はキャンセルを確認し、再帰準備前に記述子をコピーし、業務コード実行の代わりに活動／リビジョンガードを使います。無関係な初期化の通常順序を保ち、任意の循環解決は保証しません。

後に宣言した静的メソッド、入れ子自己型、別名、const/ref アクセス、選択コンストラクター、callable／参照更新、コピー／破棄寿命、無関係な所有者を確認します。Core VM、選択 JIT、Core AOT を個別検証し、独立ソース出力とは区別します。シナリオは `Private/Tests/VeroScriptStaticMemberTests.cpp`、バックエンド差分スイート、C++ Core AOT スイートにあります。存在だけで成功結果とは扱いません。

## 10 進変換の実装

`Shared/Private/VeroScriptFloatParsing.h` の `FromCharsDecimal` をパーサーとスカラーテキスト処理で共有します。浮動小数点 `std::from_chars` の有無を検出し、代替経路は対象幅へ直接変換します。有効桁の接頭部と sticky 桁で入力を制限し、数値ロケール C と最近接丸めを使い、呼出し元の状態を復元します。非ゼロの非正規化数は有効ですが、非ゼロ入力がゼロになる場合は出力を代入せず範囲エラーにします。`ParserDetail::ParseFloatLiteral` は double で表現できる `f` リテラルの下限例外を別に維持します。中点、負のゼロ、長い仮数、ロケール、失敗時の出力保持を個別に確認し、ソース上のケースをプラットフォーム合格としないでください。

Core のバインディングカタログ JSON と Tooling の LSP/DAP JSON も同じ変換を使います。`vs_use_shared_private_headers` は非公開 include パスを提供し、共有ヘッダーは公開 SDK API ではありません。

`Shared/Private/VeroScriptJsonString.h` は、Core ランタイムに依存せず、バインディングカタログ、Interop、ネイティブ言語ツールに JSON 文字列のエスケープを提供します。追加処理は外側の引用符や UTF-8 検証を含めず、文字列の内容を書き込みます。呼び出し側は既存の制御文字の表記を維持し、出力バッファと重ならない入力を渡します。この非公開ビルド入力をソース配布に含め、各利用側の実装フィンガープリントにも反映します。

<a id="floating-formatting-support"></a>

`Shared/Private/VeroScriptFloatFormatting.h` はパッケージのマクロ直列化、アプリケーション i18n、Tooling JSON、Json、GUI 数値コントロールに `ToCharsDecimal` と `ToCharsGeneral` を提供します。利用可能なら浮動小数点の `std::to_chars` を使い、互換経路は C 数値ロケールと元の float/double 幅で往復変換可能な表記を選び、呼び出し元の errno と丸め状態を復元します。公開 SDK API ではなく実装専用です。Core／対象 AOT、Json バインディング、GUI コンパイルのフィンガープリントとソース配布に含め、charconv とは別に互換経路を検証してください。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/architecture-and-pipeline.md) · [fr-FR](../fr-FR/architecture-and-pipeline.md) · [ja-JP](architecture-and-pipeline.md) · [ko-KR](../ko-KR/architecture-and-pipeline.md) · [zh-CN](../zh-CN/architecture-and-pipeline.md) · [zh-TW](../zh-TW/architecture-and-pipeline.md)
