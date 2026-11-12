# C++ 程式碼規範

適用範圍：[VeroScript 本專案開發與貢獻](README.md)。

本檔案是 C++ 的當前權威規範。除非明確寫為“推薦”或“按需”，本檔案中的規則均視為
必須遵守的格式和結構規則。

## 1. 縮排、作用域與花括號

- 一個邏輯層級使用一個 Tab 縮排；命名空間、型別、方法、條件、迴圈、lambda 和區域作用域
  都必須增加一級縮排。
- 左花括號必須獨佔一行，且與其所屬宣告或控制語句對齊：

  ```cpp
  void Process(const FRequest& Request)
  {
  	if (Request.bEnabled)
  	{
  		Execute(Request);
  	}
  }
  ```

- 不強制在左花括號後或右花括號前留空行；空行只用於分隔不同的邏輯分塊。
- 右花括號與對應左花括號對齊。`else`、`else if`、`catch` 和 `do` 尾部的 `while` 必須另起一行，與對應右花括號對齊；子句的左花括號也獨佔一行：

  ```cpp
  if (bPrimary)
  {
  	HandlePrimary();
  }
  else
  {
  	HandleFallback();
  }
  ```

- 所有具有執行體的控制分支必須使用花括號和獨立作用域，包括 `if`、`else`、`for`、
  `while`、`do`、範圍 `for` 和每個 `switch case`。
- 唯一允許省略花括號的執行分支，是**分支體僅含一條直接 `return` 的單行衛語句**：

  ```cpp
  if (!bInitialized) return;
  ```

  此例外只適用於 `return;` 或 `return <表示式>;`，且不得與 `else` 鏈、清理、日誌、賦值、
  巨集條件或巢狀邏輯組合。`break`、`continue`、`throw`、`co_return` 以及任何包含多條語句的
  分支都不是例外，必須使用花括號作用域。

## 2. 型別整體佈局

型別內容按以下順序安排。沒有內容的分塊不寫空分塊。

1. 型別頂部按需放置 `friend` 宣告、`using`、`typedef` 等關係或別名；
2. 隨後放置型別需要的巨集，例如 `DECLARE_XXX`；
3. 訪問區域固定按 `public`、`protected`、`private` 的順序出現；
4. 每個訪問區域內，先放繼承而來的 `override` 方法；
5. 再放內部型別；
6. 然後放本型別的方法；
7. 最後放成員變數。

建議骨架：

```cpp
class FExample : public IBase, protected ISecondary
{
	friend class FExampleFactory;
	using FBase = IBase;

	DECLARE_EXAMPLE_TYPE(FExample)

public:
	//~ Begin IBase Interface
	virtual void Initialize() override;
	virtual void Shutdown() override;
	//~ End IBase Interface

	enum class EState
	{
		Idle,
		Running,
	};

	struct FOptions
	{
		bool bEnabled = false;
	};

	void Start(const FOptions& Options);

protected:
	//~ Begin ISecondary Interface
	virtual bool CanStart() const override;
	//~ End ISecondary Interface

	void NotifyStarted();

private:
	void ResetState();

	EState State = EState::Idle;
	FOptions Options;
};
```

### 2.1 訪問區域

- `public`、`protected`、`private` 必須按此順序安排。
- 在一個型別中，同一訪問級別應儘量只出現一個連續區域。只有語言、巨集展開或清晰的語義邊界
  確有必要時才重複訪問說明符；重複後仍不得破壞總體順序。
- 成員變數按其實際訪問屬性放在對應區域的最後，不把資料夾在方法或內部型別之間。

### 2.2 繼承方法

- 在每個訪問區域中，`override` 方法優先於本型別新定義的方法。
- 多個基類時，按型別繼承列表從左到右的順序分組；同一基類內按該基類介面宣告順序排列。
- 當一組繼承方法不止一個、所屬基類不直觀，或與本型別方法相鄰容易混淆時，使用配對標記：

  ```cpp
  //~ Begin ISerializable Interface
  virtual bool Serialize(FArchive& Archive) override;
  virtual bool Deserialize(FArchive& Archive) override;
  //~ End ISerializable Interface
  ```

- `Begin` 和 `End` 必須成對、名稱完全一致，使用直接基類或介面名。只有一個明顯的 `override`
  方法時按需省略該標記，不能為每一個函式機械新增。

### 2.3 內部型別

同一訪問區域內的內部型別固定按以下優先順序排列：

1. `enum`、`enum class`；
2. `struct`；
3. 其他型別宣告，例如 `union`、區域別名、函式物件型別；
4. `class`。

- 內部型別各分塊之間空一行；同類內部型別按依賴關係和名稱形成易讀順序。
- 本應放在型別頂部的普通 `using`、`typedef` 不應無故留到內部型別區域。只有別名依賴前面的
  內部型別時，才放在“其他型別宣告”位置。
- 若完整定義的依賴關係與此順序衝突，優先使用前向宣告解除依賴；確實必須打破順序時，在
  緊鄰位置說明完整型別依賴原因。

## 3. 標頭檔案、前向宣告與實作檔案

### 3.1 標頭檔案與按需實作檔案

- 每個自有 C++ `.h` 檔案的檔案頭固定為：兩行 SPDX 版權與許可證宣告後空一行，再寫 `#pragma once`。`#pragma once`
  後空一行，再開始 include 或其他宣告。

  ```cpp
  // SPDX-FileCopyrightText: <copyright-holder>
  // SPDX-License-Identifier: MIT

  #pragma once

  #include <cstdint>
  ```

- `.inl` 是被包含的實作片段，不等同於 `.h`。它同樣必須以兩行 SPDX 版權與許可證宣告開始，宣告後空一行，
  但預設**不**寫 `#pragma once`，也不要求配套 `.cpp`；只有確認該檔案是每個翻譯單元僅能包含一次的
  標頭檔案式內容時，才按 `.h` 的方式使用 `#pragma once`。

- `.h` 不強制配套同名 `.cpp`。只有實作或建置職責確實需要獨立編譯單元時，才按職責組織 `.cpp`；不為檔案數量或目錄對稱建立空實作檔案。
- 僅含宣告、型別定義、常數、範本或 `inline` 定義的標頭檔案可以獨立存在。必須在標頭檔案中可見的定義繼續保留在標頭檔案中，不因檔案拆分移走。
- 確需 `.cpp` 時，將其放在同一模組的實作位置，並顯式包含所實作的標頭檔案。沒有 `Public/`、`Private/` 分層時通常同目錄；有分層時位於 `Private/` 的合理語義子目錄，相對路徑無需完全對應 `Public/`。
- 僅有實作職責的 `.cpp` 可以沒有同名 `.h`。
- 清理既有空 `.cpp` 時，核對其實際建置、註冊、顯式實例化及平台職責，並同步更新建置引用。
- 第三方和自動生成檔案保留其適用的檔案組織方式。

### 3.2 宣告與定義順序

- `.h` 中宣告瞭、且需要在 `.cpp` 實作的方法，`.cpp` 必須按 `.h` 中的宣告順序定義。
- 分組也必須保持一致：繼承介面實作先後、本型別公開/保護/私有方法的相對順序不得在 `.cpp`
  中打亂。
- `.cpp` 的匿名命名空間輔助函式、檔案區域常量和僅實作需要的型別可以位於方法定義前；它們
  不改變標頭檔案宣告對應定義的相對順序。
- 新增宣告時，應同時確認 `.cpp` 的定義位置，而不是把實作統一追加到檔案末尾。

### 3.3 前向宣告與同頭多型別宣告

- 所有前向宣告按以下優先順序排列：`enum` → `struct` → 其他宣告 → `class`。
- 前向宣告的 `enum` 必須採用 C++ 合法的 opaque-enum 形式：`enum class EName;`，或顯式指定
  底層型別的 `enum EName : std::uint8_t;`。不得宣告沒有底層型別的非作用域 `enum EName;`。
- 同一個標頭檔案中可以定義多個頂層型別；在不受完整型別依賴約束時，也按：
  `enum` → `struct` → 其他宣告 → `class`。
- “其他型別宣告”包括 `union`、`using`/`typedef` 別名和函式型別別名。類别範本按 `class` 排列，
  結構範本按 `struct` 排列；不能機械地把所有 `template` 宣告歸入“其他”。
- 如果完整型別依賴要求不同順序，先嚐試前向宣告；無法避免時，依賴關係優先，並在相鄰處
  留下簡短說明。

### 3.4 成員變數預設值與布林位域

- 成員變數需要初始值，且預設狀態能在宣告處直接、清晰地表達時，應在宣告處給出預設值。
- 依賴建構參數、執行期狀態或其他成員初始化結果的值，應在建構函式初始化列表中設定；不要為了遵循本條而寫出誤導性的固定預設值。
- 僅表示開關狀態、適合壓縮儲存，且不需要取地址、引用、原子操作或穩定外部二進位制佈局的布林成員，優先使用 1 位位域。例如：`uint8 bEnabled : 1;`（或 `std::uint8_t bEnabled : 1;`）。位寬寫作 `: 1`；`uint8` 是儲存型別，不是位寬。
- 本專案當前最低標準為 C++17。C++17 的位域不能使用類內預設成員初始化；位域需要非零或明確初始狀態時，應由建構函式初始化列表初始化。升級到支援該語法的語言標準前，不要寫 `uint8 bEnabled : 1 = true;`。

## 4. `switch` 與執行分支

- 每一個 `switch` 必須包含 `default`。
- `case` 和 `default` 與 `switch` 內容層級對齊；標籤下的執行體花括號整體再縮排一級，花括號內的
  語句再縮排一級：

```cpp
switch (Kind)
{
case EKind::First:
	{
		HandleFirst();
		break;
	}
case EKind::Second:
	{
		HandleSecond();
		break;
	}
default:
	{
		HandleUnknown();
		break;
	}
}
```

- `case` 標籤若唯一執行路徑是直接結束當前方法的 `return` 或 `return <表示式>`，可以沿用第 1 節的
  單行衛語句例外；該標籤下不得混入日誌、清理、區域宣告、條件或其他語句。除此以外，所有
  `case` 和 `default` 都必須使用上述獨立花括號作用域。
- `default` 即使按設計不執行任何業務動作，也必須保留花括號作用域和顯式 `break`。只有“不處理”的
  原因不從上下文顯而易見時，才新增簡短英文註解說明該選擇。
- 不依賴隱式 fallthrough。需要貫穿時，使用專案允許的顯式標記並說明原因。
- 除本規範第 1 節嚴格定義的直接 `return` 衛語句例外外，所有執行分支都使用花括號。

## 5. 預處理巨集

- `#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、`#endif` 與它們包裹程式碼所在的**同一邏輯層級**
  縮排；預處理條件不額外增加一層縮排。
- 巨集包裹的程式碼在移除巨集後應保持原來的層級和可讀性：

  ```cpp
  void Initialize()
  {
  	#if WITH_DIAGNOSTICS
  	RegisterDiagnostics();
  	#endif

  	StartRuntime();
  }
  ```

- 條件巨集必須儘量覆蓋完整的語義分塊，不把單條宣告或單個表示式切成難以閱讀的碎片。
- 成對巨集必須保持相同縮排，`#endif` 可按需新增簡短的條件註解以幫助識別較長分塊。
- 巨集生成的宣告仍按本檔案的型別佈局順序放置。

## 6. 註解與區域

- 單行說明使用 `//`，放在所說明宣告或分塊的緊鄰上方；短行尾說明只用於不會妨礙閱讀的情況。
- 多行 API 語義、所有權、執行緒、失敗條件或複雜不變數使用 `/** ... */` 文件註解。
- 不用註解重複名稱明顯表達的內容，例如 `// Gets the name` 後接 `GetName()`。
- 需要解釋的成員變數優先說明單位、所有權、有效期、執行緒約束、快取失效條件或與其他欄位的關係。
- `//~ Begin <Name> Interface` / `//~ End <Name> Interface` 只用於繼承介面分塊；不能被濫用為
  普通方法區標題。
- 其他較大的分塊可以使用簡短說明註解，但優先透過本規範規定的固定佈局讓結構自解釋。

## 7. 其他推薦規則

- 當前不規定 `.cpp` 或 `.h` 的 include 順序或分組方式；遵循當前檔案的可讀性和構建約束即可。
  前向宣告的佈局仍遵循第 3.3 節。
- 使用 `nullptr` 表達空指標，使用顯式轉換表達可能損失資訊的型別轉換。
- 資源所有權、可能為空的物件和跨執行緒回撥必須在型別或方法註解中清楚表達。
- 空函式、預設實作和刻意忽略參數應透過最短且可讀的方式表達意圖；不要為了滿足形式而製造
  無意義區域變數或空作用域。

## 8. 待維護者補充

以下主題尚未形成硬性規則，後續由維護者決定後補充：命名細節、範本/概念、異常策略、智慧指標
策略、測試框架、格式化工具和行寬。include 順序當前明確不作規定。
