# 重載解析

當同一作用域中存在多個同名函式、方法或運算子時，編譯器會根據呼叫上下文選擇唯一的可呼叫候選。

## 候選函式

重載解析首先根據名稱和呼叫形式收集候選，然後檢查：

- 參數數量是否匹配；
- 實參型別是否符合參數型別的轉換規則；
- `ref`、`const ref` 或 `&` 標記是否與參數宣告一致；
- 泛型類型參數是否能夠推導，且 `where` 約束是否滿足；
- 預設參數是否能夠補齊省略的位置。

傳回類型通常不單獨用於區分同名重載：

```VeroScript
int parse(int value)
{
	return value;
}

string parse(string value)
{
	return value;
}
```

提供實參時，編譯器根據實參型別和可用轉換選擇更匹配的候選；如果沒有候選或多個候選同樣匹配，編譯會報告診斷。

候選匹配會考慮隱式轉換，包括相容的數值轉換和未標記為 `explicit` 的單值轉換建構函式。`explicit` 建構函式不會參與普通指派或引數轉換；需要透過直接建構或 `cast<T>` 明確要求。編譯器不會串聯多個使用者自訂轉換。

## 引用參數

引用標記是參數匹配的一部分，呼叫時必須明確寫出：

```VeroScript
void update(int& value)
{
	value = value + 1;
}

void update(const int& value)
{
	print(value);
}

int number = 1;
update(&number);
```

不要省略引用標記，也不要把唯讀引用當作可寫引用使用。涉及引用重載時，明確標記可以避免候選選擇不明確；`ref T` 和 `ref expression` 仍可作為相容寫法。

## 泛型重載

泛型候選只有在類型參數能夠確定、約束滿足且參數可轉換時才有效：

```VeroScript
T identity<T>(T value)
{
	return value;
}

int number = identity(42);
string text = identity<string>("text");
```

需要消除推導歧義時，可以提供明確類型參數，或調整函式參數使目標類型能夠確定。

## 運算子重載

運算子重載也遵循候選匹配規則。成員運算子包含隱含的目前對象，非成員運算子函式使用明確運算元；參數和傳回類型必須滿足該運算子的實作規則。運算子重載的宣告方式請參閱[運算子](../language-basics/operators.md)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-reference/overload-resolution.md) · [fr-FR](../../fr-FR/language-reference/overload-resolution.md) · [ja-JP](../../ja-JP/language-reference/overload-resolution.md) · [ko-KR](../../ko-KR/language-reference/overload-resolution.md) · [zh-CN](../../zh-CN/language-reference/overload-resolution.md) · [zh-TW](overload-resolution.md)
