# C++ AOT emitter

`VeroScriptCppAot` は compile-checked VeroScript program から C++ を emit します。public API は source string、file、program、単一 generated source string、および relative file と JSON manifest を持つ `FCppAotSourceBundle` を扱います。

## Bundle 不変条件

generated bundle は意図的に一つの C++ translation unit と generated `.inl` fragment を使います。独立 link module への分割は template instantiation、internal linkage、global initialization order を変更します。そのため layout は formatting option ではなく output contract です。

## 対象範囲と runtime support

emitter は entry module を検証し、entry function `main` の `void main()`、`int main()`、または対応する `string[] args` form だけを受理します。value、container、channel、worker task など、program が使う機能だけの compatibility support を emit します。AOT worker global と static field は native thread ごとに分離され、channel が同期共有の明示的な能力です。

文字列で指定する task entry、協調的な `std::concurrent::cancel_task()` / `std::concurrent::cancel_requested()`、filesystem 操作の `std::filesystem::is_file()`、`std::filesystem::is_directory()`、`std::filesystem::remove_dir()` をサポートします。callable を指定する task entry と実行可能な `field_info<T>` / `method_info<T>` reflection は、AOT 診断とともに拒否します。

lowering により VeroScript の numeric、ownership、exception、destruction、module、concurrency の意味論が変わる program は拒否します。compile、eligibility、lowering、output write の失敗後に partial source を返してはいけません。

生成される `array<bool>` は、添字アクセスと参照引数で実際の `bool&` を提供するため、各要素にアドレスを取得できる独立した bool スロットを使用します。ビット圧縮は行わないため、同じ容量の `std::vector<bool>` より多くのメモリを使用します。

## 保守

public emitter header、implementation、generated-source test、bundle test、generated-file validation path が根拠です。runtime helper は test が対応する VeroScript behavior を示し、helper が generated bundle 内で self-contained の場合だけ追加します。

[C++ AOT — ユーザー定義 pair テンプレート](../../User/ja-JP/aot.md#cleanup-container-operations) を参照してください。

独立ソースの 10 進変換を `Shared/Private/VeroScriptFloatParsing.h` と一致させます。`VeroScriptFloatParsingSource.h`、`MakeRuntimeSupport`、生成した `VsFloatParsingDetail` が自己完結した出力経路です。共有入力を移動するときは CMake の読込みパスと configure 依存を同時に更新し、利用者にソースツリーの非公開ヘッダーを要求しないでください。対象幅、ロケール、範囲を Core AOT 実行とは別に検証します。

<a id="standalone-file-paths"></a>

生成される `VsPathFromUtf8` は自己完結します。Windows の char8_t／C++17 u8path 経路はいずれもパスを構築してネイティブ区切りへ変換し、ネイティブ UTF-16 単位で 248 以上の絶対パスだけに拡張接頭辞を付けます。完全な UNC は専用の拡張形式を使い、既存の接頭辞は保持し、相対パスを絶対化しません。変換前に埋込み NUL を拒否します。Unicode、長い絶対／UNC、相対パス、両言語標準の生成と回帰を同時に維持し、生成物の利用側に非公開ソースヘッダーを要求してはいけません。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
