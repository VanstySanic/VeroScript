# Mobile — 開発者文書

UI 配信とライフサイクルはアダプター／ホストが担当し、Core はプラットフォーム非依存を保ちます。embedded-only はデスクトップ CLI/tooling と言語 exporter を除外します。静的 Interop はソース依存グラフまたは結合 iOS アーカイブに属し、デスクトップ配布は共有です。Android/Linux、iOS/macOS、実機／シミュレーターを区別します。許可フラグだけでバックエンド生成／選択や実機検証済みとは言えません。import、キャッシュ失効／破損、上限、取消、破棄を検証します。

```text
cmake -S . -B build/embedded -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF -DBUILD_TESTING=ON
cmake --build build/embedded --config Release --target VeroScriptMobileTests
```

`package-veroscript-mobile.py` は独立実験出力を作り既存出力を拒否し、デスクトップ配布集合には公開しません。

ツールのバージョンとプラットフォーム固有の検証条件は [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/ja-JP/index.md) と [iOS](../../../../../VeroScriptIOS/Resources/Docs/Developer/ja-JP/index.md) で管理します。

[ユーザー文書](../../User/ja-JP/index.md)


## モバイルリソースと言語

<a id="mobile-localization"></a>

`mobile` プロファイルは Core、C++ Session、Interop、`Extensions/Platform/Mobile/VeroScriptMobile/Resources/Localization` を集約し、デスクトップ Tooling/UI は除外します。Gradle は AAR に同じ catalog をコピーし、packager は一覧とバイトを確認します。iOS は `assets/localization` へコピーします。アプリは言語選択前に明示展開/ロードし、モバイルの環境探索を導入しません。モバイルパッケージには機能ファイル、実行時リソース、ライセンスを含めます。README、マニュアル、サンプルは含めず、統合 User 文書は独立した Resources 配布物から取得します。ネイティブリソースのパスと実機での検証要件は維持します。

Mobile の `TranslateMessage` は公開 C ABI に委譲します。バッファーの所有権とエラー動作は [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/ja-JP/abi-and-lifetime.md)、Android JNI の文字コードは [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/ja-JP/index.md) で管理します。

Core と対象 AOT の実装フィンガープリントには `Shared/Private/VeroScriptFloatParsing.h`、`VeroScriptFloatFormatting.h`、`VeroScriptJsonString.h` を含めます。Python ソース配布には Application／パッケージランタイム用の `VeroScriptDarwinPaths.h` を含む必要な `Shared/Private` 入力を収録します。Android Python のソースダイジェストは Application とその C ソースも対象にします。公開 SDK ヘッダーや端末検証の証拠ではなく、非公開のビルド入力です。

<a id="mobile-execution-worker"></a>

`Private/VeroScriptMobile.cpp::WithCompilerStack` は Android／Apple で呼び出し元のスタックを確認します。8 MiB 未満なら 8 MiB の pthread を生成して同期的に join し、それ以外は呼び出し元で実行します。解析だけでなく FSession 全体の生成・実行・破棄をワーカーが所有し、例外は捕捉して join 後に再送出します。破棄前に取消ミューテックスの下でアクティブ Session ポインターを消去し、すべての終了経路で外側の実行中状態を解除してください。直接 FSession を使う場合はこの補助処理を経由せず、ホストがスタックを用意します。言語の入れ子・呼び出し深度・実行上限は増えません。小さいスタックでの正常入力、上限診断、例外、取消、再利用を検証します。Android の検証アプリは入れ子後の復旧を扱い、Apple の回帰は呼び出し元を明示的に 512 KiB に固定します。

<a id="mobile-message-coverage"></a>

Mobile カタログは C++ Session／runner、Android AOT／Java 検証、iOS ファクトリ／バックエンドのエラーを網羅します。C++ と Java は既存の翻訳入口を使い、JNI／NSError は境界で例外を翻訳します。std::system_error ではコードとカテゴリを変えずプロジェクトの文脈を翻訳し、OS の接尾辞はシステムライブラリの言語に従います。資源エラー時は既存のフォールバックを保ち、6 言語のキーとプレースホルダーを一致させます。

Core／C++／C だけのソースホストは `VEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF` でアダプターを無効にできます。`VEROSCRIPT_INTEROP_STATIC=ON` はソース／静的モバイル向け設定で、デスクトップの共有 Interop 配布を変更しません。

<!-- docs-navigation -->

---

[開発者文書](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
