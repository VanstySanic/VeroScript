# 列舉

枚舉使用一組有名稱的整數成員表示有限的值集合。 VeroScript 支援普通枚舉和作用域枚舉（`enum class`），並允許指定底層整數類型。

## 宣告枚舉

普通枚舉的成員預設從 `0` 開始，後續成員依序遞增：

```VeroScript
enum State
{
	Stopped,
	Running,
	Paused
};
```

也可以為成員指定整數值。沒有顯式值的成員會在前一個成員的基礎上遞增：

```VeroScript
enum ErrorCode
{
	None = 0,
	NotFound = 404,
	Timeout = 408
};
```

列舉成員的值必須是底層型別能夠表示的整數常數運算式。支援受檢運算、已宣告的先前列舉成員及合格的 [constexpr 呼叫](../language-reference/constexpr.md)；向前參照、循環和溢位會報錯。

## 作用域枚舉

使用 `enum class` 可以將成員限制在枚舉類型的作用域內：

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u
};

RunState state = RunState::Running;
```

作用域枚舉的成員不會以裸名稱出現在外層作用域中，因此更適合用於成員名稱可能重複、或需要明確區分不同枚舉類型的場景。存取成員時，檔案和新程式碼建議使用 `Type::Member` 形式。

普通枚舉的成員可以直接使用：

```VeroScript
State state = Running;
```

## 底層整數型

如果沒有指定底層類型，則枚舉預設使用 `int`。需要明確控制儲存範圍時，可以使用固定寬度整數型別，例如 `int8`、`uint8`、`int16`、`uint16`、`int32`、`uint32` 和 `int64`：

```VeroScript
enum class ByteState : uint8
{
	Empty = 0u,
	Full = 1u
};

enum class SignedLimit : int16
{
	Minimum = -32768,
	Maximum = 32767
};
```

無符號底層類型不能表示負數。明確指定底層類型後，成員值必須落在該類型可表示的範圍內。

## 使用枚舉值

枚舉值可以參與比較，也可以作為容器的鍵或元素：

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u,
	Paused = 2u
};

map<RunState, string> labels = map<RunState, string>();
labels[RunState::Running] = "running";

set<RunState> states = set<RunState>();
states.add(RunState::Paused);
```

列舉型別適合表達有限且穩定的狀態集合。對於 `enum class`，應保留其列舉型別語意，不要把它當作可以與任意整數自由混用的普通整數。

## 匯出枚舉

枚舉可以使用 `export` 匯出，供其他模組透過 `import` 使用：

```VeroScript
export enum class Operation : uint8
{
	Add = 1u,
	Multiply = 2u
};
```

匯入模組後，使用完整的枚舉類型名稱存取作用域枚舉成員：

```VeroScript
Operation operation = Operation::Add;
```

巢狀型別、區域宣告、存取規則與分段泛型實參請參閱[型別範圍](scope.md)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/enums.md) · [fr-FR](../../fr-FR/language-basics/enums.md) · [ja-JP](../../ja-JP/language-basics/enums.md) · [ko-KR](../../ko-KR/language-basics/enums.md) · [zh-CN](../../zh-CN/language-basics/enums.md) · [zh-TW](enums.md)
