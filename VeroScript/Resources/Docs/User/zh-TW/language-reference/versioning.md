# 版本與相容性


版本資訊同時影響原始碼、位元組碼快取、應用程式套件、函式庫套件和宿主綁定。部署時應讓這些元件使用相互相容的語言和執行時期版本。

可以使用以下命令查看實際安裝版本：

```powershell
veroscript --version
```

## 檔案格式相容性

產品、位元組碼、Interop ABI 和 WebAssembly ABI 統一使用初始版本 `0.1.1`。位元組碼載入器要求格式版本與魔數完全匹配。請從原始碼重建舊開發版本產生的 `.vsbc`、`.vsapp` 與 `.vslib`；不提供這些舊二進位檔案的相容性。

- `.vs` 是原始檔案，通常需要由對應版本的編譯器重新檢查；
- `.vsbc` 是位元組碼快取，不應作為跨版本發佈格式；
- `.vsapp` 是可執行應用程式套件，執行階段和套件版本必須匹配；
- `.vslib` 是函式庫套件，公開宣告、套件版本和執行階段必須保持相容。

版本升級後，應重新執行 `--check`，重新建置套件，並在使用 AOT 或宿主綁定時重新產生或驗證本機產物。

目前長度及線性索引 API 使用 `int64`。即使 `--version` 顯示相同產品版本，也須從原始碼重建此次變更之前產生的二進位套件及位元組碼快取。升級時使用相符的標準庫，並重新產生 AOT 成品。

## 語言能力邊界

目前實作不提供腳本指標語意和獨立的 C++ concepts 語法；可變參數巨集的 `__VA_OPT__`、Lambda 引用擷取、引用回傳和執行階段反射已經屬於公開能力。圖形介面也不屬於核心執行階段。檔案中的能力清單以目前實作和版本說明為準，不應把 C++ 中存在的語法自動推斷為 VeroScript 能力。

## 宿主綁定相容性

宿主綁定不僅依賴函式名稱，還依賴參數類型、傳回類型、容器元素類型、鍵值類型和宿主物件生命週期。修改這些邊界時，應同時更新綁定目錄、宿主程式碼和腳本呼叫方，並進行完整的檢查和執行驗證。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-reference/versioning.md) · [fr-FR](../../fr-FR/language-reference/versioning.md) · [ja-JP](../../ja-JP/language-reference/versioning.md) · [ko-KR](../../ko-KR/language-reference/versioning.md) · [zh-CN](../../zh-CN/language-reference/versioning.md) · [zh-TW](versioning.md)
