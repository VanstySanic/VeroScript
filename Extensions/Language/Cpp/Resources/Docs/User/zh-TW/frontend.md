# C++ 前端

## 專案合併與表示邊界

多檔案合併不是 linker：它不會依輸入順序選擇定義、不會重新命名內部連結
實體，也不會替任意 C++ 所有權或儲存規則進行解析。`std::shared_ptr<T>`
不會直接抹除為 `T`；共享所有權需要明確的配接器。`std::size_t` 和
`std::ptrdiff_t` 依目標 ABI 對映，指標及其指向物件的 cv 限定屬於多載身分
的一部分。

`VeroScriptCppFrontend` 將有意限定的 C++17 子集解析為穩定的公開 AST，供 [BindGen](bindgen.md) 和 [Transpiler](transpiler.md) 共用。無法支援的構造會產生診斷，絕不會靜默近似。

公開 API 包括 `ParseSource`、`ParseFile`、`ParseFiles`、`MergeTranslationUnits`、`MapTypeToVeroScript`、`ResolveTypeAliases`、`EvaluateEnumValues`、`TryGetEnumValueRange`、`GetFunctionIdentity`、`FormatDiagnostic` 和 `JoinTokens`。AST 保留原始碼位置、命名空間、存取控制、限定名稱、建構函式、欄位、函式、列舉、基底型別、參照種類、指標及指向物件的 cv 限定，以及 `constexpr`、`consteval`、`constinit`、`noexcept` 等語意修飾詞。

`FParseOptions` 控制 include、`#pragma`、未知前處理指令的拒絕策略及函式本體解析。`FTypeMapOptions` 明確指定已知宿主型別的原始指標、右值參照、已知宿主型別、列舉、型別參數及忽略 include 時所需別名的對映策略。應主動設定這些策略；允許解析某個宣告不代表它一定可以自動繫結或轉譯。

支援命名空間、`using Name = Type`、普通 `typedef Type Name`，以及帶型別參數和末尾預設型別參數的類別/函式/別名樣板；還支援有作用域和無作用域列舉、類別/結構體、存取指定詞、欄位、建構函式、方法、自由函式、全域變數、定寬整數、標準字串以及擁有 VeroScript 對應型別的標準容器。普通和 `u8` 原始字串會按完整分隔符掃描並正規化為 UTF-8 逸出字面值；寬原始字串會被拒絕。簡單的同型別宣告器清單會展開，混合指標/參照宣告器清單仍不支援。命名的前置宣告可以與定義合併。

類別外的建構函式、解構函式、方法、運算子和傳統靜態資料成員定義可以與宣告配對，也可以放在專案的其他輸入檔案中。C++17 屬性會在記錄、列舉、列舉成員、欄位、函式和參數上以結構形式保留。列舉初始化器支援可檢查的整數及可攜單一字元表達式，保留目標 ABI 的整數字面值寬度與符號性，遵循 C++ 整數提升，允許無符號回繞，並拒絕有符號溢位和非法移位量。最終值必須能以公開的 `int64` 表示且符合映射後的底層型別範圍。負有符號數的移位以及短路 `&&`／`||` 表達式仍不支援。

未指定底層型別的無作用域列舉，引用先前成員時保留初始化表達式的型別；固定底層型別的列舉按該型別進行整數提升。依賴此差異的 MSVC 宿主必須啟用 `/Zc:enumTypes`（VS 2022 17.4 或更高），或為這些列舉明確指定底層型別。此選項預設關閉，`/permissive-` 也不會啟用；SDK 不會修改宿主編譯選項。

預處理器邊界很小：支援 `#pragma once`、include、常規完整檔案標頭保護，以及 `extern "C"` 宣告或宣告區塊；一般條件預處理和未知指令會被拒絕。多檔案收集會合併相容的外部宣告，但會拒絕重複定義、重複輸入路徑、內部連結衝突和匿名命名空間。

非型別樣板參數、參數包、特化、SFINAE、concept、相依編譯期表達式、union、C 陣列、匿名命名空間、未知前處理指令以及無法表示的指標均不支援。型別、函式和成員 friend 宣告會作為記錄級宣告保留，但 inline friend 定義會被拒絕。允許的原始指標策略最多可放行指向已知宿主型別的單一可變指標；純量指標、指向 const 的指標和多層級指標仍會被拒絕。

型別查找優先目前宣告作用域，支援相對限定名稱與 `::` 絕對限定名稱。已宣告的使用者型別優先於 `string`、`vector` 等便利映射名稱；無法解析的限定型別會報錯。

記錄、列舉及已繫結的樣板參數優先於外層別名。手動解析別名時，可使用 `ResolveTypeAliases` 的三參數多載，透過查找選項提供 `KnownHostTypes`、`KnownEnumTypes` 和 `KnownTypeParameters`。

空的 `FType::DeclarationScope` 表示全域作用域，別名替換時仍會保留。`FBaseType::Type` 保留基底類別的結構化型別、查找作用域和樣板參數。`bQualifyNamedTypes` 可為原始碼產生要求絕對限定的使用者型別名稱；預設仍傳回繫結目錄使用的標準名稱。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](frontend.md)
