# C# 前端

`VeroScriptCSharpFrontend` 是 C# BindGen 與 C# Transpiler 共用的語意前端。內部使用 Roslyn，
但公開模型歸 VeroScript 所有，不會把 Roslyn 物件暴露給消費者。

前端支援多檔案、命名空間、類別、結構、介面、record、列舉、delegate、欄位、
屬性、事件、建構函式、方法、運算子、attribute、XML 文件、泛型形狀、來源
位置，以及 `ref`/`in`/`out` 參數和回傳中繼資料。型別對映涵蓋 C# 基本型別、
一維陣列、`List<T>`、`Dictionary<TKey,TValue>`、`SortedDictionary<TKey,TValue>`、
`HashSet<T>`、`Queue<T>`、`Stack<T>`、宣告的列舉和已知宿主型別。

nullable、pointer、delegate、`dynamic`、無法解析的型別和開放型別參數會
明確失敗，不會靜默遺失語意。本模組是建置期宣告函式庫；產生 Catalog 和
程式碼時使用 C# BindGen；需要執行嚴格的 C# 到 VeroScript 轉換和 source map 時使用
`VeroScriptCSharpTranspiler`。

## 標準容器的泛型參數數量

<a id="container-mapping-arity"></a>

標準容器對應要求宣告的參數數量一致：`List<T>`、`HashSet<T>`、`Queue<T>` 和 `Stack<T>` 為一個，`Dictionary<TKey,TValue>` 和 `SortedDictionary<TKey,TValue>` 為兩個。同名但參數數量不同的自訂型別不會轉為格式錯誤的內建容器；它需要明確的已知宿主型別對應，否則以 `VSCF2008` 失敗。

## 宣告可存取性

屬性與巢狀型別的可存取性分別判斷。CLR 隱含基底型別與明確宣告的繼承分開處理，使用宣告模型時不應公開私用存取子，也不應將 `System.ValueType` 作為宿主基底類別。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](frontend.md)
