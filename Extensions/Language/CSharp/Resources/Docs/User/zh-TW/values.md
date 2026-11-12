# C# 值與所有權

`VeroScriptValue` 擁有獨立的 Interop 值控制代碼，不是工作階段的子物件。使用 `Dispose()` 或 `using` 釋放擁有值。`Clone()` 依執行階段的複製語意建立另一個擁有控制代碼，不保證深層複製。`Reset()` 將目前值替換為空值。並行修改或釋放須由呼叫端同步。

## 相依於模組的值內容

擁有控制代碼不代表值內容獨立於工作階段。指令碼物件、函式及具有模組值欄位的可變容器，
即使為空，仍只能在所屬工作階段的建立執行緒使用，工作階段關閉後不可存取。主機建立的
同類儲存體在首次交給工作階段時建立歸屬，既有共用別名也遵守該歸屬。`Clone()` 不會解除
這些限制。VM 背景工作執行緒回呼只能匯出獨立的擁有值；具有 variant/函式欄位的空容器也
不能複製到外部擁有儲存體。私有的借用回呼傳回欄位仍可在背景工作執行緒內轉送值。
使用後仍應釋放擁有控制代碼，包括工作階段已經關閉的情況。

## 純量

讀取方法應與指令碼型別一致：

| 設定 | 讀取 | 指令碼型別 |
| --- | --- | --- |
| `Set(sbyte)` | `GetInt8()` | `int8` |
| `Set(byte)` | `GetUInt8()` | `uint8` |
| `Set(short)` | `GetInt16()` | `int16` |
| `Set(ushort)` | `GetUInt16()` | `uint16` |
| `Set(int)` | `GetInt32()` | `int32` |
| `Set(uint)` | `GetUInt32()` | `uint32` |
| `Set(long)` | `GetInt64()` | `int64` |
| `Set(ulong)` | `GetUInt64()` | `uint64` |
| `Set(float)` | `GetFloat()` | `float` |
| `Set(double)` | `GetDouble()` | `double` |
| `Set(bool)` | `GetBool()` | `bool` |
| `SetString` | `GetString` | `string` |
| `SetWideString` | `GetWideString` | `wstring` |

具型別的列舉值使用 `SetEnum(typeName, value)` / `GetEnum()`；`Kind` 與 `TypeName` 描述目前表示。`SetNull()` 建立 `nullptr`，`IsNull` 只檢查該值。`Reset()` 後 `Kind` 為 `Invalid`，`IsNull` 為 false。存取已釋放包裝物件會擲回 `ObjectDisposedException`。是否允許空主控端參考，由接收參數的契約決定。

`Set(VeroScriptValue)` 指派完整的執行階段值，保留容器、主控端物件、函式及通道的識別語意。目標必須可寫入；唯讀借用值不能作為目標。

## 容器與通道

`SetArray`、`SetSequence`、`SetMap`、`SetSet`、`SetChannel` 要求明確型別名稱。帶有 `VeroScriptValueKind` 的多載選擇具體型別：`SetSequence` 支援包括 `Queue`、`PriorityQueue`、`Stack`、`Deque` 的序列；`SetMap` 支援 `Map`、`OrderedMap`、`MultiMap`、`OrderedMultiMap`；`SetSet` 支援 `Set`、`MultiSet`、`OrderedMultiSet`。

一般 map 替換重複索引鍵的值，multi-map 保留每個索引鍵與值的關聯；一般 set 移除重複元素，multi-set 保留重複次數。優先佇列的執行階段 `top` / `pop` 優先提供最高優先順序元素。

使用 `SequenceCount` / `GetSequenceElement`、`MapCount` / `TryGetMapValue`、`SetCount` / `SetContains` 讀取對應容器。`EnumerateMap`、`EnumerateSet` 要求 `VeroScriptCapabilities.ContainerEnumeration`，存取唯讀快照。回呼中的 `VeroScriptBorrowedValue` 在每次回呼傳回時失效；若要保留值，應在該次回呼中 `Clone()`。

```CSharp
using var key = new VeroScriptValue();
key.SetString("answer");
using var number = new VeroScriptValue();
number.Set(42);
using var map = new VeroScriptValue();
map.SetMap("string", "int", [key], [number]);

map.EnumerateMap((entryKey, entryValue) =>
{
	Console.WriteLine(entryKey.GetString());
	Console.WriteLine(entryValue.GetInt32());
});
```

通道 API 為 `ChannelSend`、`ChannelTrySend`、`ChannelSendFor`、`ChannelReceive`、`ChannelTryReceive`、`ChannelReceiveFor`、`ChannelClose`。`ChannelTrySend`、`ChannelSendFor` 傳回 bool，逾時以毫秒計。非封鎖和限時接收傳回擁有值或 `null`。`ChannelCount`、`ChannelCapacity`、`IsChannelClosed` 提供目前狀態。不要在需要保持回應的執行緒上進行封鎖接收。執行階段同步通道操作；並行使用期間不能重設、替換或釋放包裝物件。

## 主控端物件與保留呼叫物件

`SetHostObject(typeId, typeName, managedObject)` 在原生值持有期間保持受控物件存活。`Func<string, bool> isType` 多載可回報相容的主控端子型別。`GetHostObject()` 傳回 `VeroScriptHostObjectInfo`；`GetHostObject<T>()` 檢查受控型別。

`VeroScriptCallable` 擁有保留的函式或閉包，使用後應釋放。預設僅允許建立執行緒呼叫。透過 `RetainCallable(..., ownerThreadOnly: false)` 允許其他執行緒呼叫後，主控端仍須將呼叫與關聯工作階段的所有操作序列化，並保護自身狀態。呼叫物件內部的鎖定不保護整個工作階段。

## 借用與生命週期

原生函式、攔截器、泛型處理站、序列化、修改通知及訪問器中的借用內容及值，僅限目前回呼執行緒使用。跨執行緒存取擲回狀態為 `WrongThread` 的 `VeroScriptException`，回呼傳回後存取擲回 `InvalidOperationException`。僅能移轉的獨立值內容可在回呼執行緒複製後交給其他執行緒；複製不會解除相依於模組的值內容限制。

## 精確數值 API

C# 將 Core bigint/rational 保留為不透明 VeroScriptValue 宿主值。VeroScriptCapabilities.Numerics 表示原生能力，不增加 System.Numerics.BigInteger 自動轉換或公開託管數值服務。以型別化腳本函式或公開 C ABI 整合。

<a id="managed-call-lifetime"></a>

## 受控呼叫生命週期

原生操作將提供控制代碼的受控值／套件包裝器保留至呼叫及傳回原生檢視的複製完成，包括 `IReadOnlyList<VeroScriptValue>` 索引器即時產生的值，以及阻塞 channel 操作或 visitor 的接收物件。保留的是包裝器快照：回呼中替換引數陣列欄位，不會把原生 `ref`／`out` 寫回重新導向至替換值。這些生命週期保證不允許並行明確釋放，亦不延長回呼借用範圍。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](values.md)
