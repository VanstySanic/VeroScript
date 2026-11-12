# GUI フレームワークの契約

## 対象範囲

ネイティブ API は `VeroScript::Gui`、スクリプト API は `VS::Gui` を使用します。ウィジェットツリー、宣言単位の id、ライフサイクル、型付きプロパティ、Attribute、イベント、Context/Application、テーマ、表示アニメーション、i18n スナップショットを提供します。基本コントロールを含みます。基本、コレクション、ナビゲーション、モーダルダイアログの各コントロールを提供します。任意のエディター連携は別ガイドで説明します。

## コンパイルと構築

ホストはカタログに `RegisterScriptBindings` を登録し、読み込み前に `ConfigureGuiScriptEngine` を呼びます。公開済み環境では、その正規カタログにも同じ GUI バインディングが必要です。構成変更時は環境をコピーし、公開済み環境を直接変更しません。

独立した `VeroScriptSession::CallFunction` 呼び出し間でグローバル状態を維持する場合は、初期化前に `session.SetHotReloadEnabled(true)` を設定します。無効な場合、各エントリーのモジュール変数は新規に作成されます。同じスクリプトエントリー内の `Application.Run/Tick` は既に状態を共有します。

GUI 対応 CLI は、通常のコンパイルや `--check` で GUI が必要な場合にスクリプトフレームワークの依存関係を自動設定します。既存の GUI プログラムに新しいスイッチは不要です。明示的な `gui-*` タスクとネイティブの `ConfigureGuiScriptEngine` はフレームワークの暗黙インポートを維持します。

変数、フィールド、引数の名前が `Gui` であるだけではフレームワークをインポートしません。名前空間参照、GUI 注釈、マクロ、検査できないインポートは従来の必要性判定または保守的な処理を維持します。

`@GuiWidget`、`@GuiProperty`、`@GuiMethod`、`@GuiEvent` は省略可能な Mark を共有します。GuiWidget は Id、GuiProperty は Style フラグも受け付けます。注釈名とオプション名は大文字小文字を区別せず、値と ML/CSS の名前は区別します。元の名前と Mark の両方を使用できます。通常のプロパティは値だけを受け取り、`std::attribute<T>` のみが Getter バインディングを受け取ります。

この ASCII の大文字小文字を区別しない規則は GUI 注釈とそのパラメーターに適用します。Core の注釈名は既定で大文字小文字を区別し、GUI 処理器はオプション名を ASCII の大文字小文字を区別せずに認識します。反射の `find_arguments(name)` は GUI 注釈でも保存された引数名の表記に完全一致で照合します。

`Type::Create().Property(value)[{ children }]` の Builder は最後の Build なしでウィジェットに暗黙変換されます。構成値の適用後に全体を初期化します。コンストラクターは内部状態を用意し、変更フックが有効値を内部要素へ同期します。GetX は基底値を読み、アニメーションは独立した表示値を生成します。

初期化は子から親へ、各インスタンスで成功時に一度だけ実行します。マウントは親から子、アンマウントは子から親の順です。マウントフックで変更した状態はレイアウト前に、レイアウトフックの変更は次の `Update` で同期します。失敗したレイアウト通知と未実行の通知は次の更新で再試行し、成功済みの通知はレイアウトが再び変わるまで繰り返しません。レイアウトコールバックは現在の全ジオメトリを参照でき、入力と描画には更新の成功が必要です。

## ML と CSS

`.vswml` は任意のウィジェットをルートにするページ、または template を含む `<component type="Demo::Card" base="Base" mark="card">` です。コンポーネントは本物の VS 型を公開し、VS/ML の継承と合成を使用できます。通常のページは専用の非公開状態を持ちます。import/script/style は Gui が処理し、内容をそのまま RML/RCSS として解釈するわけではありません。

通常の属性は固定値です。`bind:gap="{ Count }"` は型付き Getter、`on:pointer-down="Increase"` または波括弧の文ブロックはイベントを設定します。bind/on は組み込みで xmlns 宣言は不要です。内蔵スクリプトはページまたはコンポーネント自身を使用し、外部スクリプトは宣言されたコンテキストを使用します。アクセス可能な外部メンバーは無修飾または context 経由で参照でき、ローカル変数のスコープは維持されます。式は VS であり、通常のフィールドに暗黙の変更監視はありません。

独自イベントは正確な引数型、戻り値、delegate/multicast_delegate の種別に従います。ネイティブ入力は捕捉・ターゲット・バブルのルーターを使用します。伝播停止、即時停止、既定処理の抑止は別の操作です。ML コールバックは弱参照であり、明示的な Core の強いバインディングは元の所有権を保持します。

宣言ごとに非公開 id スコープがあります。使用側の子は内部テンプレートの要素にはなりません。`elements.Require("part")` は宣言スコープ内を検索します。リソースの相対パスは所有する ML/CSS/VS モジュールが基準で、現在ディレクトリではありません。 `elements.Find("part")` はウィジェットが見つからない場合に `nullptr` を返し、`elements.Require("part")` は検索の成功をアサートします。

VSWCSS は型/Mark、*、class/id、複合、子孫・子、状態、制約付き var を扱います。未対応構文は診断します。優先順位は型の既定、テーマ、ローカルシート、ローカル設定/バインディングです。Setter は Getter を置き換え、ClearX は古い Getter を復元しません。Style だけで独自オブジェクトがアニメーション可能になることはありません。

## 実行と配布

Context は組み込み UI 用、Application は Platform/Graphics/Gui のサービス組み立て用です。イベントポンプの所有者は一つです。MakeRegisteredServiceFactories は一致するネイティブ提供者を使い、コードの所有権を保持します。借用したデバイスや描画先はアダプターが閉じません。 GUI プロバイダーは `IBackendService` を返します。

Application はプロバイダーのクリーンアップ例外をエラーに変換し、未解放のリソースとイベントポンプの使用権を保持します。Close は再試行できます。最終参照の解放時に失敗した場合は、所有スレッドの CollectRetired で再試行します。

SetLocalization は Core の i18n context を接続します。GetLocalizationSnapshot はフレーム開始時の固定スナップショットを返し、変更は次のフレームに反映されます。型付きメッセージと引数は Core が保持し、Gui は ICU を再実装しません。

`BakeGuiApplication` は GUI メタデータを保持する内部 Core アプリイメージを生成します。ネイティブのイメージ利用側は `ReadPackage`、`RegisterGuiArtifactBindings`、Session の `LoadPackage` を使用します。`gui-compile` はさらに Application サービスで標準 ZIP `.vsapp` コンテナーに包み、`FApplication`/Session の `LoadApplication` で読み込みます。コンテナーの生成と読み込みには Application 対応が必要です。ソースの検査とプレビューはこのサービスがなくても利用できます。

`CompileGuiSource`/`CompileGuiFile` は初期化子を実行せず、ウィンドウやデバイスを作らずに完全なコンパイルを行います。ネイティブでイメージを直接読み込む場合は、Session の `LoadPackage` より先に `RegisterGuiArtifactBindings` を呼びます。リソース一覧はソース／デバッグ情報の削除後も呼び出し元に相対的な検索を保持します。焼き込み済みのスクリプトとスタイルにソースは不要ですが、外部アセットにはプロバイダーが必要です。

対応する GUI 有効の Runtime は `gui-check INPUT`、`gui-compile INPUT --output app.vsapp`、`gui-preview INPUT [--frames N] [-- SCRIPT_ARGS]` を提供します。検査とコンパイルは初期化処理を実行せず、UI サービスも作成しません。`--frames N` は自動生成された ML プレビューループだけを制限します。VS エントリーは自身で実行期間を管理し、このオプションを拒否します。

`--platform-backend`、`--graphics-backend`、`--gui-backend` はコマンド／スクリプトより前に指定し、通常の VS アプリにも適用されます。スクリプトの明示指定は呼び出し／環境の設定より優先されます。空値は継承、`auto` は利用可能な提供者の自動選択、`none` はサービスの無効化です。ウィンドウ、GUI、または描画時に必要な Graphics の無効化はエラーになります。そのサービスの既定選択がないネイティブホストでは sdl3／sdl3／rmlui を従来のフォールバックとして使用します。`--backend` は引き続き VM／JIT 用です。

CreateCommandLineService は同じタスクを埋め込みホストへ提供します。借用記述子の使用中は Owner を保持します。SetEngineOptions、SetRuntimeServices、RegisterBindings、ConfigureEngine、UseArtifact が呼び出し単位の構成を共有します。GuiToolsModule はホストサービスへ転送するだけで Core を重複リンクせず、ホストが登録済みなら再利用します。

ネイティブの `FMarkupProgram` API は、ネイティブプロファイルと任意の `IMarkupScriptContext` で引き続き使用できます。VS 型のエクスポート、Builder 生成、独自プロパティ／イベントには完全なコンパイラ経路を使用します。[フレームワークの例](../../../../Examples/Framework/main.vs)を参照してください。Windows/Linux の検証は macOS、Android、iOS、ブラウザーの実行を保証しません。

ホストは `FGuiCompilationConfiguration` で必須の業務コンテキストと値コーデックを宣言できます。リテラル／スタイルのパーサーは `string` を受け取り宣言した値型を返す、通常の可視 VS 関数です。不変のプロファイルはコンパイルの識別情報に含まれます。

共通の例: [main.vs](../../../../Examples/Framework/main.vs)、[page.vswml](../../../../Examples/Framework/page.vswml)、[page.vswcss](../../../../Examples/Framework/page.vswcss)。check/preview はコンパイル済み GUI .vsapp も受け付けます。

## テキスト、フォント、画像と描画

FreeType 2.13.3 と HarfBuzz 10.4.0 を使用し、未変更の Lato Latin を Vero Sans として内蔵します。他の文字には適切なフォントとフォールバックを Context.LoadFont で読み込みます。6 引数版はウェイト、斜体、フォールバック、コレクション番号を指定します。

Text は UTF-8 の文字列で、attribute バインドが可能です。RML として解釈せず、ML 本文や混在するインライン要素は未対応です。フォントと文字装飾、方向、言語は親から継承します。Direction は段落の基底方向 auto/ltr/rtl を指定し、auto は各段落の最初の強い方向文字を使い、なければ LTR になります。Language はシェーピングの言語だけを指定します。RmlUi は Unicode 双方向ランを解析してから成形し、表示行ごとに並べ替えます。UTF-8 値は論理順序を保ち、left/right は物理的な方向を表します。

<a id="image-resources"></a>

`Image` はコンテンツ画像を読み込みます。`BackgroundImage` は `url(...)`、線形・放射・円錐グラデーションとその繰り返しを受け付けます。相対画像は遅延属性 getter も含めて元の VS/ML/CSS ソースから解決します。ネイティブホストは `FResourceReader` と必要に応じて `FImageDecoder` を設定し、デコードした画素は非乗算アルファの sRGB RGBA8 を使います。対応形式は選択した [Image サービス](../../../../../../VeroScriptImage/Resources/Docs/User/ja-JP/index.md)によって異なります。`image/stb` は [Stb](../../../../../../Backend/VeroScriptStb/Resources/Docs/User/ja-JP/index.md) を参照してください。SVG とアニメーションには適切なホストデコーダー／プレーヤーが必要です。

既定の内容レイアウトは画像、テキスト、子ウィジェットの順です。プロパティの変更やツリーの再構築後も、この順序を保ちます。

Image、BackgroundImage、MaskImage の getter は、直接値とバインド値のどちらも解決済みの識別子／URL を返すため、別モジュールに渡しても参照元が変わりません。

<a id="compositor-properties"></a>

`CornerRadius`、`ClipContent`、`Transform`、`Filter`、`BackdropFilter`、`MaskImage`、`BoxShadow` は同じプロパティ・バインディング・スタイル経路を使います。合成操作とリソース上限は選択したバックエンドが決めます。RmlUi プロバイダーについては [RmlUi の合成とリソース](../../../../../../Backend/VeroScriptRmlUi/Resources/Docs/User/ja-JP/index.md#composition-resources)を参照してください。

OnPaint(PaintContext canvas) は背景の後、子の前に内容領域座標で実行します。VS は FillRect、DrawLine、DrawImage、PushClip/PopClip、GetWidth/GetHeight を提供し、ネイティブの `FPaintContext` ではメッシュと変換も利用できます。クリップ座標は固定、変換は後続の形状に適用されます。キャンバスはコールバック中のみ有効で、GUI の変更はできません。

FGuiAssetOptions を BakeGuiApplication に渡すか、gui-compile の --asset を繰り返して画像とフォントを内蔵できます。指定は作業ディレクトリ基準、ソース中の参照はモジュール基準です。SDK は依存ライセンスを同梱します。GUI 契約は 0.1.1 です。

```cpp
@GuiWidget
export class CanvasView : public VS::Gui::Widget
{
protected:
	void OnPaint(VS::Gui::PaintContext canvas) override
	{
		canvas.FillRect(8, 8, 80, 24, VS::Gui::Color(0, 0.4, 1, 1));
		canvas.DrawLine(8, 40, 88, 40, 2, VS::Gui::Color(1, 1, 1, 1));
	}
};
// String attribute getters may call std::i18n using the frame's localization snapshot.
VS::Gui::Widget title = VS::Gui::Widget::Create().Text("Hello, VeroScript!").FontSize(24);
```

```xml
<Widget>
    <style>
        .title { font-size: 24px; color: #ffffff; }
        .surface { border-radius: 12px; clip-content: true;
                   background-image: linear-gradient(90deg, #173b70, #438bd0); }
    </style>
    <Widget class="title" Text="Hello, VeroScript!" />
    <Widget class="surface" Width="240px" Height="80px" />
    <Widget Image="assets/logo.png" />
</Widget>
```

<a id="native-resource-services"></a>

VeroScriptImage と VeroScriptFont は共通のリソースインターフェイスです。VeroScriptStb は `image/stb`、VeroScriptFontBackend は `font/font-backend` と独立した `text-layout/font-backend` サービスを提供します。ネイティブ GUI ホストは `FBackendOptions.Images` / `Fonts` / `TextLayouts` で画像・フォントサービスを渡します。選択する Font とテキストレイアウト契約に対応したホストと SDK を使用し、必要なサービス・バックエンドのパッケージとライセンスを導入してください。

`ApplicationOptions.FontBackend` と `TextLayoutBackend` は独立したサービス選択です。空値はホスト／CLI の選択（`--font-backend`、`--text-layout-backend`）を継承し、`auto` は提供者の選択を委譲し、`none` は任意サービスを無効にします。そのサービスの既定選択がないネイティブホストは `font-backend` にフォールバックします。 ネイティブホストは `FApplicationFactories.Font`／`TextLayout` または `Fonts`／`TextLayouts` を渡します。GUI `0.1.1`、Font `0.1.2`、text-layout `0.1.0` に互換のプロバイダーを使ってください。コンテキストが存続する間はサービスを保持し、共有設定の交換前に全コンテキストを閉じます。`FTextLayout::Create` の最初の引数に選択したサービスを渡します。

## 基本コントロール

ProgressBar、Slider、ScrollBar は GetNormalizedValue を計算する前に GetActualValue を最新の設定範囲に収めます。まだ同期されていない範囲変更にも対応し、不正な範囲は引き続き同期時に拒否します。

`VS::Gui` は Text、Image、Border、Spacer、Separator、Stack、Overlay、Button、CheckBox、ToggleSwitch、ProgressBar、Slider、ScrollBar、ScrollView、TextBox、TextArea を提供します。子、Builder、Attribute、イベント、ML/CSS は共通です。Border は BorderWidth/BorderColor、編集は Value を使います。

Clicked、CheckedChanged、ValueChanged、Scrolled、TextChanged/Submitted はユーザー操作を通知します。未バインド値は更新し、バインド済み Getter は維持して候補値を通知します。Setter と通常の更新はこれらを送信しません。IsXBound() で確認します。継承イベントは CheckBox/TextBox の sender 型を維持します。Minimum/Maximum/Step で正規化し、Step=0 は連続、GetActualValue は表示値です。

マウス、タッチ、Space/Enter、Tab/Shift+Tab、矢印、Home/End、PageUp/PageDown に対応します。PreventDefault は既定動作を抑止し、:checked/:read-only を追加します。ScrollView はホイール/タッチ/キーボードに対応し、ScrollX/Y は要求、GetActualScrollX/Y と GetScrollWidth/Height は最後のレイアウト結果です。

論理コードポイント選択、クリップボード、Undo/Redo、読み取り専用、プレースホルダー、IME を扱います。Select の終端は含まず、一つの論理選択が複数の視覚区間に対応できます。矢印、ポインター判定、IME キャレットは同じ表示行の配置を使い、Backspace/Delete は論理順で削除します。復元、フォーカス変更、再構築でも選択のアクティブ端点とキャレットの向き境界の位置を保ちます。UTF-8 文字列は1 MiB、各履歴は128件/8 MiBです。Application は平台サービスを接続し、組み込みホストは FTextInputServices を提供します。クリップボード失敗時は編集を戻します。実機 IME、アクセシビリティ、完全な書記素移動、他平台は別途検証または実装が必要です。GUI 契約 0.1.1 の利用側を一緒に再ビルドします。[ML](../../../../Examples/Controls/main.vs)、[ページ](../../../../Examples/Controls/page.vswml)、[スタイル](../../../../Examples/Controls/page.vswcss)、[コード](../../../../Examples/Controls/code.vs)。

Checked、Value、範囲/ステップ、ReadOnly、スクロール位置はコード/MLで設定し、スタイルは外観と方向に使用します。TextBoxは未バインドの単行値から改行/タブを除き、GetActualValueで実際の文字列を取得します。

Step が範囲を割り切れない場合も最小値と最大値に到達できます。Slider／ScrollBar のタッチドラッグは祖先のスクロールより優先されます。Select は確定済み文字列のコードポイント位置を使い、未フォーカス時の選択をフォーカスまで保持します。有効な選択指定はプリエディットを終了します。文字列の置換やエディター種別の変更は、ツリー再構築時も undo／redo を消去します。完全なコントロールライブラリにはソースコンパイルのコストがあり、配布時は gui-compile でベイクして起動時のコンパイルを省略できます。

ScrollWidth/Height は Padding を除くコンテンツ範囲です。コンテンツの表示領域を引いた値がバックエンドの実際のスクロール範囲と一致します。組み込みスクリプト UI はフレームごとに一度セッションへ入り、そのスクリプト関数から Context.Update または Application.Tick/Run を呼び出してください。FScriptServices.Invoke は個別のホストコールバック用フォールバックで、セッションへの再入コストが加わります。ネイティブ専用 Context はこのスクリプトブリッジを使用しません。

要求されたスクロール位置が現在のコンテンツ範囲を超える場合、ホイール、キーボード、タッチ操作は範囲内に制限した位置から計算します。次のレイアウトまでの連続イベントも累積します。テキスト選択をドラッグ中のマウスまたはタッチデバイスを取り外すとドラッグを終了しますが、別のデバイスの取り外しは現在の押下を取り消しません。 内側のコンテナがそれ以上スクロールできない場合、ホイール入力はスクロール可能な祖先に渡ります。移動量がゼロの場合は変更を通知しません。デバイス取り外し時は、ホストのテキスト入力サービスがエラーを返すか例外を送出しても、そのデバイスのキャプチャを解除し、失敗を報告します。

## 追加のレイアウト、入力、フローティング要素

Grid は Columns（int64、1–4096）の等幅列と Gap を使い、行高は最大の子に従います。Collapsed はセルを占有せず、子の幅は Grid が決定します。トラックやセル結合はありません。Wrap は横方向に折り返し、縦方向には Layout=Vertical と制限された Height を使います。Splitter は通常の子を最大二つ保持し、浮動要素は除外します。Value は DividerSize を除いた最初の領域の割合です。Minimum/Maximum は [0,1]、Step は既定 0.01、DividerSize は 6。Vertical で上下分割に切り替えます。ドラッグとキー操作は ValueChanged(Splitter,double) を通知し、ReadOnly は変更を禁止します。

RadioGroup.SelectedKey が最も近いグループ内で一意かつ空でない RadioButton.Key を選択します。入れ子のグループは独立し、不一致なら未選択です。RadioButton は IsChecked/Select/Focus と Selected(RadioButton)、グループは SelectionChanged(RadioGroup,string) を提供します。矢印は循環移動、Space/Enter とポインターは選択です。束縛は Getter を保持して候補を通知し、プログラム設定はユーザーイベントを発生させません。

NumberBox は double Value/Minimum/Maximum/Step（既定 0/0/100/1）と ReadOnly、Placeholder、Focus、Select、GetActualValue、ValueChanged/Submitted(NumberBox,double) を提供します。編集中の未完成文字列は Enter またはフォーカス移動まで保持し、有限の十進数・指数、範囲、Step を検証します。不正なら直前の有効値に戻ります。Up/Down は Step（0 の場合 1）、Escape は破棄です。小数点はロケール非依存で、翻訳表示には既存のコア整形を使います。PasswordBox は TextBox を継承し、選択、IME、イベント型を維持して表示をマスクします。Getter は実際の文字列を返します。

Popup は非モーダルで論理親をアンカーとし、通常の配置と祖先クリップから独立した文書レイヤーに表示されます。論理所有権とイベント経路は維持します。OffsetX/Y とビューポートで位置を調整し、必要なら上側に配置します。Open は既定 false。Escape または外側の押下（DismissOnOutside=true）は OpenChanged(Popup,bool) を通知し、その押下を消費します。束縛は維持し、フォーカスは明示設定、閉じると以前の有効なフォーカスを復元します。IME は先に Escape を処理します。ToolTip の子孫も非対話です。AutoOpen=true なら親のホバーと Delay（0.5 秒）で表示し、離脱やマウスリセットで隠します。false なら Open を使います。IsActuallyOpen は最近の状態を読みます。Modal=true ならフォーカスを制限し、Dialog はこの設定を既定にします。

Columns、DividerSize、Vertical、OffsetX/Y、Delay は CSS 対応です。その他の値と束縛方針はコード／ML で指定します。既存のビルダーと型付きイベントを共用します。GUI 契約 **0.1.1** と bExtendedControls に合わせて提供側と利用側を再ビルドします。[入口](../../../../Examples/AdditionalControls/main.vs)、[ML](../../../../Examples/AdditionalControls/page.vswml)、[CSS](../../../../Examples/AdditionalControls/page.vswcss)。

Image/Text の直接コンテンツも子ウィジェットより前の等幅セルになります。Splitter の既定の高さは 120 GUI 単位です。

コンテナーが DividerSize より小さい場合、区切り幅を利用可能な範囲に制限します。RadioGroup のコールバックには選択状態全体を反映し、そこで別の項目を選ぶと元の要求の古い Selected イベントは発生しません。NumberBox はウィンドウのフォーカス喪失や入力状態のリセットでも編集中の値を確定します。確定とホストの入力解除が両方失敗しても元の確定エラーを保持し、ホストの復旧後に次の Update で解除を再試行します。数値の先頭の符号は一つまでです。

FTextInputServices の交換で旧サービスの停止に失敗した場合、旧サービスと論理フォーカスを保持して再試行できます。数値確定のコールバックはコンテキストを閉じたり対象を削除したりできるため、フォーカス切り替えはコールバック後に両者の有効性を再確認します。

## フォーカスの可視性とコールバックエラー

レイアウト完了後、Tab/Shift+Tab と Focus() は最後の座標を使い、内側から外側へ祖先 ScrollView を最小限スクロールして対象を表示します。大きなコントロールが両端を往復することはありません。浮動コンテンツは論理上の祖先をスクロールしません。オフセット変更が必要な場合のみ Scrolled を送信します。バインド済みプロパティは Getter を保持し、アプリがイベント経由で候補値を受け入れます。座標は次回 Update に反映されます。ポインター押下によるフォーカスではスクロールせず、押している対象をポインターの下に保ちます。

直接の Activate/Focus と入力配信は同じ例外処理を使用します。標準例外とその他の例外を gui.callback_exception エラーに変換し、エラーを返す API の外へ伝播させません。確定済みの値やアプリの副作用は巻き戻しません。エラー処理後に再試行してください。

各マウスの最後のウィンドウ座標を保持し、レイアウト後に再度ヒットテストします。ホバースタイルと自動 ToolTip は後続の Update で更新し、ポインター入力イベントは合成しません。キャンセル、デバイス削除、入力リセット、失焦、終了時に対応する座標を破棄し、静止マウス下で移動したコントロールの古いホバーを防ぎます。

## マウスカーソルとキャプチャ

Cursor は継承とスタイルに対応する VS::Gui::Cursor プロパティです。ビルダーと SetCursor は値または std::attribute<VS::Gui::Cursor> を受け取り、ML は Cursor="pointer"、CSS は cursor: pointer を使います。ClearCursor は継承／スタイルへ戻し、明示的な Auto はコントロール既定動作を使います。

Auto はボタン、チェック、切替、ラジオ、スライダー、スクロールバーで手形、入力欄でテキスト、Splitter の区切り線上だけで方向別リサイズを選びます。通常領域と無効なコントロールは矢印です。:disabled { cursor: not-allowed; } などで上書きできます。子要素は操作可能な祖先を使い、ポータルはアンカーボタンの自動カーソルを引き継ぎません。

値：Auto、Default、Text、Wait、Crosshair、Progress、ResizeNWSE、ResizeNESW、ResizeEW、ResizeNS、Move、NotAllowed、Pointer、None。CSS：auto、default、text、wait、crosshair、progress、nwse-resize、nesw-resize、ew-resize、ns-resize、move、not-allowed、pointer、none。None は非表示で、Showcase のグラフは crosshair の例です。

Application は Platform の能力に応じて接続します。組込みホストは任意で `FContext::SetPointerServices(FPointerServices, Error)` を提供でき、コールバックは所有スレッドで実行され GUI を変更できません。未接続でも内部ルーティングは使えます。同じ設定を再送せず、マウス入力後だけカーソルを有効にします。タッチはマウスキャプチャを要求しません。

ドラッグ中はウィンドウ外でもカーソルとキャプチャを維持します。解放、取消、機器取り外し、フォーカス喪失、リセット、終了で状態を解放し、レイアウト変更でも静止マウスを更新します。MouseEntered/MouseLeft はウィンドウ通知で、GUI のルートイベントは mouse-entered/mouse-left です。退出はホバーと ToolTip を消し、ドラッグやキーボードフォーカスは取り消しません。外部ホストも通知を転送します。

サービスの失敗や例外はエラーになります。キャプチャ取得失敗は論理状態を戻し、カーソルは Update で再試行できます。サービス交換と Close は未完の後処理を保持し、解除は表示状態の既定カーソルとキャプチャ解放へ戻します。GUI 契約 **0.1.1** に合わせて提供側と利用側を一緒に再ビルドしてください。

キャプチャの同期はカーソル評価より先に実行するため、カーソルのエラーがネイティブなマウスキャプチャの解除を妨げることはありません。SetMouseCapture のみを提供するホストでは、このサービスのために Cursor を評価しません。公開ポインター操作は、ホストが最後の参照を解放する場合も、コールバック終了までコンテキストを保持します。

ネイティブウィンドウが既に閉じている場合、Application は入力停止、キャプチャ解除、既定の可視カーソルへの復元を完了したクリーンアップとして扱います。入力の有効化や別のカーソルの適用は引き続きエラーになります。

Application.Run の WaitMilliseconds は待機時のフレーム間隔の上限（既定 16 ms）です。前フレームの更新と描画に要した時間を差し引いて待機し、イベントがあれば早く再開します。Tick 自体は待機せず、ホストが実行時機を制御します。未設定のネイティブプロパティはスクリプト側の走査を省略し、設定済み attribute の同期規則は維持します。直接代入と Clear は対応するバインドを解除します。

`Run(beforeUpdate)` と `Tick(beforeUpdate)` は GUI 同期前、通常は入力配送後にコールバックを実行します。ルートにまだレイアウトがない場合、入力配送前の初期レイアウト更新でも実行するため、最初のステップでは2回呼ばれることがあります。アニメーションやモデル更新には、既存のイベント待機予算を使う Run オーバーロードを利用できます。Tick は待機しない単一ステップのままです。例外は伝播し、後処理でもルートビュー参照を更新します。

空の optional を返すものも含め、バインドされた Getter は必要な同期ラウンドごとに評価されます。直接 Setter と Clear はローカルバインディングを置換または解除します。変更通知はフック成功後だけ確定し、失敗したフックは後の更新で再試行できます。独自の値型は通常の言語コピー規則に従います。

GUI はホストの実行設定を使用します。`gui-preview` の前に `--backend=vm` または `--jit-backend="/path/to/Jit" --backend=jit` を指定します。C++ ホストは FEngineConfig.ExecutionBackend と JitBackendFactory（LoadJitBackend の結果）を設定します。JIT ファクトリのない mixed、デバッグ、状態を保持する初期化、有界クリーンアップは VM を使用します。永続セッションの通常の関数は、選択された JIT バックエンドを使用できます。ソースを読み込む前に `VeroScriptSession::ApplyConfig` の戻り値を確認し、設定が拒否された理由を伝播させます。

正式配布名は `vs-gui` と `vs-rmlui`です。Gui は Platform/Graphics/Image/Font、RmlUi は Gui に依存します。バックエンドは選択可能です。ネイティブでは `find_package(VeroScriptGui)`、スクリプトでは `COMPONENTS Bindings` と一致する Core/C++ Runtime SDK を指定します。`VeroScriptGuiFramework` の互換入口も維持します。

## スクリプトとホストの契約

プロパティとイベントの閉じた値型は、bool、固定幅整数、列挙型を含むコアの型および値テンプレート引数を保持します。生成コードは整数の型と範囲を正確に保持し、組み込みコンテナー、参照、関数シグネチャ内の入れ子の型を復元し、固定長配列の次元順序を維持します。GuiWidget は名前空間スコープの閉じた非 static クラスを要求し、ローカルおよび入れ子のクラスは拒否します。GuiMethod とプロパティ変更フックも、値パラメーターを含むテンプレートではないインスタンスメソッドが必要です。

CompileGuiSource/File は FCompileOptions.ConstantEvaluationLimits を保持し、コアの constexpr 評価を使用します。動的 attribute は通常の GUI 同期ラウンドで引き続き評価されます。

Context.Post はコールバックとキャプチャーを実行または破棄まで保持します。キャプチャーしたオブジェクトは遅延クリーンアップまで残る場合があります。Context.SetFocus はコントロールの通知を同期的に送ります。

TextArea は CR、LF、CRLF、NEL（U+0085）、行区切り（U+2028）、段落区切り（U+2029）を論理値に保持し、各終端列を一回の改行として表示します。Backspace/Delete は CRLF をまとめて削除します。選択のコードポイント数とクリップボードは元の列を保持します。単一行コントロールはこれらを除去します。タブの既存フィルターは維持し、プレースホルダーの空白は表示時だけ正規化します。

方向境界の視覚位置は leading/trailing の帰属で区別します。水平移動は段落の基底方向に従って折り返し行をまたぎ、対応する視覚端に入ります。逆方向のランだけの行にも適用し、すべてのシステムエディターと同じ動作を保証しません。完全な書記素移動と連字内部の精密なキャレット位置は別の機能です。

ホストのテキスト入力サービスのコールバックは GUI 状態を参照できます。コンテキストやマウントされたウィジェットの変更、サービスの置換、Close はコールバックが戻るまで拒否します。

コアの公開 ABI またはバイトコードの内部レイアウトが変わった場合、対応する Core と C++ Runtime SDK で GuiScript とネイティブプロバイダーを再構築してください。製品バージョン 0.1.1 だけではバイナリ互換性を保証できません。パッケージのバイトコードも対応するレイアウトで再生成してください。

## 変更通知バインディングと一括更新

`Observable<T>.Get()/Set()` は変更通知可能なデータを保持します。`Set` は `==` で比較し、同じ値では通知しません。可変オブジェクトの内部を変更した場合は継承した `Notify()` を呼びます。他サービスの入力には `ChangeSignal.Notify()` も使えます。単一の値は `AsBinding()`、計算式は明示的な signal 配列を渡す `Binding<T>(getter, inputs)` を使います。通常のスクリプトフィールドは自動追跡しません。 AsBinding の繰り返し呼び出しは弱いキャッシュで存続中の Binding を再利用し、循環所有を作りません。

Getter は読み取り専用とし、変化する全入力を宣言してください。optional の空結果も入力の改訂番号でキャッシュし、同じ Binding を共有するウィジェットでは計算結果も共有します。`Binding<T>::FromAttribute(attribute, inputs)` は明示的に通知契約を選択します。不明な依存、時刻、weak オブジェクトの失効にも通知が必要です。直接渡した `std::attribute<T>` のポーリングとフォールバックは変わりません。

重複通知はネイティブの変更キューでまとめ、対象ウィジェットを一度だけ起動します。`BindingUpdates.Batch(work)` は入れ子に対応し、GUI 処理を次の安全な更新へ送ります。データ自体は直ちに変化し、例外時も代入を巻き戻しません。バッチ内では Context.Update、Application.Tick、Application.Run を呼べません。構築時の初期化と親子ライフサイクルの順序は従来どおりです。

全フレームサービスのスナップショット取得後にキューを公開し、既存のウィジェット同期で対象属性を読みます。同じ結果では変更フックや不要なネイティブ属性の無効化を起こしません。フレーム入力は取得後の通知が必要です。観測できないフレームデータは通常の動的 attribute を使います。プログラム更新は入力イベントを発行せず、SetX/ClearX は旧購読を解除します。IsXBound の attribute セマンティクスは変わりません。破棄・最終解放で解除し、アンマウントは設定を保持します。Getter 失敗時は確定値を維持し、既存のライフサイクル処理で再試行します。FromAttribute は渡された設定を保持し、元の attribute の設定を後から交換しても Binding は交換されません。 FromAttribute は既定 attribute やネイティブスタイルの独立した変更に対応するためフォールバックのポーリングを維持します。渡された Getter の結果自体は通知リビジョンでキャッシュします。

VS、生成 Builder、.vswml 内の VS コードで同じ setter を使えます。既存の `bind:` はポーリングを維持し、.vswcss はスタイル層です。通知とキュー処理は GUI 所有スレッドで行います。

```cpp
auto amount = VS::Gui::Observable<double>(0.25);
VS::Gui::ProgressBar bar = VS::Gui::ProgressBar::Create().Value(amount.AsBinding());

// Setters need a mutable value capture; read-only getters do not.
VS::Gui::BindingUpdates::Batch([amount]() mutable -> void
{
	amount.Set(0.4);
	amount.Set(0.6);
});
// The next Context.Update/Application.Tick commits 0.6 once.

array<VS::Gui::ChangeSignal> inputs = {amount};
VS::Gui::Binding<string> caption = VS::Gui::Binding<string>([amount]() -> string
{
	return to_string(amount.Get());
}, inputs);
VS::Gui::Text label = VS::Gui::Text::Create().Text(caption);
```

## コレクション、ナビゲーション、ダイアログ

ListView/ListItem と TreeView/TreeItem は文字列 SelectedKey による単一選択、FindItem、GetActualSelectedKey、Select、Activate、SelectionChanged/ItemActivated を提供します。Key の省略時は Id、次に順序番号です。変更するデータには安定した一意の Key を指定します。Select は既存項目の選択要求、Activate は対話可能性も確認します。矢印/Home/End で移動、Enter/Space で実行します。ツリーは Expanded attribute、IsActuallyExpanded、ExpandedChanged、左右キーの展開/折り畳みを追加します。子はマウントされたままで Visibility を保持します。

TableView は TableColumn/Row、Key、正のピクセル ColumnWidth、Sortable、列ごとに1個の widget セルを使います。SetColumns/SetRows、GetCellText/SetCellText、SortBy を提供します。UTF-8 文字列の安定順序ソートで、数値/ロケールを推測せず、行の同一性と選択を保持します。AutoSort=true は見出しクリックを扱い、ColumnClicked/Sorted 等を通知します。データソースの固定行高仮想化を選択できますが、表計算編集は含みません。手書きの子 widget は一括 SetChildren/SetRows を使います。

ComboBox は ListItem、SelectedKey/Open/Placeholder/DropDownHeight、Select/SetItems/GetList/RequestOpen と各イベントを提供し、表示 Text は選択項目から管理します。TabView の TabItem は Key/Title を持ち、未束縛の初期選択は最初の有効ページです。Select/SelectionChanged で切り替えてもページはマウントを維持し、Visibility を上書きしません。見出しは左右/Home/End に対応します。

Menu/MenuItem は入れ子、Command、Checkable/Checked、Invoked、Invoke/OpenMenu/CloseMenu を提供します。サブメニューは下/右ポータルで、外側の1回の押下で活動中のチェーンを閉じて押下を消費します。OS ネイティブメニューやアクセラレーターはホスト側の責務です。

Popup の既定は非モーダルです。Placement=below/right/center、MatchAnchorWidth、DismissOnEscape と GetDismissReason（escape/outside/空文字）を追加します。Dialog は Modal=true、中央、DismissOnOutside=false。Show/Close(result)、Closed(Dialog,string) を既存ループで使用し、背景入力と Tab フォーカスを制限して以前の有効フォーカスを戻します。Open の束縛元は保持し、次の同期で候補をモデルに戻します。

Builder は OnXxx、インスタンスは AddXxx です。複合 widget の SetChildren は利用者コンテンツへ挿入します。GetChildren/親は実際のツリー、GetContentChildren はコンテンツ子を返します。構造変更は getter/更新の外、または Context.Post を使います。Grid は等幅のまま、独立列幅は TableColumn です。[例](../../../../Examples/CompositeControls/main.vs)、[ML](../../../../Examples/CompositeControls/page.vswml)、[CSS](../../../../Examples/CompositeControls/page.vswcss)。

`MenuItem.IsActuallyChecked()` は入力候補のチェック状態を返します。連続操作は候補を反転し、次の同期で束縛された Checked モデルに戻ります。Closed は OpenChanged コールバック前の結果を保持し、コールバック中の再表示でもその終了結果は変わりません。

データリストと表は固定行高の自動仮想化に対応します。`ListView.SetVirtualItems(labels,rowHeight=28,overscan=2)` と `TableView.SetVirtualRows(rows,rowHeight=28,overscan=2)` は表示範囲の行だけを生成・再利用します。`SetVirtualItemText(index,text)` と `SetCellText(key,column,text)` は画面外のデータも更新します。独自の行は `SetVirtualSource(keys,rowHeight,factory,overscan=2)` を使用します。factory は `(string key, CollectionItem recycled)` を受け取り、同じ行または未接続の新しい行を返します。表では TableRow が必要です。以前のキーの状態はリセットしてください。`RefreshVirtualItems()` は可視行を再バインドし、`SetVirtualOrder(keys)` は全キーの順序を変更します。組み込みデータ表は安定した文字列ソートを維持します。独自ソースは ColumnClicked を処理して順序を提供します。

`GetItemCount()` は論理行数です。`GetItems()`、`GetRealizedItemCount()`、`FindItem()` は実体化した行のみを対象にします。選択キーは保持され、`ScrollToItem(key)` で画面外の行を表示できます。フォーカス・押下・ポインターキャプチャを持つ行は保持されます。実体化は Context.Update の安全な遅延処理段階で実行し、レイアウト中に検出した変更は次回更新で処理します。行は固定の border-box 高さで内容をクリップし、padding は高さに収まる必要があります。キーは空でなく一意、overscan は 0..256、総行高は 16,777,216 論理ピクセル以内です。factory や getter 内でソースの交換・並べ替えはできません。factory の例外は伝播し、後続更新で再試行できます。返した行の所有権はビューへ移ります。従来の子ウィジェット、SetItems、SetRows のライフサイクルは変わりません。可変行高とツリーの仮想化は対象外です。

初回レイアウト前の ScrollToItem は実際の表示領域が判明してから補正します。その後に明示したスクロール位置を優先します。

## Context のバックエンド既定値

標準のアプリケーションファクトリブリッジを使う `VS::Gui::Context("", width, height)` では、コンストラクターで明示したバックエンドが優先し、指定がなければ呼び出し設定、次にアプリケーション／環境ファクトリの既定値を使います。どちらにも指定がなければ `rmlui` に戻ります。ホストの選択コールバックが空文字列を返す場合は、その階層で未指定という意味で、無効化ではありません。明示した `auto` と `none` は `rmlui` に置き換えません。環境設定に保存した明示的な空値は共通規則どおり `none` に正規化します。独自のホスト Context ファクトリは名前の処理を自身で管理します。

<a id="context-callback-reentrancy"></a>

## Context コールバックの制限

テーマ、フレームサービス、ポインターサービス、テキスト入力サービスの置換中に呼ばれるコールバックから Context の設定を変更しないでください。同じ Context の再帰的な設定は拒否されます。所有スレッドで後から行う処理には `Post` を使用してください。

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/Framework.md) · [fr-FR](../../fr-FR/Reference/Framework.md) · [ja-JP](Framework.md) · [ko-KR](../../ko-KR/Reference/Framework.md) · [zh-CN](../../zh-CN/Reference/Framework.md) · [zh-TW](../../zh-TW/Reference/Framework.md)
