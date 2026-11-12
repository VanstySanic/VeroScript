# Python 值、容器與生命週期

## 型別化容器與引用

公開工廠包括 `Value.array`、`Value.map`、`Value.set`、`Value.queue`、
`Value.priority_queue`、`Value.stack`、`Value.deque` 和 `Value.channel`。
這些工廠要求明確提供元素型別或鍵和值型別，讓空容器也不會產生型別歧義。
一般 `map` 和 `set` 具有唯一元素語意；需要多重映射或多重集合時，請使用相應的
執行階段或宿主繫結。`to_python()` 在支援時回傳快照，修改快照不會寫回腳本容器。

`HostParameter(..., by_ref=True)` 表示以 `Value` 傳入的可變引用，
`const_ref=True` 表示唯讀引用。兩個旗標不能同時設定；呼叫失敗或明確 `exit`
後不會執行引用寫回。

## channel 公開 API

精確的方法名稱是 `send`、`try_send`、`send_for`、`receive`、`try_receive`、
`receive_for` 與 `close_channel`。`try_send` 和 `send_for` 回傳 bool；非阻塞或
限時接收回傳值或 `None`。使用 `count`、`channel_capacity` 和 `channel_closed`
讀取目前狀態。

Python SDK 的 `Value` 表示由 Interop ABI 管理的腳本值。每個 `Value` 擁有獨立的原生值控制代碼；工作階段操作與保留呼叫物件還受工作階段生命週期約束。持有宿主物件的獨立值及其協定在原工作階段關閉後仍可使用，詳見[宿主繫結](host-bindings.md)。

支援整數、浮點數、布林值、字串、寬字串、列舉、陣列、序列、映射、集合和通道的型別化存取。容器讀取結果是快照，修改 Python 端的列表、字典或集合不會直接修改腳本容器。

`Session.retain_callable()` 回傳 `RetainedCallable`：

```Python
from veroscript import Session

with Session() as session:
	session.load_source("""
int add(int left, int right) { return left + right; }
function(int, int)->int make_callback() { return add; }
void main() {}
""", "callback.vs")
	function_value = session.call("make_callback")
	with session.retain_callable(function_value) as callback:
		result = callback(20, 22)
		assert result == 42
```

保留呼叫物件屬於建立執行緒，工作階段重載或關閉後可能失效。使用前檢查 `is_valid`，完成後關閉。map/set 反覆運算及 `keys()`、`values()`、`items()` 傳回擁有的快照，可以保留。純量轉換為一般 Python 值，其餘元素具有獨立擁有的 `Value` 控制代碼。一般 map/set 的順序未規定；`ordered=True` 保持首次插入順序。這些讀取操作不支援直接寫回元素；修改時應呼叫 VeroScript 函式或主控端回呼。型別、執行緒與回呼錯誤分別以 `TypeMismatchError`、`ThreadError`、`CallbackError` 回報。

`Value(None)` 會建立 VeroScript 的 `null` 值。將 `None` 指派給宣告為不可為
null 的值時會擲出 `TypeError`。使用 `Value.priority_queue("int", [3, 1, 2])`
建立優先佇列；一般 `map` 和 `set` 不會保留重複項目。

`kind`、`type_name` 和 `value` 屬性會顯示目前表示方式。`clone()` 會建立另一個由呼叫端擁有的 `Value` 控制代碼；其行為遵循 runtime 的複製語意，不保證深層複製。每個 `Value` 都擁有原生值控制代碼，並在 Python 物件銷毀時釋放；`Value` 沒有公開的 `close()` 方法。工作階段操作回傳的值由呼叫端擁有。工作階段與保留 callable 可透過內容管理器關閉。

阻塞的 `receive()` 會釋放 GIL，讓其他 Python 執行緒繼續執行。channel 元素型別必須符合 runtime 的 channel 契約。

擁有所有權的 Python 宿主值存活期間會保留其複製、相等與雜湊回呼，即使回呼形成參照循環或原工作階段已關閉。建立執行緒上的工作階段自動回收也會保護這些存活的協定。需要確定性的釋放時，請使用工作階段內容管理器；將最後一個值交給其他執行緒之前，應先在建立執行緒關閉工作階段。

容器支援 `len`、反覆運算、索引、`get` 及 `contains` 讀取。`Session.construct` 建立物件值；`get_field`、`set_field`、`call_method` 存取已註冊成員。`weak_this=True` 避免保留方法的接收物件，呼叫端須處理接收物件到期。`closed` 表示包裝物件已明確關閉，`is_valid` 表示目標仍有效。上例得到 `42`。

`count`、`channel_capacity`、`channel_closed` 是唯讀屬性，不加呼叫括號。以下完整範例成功結束，不產生輸出。

```Python
from veroscript import Value

channel = Value.channel("int", 1)
assert channel.channel_capacity == 1 and not channel.channel_closed
assert channel.try_send(42)
assert channel.count == 1
assert channel.receive() == 42
channel.close_channel()
assert channel.channel_closed
```

## 精確數值

`RuntimeCapabilities.NUMERICS` 表示核心支援 bigint/rational。這些值使用
`kind == "host_object"` 的 `Value`；`NumericKind(value.numeric_kind)` 可驗證
`BIGINT`、`RATIONAL` 或 `NONE`。腳本與宿主回呼皆以 `Value` 傳遞，不自動轉成
Python 整數或浮點數。

`session.numerics.bigint(text)`、`rational(text)`、
`parse(text, NumericKind.BIGINT)` 解析明確文字，`to_text(value)` 輸出標準文字。
`convert(value, type_name, exact_float=False)` 回傳保留固定寬度整數、`float` 或
`double` 類型的 `Value`；溢位與 `exact_float=True` 時無法精確表示的轉換會報錯。

`Session.set_numeric_limits` 接受關鍵字 `max_value_bits`、`max_input_bytes`、
`max_output_bytes`、`max_scratch_bytes`、`max_work_units`。零使用核心的有限預設值，
這些操作預算不限制整個程序的總記憶體。會話操作要求在建立執行緒上且會話閒置；
宿主回呼內使用 `current_numerics()` 借用作用中的上下文，沒有作用中的回呼時呼叫
其方法會失敗。原生數值操作釋放 GIL。獨立 Python AOT 輸出不支援 bigint/rational。

## 模組相關值的所有權

請參閱[Interop SDK — 模組相關值的所有權](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#value-domain-confinement)。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](values.md)
