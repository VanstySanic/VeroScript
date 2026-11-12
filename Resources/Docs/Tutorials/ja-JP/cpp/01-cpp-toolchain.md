# 18: C++ ツールチェーン モジュールを選ぶ

このチュートリアルでは、ネイティブ統合の目的に応じて embedding、BindGen、
Transpiler、AOT を選び、最小のワークフローを実行します。

前提条件は、対応する `veroscript`、Cpp BindGen、Transpiler が実行パス上にあり、作業ディレクトリへ書き込めることです。以下のパスは利用者の入力を表します。対応する宣言を `Include/Types.hpp` と `Include/Api.hpp`、定義を `Source/Types.cpp` と `Source/Game.cpp` に準備し、有効な `script.vs` を用意するか、すべてのパスを既存ファイルに置き換えます。このディレクトリからコマンドを実行してください。期待する出力は統合 `bindings.json`、API リファレンスとマニフェスト、ソースマップ付き VeroScript モジュール、AOT の `generated.cpp` です。ファイルを生成しても実行時のホストコールバックは登録されません。

## 手順 1: 境界を選ぶ

[C++ SDK リファレンス](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/index.md) を参照してください。
実行中のセッションと script 呼び出しには `VeroScriptCpp`、C++ 宣言から catalog
を作るには `VeroScriptCppBindGen`、対応する C++ source を VeroScript に変換する
には `VeroScriptCppTranspiler`、VeroScript を native build 用 C++17 に変換する
には `VeroScriptCppAot` または `veroscript aot` を使います。

BindGen は native API を記述し、Transpiler は source の動作を変換し、AOT は
VeroScript を C++ に変換します。目的を混同しないでください。

## 手順 2: catalog を作る

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

catalog は compiler/tooling 用、API reference は人向けです。実際の callback は
host が登録します。

```powershell
veroscript bindings validate build/bindings.json
veroscript --check script.vs --binding-catalog build/bindings.json
```

複数の入力では project mode を使い、出力前に宣言をマージします。

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp --output-dir build/generated --manifest build/generated/transpile.manifest.json
```

## 手順 3: 変換と AOT

```powershell
VeroScriptCppTranspiler Source/Types.cpp `
  -o build/Types.vs `
  --source-map build/Types.vs.map.json

veroscript aot script.vs -o build/generated.cpp
```

複数入力では project mode を使用し、manifest と source map を生成 source と一緒に
保持します。AOT の結果は executable ではなく C++17 source です。runtime linkage、
callback の寿命、host state の同期を対象 project で確認してください。

決定的な複数ファイルの C++ 出力には `FCppAotSourceBundle` API を使います。エントリーソース、生成フラグメント、マニフェストを一緒に保持してください。これらは対象 C++ プロジェクトへの入力であり、ビルド済み実行ファイルではありません。

生成されたトップレベル グローバルとクラス static field は native worker ごとに
分離されます。AOT は各 worker のグローバル アクセサーを宣言順に `main` より前に
初期化します。グローバルを暗黙のスレッド間共有機構として使わず、channel または
同期された host state を使用してください。

メンバー ラムダがレシーバーを使用する場合、AOT はソース型のセマンティクスを
維持します。クラス レシーバーは有効な保持オブジェクトとして扱われ、構造体
レシーバーは値のスナップショットになります。ネストしたラムダはすでに
キャプチャされたレシーバーを保持し、キャプチャなしのラムダは `this` を使用
できません。C++ AOT は、レシーバーが破棄中であるため、デストラクター本体から
クラス レシーバーをキャプチャすることも拒否します。

## 演習

1. catalog の項目と host の登録を比較します。
2. 2 つの C++ 入力を project mode で変換し、manifest を確認します。
3. channel を使う script を AOT 化し、同期が必要な host state を記録します。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/cpp/01-cpp-toolchain.md) · [fr-FR](../../fr-FR/cpp/01-cpp-toolchain.md) · [ja-JP](01-cpp-toolchain.md) · [ko-KR](../../ko-KR/cpp/01-cpp-toolchain.md) · [zh-CN](../../zh-CN/cpp/01-cpp-toolchain.md) · [zh-TW](../../zh-TW/cpp/01-cpp-toolchain.md)
