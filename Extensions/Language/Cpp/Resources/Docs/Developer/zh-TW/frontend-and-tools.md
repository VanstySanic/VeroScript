# C++ 前端、BindGen 與 Transpiler

## 前端 AST

`VeroScriptCppFrontend` 將有界 C++17 子集解析為穩定模型：token、源位置、屬性、型別、引數、模板、record、enum、變數、函式、普通 `typedef`/`using` 別名、名稱空間和函式體。可表示時保留限定名、C 連結、儲存/語義限定符、構造/析構、friend 和同檔案類外定義。

`FParseOptions` 控制 include/pragma、未知預處理指令和函式體解析；`FTypeMapOptions` 控制原始宿主指標、右值引用、已知宿主/enum 型別、型別引數和專案別名。它們是顯式選項，不是隱藏全域策略。

收集器跨輸入合併相容宣告和前向宣告，並可將一份輸入中的常規類外成員定義與另一份輸入的完整 record 配對。它拒絕重複定義、重複輸入路徑、內部連結衝突、匿名名稱空間、不支援模板和無法保持所有權或控制流含義的構造。

## Binding Catalog 生成

BindGen 發射機器可讀的 Binding Catalog JSON，也可為每個輸入發射 API 引用和專案清單。合併後的目錄是權威輸入；API 引用僅供審閱，絕不能編譯。僅當目錄模式可表示時，才保留結構化屬性、別名、enum 身份、過載資訊、成員標誌和文件。

專案生成先解析全部宣告，為同名輸入基名賦予確定名稱，並以單一事務替換目錄、引用與清單。

## 原始碼 Transpiler

Transpiler 僅降級擁有直接 VeroScript 含義的構造。它發射 VeroScript 源和宣告級原始碼對映；專案模式為每份輸入發射一個模組和對映及清單。預設以真實 VeroScript 編譯器編譯生成源。無法表示的所有權、RTTI、指標訪問、高階模板、不支援的 `std` 設施和屬性必須明確失敗。

成員 `operator()` 與 `operator[]` 保留多載和參考參數。原始碼轉換以 `T&` / `const T&` 輸出參考簽章，包括參考傳回；運算子引數依核心規則隱式繫結參考。BindGen 保留運算子與參考參數旗標，但目前 Binding Catalog 無法表示原生參考傳回的儲存與生命週期，因此明確拒絕。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/frontend-and-tools.md) · [fr-FR](../fr-FR/frontend-and-tools.md) · [ja-JP](../ja-JP/frontend-and-tools.md) · [ko-KR](../ko-KR/frontend-and-tools.md) · [zh-CN](../zh-CN/frontend-and-tools.md) · [zh-TW](frontend-and-tools.md)
