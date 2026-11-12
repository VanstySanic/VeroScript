# 建置程式庫與應用

前提是匹配且含 Build/Application 服務的 CLI、可寫目錄與分發 hello 範例。僅使用腳本內容，不需原生提供者安裝或網路服務。

1. 將連結的整個 hello 目錄複製到 /absolute/hello 等可寫絕對位置，保留 src/package。先查計畫，再僅建置 application 目標，庫相依會自動建置；明確選 release 設定。

   [hello](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)

```sh
veroscript build --path /absolute/hello --profile release --target application --plan --json
veroscript build --path /absolute/hello --profile release --target application
veroscript /absolute/hello/dist/release/portable/application/sample.vsapp
```

2. 計畫顯示相依圖但不執行自訂步驟。成功產生 dist/release/portable/application/sample.vsapp 與庫，執行印 result: 42、回傳0。先修復計畫/建置錯誤，不能以生成檔案代表執行成功。

3. 將庫回答改為43，重取計畫並建置，預期 result: 43。不要修改生成容器，以原始碼與設定為輸入。

[Build](../../../../../VeroScriptBuild/Resources/Docs/User/zh-TW/index.md) · [Application](../../../../../VeroScriptApplication/Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](23-application-build.md)

將 /absolute/hello 換成實際系統絕對路徑；Windows 例如 C:/work/hello。

## 原始碼輸入

- [veroscript-build.json](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)
- [src/main.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/main.vs)
- [src/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/library.vs)
- [package/veroscript-package.json](../../../../../VeroScriptBuild/Resources/Examples/hello/package/veroscript-package.json)
- [package/scripts/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/package/scripts/library.vs)

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](23-application-build.md)
