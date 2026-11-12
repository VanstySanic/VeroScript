# 01：第一個腳本

本篇只做一件事：建立一個腳本，讓 VeroScript 運作起來並輸出一行文字。

## 學完你會知道什麼

- 一個最小 VeroScript 程式需要什麼。
- 如何執行腳本。
- 如何在不執行腳本的情況下檢查它。
- `main` 和 `print` 分別負責什麼。

## 第一步：建立檔案

在工作目錄中建立 `hello.vs`，寫入：

```VeroScript
int main()
{
	print("hello, VeroScript");
	return 0;
}
```

這裡有兩個關鍵點：`main` 是腳本入口，`int` 表示它回傳處理程序退出狀態；`print` 把字串寫到標準輸出。`void main` 仍然支援，但完整程式通常使用 `int main` 並明確傳回 `0` 表示成功。

## 第二步：先檢查，再執行

在儲存 `hello.vs` 的目錄中執行：

```powershell
veroscript --check hello.vs
veroscript hello.vs
```

如果檢查通過，執行結果應類似：

```text
hello, VeroScript
```

`--check` 只編譯並回報問題，不會執行 `main`。開發時可以先檢查，確認沒有問題後再運作。

## 第三步：讓輸出變得有用

把腳本改成：

```VeroScript
int main()
{
	string name = "Ada";
	print("hello, " + name);
	return 0;
}
```

再次執行，你會看到：

```text
hello, Ada
```

`string name` 是一個字串變數。`+` 在這裡連接兩個字串。變數名稱可以修改，但型別和初始化運算式要保持一致。

## 遇到問題時

- 如果提示找不到 `veroscript`，先確認 CLI 已經安裝並加入 `PATH`。
- 如果提示找不到 `hello.vs`，確認終端目前目錄就是檔案所在目錄，或是傳入絕對路徑。
- 如果提示語法錯誤，先執行 `veroscript --lang zh-TW --check hello.vs`，讓診斷以中文顯示。

## 練習

1. 把名字改成自己的名字。
2. 增加一個 `string language = "VeroScript";` 變數，輸出 `name + " is learning " + language`。
3. 故意刪掉一個分號，執行 `--check`，觀察診斷訊息，再把程式碼修好。

下一篇會將固定字串換成命令列參數：[檢查、執行和讀取命令列參數](02-cli-and-arguments.md)。

完整的最小範例請參考 [`examples/hello.vs`](../../../../../VeroScript/Resources/Scripts/examples/hello.vs)；語法總覽見[基本語法](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/basic-syntax.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/getting-started/01-first-script.md) · [fr-FR](../../fr-FR/getting-started/01-first-script.md) · [ja-JP](../../ja-JP/getting-started/01-first-script.md) · [ko-KR](../../ko-KR/getting-started/01-first-script.md) · [zh-CN](../../zh-CN/getting-started/01-first-script.md) · [zh-TW](01-first-script.md)
