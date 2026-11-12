# Android — 實驗性行動嵌入

實驗性 AAR 面向 `arm64-v8a`、`x86_64`、API 26+，使用 VM 與 NDK `c++_shared`。向應用程式新增 AAR 並保持原生相依項相容。包含 `assets/veroscript/stdlib`、C ABI Prefab 標頭和授權；應用腳本放在 `src/main/assets/scripts`。`scripts/helper.vs` 對應 `/veroscript/scripts/helper.vs`。在工作執行緒建立／執行／關閉，保留 engine 參考供 `requestCancel()` 使用。透過 `executeAsset` 讀取入口，或呼叫 `execute(source, absoluteLabel)`。每次執行使用新狀態，不支援並行執行。`close()` 等待並可重複呼叫，但不能在自身回呼中同步關閉。UTF-8 位元組陣列保留 NUL 和補充字元，須檢查 `outputTruncated`。

匯入 SDK 的 [VeroScriptEngine](https://github.com/VanstySanic/VeroScript/blob/main/Extensions/Platform/Mobile/VeroScriptAndroid/src/main/java/com/vanstysanic/veroscript/android/VeroScriptEngine.java) 型別。以下方法本體片段應在工作執行緒中執行，並假定已有 Android `context`。

```java
try (VeroScriptEngine engine = new VeroScriptEngine(context.getAssets(), "scripts"))
{
	VeroScriptEngine.Result result = engine.executeAsset("scripts/main.vs");
	// Inspect result.success, output, diagnostics and outputTruncated.
}
```

[實驗性行動嵌入](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/zh-TW/index.md)

## 行動本地化的明確設定

[行動本地化的明確設定](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/zh-TW/index.md)

## 受管理位元組儲存限額

Android 透過完整 `VeroScriptEngine` 建構函式的最後兩個引數 `maxBufferBytes` / `maxRetainedBytes` 設定。Java `long` 攜帶 uint64 位元模式，一般預算使用正的位元組數。 預設 16 MiB / 32 MiB，0 表示無限。

## 受控原生執行與靜態 AOT

請參閱[實驗性行動嵌入 — 受控原生執行與靜態 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/zh-TW/index.md#mobile-jit-selection)。

<a id="android-native-selection"></a>

Android 接受 `ExecutionBackend`、`jitBackendPath`、應用程式建立的 `AotPrograms` 目錄和 `aotProgramId`；產生應用程式使用 `executePackage`，原生引導使用 `VeroScriptAndroid.h`。目錄保留程式，產生入口程式碼須保持靜態連結，ID 不能攜帶函式位址。

<!-- docs-navigation -->

---

[使用者文件](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
