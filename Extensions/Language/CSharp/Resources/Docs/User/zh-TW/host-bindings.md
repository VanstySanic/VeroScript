# C# 宿主繫結

## 攔截與變更

使用 `SetInstanceCallInterceptor` 安裝執行個體攔截器，使用 `SetMutationHandler`
安裝變更處理器。需要繞過攔截器時使用 `CallMethodBypassingInterceptor`。

宿主繫結透過明確的 Interop 型別契約將 CLR 函式、型別、成員和序列化器公開給 VeroScript。`VeroScriptNativeType` 描述原始型別、宿主型別、型別引數與遞迴結構；函式的參數旗標必須符合實際回呼契約。

會話 API 可註冊宿主型別、成員、泛型型別和別名。`Construct`、`CallMethod`、`GetField` 和 `SetField` 只操作已註冊成員。攔截器以 `VeroScriptInterceptResult` 明確表示繼續、取代或拒絕，不得繞過存取策略。

泛型宿主型別工廠使用 `VeroScriptGenericHostTypeContext` 的
`ArgumentCount`、`GetArgument`、`SetType`、`AddMember`、`SetProtocol` 和
`SetError`。攔截上下文提供 `Receiver`、`ArgumentCount`、`GetTarget`、
`GetArgument`、`Proceed` 和 `SetError`；借用值只在回呼期間有效。

序列化器以及 `SerializeObject`、`DeserializeObject` 應驗證型別、版本和資料來源。序列化內容使用 `GetField`/`SetBytes`，反序列化內容使用 `EncodedVersion`/`GetBytes`/`AddField`。`SetMutationHandler` 接收 `VeroScriptMutationKind` 等變更資訊；回呼應避免重入同一工作階段，並在回呼結束前讀完借用資料。通知無法回復已完成的變更。託管例外不得穿越原生 ABI。

## 函式與回呼

`RegisterFunction` 會明確宣告回傳型別與參數型別：

```CSharp
session.RegisterFunction(
	"host_double",
	VeroScriptNativeType.Int32,
	[VeroScriptNativeType.Int32],
	(context, result) => result.Set(context.GetArgument(0).GetInt32() * 2));
```

`GetArgument` 提供唯讀參數；只有宣告為可變引用的參數才能使用 `GetMutableArgument`。回呼會將結果寫入傳入的借用回傳檢視；要回報預期失敗時使用 `context.SetError(...)`。`ArgumentCount`、`ProgramGeneration` 和 `GetHostThis` 提供呼叫內容。託管例外會轉換成回呼失敗。

需要較完整的中繼資料時使用 `VeroScriptHostFunctionDefinition`，設定文件、參數名稱與說明、註記、型別參數和泛型宿主物件旗標。`ApplyClassDefaults` 會在物件建立或載入後套用已註冊的預設值；`EnumerateObjectReferences` 會巡訪腳本與宿主參考。巡訪回呼取得的借用值會在回呼返回時失效。`RetainCallable` 會建立由宿主擁有的腳本函式或閉包參考。

除 `RegisterFunction` 外，還可使用 `RegisterHostFunction`、`RegisterAnnotation`、`RegisterHostType`、`RegisterHostMember`、`RegisterHostProtocol`、`RegisterTypeAlias`、`RegisterGenericHostTypeTemplate` 和 `InstantiateGenericHostType`。`VeroScriptNativeParameter` 將型別與 `None`、`Ref` 或 `ConstRef` 組合；只有回呼要寫回指令碼儲存空間時才使用 `Ref`。`VeroScriptHostTypeDefinition` 與 `VeroScriptHostMemberDefinition` 描述要公開的 enum/struct/class、建構函式、方法、屬性、欄位、常數和運算子。

## 註記名稱策略

`RegisterAnnotation` 的 `VeroScriptAnnotationDefinition.CaseSensitive` 預設為
`true`。設為 `false` 可對該註記名稱中的 ASCII 字母忽略大小寫；參數名稱與
metadata 鍵仍精確比對，反射保留原始碼拼寫。精確比對與忽略大小寫的註冊重疊時，
操作失敗且目錄不變；`ExportBindingsJson()` 保留 `caseSensitive`。註冊同時向編譯、
反射與工具提供文件，處理器需另外安裝。應配套部署目前的原生 SDK，其註記描述符
已增加名稱策略欄位。

## 簽章、巢狀呼叫與持續性

`VeroScriptNativeType` 區分 `Void`、`Bool`、`String`、`HostObject`、`Enum`、`Function` 等型別。容器與函式使用 `TypeArguments`；原生簽章 `fn(int)->int` 對應原始碼型別 `function(int)->int`，這些識別碼不翻譯。

`VeroScriptCallContext` 回呼內的 `InvokeFunction`、`InvokeCallable` 可呼叫指令碼目標，不應改為重入工作階段操作；傳回的擁有值要釋放。借用參數與傳回檢視僅限目前回呼使用。泛型型別的複製、相等、雜湊及子類別協定透過處理站安裝，並由工作階段保留。

`VeroScriptInterceptContext` 支援 `Unhandled`、`Proceed`、替換結果或 `Failed`。`RegisterExternalSerializer` 註冊外部持續性；序列化內容提供 `TypeName`、`MutationVersion`、`FieldCount`、`GetField`、`SetBytes`，還原序列化內容提供 `EncodedVersion`、`GetBytes`、`AddField`、`SetError`。須驗證位元組的型別、版本與來源；這些 API 不會驗證套件或原生程式庫的真實性。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](host-bindings.md)
