# 10：建置套件並發佈應用

當腳本從“本機檔案”變成“可交付程式”，可以把它建置為應用程式套件或程式庫套件：`.vsapp` 適合直接執行，`.vslib` 適合被其他腳本匯入。

## 第一步：建立庫模組

建立 `package_library.vs`：

```VeroScript
export enum class OperationKind
{
	Add
};

export int calculate(int left, int right)
{
	return left + right;
}
```

只有 `export` 的宣告會成為函式庫 API。先建構函式庫套件：

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
veroscript package inspect DemoLibrary.vslib
```

## 第二步：建立應用程式入口

建立 `package_app.vs`：

```VeroScript
#ifdef VERO_USE_BINARY_PACKAGE
import "DemoLibrary.vslib";
#else
import "package_library.vs";
#endif

int main()
{
	print(calculate(20, 22));
	return 0;
}
```

開發時編譯應用程式需要讓工具找到函式函式庫套件：

```powershell
veroscript -D VERO_USE_BINARY_PACKAGE --emit-package DemoApp.vsapp package_app.vs
veroscript package inspect DemoApp.vsapp
veroscript DemoApp.vsapp
```

產生的應用程式套件包含入口及其依賴，執行階段不必把來源函式庫套件放在旁邊。 `package_app.vs` 預設從同目錄原始檔匯入庫；使用 `-D VERO_USE_BINARY_PACKAGE` 後才會切換為匯入已產生的 `DemoLibrary.vslib`，因此函式庫套件必須在編譯應用時可見。

## 第三步：檢查公開邊界

如果你在函式庫中寫了沒有 `export` 的函式，它仍然可以在函式庫自己的原始檔中使用，但下游腳本不能呼叫。把套件 API 當作長期契約，發佈前檢查：

1. 是否只匯出了真正需要的型別和函式。
2. 參數、傳回型別和枚舉值是否穩定。
3. 套件版本是否與目標執行階段和綁定相容。
4. 是否已經執行過 `package inspect`。

倉庫中有一套完整範例：[`examples/package/README.md`](../../../../../VeroScript/Resources/Scripts/examples/package/README.md)、[`package_library.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_library.vs) 和 [`package_app.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_app.vs)。

## 練習

1. 為函式庫增加一個 `subtract` 函式。
2. 刪除 `export`，觀察應用編譯時的錯誤。
3. 為套件設定版本號，並在發佈前記錄產生指令。

套件選項和相容性細節請參閱[套件與發佈](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/packages.md)。下一篇學習如何定位執行階段問題並產生 AOT 原始碼：[偵錯與 AOT 產生](11-debugging-and-aot.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/toolchain/10-packages-and-distribution.md) · [fr-FR](../../fr-FR/toolchain/10-packages-and-distribution.md) · [ja-JP](../../ja-JP/toolchain/10-packages-and-distribution.md) · [ko-KR](../../ko-KR/toolchain/10-packages-and-distribution.md) · [zh-CN](../../zh-CN/toolchain/10-packages-and-distribution.md) · [zh-TW](10-packages-and-distribution.md)
