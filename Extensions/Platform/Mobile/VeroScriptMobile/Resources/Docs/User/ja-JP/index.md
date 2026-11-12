# 実験的なモバイル埋め込み

Android と iOS のアダプターは実験段階です。対象に一致する SDK 成果物を使い、実際の配備環境でアプリケーションを検証してください。`FSessionOptions` は絶対パスの標準ライブラリとソース資源スナップショットを指定します。仮想ソースをすべて `Sources` に渡せば実パスまたは仮想パスを使用でき、入口と相対インポートの絶対ラベルを一致させます。任意のキャッシュは実在する書き込み可能な絶対ディレクトリで、空なら無効です。資源更新後は runner を作り直します。

```cpp
VeroScript::Mobile::FSessionOptions options;
options.StdlibDirectory = "/app/resources/stdlib";
VeroScript::Mobile::FRunner runner(std::move(options));
auto result = runner.Execute("int main(){ print(42); return 0; }", "/app/scripts/main.vs");
```

`FRunner` は一度に一実行で、呼び出しごとに新しい Session を作るためグローバル状態は残りません。Execute 呼び出しは同期的です。Android と Apple では内部ワーカーで実行される場合があるため、ホストのコールバックは呼び出し元や UI スレッドで実行されると仮定してはいけません。構築や実行は読み込み・コンパイルを行うので UI スレッド以外に配置します。寿命を保つ間は別スレッドから取消を要求できます。実行・取消の呼び出し終了を待ってから破棄します。`FSession` の生成、通常操作、破棄は生成スレッドが所有し、ホストはコンパイルに 8 MiB 以上のネイティブスタックを用意します。取消はラッチされます。コールバックは自分の権限と取消を実装します。取消はコンパイルを中断せず、ブロックしたネイティブ呼び出しも強制終了しません。

キャッシュを有効にすると、入口とインポートしたソースの内容が無効化判定に使われます。ヒット時も現在の内容を確認するため資源の読み込みや解析を行う場合があり、起動時の処理がすべて省略されるとは限りません。使用不能または破損したキャッシュは通常のコンパイル経路に戻ります。

既定値は VM、1,000,000 命令、100 ms、呼び出し深度 256 です。コンソール出力のみ許可し、入力、ファイル、環境、ディレクトリ変更、タスクは無効です。出力と診断は合計 1 MiB、エラーは最大 64 KiB。資源ソースと各入口は 16 MiB、最大 4096 資源と 4096 バイトのパスです。これは VM ヒープ上限や完全なサンドボックスではありません。ホストは展開したカタログを `vs_interop_locale_load_catalog_directory` で明示ロードし言語を選びます。環境、cwd、実行ファイル隣接からの探索はなく、未設定なら英語です。iOS 実機とシミュレーターは動的 JIT とネイティブ生成を禁止します。

## モバイル言語の明示設定

SDK は六言語 catalog を含みますが自動ロードしません。エンジン作成/実行前に全 catalog をアプリ管理の実ディレクトリへコピーしロードします。APK asset/仮想パスではありません。Android AAR は `veroscript/localization`、iOS SDK は `assets/localization` です。Android の `VeroScriptEngine.loadCatalogDirectory(path)` と `setLanguage(tag)` は成否を返します。iOS/C++ は `vs_interop_locale_load_catalog_directory` と `vs_interop_locale_set_language` を使い、エラーバッファーを `vs_interop_buffer_destroy` で解放します。全戻り値を確認します。言語はプロセス全体でエンジン別ではなく、失敗時は前の選択を保持します。catalog なしの不明メッセージは英語です。診断と適配検証は同じ言語で、スクリプト出力は変わりません。メモリ確保失敗時は英語 fallback を保持する場合があります。 アダプターの文脈を翻訳しても、OS エラーの詳細はシステムライブラリの言語に従う場合があります。

## 管理バイト領域の上限

既定はバッファごとに 16 MiB、保持総量 32 MiB です。Core モバイルの `MaxBufferBytes` / `MaxRetainedBytes` で設定し、0 は無制限です。所有容量と外部バイト、拡張ピークを計上し、VM の heap 全体を制限するものではありません。既定のコンソール出力のみのポリシーではネイティブアドレスは無効です。

## 実行制御付きネイティブ実行と静的 AOT

<a id="mobile-jit-selection"></a>

モバイルの既定は VM です。Android は独立した `vs-asmjit` AAR と JIT ファイル／ディレクトリを明示選択できます。互換性のある `libc++_shared.so` は一つだけ配置してください。ファイルパスで JIT を読み込むアプリはネイティブライブラリを展開します（アプリ側で `jniLibs.useLegacyPackaging = true`）。iOS の実機とシミュレータでは動的 JIT を禁止します。

<a id="mobile-core-aot"></a>

`VeroScriptCppCoreAot.h` の `CompileSourceToCoreAot`／`GenerateCoreAotSourceBundle` は対象 C++、完全一致する `.vsapp`／`.vsbc` と範囲レポート JSON を生成します。対象 SDK でコンパイルし、Mixed を選択して対応パッケージを実行します。未対応関数とモジュール／フィールド初期化は VM、厳密なネイティブモードでは拒否します。Core の意味を保ち、独立ソース出力とは異なります。

プラットフォーム API：[Android](../../../../../VeroScriptAndroid/Resources/Docs/User/ja-JP/index.md#android-native-selection) · [iOS](../../../../../VeroScriptIOS/Resources/Docs/User/ja-JP/index.md#ios-native-selection)。

[オプションの JIT バックエンド — 実行制御付きネイティブ実行と静的 AOT](../../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/ja-JP/index.md#controlled-native-execution) を参照してください。

完全なモバイル SDK パッケージは、生成 C++ 用の `aot/include` を含みます。Core サービス公開ヘッダーと一致する対象ビルド ID を、Interop ヘッダーと共にインクルードパスへ追加してください。ヘッダーのみのため、リンクする Interop ランタイムは一つです。AAR だけを導入する場合、このヘッダー群を別途取得します。

VM の選択は `ExecutionBackend` と JIT から独立しています。`FSessionOptions.VmBackendPath` が空なら製品に明示的に組み込まれた静的 VM を保持します。指定時はネイティブライブラリのファイルまたはディレクトリの絶対パスが必要で、失敗時にフォールバックしません。Android の完全なコンストラクターは `vmBackendPath`、iOS は `VSMConfiguration.vmBackendPath` を提供し、iOS の既定は静的構成です。

<!-- docs-navigation -->

---

[ユーザー文書](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
