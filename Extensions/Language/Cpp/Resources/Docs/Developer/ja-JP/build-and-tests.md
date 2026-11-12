# C++ build と test の契約

## ターゲット

C++ group は `VeroScriptCpp`、`VeroScriptCppAot`、`VeroScriptCppFrontend`、`VeroScriptCppBindGen`、`VeroScriptCppTranspiler` で build します。library target は focused test source を除外し、test は対象 public target を link します。

## テストの分類

test は public-header compilation、engine/session/JIT integration、AOT source/bundle、frontend parse/merge/type mapping、BindGen catalog/reference、Transpiler source/map を対象とします。public result、artifact、diagnostic、subset rule の変更には該当 test category を拡張します。

## インストールの境界

public header と generated library は install input です。README と generated API reference は document artifact であり、implementation header ではありません。installed consumer は build-tree path に依存しません。

独立 C++ package は required `Runtime` module に `VeroScriptCppConfig.cmake` を提供します。consumer は matching Core SDK root と C++ package `Runtime` を `CMAKE_PREFIX_PATH` に加え、`find_package(VeroScriptCpp <version> EXACT CONFIG
REQUIRED)` を呼び、`VeroScript::VeroScriptCpp` を link します。optional `Tools`/`Aot` component は sibling module が installed の場合だけ imported target を公開します。

package は同じ release build の Core library SHA-256 identity を記録します。別 build の同 version Core SDK との混在は configure に失敗しなければなりません。release pipeline は archive/installer 前に staged package path のみで `Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal` を compile/run し、repository include/library path を使いません。

## メモリ統合の検証

`VeroScriptCppMemoryIntegrationTests` は Engine の制限、Session の連続呼び出し、ネイティブサービス、ホットリロード後の同一性と制限超過時の復元、ドメイン終了前のクリーンアップ、Session 破棄後のリース解放を検証します。
`CppMinimal/memory.cpp` はインストール済み Runtime の利用例です。メモリ契約を変更した場合は両方に加え、`VeroScriptMemoryTests`、Session／デバッグ／JIT テスト、AOT の拒否テストを実行します。省略可能なバックエンド引数で実際の JIT モジュールを検証します。[ホストメモリ規則](../../User/ja-JP/embedding.md)を参照してください。

## クリーンアップと AOT の回帰テスト

`VeroScriptCppSessionDebugTests` は消去、置換、ホットリロード無効化、callable 解放、破棄中のコールバック再入を検証し、新しい保持ルートの拒否とクリーンアップ後の再利用も確認します。`VeroScriptCppJitContractTests` は構造化されたネイティブ登録のキャッシュ無効化を検証します。公開前には `VEROSCRIPT_AOT_REQUIRE_NATIVE=1` を設定して `VeroScriptCppAotHelloTests` を実行してください。生成 C++ をコンパイル・実行して VM と比較し、自増・自減の副作用、寿命、ジェネリックの数値幅、オーバーフロー失敗を確認します。`VeroScriptCppAotEmitTests` はジェネリック特殊化を含むユーザー型の未対応の自増・自減が明示的に拒否されることも検証します。

## モジュールマクロの統合ゲート

`VeroScriptCppEngineIntegrationTests` はホストの初期マクロ、ソースオーバーレイ、明示的／再エクスポート／暗黙の可視性、ソースとファイルのキャッシュ、依存変更による無効化、ホットリロード、依存読み込み失敗時の旧プログラム保持、物理位置の診断、デバッグ情報を除去したライブラリのマクロを検証します。`VeroScriptCppAotHelloTests` は公開可変引数マクロと呼び出し側の展開をネイティブ／VM で比較します。`CppMinimal/main.cpp` はステージ済み SDK ヘッダー経由で公開マクロのメタデータを使用します。モジュールの前処理やバイトコードの変更時に実行してください。

## アノテーション統合の検証

`VeroScriptCppAnnotationTests` は VM／Mixed／実際の JIT で Engine の検査と実行、Session のソース／ファイル読み込みとホットリロード、非致命的な診断、依存入力のキャッシュとキャッシュ不可の処理を検証します。読み込み、独立コンパイル、再読み込み中のコールバック再入拒否と、キャンセル後の状態保持・再利用も確認します。`VeroScriptCppAotEmitTests` は生成予算の拡張が二度目のデフォルト準備によって拒否されないことを確認します。ネイティブ `VeroScriptCppAotHelloTests` は生成された仮想メソッド、別ファイルのジェネリック本体、宣言と実装のスコープを比較します。SDK サンプルはインストール済みヘッダー、プロセッサー登録、Session／AOT API を検証します。

## プロジェクト変換とファイルトランザクション

`VeroScriptCppTranspilerTests` は最終モジュール、実装のスコープ、仮想呼び出し、既定引数、元ファイルへの診断を検証します。`VeroScriptCppTranspilerNativeTests` は C++ → VeroScript → C++ を実際にコンパイルして実行します。`VeroScriptCppFrontendTests` は未対応構文の拒否、パスの正規化、ハードリンク、書き込み前の衝突防止を検証します。バッチ処理、CLI 入力保護、出力トランザクションは `FFilePathSet` を共有し、ファイル ID を取得できない場合は従来の比較を維持します。

## ホスト端末の統合ゲート

`VeroScriptCppConsoleTests` は VM／Mixed／実際の JIT で端末機能、正確な文字列出力と `print` のまとまり、権限拒否、ホストコールバックの失敗、実行エラーや命令数上限到達後のスタイル／カーソル復元、Session の反復呼び出し、通常の出力ストリームを検証します。`VeroScriptCppAotHelloTests` のネイティブ／VM 比較は色とカーソルの引数評価順序を、検証失敗前の副作用も含めて確認します。ステージング SDK の `CppMinimal/main.cpp` は公開ホストインターフェースと実行終了時の復元を検証します。端末契約を変更した際にはこれらのゲートを実行してください。

ネイティブゲートでは、コマンドライン引数の有無にかかわらず、端末アダプターの失敗、標準／非標準例外、診断ストリームの失敗時にエラーを返し、後処理を完了することも確認します。

ワーカーのゲートでは、端末ガード構築時の標準／非標準例外がタスク失敗として処理され、端末復元の完了後にタスク完了が通知されることも確認します。

## AOT の評価と参照の検証

`VeroScriptCppAotHelloTests` はネイティブ実行と VM を比較し、通常・ジェネリック・拡張・コンストラクター呼び出しの引数順序と変換、値のスナップショット、ref 引数、短絡演算、複合代入、グローバル callable の初期化を検証します。関数値は引数の後、インスタンスのレシーバーは引数の前に評価し、VM と一致させます。後続の引数がコンテナーを拡張または再束縛しても、添字参照は有効でなければなりません。添字変換が失敗した場合、後続引数の副作用は実行しません。純粋なスカラー式は直接生成し、保存した値引数はムーブで渡して追加コピーを避けます。

代入の回帰テストでは、保存したレシーバーの破棄後も式の結果が有効な値であることを確認します。単純な添字書き込みは保存したコンテナを元の変数またはフィールドに書き戻し、入れ子の線形添字と修飾されたグローバル変数は現在の格納パスを解決します。右辺による再束縛でも、別名とオブジェクトの同一性は VM と一致しなければなりません。単純な添字代入の境界検査は右辺の副作用の後に行い、参照パスの添字変換はその前に行います。変換不要の単純なスカラー添字書き込みは直接生成し、固定長配列のコピーを避けます。

`CppMinimal` サンプルは、対応する Core SDK の標準ライブラリとローカライズリソースを、再リンク不要の増分ビルドを含む毎回のビルドで実行ファイルの隣に配置します。Visual Studio と Ninja/Make の両方で、欠落したファイルを復元し、変更された SDK リソースを更新する必要があります。

[C++ AOT — 参照戻り値と評価時の寿命](../../User/ja-JP/aot.md#reference-return-lifetime) を参照してください。

長期に保持する添字付きの別名と、添字付き参照の返却には管理された格納経路が必要です。一時的な値型レシーバーから取得した参照の長期保持にも管理された所有者が必要です。AOT はこれらを明示的に拒否します。VM バックエンドを使用してください。

暗黙の構造体フィールドにはスクリプトのフィールド初期値を設定します。ジェネリックのフィールド型と固定長配列も含みます。この格納領域の初期化では、構造体のコンストラクター本体を実行しません。

ネイティブ検証は、ローカルのフィールド別名の保持、引数なし呼び出しのレシーバー寿命、参照代入対象の一度だけの評価、数値参照のオーバーフローと縮小変換を確認します。生成テストは、クリーンアップが必要なレシーバーとローカル別名の返却が明示的に拒否されることを確認します。メンバー、デストラクター、static フィールド、オープンテンプレートの定義は、完全な型レイアウトとグローバルアクセサー宣言の後に配置します。ネイティブ検証は前方のグローバル参照と制約付きテンプレートのメンバーおよび static フィールドを含みます。

検証には直接および入れ子のフィールド別名、for 初期化内の参照、メンバーおよびキャプチャされたレシーバー、ジェネリックのフィールド、構造体フィールドの初期格納領域も含みます。生成テストは長期の添字付き格納領域と一時的な値型レシーバーの別名を拒否します。再帰的なクリーンアップ検査は、ジェネリック特殊化を登録し得る検索の前に型名のスナップショットを取得します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/build-and-tests.md) · [fr-FR](../fr-FR/build-and-tests.md) · [ja-JP](build-and-tests.md) · [ko-KR](../ko-KR/build-and-tests.md) · [zh-CN](../zh-CN/build-and-tests.md) · [zh-TW](../zh-TW/build-and-tests.md)
