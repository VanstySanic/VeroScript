# ライブラリとアプリを作る

前提はBuild/Application対応CLI、書込可能領域、配布hello例です。script内容だけを使い、nativeproviderやnetworkserviceは不要です。

1. リンクしたhelloディレクトリ全体を/absolute/helloなど絶対書込先へコピーし、src/packageを保ちます。先にplan、次にapplication targetだけbuildします。依存libraryは自動構築、profileはreleaseを明示します。

   [hello](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)

```sh
veroscript build --path /absolute/hello --profile release --target application --plan --json
veroscript build --path /absolute/hello --profile release --target application
veroscript /absolute/hello/dist/release/portable/application/sample.vsapp
```

2. planはcustomstepを実行せず依存graphを示します。成功時dist/release/portable/application/sample.vsappとlibraryを生成し、実行はresult: 42、終了0です。plan/build失敗は修正し、生成だけで実行成功としません。

3. libraryのanswerを43へ変え、新planで再buildしてresult: 43を確認します。生成containerは編集せずsourceと設定を入力に保ちます。

[Build](../../../../../VeroScriptBuild/Resources/Docs/User/ja-JP/index.md) · [Application](../../../../../VeroScriptApplication/Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)

/absolute/hello は環境の絶対パスに置き換え、Windows なら C:/work/hello などを使用します。

## 入力ファイル

- [veroscript-build.json](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)
- [src/main.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/main.vs)
- [src/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/library.vs)
- [package/veroscript-package.json](../../../../../VeroScriptBuild/Resources/Examples/hello/package/veroscript-package.json)
- [package/scripts/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/package/scripts/library.vs)

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)
