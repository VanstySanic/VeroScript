# C# toolchain と package 契約

## 厳密なツールの対応範囲

entry point がある場合、C# Transpiler は class 名の heuristic ではなく C# compiler が選んだものを lower します。static type に宣言された parameterless `static void Main()` または `static int Main()` だけを受け入れ、ambiguous、argument 付き、instance container の entry point は `VSCT2001` で失敗します。Binding Catalog v1 には host-index lowering がないため、BindGen は C# indexer を拒否します。どちらの tool も未対応の entry/member を黙って省略せず、validation 成功後にだけ output を commit します。

## framework と SDK

`VeroScriptCSharp` は `net8.0`/`net9.0`、C# language version 12.0、XML documentation、package API validation を使用し、runtime package は `VeroScript.CSharp` です。CMake は installed .NET SDK を確認し、両 framework の managed test を実行します。frontend、Transpiler、BindGen も `net8.0`/`net9.0` target ですが、build-time tool には current CMake toolchain の .NET 9.0 以降の SDK が必要です。これは runtime package の supported framework とは別です。パッケージ化された command-line tool は、要求された .NET 8/9 runtime がインストールされていない場合に `Major` roll-forward を使用し、サポート対象の新しい major runtime で実行できます。

## パッケージの内容

実行時 `.nupkg` には net8.0/net9.0 のマネージドアセンブリ、XML API 文書、buildTransitive の統合ターゲット、ライセンスを含めます。デバッグシンボルは `.snupkg` で別途配布します。ソースの README.md、API.md、MOBILE.md は同梱せず、利用ガイドは独立した Resources 文書から取得します。ネイティブ Interop ランタイムは引き続きホスト側の配置対象です。CMake はフレームワークごとのマネージド出力と生成 NuGet パッケージを SDK 領域にインストールします。

## C# AOT source bundle

## 生成した登録コードの検証

`VeroScriptCSharpAot` は single generated compilation unit と deterministic `FCSharpAotSourceBundle` を公開します。bundle は runtime support、lowered program、entry point の relative source、SDK project、machine-readable `ManifestJson` を含みます。`BuildCSharpSourceBundle` は emitted C# を書き換えずに package 化します。source/file/checked-program entry は import path、implicit import path、`Defines`、circular-import、project-wide-access を共有し、options overload は host catalog/codegen setting も取ります。load、semantic check、lowering、bundle construction の失敗は partial build-ready bundle を返しません。relative path/manifest field は output contract で翻訳しません。

BindGen package は catalog generation、generated registration compile/runtime test、C++ catalog import test、package verification を含みます。generated C# または catalog shape の変更後も全 consumer が compile/run しなければなりません。

## ソースのトランスパイル

`VeroScript.CSharpTranspiler` と `VeroScript.CSharpTranspiler.Tool` は strict C#-to-VeroScript conversion を提供し、project mode は一つの Roslyn compilation として source を解決し、semantic cross-file import/source map/deterministic manifest を emit して atomically commit し、既定で real VeroScript validator により compile します。

## ツールの依存ライブラリ通知

C# フロントエンド、BindGen CLI、Transpiler CLI の出力は、Roslyn の原文通知を `licenses/` に保持します。フロントエンドの NuGet と両 .NET ツールパッケージにも含まれます。これらはプロジェクトの MIT 通知を補足します。Roslyn のバージョンを変更したら[通知の出典](../../../Licenses/README.md)を更新し、`Roslyn-LICENSE.txt` と `Roslyn-ThirdPartyNotices.rtf` を RTF のエンコードを含めて完全なまま保持してください。パッケージ検証は原文との一致を確認します。上流のライセンス本文は翻訳しません。

## 標準コンテナーのジェネリック引数数

[C# フロントエンド — 標準コンテナーのジェネリック引数数](../../User/ja-JP/frontend.md#container-mapping-arity) を参照してください。

## 整数の変換と数値の対応範囲

Transpiler は整数の加算、減算、乗算、符号反転、増減、複合代入、整数キャストについて、解決された幅で回り込む C# の既定 unchecked 動作を保持します。シフト数は31または63でマスクします。複合代入は右辺より先に対象とその以前の値を一度だけ評価し、自動プロパティはフィールドとして出力します。整数の除算と剰余は、除数ゼロまたは符号付き最小値/-1 のオーバーフローで捕捉可能なスクリプト例外を送出します。整数値の `double` 定数も浮動小数点型を保持します。CLR と VeroScript の実行結果を比較するテストで確認します。

[C# トランスパイラー — 整数の変換と数値の対応範囲](../../User/ja-JP/transpiler.md#numeric-and-console-boundaries) を参照してください。

ツールパッケージ検証では Transpiler をインストールし、ソースを生成して実際のコンパイラで検証し、実行します。符号なし除算と剰余、符号付き整数のラップアラウンド、真偽値の文字列と改行、捕捉可能なゼロ除算、および検証無効時でも未対応の checked 文字列定数が拒否されることを確認します。

C# AOT の浮動小数点数から整数への変換はゼロ方向に切り捨て、キャスト前に二進数の値が対象範囲内であることを確認します。表現可能な 64 ビット整数値を十進数への丸めなしに保持します。符号なしの変換先では負の小数を含む負の入力を拒否し、NaN、無限大、範囲外の値も失敗します。整数の境界値はマネージド実行と NativeAOT 実行で検証します。

[C# AOT — 明示的な除外](../../User/ja-JP/aot.md#aot-floating-conversions) を参照してください。

C# トランスパイラーは整数と浮動小数点数の混合比較前に Roslyn のオペランド昇格を適用し、コアの比較前に C# の float 丸めを維持します。

数値の条件分岐には Roslyn が選択した暗黙の変換を適用し、整数の幅と float の丸めを維持します。選択された分岐のみを評価します。条件分岐内でも、定数でない 64 ビット整数から float への変換は拒否します。

[C# runtime と Interop adapter — 保持された callable の破棄](runtime-and-interop.md#managed-marshalling-maintenance) を参照してください。

[Android と iOS / MAUI](../../User/ja-JP/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

モバイル対応パッケージも同じプロジェクトとパッケージ名を使用します。`VeroScriptIncludeMobile=true` により .NET 8/9 と .NET 10 Android/iOS のターゲット、および検証済みネイティブ SDK を含めます。

`core.vs` が取り込む未使用の実装を削除する際、解決済みのバイトコード呼び出しから実行時の依存関係を、ジェネリック本体から自由シンボルの依存関係を保持し、型の依存関係を AST の宣言とスコープ内の参照から収集し、公開名を Core の別名テーブルで照合します。`std::sqrt`、`std::algorithm::sort` などの呼び出しや `std::delegate` などの型に必要な実装は保持されます。ローカル名、インスタンスメンバー名、通常の文字列の内容はグローバル関数への参照として扱いません。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
