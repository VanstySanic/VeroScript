# 變數

變數是具有名稱和類型、可以在執行階段儲存值的宣告。變數可以在模組、函式、程式碼區塊、結構體或類別中宣告。

## 宣告

變數宣告指定型別、名稱和初始化運算式：

```VeroScript
int count = 0;
int limit = 10;
string name = "VeroScript";
```

區域變數和模組級變數必須初始化，`int count;` 是無效寫法。可以提供明確值、建構呼叫或大括號初始化。結構體和類別的欄位可以省略初始化運算式，此時使用對應型別的欄位預設值規則。

## 初始化

VeroScript 支援使用構造呼叫和花括號初始化：

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
```

使用者定義類型可以直接呼叫建構函式：

```VeroScript
struct Point
{
	int x;
	int y;
};

int main()
{
	Point point = Point();
	point.x = 10;
	point.y = 20;
}
```

## 賦值

變數宣告完成後，可以透過賦值運算式修改其值：

```VeroScript
int value = 1;
value = 2;
value += 3;
```

賦值兩側的類型必須滿足語言規定的可轉換關係。賦值不會改變變數的靜態型別。

## 局部變數和全域變數

函式或程式碼區塊中的變數是局部變數。模組頂層宣告的變數是全域變數，可以被同一模組中的函式存取：

```VeroScript
int global_count = 0;

void increment()
{
	global_count += 1;
}
```

跨模組存取全域變數需要滿足模組匯入和匯出規則。變數的可見範圍與其作用域不同：作用域描述名稱在哪裡有效，可見性規則也會受到模組和存取修飾符影響。

## `auto` 變數

`auto` 根據初始化運算式推導類型：

```VeroScript
auto answer = 42;
auto text = "hello";
```

`auto` 變數必須初始化。類型推導完成後，變數仍然是靜態類型變數，不能在後續賦值中變成另一種類型。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/variables.md) · [fr-FR](../../fr-FR/language-basics/variables.md) · [ja-JP](../../ja-JP/language-basics/variables.md) · [ko-KR](../../ko-KR/language-basics/variables.md) · [zh-CN](../../zh-CN/language-basics/variables.md) · [zh-TW](variables.md)
