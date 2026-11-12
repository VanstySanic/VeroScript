# 11：偵錯與 AOT 產生

本篇介紹兩種交付前常用的工具鏈動作：用偵錯器觀察腳本，以及依部署目標選擇 AOT 輸出形式。

## 第一步：準備一個可觀察的腳本

```VeroScript
int calculate(int value)
{
	int result = value * 2;
	return result + 1;
}

int main()
{
	int input = 20;
	int output = calculate(input);
	print(output);
	return 0;
}
```

將它儲存為 `debug_demo.vs`，先做普通檢查：

```powershell
veroscript --check debug_demo.vs
```

## 第二步：啟動 DAP 偵錯

標準輸入輸出模式適合由編輯器啟動並管理：

```powershell
veroscript --debug debug_demo.vs
```

如果偵錯客戶端需要透過 TCP 連線：

```powershell
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 debug_demo.vs
```

在編輯器中設定斷點、單步和變數檢視。偵錯執行使用虛擬機器路徑，因此不要用偵錯時的後端行為代表普通執行效能。

應在作用中的程式碼上設定原始碼中斷點。使用 DAP 用戶端時，應透過 `setBreakpoints` 傳送。啟動前設定的
中斷點會處於待繫結狀態；程式載入後，用戶端會收到相同中斷點 ID、`reason: "changed"` 的 `breakpoint`
事件。一律採用回傳的已驗證狀態和行號。被 `#if` 移除、超出原始檔範圍或沒有可執行位置的行會保持未驗證。

## 第三步：產生 AOT 原始碼

當腳本不需要動態載入時，應依部署目標選擇 AOT 目標。預設目標是 C++17：

```powershell
veroscript aot debug_demo.vs -o generated.cpp
```

`generated.cpp` 不是最終可執行檔；宿主專案仍需使用相容的 C++17 工具鏈和 VeroScript 執行階段完成編譯連結。產生後先檢查檔案，再在目標專案中建置，不要把產生檔案誤當作獨立程式執行。

如果需要獨立 Python 程式，請明確選擇 Python：

```powershell
veroscript aot --target=python debug_demo.vs -o generated.py
python generated.py
```

如果需要產生 C# 專案或 NativeAOT 程式，請產生原始碼 bundle：

```powershell
veroscript aot --target=csharp debug_demo.vs --output-dir generated-csharp --bundle-name Demo
dotnet publish generated-csharp/Demo.csproj -c Release
```

支援範圍與明確限制請參閱 [Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/aot.md) 和 [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/aot.md)。目標無法保持語意時會回傳診斷並停止產生。

## 選擇建議

- 快速迭代、熱重載和互動偵錯：使用 VM 或預設混合後端。
- 需要斷點和變數檢查：使用 `--debug`。
- 納入本地 C++ 建置：使用 C++ AOT，並驗證宿主繫結與並發行為。
- 獨立 Python 原始碼：使用 `--target=python`，並在目標 Python 版本中測試。
- .NET 或 NativeAOT：使用 `--target=csharp`，並建置產生的 bundle。

## 練習

1. 在 `calculate` 內設定斷點，查看 `input` 和 `result`。
2. 修改輸入值，比較 VM 執行與偵錯執行的輸出。
3. 為一個使用陣列的腳本產生 C++ AOT 原始碼，確認目標專案能夠編譯它。
4. 為同一個腳本產生 Python 或 C# 輸出，比較不同目標的限制診斷。

完整選項請見[偵錯](../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/debugging.md)、[C++ AOT](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/aot.md)、[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/aot.md)和[C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/aot.md)。下一篇進入嵌入式場景：[宿主繫結與腳本整合](../advanced/12-host-bindings.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/toolchain/11-debugging-and-aot.md) · [fr-FR](../../fr-FR/toolchain/11-debugging-and-aot.md) · [ja-JP](../../ja-JP/toolchain/11-debugging-and-aot.md) · [ko-KR](../../ko-KR/toolchain/11-debugging-and-aot.md) · [zh-CN](../../zh-CN/toolchain/11-debugging-and-aot.md) · [zh-TW](11-debugging-and-aot.md)
