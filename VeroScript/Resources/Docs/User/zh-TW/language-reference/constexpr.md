# constexpr 編譯期常數

`constexpr` 要求編譯期純量值，並隱含 `const`。適用於區域、模組及命名空間變數，以及明確標示 `static constexpr` 的欄位。

自由函式、命名空間函式與靜態方法可宣告為 `constexpr`，包含支援的泛型形式。按值參數與非 void 回傳值限於 bool、固定位元寬度整數、char 和列舉。本文支援已初始化的純量區域變數、賦值、分支、迴圈、switch 與有限遞迴；一般執行期呼叫仍按一般函式執行。

一般 if 的全部分支都會檢查；if constexpr 只檢查選中分支。編譯期不能呼叫原生回呼、讀取可寫全域狀態、建構物件，或使用參考、浮點數、字串、容器、例外及非同步工作。一般函式參數不能初始化明確宣告的 constexpr 區域變數。

常數初始化、值範本參數及預設值、固定陣列長度、整數／列舉 case 標籤與列舉值使用同一精確求值器。溢位、無效轉換、除零及預算耗盡會明確報錯，不會改成執行期初始化。短路求值不會執行未選取分支中的算術。

匯出的 constexpr 使用一般 import。透過核心編譯與封裝服務建立不依賴來源工程的 `.vslib`，保留定義環境與私有輔助宣告；載入時不會重新執行編譯處理器。

宿主透過 `FCompileOptions::ConstantEvaluationLimits` 或直接編譯器的 `SetConstantEvaluationLimits` 設定預算。預設工作量 1,000,000、呼叫深度 64、運算式／原生遞迴深度 256、暫存 16 MiB、快取 8 MiB。限額必須為正，快取零表示停用。分析取消與期限有效，VM 執行限額獨立。

反射欄位、方法、函式與全域描述符提供 `is_constexpr()`；`std::compiler` 宣告檢視提供 `is_constexpr`。查詢不執行常數函式。Core AOT 使用相同位元碼；獨立原始碼匯出器輸出已驗證的帶型別常值，並保留既有泛型限制。

```VeroScript
constexpr int square(int value)
{
	return value * value;
}
constexpr int capacity = square(8);

int main()
{
	constexpr int count = capacity + 1;
	int input = 3;
	print(count);         // 65
	print(square(input)); // 9
	return 0;
}
```

[constexpr.vs](../../../../Scripts/examples/constexpr.vs)

獨立原始碼匯出保留既有能力限制：C++ 使用已確定的封閉陣列長度；C#／Python 原有不支援的固定陣列，以及具有執行個體相關常數的泛型，使用 Core AOT。函式預設引數繼續受既有常值／範本值運算式規則限制。

## if constexpr 編譯期分支

條件必須是 bool、整數、char 或非 scoped 列舉常數；scoped 列舉需要明確轉換。支援 constexpr 呼叫與模板值，不接受一般執行期參數。每個模板實例獨立選擇；所有分支必須語法正確，只有選中分支參與型別檢查、後續實例化、回傳路徑檢查與 Lambda 擷取。選中分支有自己的區塊作用域。一般函式採用相同規則，一般 if 仍檢查兩側。

```VeroScript
template<int N>
int sum()
{
	if constexpr (N == 0)
		return 0;
	else
		return N + sum<N - 1>();
}
```

constexpr 函式也支援此語句，但一般參數即使在編譯期呼叫時有值，也不能用作條件。VM、JIT 與 Core AOT 使用相同的選擇結果；實例相關泛型的獨立原始碼匯出繼續使用 Core AOT。

區域非 scoped 列舉的裸成員名稱也可作為常量條件；名稱遮蔽遵循一般詞法作用域。延遲求值的 const 初始化式保留宣告時環境，不受後續同名宣告影響。Lambda 使用外層列舉常量不需捕獲執行期儲存；模板內區域列舉按具體實例求值。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-reference/constexpr.md) · [fr-FR](../../fr-FR/language-reference/constexpr.md) · [ja-JP](../../ja-JP/language-reference/constexpr.md) · [ko-KR](../../ko-KR/language-reference/constexpr.md) · [zh-CN](../../zh-CN/language-reference/constexpr.md) · [zh-TW](constexpr.md)
