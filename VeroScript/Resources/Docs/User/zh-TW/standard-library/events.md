# 事件模組

模組檔案：`delegate.vs`

匯入方式：

```VeroScript
import "delegate.vs";
```

公開腳本建議使用 `std::delegate` 和 `std::multicast_delegate`。未限定寫法對應同一型別，兩種寫法都需要匯入 `delegate.vs`。成員名稱統一使用小寫。

## 單播委託

`delegate` 表示最多帶三個參數的單一處理函式。類型參數的第一個參數是傳回類型，後續參數是呼叫參數：

```VeroScript
std::delegate<int, int> mitigate = std::delegate<int, int>();
mitigate.bind(function(int value) -> int => value / 2);
int result = mitigate.execute(10);
```

支援 `delegate<R>`、`delegate<R, A>`、`delegate<R, A, B>` 和 `delegate<R, A, B, C>`。成員如下：

| 成員 | 說明 |
| --- | --- |
| `bind(handler)` | 綁定或取代處理函式。 |
| `unbind()` | 清除目前處理函式。 |
| `is_bound()` | 判斷是否已經綁定。 |
| `execute(...)` | 執行處理函式；未綁定時會觸發斷言。 |
| `execute_if_bound(...)` | 僅在已綁定時執行，並傳回是否執行。 |

`execute_if_bound` 只傳回是否執行，不回傳處理函式的結果。

`delegate` 的處理函式欄位是私有的。`unbind()` 會清空委託保存的處理函式參照；請透過 `is_bound()` 查詢狀態。

## 多播委託

`multicast_delegate` 儲存一組傳回 `void` 的處理函式，支援零到三個參數：

```VeroScript
std::multicast_delegate<int> on_score = std::multicast_delegate<int>();
on_score.add(function(int value) -> void => print(value));
on_score.broadcast(42);
```

成員如下：

| 成員 | 說明 |
| --- | --- |
| `add(handler)` | 新增處理函式。 |
| `remove(handler)` | 刪除符合的處理函式。 |
| `broadcast(...)` | 以目前註冊順序呼叫全部處理函式。 |
| `count()` | 以非負 `int64` 傳回處理常式數量。 |
| `clear()` | 清空處理函式。 |

廣播開始時會先複製處理函式列表，因此廣播期間對註冊列表的修改不會改變目前這一輪已經確定的呼叫快照。

`multicast_delegate` 的 `handlers` 欄位在原始碼中是公開欄位；需要保持註冊順序和快照語意時，應優先使用 `add`、`remove` 和 `clear`。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/events.md) · [fr-FR](../../fr-FR/standard-library/events.md) · [ja-JP](../../ja-JP/standard-library/events.md) · [ko-KR](../../ko-KR/standard-library/events.md) · [zh-CN](../../zh-CN/standard-library/events.md) · [zh-TW](events.md)
