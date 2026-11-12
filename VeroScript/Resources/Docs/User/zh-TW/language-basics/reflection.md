# 反射

VeroScript 提供執行階段反射介面，可查詢模組、類型、繼承關係、全域變數、自由函式、欄位、方法、枚舉成員與宣告元資料，也可透過具型別的操作存取公開的腳本實例欄位，並呼叫宣告於腳本類別或介面中的公開方法。反射不會繞過腳本存取權限。

## 反射對象

語言提供以下公開資訊類型：

- `type<T>`：描述類型 `T` 本身。
- `field_info<T>`：描述 `T` 的一個欄位。
- `method_info<T>`：描述 `T` 的一個方法。
- `enum_member_info<T>`：描述枚舉類型 `T` 的一個列舉成員。
- `module_info`：描述目前模組視圖。
- `function_info`：描述一個可見的自由函式。
- `global_info`：描述一個可見的頂層全域變數。

這些類型由編譯器提供，不需要匯入額外模組。查詢使用類型的限定名稱和從零開始的索引；索引無效時，執行時會報告錯誤。

## 類型資訊

`type<T>` 的公共成員如下：

| 成員 | 作用 |
| --- | --- |
| `name` | 類型的短名稱。 |
| `qualified_name` | 類型的限定名稱，可用來建立 `field_info<T>` 和 `method_info<T>`。 |
| `kind()` | 傳回類型類別。 |
| `is_abstract()` | 判斷型別是否為抽象型別。 |
| `base_count()` / `base_name(i)` | 查詢直接基底類型數量和名稱。 |
| `field_count()` | 查詢可反射欄位數。 |
| `method_count()` | 查詢可反射方法數。 |
| `metadata_count()`、`metadata_key(i)`、`metadata_value(i)` | 查詢類型宣告上的元資料。 |
| `has_metadata(key)`、`metadata(key)` | 依名稱直接尋找元資料。 |
| `has_annotation(name)` | 判斷是否有指定註解；`name` 與 `@name` 皆可。 |
| `enum_member_count()`、`enum_member_name(i)`、`enum_member_value(i)` | 查詢枚舉成員。 |
| `enum_underlying_type()` | 查詢枚舉的底層型別。 |
| `is_scoped_enum()` | 判斷枚舉是否為作用域枚舉。 |

枚舉專用查詢對非枚舉型別沒有有意義的結果；呼叫前應先根據 `kind()` 判斷型別類別。

## 欄位資訊

透過 `field_info<T>(owner, index)` 查詢欄位：

```VeroScript
type<Record> info = type<Record>();
field_info<Record> field = field_info<Record>(info.qualified_name, 0);

print(field.name());
print(field.declared_type());
print(field.runtime_type());
print(field.is_static());
print(field.is_mutable());
```

欄位資訊提供以下查詢：

| 成員 | 作用 |
| --- | --- |
| `name()` | 欄位名稱。 |
| `declared_type()` | 原始碼宣告的欄位類型。 |
| `runtime_type()` | 執行時期記錄的欄位類型。 |
| `is_static()` | 判斷欄位是否為靜態欄位。 |
| `is_mutable()` | 回報欄位是否使用具上下文意義的 `mutable` 修飾詞。 |
| `is_declared_on_type()` | 判斷欄位是否直接宣告在被查詢的類型上。 |
| `metadata_count()`、`metadata_key(i)`、`metadata_value(i)` | 查詢欄位宣告上的元資料。 |
| `has_metadata(key)`、`metadata(key)`、`has_annotation(name)` | 依名稱查詢欄位元資料和註解。 |

## 方法訊息

透過 `method_info<T>(owner, index)` 查詢方法：

```VeroScript
method_info<Record> method = method_info<Record>(info.qualified_name, 0);

print(method.name());
print(method.return_type());
print(method.parameter_count());
print(method.parameter_type(0));
print(method.parameter_is_ref(0));
print(method.parameter_is_const(0));
```

方法資訊提供以下查詢：

| 成員 | 作用 |
| --- | --- |
| `name()` | 方法名稱。 |
| `return_type()` | 傳回類型。 |
| `parameter_count()` | 參數數。 |
| `parameter_type(i)` | 第 `i` 個參數的基礎型別。 |
| `parameter_name(i)` | 第 `i` 個參數的名稱。 |
| `parameter_is_ref(i)` | 判斷參數是否為參考參數。 |
| `parameter_is_const(i)` | 判斷參數是否為常數參數或唯讀參考。 |
| `is_static()` | 判斷是否為靜態方法。 |
| `is_virtual()` | 判斷是否為虛方法。 |
| `is_abstract()` | 判斷是否為抽象方法。 |
| `is_const()` | 判斷成員方法是否為 `const` 方法。 |
| `is_deleted()` | 判斷方法是否被 `= delete` 刪除。 |
| `is_constructor()` / `is_destructor()` | 判斷是否為建構子或析構函式。 |
| `metadata_count()`、`metadata_key(i)`、`metadata_value(i)` | 查詢方法宣告上的元資料。 |
| `has_metadata(key)`、`metadata(key)`、`has_annotation(name)` | 依名稱查詢方法元資料和註解。 |

`parameter_type(i)` 傳回移除 `ref` 或 `const ref` 修飾後的基礎類型。若需要復原參數的完整語意，必須同時檢查 `parameter_is_ref(i)` 和 `parameter_is_const(i)`。

## 可執行反射

選定描述子後，可使用 `field_info<T>.get<V>(物件)`、`set<V>(物件, 值)`、`method_info<T>.invoke<R>(物件, 參數...)` 與 `invoke_static<R>(參數...)`。`V` 與 `R` 必須明確指定，且與成員的執行階段型別完全一致；`R` 可為 `void`。參數依序求值一次，呼叫前會核對固定簽章。請用 `parameter_count()` 與 `parameter_type(i)` 區分多載。

`set<V>` 不允許寫入欄位本身宣告為 `const` 的成員，即使接收物件可寫；接收物件為 `const` 時，只有 `mutable` 欄位可寫。`get<V>` 仍可讀取這些欄位。

公開的腳本實例欄位，以及宣告於腳本類別或介面中的公開方法可以執行。抽象介面方法會解析至接收物件的具體實作；具有方法本體的公開介面方法與公開靜態介面方法也受支援。實例方法保留虛擬分派，結構體欄位寫入會修改原物件。無效、偽造、過期、來自其他程式或不可存取的描述子會在執行階段失敗。私有/受保護成員、宿主成員、靜態欄位、建構/解構函式、刪除的方法仍可查詢，但不能透過這些介面執行。C++、Python 與 C# AOT 目前會在編譯階段拒絕可執行反射。不提供舊開發版本位元組碼套件的相容支援，請使用目前編譯器從原始碼重建。完整用法見 [可執行反射範例](../../../../Scripts/examples/reflection_executable.vs)。

`invoke` 與 `invoke_static` 支援參照參數與參照回傳。參照實參須明確寫為 `&value` 或 `ref value`；型別須完全相符，可寫參照不能綁定唯讀儲存。參照回傳使用 `invoke<int&>(receiver, ...)` 或 `invoke<const int&>(receiver, ...)`，靜態呼叫亦同。回傳值保留別名及唯讀屬性；一般值讀取會取得參照的值。儲存沿用一般呼叫的受管理參照單元，不複製實參來模擬寫回。全部參數通過檢查後才進入方法；進入後發生例外時，已完成的修改遵循一般呼叫規則。

若要將參考回傳繼續作為參考引數傳入，請先繫結區域參考，再傳入 `&alias` 或 `ref alias`。

物件引數也會依呼叫點的靜態型別驗證：允許合法的向上轉型與空控制代碼，隱含向下轉型會在方法本體執行前被拒絕。需要向下轉型時，先明確使用 `cast<T>`。`set<V>` 會在編譯階段檢查來源型別是否可指派給 `V`。`void` 運算式不能作為引數。

`set<V>` 會對值執行一般指派的受檢轉換，因此可用 `set<int64>` 將 `int` 值寫入 `int64` 欄位。數值溢位會在欄位修改前被拒絕；`V` 仍須與欄位宣告型別精確一致。

以值傳遞的結構體和定長陣列引數會在該引數求值時複製，早於後續引數對原資料的修改。參考回傳用於初始化值變數、傳遞值參數或產生值回傳時，也會複製其值。類別與介面控制代碼沿用一般呼叫語意。舊位元組碼套件應從原始碼重建，以取得這些編譯器修正。

## 模組、自由函式、全域變數與枚舉成員訊息

`module_info()` 描述從入口模組可見的宣告。使用 `function_count()`、`global_count()` 和 `type_count()` 取得邊界，再建構 `function_info(i)` 或 `global_info(i)`；`type_name(i)` 傳回可見腳本類型的名稱。被匯入模組的私有宣告不會洩漏，入口模組本身的私有宣告和正常匯出的匯入宣告仍遵循原有 import/export 可見性規則。

`function_info` 提供名稱、穩定連結名稱、模組路徑、傳回與參數簽章、`ref`/`const` 標記、deleted/exported 標記及宣告元資料。 `global_info` 提供名稱、連結名稱、模組路徑、宣告類型、執行階段類型、const/exported 標記及宣告元資料。靜態資料成員仍作為欄位反射，不會在全域變數清單中重複出現。

枚舉 `E`，可使用 `enum_member_info<E>(type<E>().qualified_name, i)` 查詢成員名稱、完整 `int64` 值及宣告元資料；它是 `type<E>` 上簡潔枚舉查詢介面的補充。

## 宣告元數據

宣告可以附加註解。語言會儲存註解的名稱、鍵和值，但不會解釋註解的業務意義：

```VeroScript
@entity(sample)
struct BaseRecord
{
	int id;
};

struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}
};
```

宿主、編輯器或其他工具可以透過 `metadata_count()`、`metadata_key(i)` 和 `metadata_value(i)` 列舉這些訊息，也可以使用 `has_metadata(key)`、`metadata(key)` 和 `has_annotation(name)` 直接查詢。鍵不存在時 `metadata(key)` 傳回空字串；需要區分「空值」和「不存在」時，應先呼叫 `has_metadata(key)`。 `@entity`、`@data` 和 `@call` 在本例中只是可查詢的元資料名稱，不代表語言內建的業務行為；指令碼或宿主整合可以定義自己的約定。

宿主綁定目錄中的註記描述與腳本執行階段反射是兩個層次：綁定目錄用於描述宿主公開 API，而這些反射資訊類型用於查詢目前已編譯腳本模組。

## 完整範例

下面的範例查詢繼承、欄位、方法、靜態方法、頂層全域量、枚舉成員和宣告元資料：

```VeroScript
@setting(primary)
export const int ReflectionVersion = 1;

@utility(sample)
export int echo_value(const int& value)
{
	return value;
}

enum class RecordState
{
	@display(primary)
	Ready,
	Archived
};

struct BaseRecord
{
	int id;

	int read_id() const
	{
		return id;
	}
};

@entity(sample)
struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}

	static int identity(int value)
	{
		return value;
	}
};

int main()
{
	type<Record> info = type<Record>();
	print("{} ({})", info.name, info.kind());
	print("bases={}, fields={}, methods={}",
		info.base_count(), info.field_count(), info.method_count());

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}",
			field.name(), field.declared_type(), field.runtime_type(), field.is_static());
	}

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, params={}, static={}, virtual={}",
			method.name(), method.return_type(), method.parameter_count(),
			method.is_static(), method.is_virtual());
	}

	module_info module = module_info();
	print("module={}, functions={}, globals={}, types={}",
		module.path(), module.function_count(), module.global_count(), module.type_count());

	for (int i = 0; i < module.function_count(); i++)
	{
		function_info function_meta = function_info(i);
		print("function {}: returns={}, exported={}",
			function_meta.name(), function_meta.return_type(), function_meta.is_exported());
	}

	for (int i = 0; i < module.global_count(); i++)
	{
		global_info global_meta = global_info(i);
		print("global {}: declared={}, runtime={}, const={}",
			global_meta.name(), global_meta.declared_type(),
			global_meta.runtime_type(), global_meta.is_const());
	}

	type<RecordState> state_type = type<RecordState>();
	enum_member_info<RecordState> state =
		enum_member_info<RecordState>(state_type.qualified_name, 0);
	print("enum member {}={} annotated={}",
		state.name(), state.value(), state.has_annotation("display"));
	return 0;
}
```

可執行版本請參考[範例索引](../examples.md)中的反射範例。反射 API 與入口函式傳回類型無關；範例使用 `int main` 並明確傳回處理程序狀態碼。

## 邊界和錯誤

- 反射支援資訊查詢與上述具型別的執行操作，但不提供對任意成員的無限制動態存取。
- `field_count()`、`method_count()` 和枚舉成員數量都是目前執行階段類型資訊的結果，應在存取前檢查索引範圍。
- 對不存在的類型資訊、無效索引或不適用的查詢，執行時會報告錯誤；不要把無效索引當作「沒有結果」。

類型系統的基礎規則請參考[資料型別](data-types.md)、[結構體](structs.md)、[類別](classes.md)和[枚舉](enums.md)。宿主側的介面描述見[綁定目錄](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/bindings.md)。

## 結構化反射

<a id="reflection-metadata-contract"></a>

`type<T>`、field_info、method_info、module_info、function_info、global_info、enum_member_info 提供 annotation_count/annotation/annotations、comment_count/comment/comments、documentation、comments_available、comments_retention、declaration、source_metadata。依索引存取前檢查數量。annotation_info 與 annotation_argument_info 提供 argument_count/argument/arguments，型別化值存取須符合純量型別。definition() 回傳已註冊的描述資訊，可能未註冊時先檢查 is_valid()。

宿主選擇 Full、Documentation 或 NoComments；Unavailable 表示沒有原始碼資訊。NoComments 不移除註記或另行註冊的文件。中繼資料物件保留不可變快照，熱重新載入不會讓舊物件悄悄改變指向。查詢不會繞過存取控制，也不會讓私有成員變得可呼叫。

[註記與原始碼中繼資料](annotations.md)

## 巢狀型別反射

使用 `display_name()`、`is_nested()`、`is_local()`、`declaring_type_name()`、`declaring_function_name()` 與 `access()` 查詢所屬關係。非型別成員的存取類別為 `none`；`qualified_name` 仍是具體執行階段識別，顯示名稱不是識別鍵。

`nested_type_count()`、`nested_type_name(i)`、`nested_type_parameter_count(i)` 與 `nested_type_declaration(i)` 列出可存取的直接子型別宣告，含尚未具現化的泛型。參數數量不含外層參數；別名不產生額外執行階段型別。建立查詢時固定存取情境，區域與私有名稱不會因此加入全域探索。

## 樣板中繼資料

`module_info.template_declaration_count()` 查詢開放宣告而不具現化。`template_declaration_id/name/kind/owner` 依索引識別記錄；`template_parameter_count`、`template_own_parameter_count`、`template_enclosing_parameter_count` 區分完整、自有及外層環境。`template_parameter_id/name/kind/value_type/default/is_enclosing` 接受 `(宣告索引, 參數索引)`，完整參數表先列外層。

`type<T>`、`method_info<T>`、`function_info` 提供 `template_declaration_id()`、`template_argument_count()` 及索引式 `template_argument_encoding/name/kind/value_type`。外層引數使用 `template_enclosing_argument_count/encoding/name`。一般宣告返回空身分及零數量。編碼保留整數寬度、正負號和列舉身分；顯示名稱不能作為身分鍵。開放查詢不產生可執行方法，既有僅型別參數計數維持原意。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/reflection.md) · [fr-FR](../../fr-FR/language-basics/reflection.md) · [ja-JP](../../ja-JP/language-basics/reflection.md) · [ko-KR](../../ko-KR/language-basics/reflection.md) · [zh-CN](../../zh-CN/language-basics/reflection.md) · [zh-TW](reflection.md)
