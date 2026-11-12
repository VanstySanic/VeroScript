# Lambda 和函式值

Lambda 是可以儲存、傳遞和呼叫的函式值。函式類型使用 `function` 表示，`fn` 是它的簡寫形式。

## 函式類型

函式類型由參數列表和傳回類型組成。建議在箭頭兩側保留空格，以便在長宣告中保持可讀性：

```VeroScript
function(int) -> int increment = function(int value) -> int => value + 1;
function() -> string get_name = function() -> string => "VeroScript";
function(int&, int) -> void mutate = function(int& value, int amount) -> void
{
	value += amount;
};
```

函式值可以作為參數、回傳值或局部變數：

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

function(int) -> int double_value = function(int x) -> int => x * 2;
int result = apply(double_value, 21);
```

`fn(int) -> int` 與 `function(int) -> int` 等價。公共檔案和較長的宣告通常使用完整的 `function` 拼字。

## Lambda 運算式

Lambda 可以使用運算式體，也可以使用程式碼區塊體：

```VeroScript
function(int) -> int increment = function(int x) -> int => x + 1;

function(int) -> int square = function(int x) -> int
{
	return x * x;
};
```

呼叫函式值的圓括號語法與呼叫普通函式相同：

```VeroScript
int a = increment(41);
int b = square(5);
```

當函式型別包含引用參數時，呼叫點必須明確寫出引用標記。公共範例推薦使用 `T&` 和 `&expression`；`ref T` 與 `ref expression` 仍是相容寫入法：

```VeroScript
void add_to(int& value, int amount)
{
	value = value + amount;
}

function(int&, int) -> void mutate = add_to;
int value = 40;
mutate(&value, 2);
```

## 按值擷取

Lambda 可以捕捉外層作用域中的局部變數。普通 Lambda 和 `[=]` 使用按值擷取；明確清單只擷取列出的名稱：

```VeroScript
int base = 40;

function(int) -> int add = function(int x) -> int => x + base;
function(int) -> int add_explicit = [base](int x) -> int => x + base;
```

按值擷取在建立閉包時初始化，預設為唯讀。於參數列表後加上 `mutable` 才可修改自有擷取，修改會保留到下次呼叫。複製函式值共享同一環境；再次建立閉包則取得獨立環境。普通擷取的 const 來源仍是唯讀。

VM、Mixed、JIT 與 Core 支援型 AOT 共用執行服務。獨立 C++／C# 原始碼匯出支援值擷取及純量區域 static；參考初始化擷取、複雜靜態物件及外層 static 參考需要 Core 路徑。Python 原始碼匯出仍不支援 Lambda 或區域 static。

獨立 C++ 匯出中，擁有擷取或接收物件的 Lambda 若傳回參考，也需要 Core AOT：原生參考無法在函式值釋放後繼續保有閉包儲存。

空擷取清單 `[]` 不擷取外層局部變數，但全域名稱仍然可以透過普通名稱查找存取：

```VeroScript
function(int) -> int increment = [](int x) -> int => x + 1;
```

## 按引用擷取

VeroScript 支援按引用擷取。 `[&]` 將使用到的外層局部變數按引用擷取，`[&name]` 只按引用擷取指定變數；預設擷取也可以與明確按值擷取組合：

```VeroScript
int running = 40;
function() -> int next = [&]() -> int
{
	running++;
	return running;
};

print(next());
print(running);

int step = 2;
function() -> int advance = [=, &running]() -> int
{
	running += step;
	return running;
};
```

可用的擷取形式包括：

- `[&]`：使用到的外層局部變數按引用擷取。
- `[&name]`：`name` 依引用擷取。
- `[=]` 或普通 `function(...)` Lambda：使用到的外層局部變數以值擷取。
- `[=, &name]`：預設按值擷取，但 `name` 以引用方式擷取。
- `[&, name]`：預設按引用擷取，但 `name` 會以值擷取。
- `[]`：不捕捉外層局部變數。

按引用擷取的變數會被提升到安全的共用儲存中。即使閉包在建立它的函式傳回後仍然存在，也不會繼續引用已經失效的棧槽；外層變數、同一變數的別的閉包以及當前閉包看到的是同一個可變狀態。若外層綁定本身是常數，引用擷取會保留其唯讀性質。

明確擷取清單中的名稱必須來自允許的外層綁定，且不能重複宣告預設擷取已經涵蓋的同一種擷取方式。空擷取清單不能存取外層局部變數；全域變數和物件成員仍按各自的名稱查找與控制代碼語意處理。

## 成員函式傳回 Lambda

成員函式可以傳回擷取當前物件狀態的函式值：

```VeroScript
class Counter
{
public:
	int value;

	Counter(int initial)
	{
		value = initial;
	}

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

Counter counter = Counter(42);
function() -> int get = counter.getter();
int current = get();
```

這裡的 `value` 是物件成員，不是已經離開作用域的普通局部變數。閉包仍然遵循類別控制代碼和物件生命週期的語意；按引用擷取只改變擷取綁定的存取方式，不會把任意物件自動複製成值物件。

如果 Lambda 直接使用接收者本身，接收者會遵循其外層型別的語意。類別接收者
會以共用控制代碼保留，因此閉包可以觀察之後發生的欄位修改；結構接收者會以
值擷取為快照，因此之後對原結構的修改不會影響已擷取的接收者。巢狀 Lambda
也會按照相同規則保留或複製已擷取的接收者。無擷取 Lambda 不能使用 `this`。

## 與引用語意的關係

引用參數的呼叫規則、引用回傳值、可安全傳回的引用來源見[引用與引用語意](references.md)。按引用擷取屬於閉套件的擷取規則，不能用普通的按值擷取來替代。

完整範例請見[範例索引](../examples.md)中的 Lambda 範例。

## 初始化擷取、mutable 與區域 static

使用 `auto` 時，應明確寫出 Lambda 傳回型別，例如 `() mutable -> void { ... }`。若已有 `function() -> void` 等函式型別上下文，Lambda 可以省略傳回型別箭頭。這沿用既有的上下文推導規則；`mutable` 本身不會從函式本體推導傳回型別。

區域靜態常數的 `static const int value = 42;` 與 `const static int value = 42;` 等價，建議統一使用 `static const`。

```VeroScript
function() -> int make_counter()
{
	return [count = 0]() mutable -> int { return ++count; };
}

int remember_first(int seed)
{
	static int value = seed;
	return ++value;
}

void capture_example()
{
	auto next = make_counter();
	auto alias = next;
	assert(next() == 1 && alias() == 2);
	int total = 0;
	auto advance = [&target = total, step = 2]() -> int { target += step;
		return target; };
	assert(advance() == 2 && total == 2);
	assert(remember_first(10) == 11 && remember_first(99) == 12);
}
```

初始化擷取的右側在外層作用域由左至右求值一次，新名字僅於正文可見。`[&name = expression]` 需要可安全保留的左值，並保留 const。全域與型別、區域 static 仍直接查找；建立快照請使用初始化擷取。

區域 static 在首次執行到宣告時初始化，後續呼叫共享；同一 Lambda 表達式的閉包共享函式 static。初始化失敗且清理成功後可重試，遞迴初始化報錯。程式狀態、Session 及 worker 互相隔離，清空狀態後重新開始。不支援 static 參考變數。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/lambdas.md) · [fr-FR](../../fr-FR/language-basics/lambdas.md) · [ja-JP](../../ja-JP/language-basics/lambdas.md) · [ko-KR](../../ko-KR/language-basics/lambdas.md) · [zh-CN](../../zh-CN/language-basics/lambdas.md) · [zh-TW](lambdas.md)
