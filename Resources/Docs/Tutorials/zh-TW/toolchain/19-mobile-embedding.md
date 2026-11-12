# 19：在行動應用程式中執行指令碼

在 Android/iOS 應用程式中使用相符的行動 SDK。應用程式需要已有的目標 CPython 嵌入環境或 .NET 10 MAUI；桌面套件不提供行動原生二進位。這些接入仍處於實驗階段。

[Python](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/mobile.md) · [CSharp](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/mobile.md)

1. 依所選繫結的指南部署原生程式庫並安裝標準程式庫、本地化資源。必須在建立 Session 前完成資源安裝。

2. 在同一工作執行緒中載入目錄、選擇語言、建立並設定 VM Session，載入 `import "core.vs"; void main() { print(42); }`，檢查載入及執行結果，收集 print 回呼並關閉 Session。再將收集到的 `42` 傳回 UI 執行緒。

3. 選用非英語語言，以 `void main() { int x = ; }` 重試，顯示診斷並檢查介面仍可回應。恢復正確指令碼，設定執行限制，並在各支援目標驗證最終簽署應用程式。

[en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](19-mobile-embedding.md)

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](19-mobile-embedding.md)
