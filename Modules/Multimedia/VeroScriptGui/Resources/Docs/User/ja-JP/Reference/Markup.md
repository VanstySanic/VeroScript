# VSWML と VSWCSS

> このページはネイティブの `FMarkupProgram` 読み込み API を説明します。VS の import、注釈 Schema、Builder、独自プロパティ／イベント、型付きファクトリは[フレームワークガイド](Framework.md)を参照してください。以下の制限は、このネイティブ API に適用されます。

公開 API `VeroScriptGuiMarkup.h` でマークアップを型付きプロパティとルーティングイベントを持つ `FWidget` ツリーへ読み込み、既存のコンテキストに接続します。テキスト、フォント、画像、カスタム描画、基本コントロールに対応します。ネイティブローダーが既定で登録する型は `Widget` のみです。組み込みコントロールと型付きプロパティ・イベントには Schema 対応の VS コンパイル経路を使うか、ネイティブ型ファクトリーを明示的に指定します。この形式は特定のレイアウト・描画バックエンドを必要としません。

## 読み込みと構文

`ReadMarkupFile` で入力を取得し、`FMarkupCompileOptions.Read` を設定して `FMarkupProgram::Compile`、`Create` を呼び、既存の `FContext` にルートを渡します。診断は `FMarkupDiagnostic::Format` で取得します。`src` は作業ディレクトリに依存せず宣言ファイルを基準に解決します。カスタム `FMarkupReader` は不透明なモジュール・リソース ID も扱えます。`GetDependencies` は直接読み込んだソースの不変スナップショットを公開し、VS の推移的 import は Core が管理します。コンパイルにウィンドウや GPU は不要です。`Create` ごとにウィジェットとスクリプト状態は独立し、コンパイル済みプログラムの破棄後もインスタンスは存続します。再読み込みと再コンパイルは明示的で、自動的な状態移行はありません。

```cpp
#include "VeroScriptGuiMarkup.h"
#include <utility>

bool LoadMarkupUi(const std::string& Entry,
				  std::shared_ptr<VeroScript::Gui::FWidget>& OutRoot,
				  std::string& Error)
{
	using namespace VeroScript;
	Gui::FMarkupSource Source;
	Gui::FMarkupDiagnostic Diagnostic;
	if (!Gui::ReadMarkupFile({}, Entry, Source, Error))
	{
		return false;
	}
	Gui::FMarkupCompileOptions Options;
	Options.Read = Gui::ReadMarkupFile;
	auto Program = Gui::FMarkupProgram::Compile(Source, Options, Diagnostic);
	if (!Program)
	{
		Error = Diagnostic.Format();
		return false;
	}
	auto Root = Program->Create({}, Diagnostic);
	if (!Root)
	{
		Error = Diagnostic.Format();
		return false;
	}
	OutRoot = std::move(Root);
	Error.clear();
	return true;
}
```

返されたルートを `SetRoot` で既存の `FContext` へ設定し、`Update` が成功してから描画してください。読み込み関数自体はウィンドウ、デバイス、バックエンドを作成しません。

UTF-8、単一ルート、大小文字を区別する XML 形式で、引用符付き属性、コメント、CDATA、XML／数値実体を使用できます。bind: と on: は組み込みで xmlns は不要です。登録済みの任意の Widget をルートにでき、各 Widget に複数の子を持てます。

ルートは複数の import/style と最大一つの script を持てます。style は src またはインラインです。通常ルートはインスタンス、component は type と一つの template を必須とし、mark/base/id は省略可能です。型の短名と mark の両方をタグにできます。base は import 済み ML コンポーネントかネイティブファクトリで、循環は禁止です。

プロパティの元名と Mark は両方使えますが、同一プロパティへの重複指定は禁止です。ML の id/class は小文字、値は大小文字を区別します。テンプレートごとに ID／スタイル／スクリプトの非公開スコープがあり、Find(componentRoot, id) で内部を検索します。利用側で追加した子は宣言側スコープを維持します。ネイティブファクトリ内部も独立し、生成後の任意 AddChild は ML スコープへ自動登録されません。

通常属性は固定値、`bind:gap="{ Count + 1 }"` またはメソッド名は getter、`on:pointer-down="{ Count = Count + 1; }"` またはメソッド名はイベント処理です。不明な型／属性／イベント、直接テキスト、未対応構文は診断されます。

## スタイルとスクリプト

セレクタは型／Mark、*、class、id、複合、子孫、>、カンマ列に対応し、状態は root/hover/active/focus/focus-within/disabled/checked/read-only です。優先順位は既定→テーマ→テンプレート→利用側→ローカル値／getter。同一スコープでは詳細度とソース順です。SetTheme/SetClasses/SetId は次の Update に反映されます。Set は getter を置換し、Clear は現在のスタイル／既定値へ戻します。getter エラーは更新を失敗させます。 スコープの優先順位は、異なるスコープ間の詳細度比較より先に適用します。`FMarkupProgram::SetTheme` はインスタンスのランタイムのテーマを設定・解除します。CSS ではスタイル指定可能なプロパティのみ使用できます。基底プロパティは getter バインディングを提供し、具体的なコントロールでは GuiProperty スキーマが通常値とバインド可能な Attribute プロパティを区別します。

長さは数値、px/dp、%、auto、padding は非負の一～四値です。色は sRGB から線形 RGBA に変換されます。変数は親の計算済み値を継承し、var と fallback に対応します。循環、欠落、型不一致はソース位置付きで診断します。変数継承は同一 ML ランタイム内のみで、フォントとテキストは後述の規則に従って継承し、他の通常属性には HTML 継承を追加しません。!important、@ 規則、兄弟／属性セレクタ、CSS アニメーション、任意 RCSS は未対応です。状態更新は Update 時で、Platform の取消／失焦／リセットも転送してください。

静的 ML/CSS は Gui のみ必要です。VEROSCRIPT_GUI_SCRIPTING=ON と VeroScript::GuiScript を使用し、FMarkupScriptOptions.Engine を構成して MakeMarkupScriptFactory を Create に渡します。同一版 C++ Runtime/Core が必要です。インライン script はインスタンス別のクラスメンバーです。通常ページは `<script src="logic.vs" type="App::Logic" />` も使用できます。ツリー構築後にスクリプトを構築し、バインドを設置します。その時点ではマウント／初回レイアウトは未完了です。 ページごとに独立した状態オブジェクトを作ります。コンポーネントの状態は `VS::Gui::Widget` を継承し、既存のネイティブルートを参照します。コンストラクターでは初期化済みの `elements` を参照できますが、マウントとレイアウトはまだ完了していません。

このネイティブ API では、スクリプトブロックの代わりに `FMarkupProgram::Create` の第 3 引数へ `IMarkupScriptContext` を渡すこともできます。両方は併用できません。コンポーネントは自身のコンテキストを使用します。

組み込み名は self/context/elements、イベントでは sender/event/arg0/arg1 も使えます。外部 context も通常の VS アクセス制御とローカル変数の遮蔽を守ります。elements.Find は空を返せ、Require は欠落時に失敗します。名前指定メソッドは @GuiMethod が必須、Mark は省略可能です。GUI の注釈名とそのオプション名は ASCII の大小文字を区別しません。Core の注釈名は既定では区別します。リフレクションの `find_arguments(name)` は保存された引数名と完全一致で照合し、Mark の値も大小文字を区別します。ルーティング処理は void で、引数なし、Event、Widget sender、または両方です。Event は入力データと弱い Widget 参照を保持し、伝播／既定動作の制御は同期配信中のみ有効です。

attribute.vs、std::attribute.get/set、Core のローカライズ／書式化は通常 VS 呼び出しで利用します。Text と編集コントロールは提供済みです。ML 文字列を暗黙にローカライズキーとして解釈せず、通常の Core ローカライズ／書式化呼び出しにテキストをバインドします。公開済み ModuleEnvironment カタログは変更せず Gui と合成します。操作は所有スレッドで行い、最終参照が別スレッドで解放された場合は終了前に CollectMarkupScripts を呼びます。

ネイティブファクトリは、子孫を含め所有スレッド上で新規作成した Widget を返す必要があります。初期化済み、または既存 ML インスタンスに属するノードは拒否されます。バインディング式／ブロックでは `self`、`context`、`elements`、`sender`、`event`、`arg0`、`arg1` は予約名です。ローカル変数、lambda 引数、foreach／catch 変数、ローカル型エイリアスによる再宣言はできません。

基本、コレクション、ナビゲーション、モーダルダイアログの各コントロールを提供します。GUI 有効の CLI は `gui-check`、`gui-compile`、`gui-preview` を提供します。コマンド、スキーマ、型付きファクトリーは[フレームワークガイド](Framework.md)を参照してください。[カウンター](../../../../Examples/Markup/counter.vswml)、[コンポーネント](../../../../Examples/Markup/card.vswml)、[共通スタイル](../../../../Examples/Markup/layout.vswcss)の例は統合 Resources 文書に含まれます。機能用 SDK アーカイブにサンプルディレクトリがあるとは限りません。

## テキスト、フォント、画像と描画

Text は UTF-8 の文字列で、attribute バインドが可能です。RML として解釈せず、ML 本文や混在するインライン要素は未対応です。フォントと文字装飾、方向、言語は親から継承します。Direction は段落の基底方向 auto/ltr/rtl で、auto は最初の強い方向文字を使い、なければ LTR です。Language は成形言語のみを指定します。RmlUi は混在方向を配置し、UTF-8 値と選択位置は論理順序を保ちます。ML の `Direction="auto" Language="ar"` と CSS の `direction: auto; language: ar;` は同じ属性で、left/right は物理方向です。

[GUI フレームワークの契約 — テキスト、フォント、画像と描画](Framework.md#image-resources) を参照してください。

既定の内容レイアウトは画像、テキスト、子ウィジェットの順です。プロパティの変更やツリーの再構築後も、この順序を保ちます。

Image、BackgroundImage、MaskImage の getter は、直接値とバインド値のどちらも解決済みの識別子／URL を返すため、別モジュールに渡しても参照元が変わりません。

[GUI フレームワークの契約 — テキスト、フォント、画像と描画](Framework.md#compositor-properties) を参照してください。

[Framework](Framework.md)

| プロパティ | CSS 名 | スタイル指定 |
| --- | --- | --- |
| Text, Image | text, image | 不可 |
| FontFamily, FontSize, FontWeight, FontStyle | font-family, font-size, font-weight, font-style | 可 |
| TextColor, TextAlign, WhiteSpace, TextOverflow | color, text-align, white-space, text-overflow | 可 |
| LineHeight, LetterSpacing, Direction, Language | line-height, letter-spacing, direction, language | 可 |
| CornerRadius, ClipContent | border-radius, clip-content | 可 |
| BorderWidth, BorderColor | border-width, border-color | 可 |
| Cursor | cursor | 可 |
| BackgroundImage, MaskImage, BoxShadow | background-image, mask-image, box-shadow | 可 |
| Transform, Filter, BackdropFilter | transform, filter, backdrop-filter | 可 |

[GUI フレームワークの契約 — テキスト、フォント、画像と描画](Framework.md#native-resource-services) を参照してください。

Cursor は `VS::Gui::Cursor` 型です。ML では `Cursor="pointer"`、CSS では `cursor: pointer` を指定します。継承と Attribute バインドに対応しています。`Auto` はコントロール既定の動作を選択し、`None` はカーソルを非表示にします。ポインターサービスとウィンドウ境界通知は [Framework](Framework.md) を参照してください。

カスタム property は `--` で始まります。`var(--name, fallback)` は継承値または入れ子にもできる fallback を使います。次の gap は 8 pixels で、`--space` を削除すると fallback の 4 pixels になります。

```css
:root
{
	--space: 8px;
	gap: var(--space, 4px);
}
```

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/Markup.md) · [fr-FR](../../fr-FR/Reference/Markup.md) · [ja-JP](Markup.md) · [ko-KR](../../ko-KR/Reference/Markup.md) · [zh-CN](../../zh-CN/Reference/Markup.md) · [zh-TW](../../zh-TW/Reference/Markup.md)
