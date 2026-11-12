# 語言基本特性

本目錄以主題介紹 VeroScript 的基本語言規則。每篇檔案說明一個相對獨立的語法主題，並使用最小範例說明行為；完整的 API 和執行階段機制不在本目錄中展開。

## 原始檔、模組和作用域

- [基本語法](basic-syntax.md)：原始檔案結構、語句、程式碼區塊、識別碼和關鍵字。
- [註解](comments.md)：單行註解與區塊註解。
- [作用域](scope.md)：模組、命名空間、函式和程式碼區塊的作用域規則。
- [命名空間](namespaces.md)：命名空間宣告、限定名稱和 `using namespace`。
- [`std` 標準命名空間](standard-namespace.md)：內建類型、執行階段函式及其相容別名的完整清單。
- [模組](modules.md)：模組匯入、匯出、重新匯出和函式庫套件匯入。
- [預處理器](preprocessor.md)：巨集、條件編譯、字串化、標記拼接和模組包含。

## 值和類型

- [字面量](literals.md)：整數、浮點數、字串、寬字串、布林值和空值。
- [資料型別](data-types.md)：內建型別、固定寬度整數、容器型別和型別推導。
- [值工具類型](value-utility-types.md)：optional、weak、共享結構體控制代碼和映射項目 pair。
- [變數](variables.md)：變數宣告、初始化、賦值、引用現有值。
- [常數](constants.md)：`const` 變數與常數成員。
- [型別與特殊宣告](declarations.md)：`using`、`typedef`、`= default` 與 `= delete`。
- [結構體](structs.md)：值類型、欄位、方法、建構和結構體複製語意。
- [類別](classes.md)：共享身分、成員存取、建構、繼承和虛擬方法。
- [介面](interfaces.md)：方法契約、預設實作、介面繼承和型別轉換。
- [枚舉](enums.md)：列舉成員、作用域、底層整數類型和枚舉值使用。
- [陣列](arrays.md)：動態陣列、固定長度原生陣列、索引和範圍遍歷。
- [容器](containers.md)：映射、集合、佇列、堆疊、雙端佇列和通道。
- [字串](strings.md)：`string`、`wstring`、字串運算和標準函式庫輔助函式。
- [引用](references.md)：引用參數、唯讀引用以及引用範圍遍歷。
- [反射](reflection.md)：`type<T>`、欄位和方法資訊、枚舉資訊以及宣告元資料。
- [靜態成員](static-members.md)：靜態工具類型、類型級欄位、靜態方法和泛型類型中的獨立靜態儲存。

## 運算式和運算

- [運算子](operators.md)：算術、比較、邏輯、位元運算、賦值和運算子重載。

## 泛型、函式與控制流

- [泛型](generics.md)：泛型類型、泛型函式、預設型別參數和 `where` 限制。
- [函式](functions.md)：函式宣告、參數、傳回值、預設參數和重載。
- [Lambda 和函式值](lambdas.md)：函式型別、Lambda 運算式、擷取與參考參數。
- [控制流程](control-flow.md)：條件、循環、`switch`、跳躍與傳回。

[註記與原始碼中繼資料](annotations.md)

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/index.md) · [fr-FR](../../fr-FR/language-basics/index.md) · [ja-JP](../../ja-JP/language-basics/index.md) · [ko-KR](../../ko-KR/language-basics/index.md) · [zh-CN](../../zh-CN/language-basics/index.md) · [zh-TW](index.md)
