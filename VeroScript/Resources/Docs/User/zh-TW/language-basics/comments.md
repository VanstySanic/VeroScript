# 註解

註解用於向來源檔案添加說明。VeroScript 支援單行註解和區塊註解。

## 單行註解

單行註解以 `//` 開始，直到目前行結束：

```VeroScript
// Explain the purpose of the variable below.
int retry_count = 3;

int value = 1; // A comment may follow a statement.
```

## 區塊註解

區塊註解以 `/*` 開始，以 `*/` 結束，可以跨越多行：

```VeroScript
/*
	This note applies to the following code block.
*/
int result = 0;
```

註解本身不是執行敘述，未閉合的區塊註解會產生詞法錯誤。宿主可選擇保留全部註解、僅文件註解或不保留註解，保留的註解及文件可透過原始碼中繼資料查詢。已啟用的編譯擴充可利用這些資訊產生程式碼。註記（annotation）中繼資料的保留獨立於註解（comment）保留原則。

## 編輯器工具使用的來源文件

Tooling 懸停會讀取緊鄰來源宣告之前的連續註解。來源定義的前置處理器巨集補全項目也可攜帶其前置註解；宿主補全文件來自繫結中繼資料。可使用 `//`（包括 `///`）或 `/* ... */` 區塊註解；註解與宣告之間不可留空行。這是 Tooling 慣例，編譯器仍會將它們視為一般詞法註解。

`@param 名稱 說明` 用於說明參數；`@return 說明` 與 `@returns 說明` 用於說明回傳值。懸停會使用目前語言目錄中的標籤顯示這些標記。未知標記仍會保留為一般註解文字；巨集補全文件則原樣保留註解。

```VeroScript
// Adds two integers.
// @param left First addend.
// @param right Second addend.
// @return The sum.
int Add(int left, int right)
{
	return left + right;
}
```

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/comments.md) · [fr-FR](../../fr-FR/language-basics/comments.md) · [ja-JP](../../ja-JP/language-basics/comments.md) · [ko-KR](../../ko-KR/language-basics/comments.md) · [zh-CN](../../zh-CN/language-basics/comments.md) · [zh-TW](comments.md)
