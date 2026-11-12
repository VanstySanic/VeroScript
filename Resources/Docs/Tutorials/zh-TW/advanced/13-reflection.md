# 13：執行階段反射

本篇使用一個小型「類型檢查器」來理解 VeroScript 的反射介面。反射 API 提供目前腳本類型系統的中繼資料，也支援對腳本公開欄位和方法執行型別明確的操作。它適合工具、編輯器和序列化適配器；不會把任意成員自動變成可透過字串動態呼叫的介面，也不會繞過存取控制。

## 第一步：為宣告新增元數據

註解可以附加在類型、欄位、方法、自由函式、全域變數和枚舉成員：

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

	int score;

	int add_score(int amount)
	{
		score += amount;
		return score;
	}
};
```

語言會儲存註解的名稱、鍵和值，但不會解釋它們的業務意義。這裡的 `entity`、`data` 和 `call` 是使用者定義的元資料約定，不是自動產生行為的關鍵字。

以下各步都接在同一個檔案中，請保留這段 `Record` 宣告，再逐步取代或擴充 `main` 函式。

## 第二步：取得類型訊息

`type<Record>` 描述 `Record` 本身：

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	print("name={}, qualified={}", info.name, info.qualified_name);
	print("kind={}, abstract={}", info.kind(), info.is_abstract());
	print("fields={}, methods={}", info.field_count(), info.method_count());
	return 0;
}
```

`name` 是短名稱，`qualified_name` 是限定名稱。欄位和方法資訊物件需要使用限定名稱和從零開始的索引構造，因此通常先儲存一個 `type<T>` 物件。

## 第三步：列舉欄位

透過 `field_info<Record>(info.qualified_name, i)` 逐項讀取欄位：

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}, mutable={}",
			field.name(), field.declared_type(), field.runtime_type(),
			field.is_static(), field.is_mutable());

		for (int j = 0; j < field.metadata_count(); j++)
		{
			print("  metadata {}={}", field.metadata_key(j), field.metadata_value(j));
		}
	}

	return 0;
}
```

欄位資訊也可以告訴你欄位是否直接宣告在目前類型上，以及是否使用了上下文相關的 `mutable` 修飾詞。`declared_type()` 適合展示原始碼宣告，`runtime_type()` 適合展示執行階段記錄的類型。

## 第四步：枚舉方法與參數

`method_info<T>` 提供方法修飾符和參數資訊：

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, static={}, virtual={}, const={}",
			method.name(), method.return_type(), method.is_static(),
			method.is_virtual(), method.is_const());

		for (int j = 0; j < method.parameter_count(); j++)
		{
			print("  param {}: name={}, type={}, ref={}, const={}",
				j, method.parameter_name(j), method.parameter_type(j),
				method.parameter_is_ref(j), method.parameter_is_const(j));
		}
	}

	return 0;
}
```

`parameter_type(j)` 回傳去掉 `ref` 或 `const ref` 後的基礎型別；是否為引用、是否唯讀要分別檢查 `parameter_is_ref(j)` 和 `parameter_is_const(j)`。這樣工具可以在顯示簽章時準確還原參數語意。

## 第五步：檢查模組公開面

`module_info` 可以列舉入口模組可見的自由函式、頂層全域變數和腳本類型。先使用數量作為邊界，再建構 `function_info(i)` 或 `global_info(i)`：

```VeroScript
module_info module = module_info();
print("module={}, globals={}, types={}",
	module.path(), module.global_count(), module.type_count());

for (int i = 0; i < module.function_count(); i++)
{
	function_info function_meta = function_info(i);
	print("{} -> {}", function_meta.name(), function_meta.return_type());
}

for (int i = 0; i < module.global_count(); i++)
{
	global_info global_meta = global_info(i);
	print("global {}: declared={}, const={}",
		global_meta.name(), global_meta.declared_type(), global_meta.is_const());
}
```

所有宣告描述物件都提供 `has_metadata(key)`、`metadata(key)` 和 `has_annotation(name)`。若要查詢枚舉成員註解，可建構 `enum_member_info<E>(type<E>().qualified_name, i)`。

例如，讀取 `RecordState` 的第一個列舉成員：

```VeroScript
type<RecordState> state_type = type<RecordState>();
enum_member_info<RecordState> state =
	enum_member_info<RecordState>(state_type.qualified_name, 0);

print("{}={}, annotated={}",
	state.name(), state.value(), state.has_annotation("display"));
```

## 第六步：處理數量與錯誤

反射查詢使用數量和索引，而不是傳回一個可能為空的動態物件。訪問前先檢查欄位、方法、函式、全域變數、型別或列舉成員數；索引越界、不存在的型別資訊、不適用的查詢都會產生執行階段錯誤。

可執行的型別化反射僅支援公開的腳本實例欄位，以及公開的腳本實例方法和靜態方法。它不會繞過存取控制，也不提供不受限制的字串查找或呼叫。`__reflection_*` 形式的內部輔助符號不是腳本 API，不應直接使用。

## 第七步：讀取、寫入和呼叫選定成員

先依據元資料選出描述物件，再明確提供欄位型別或回傳型別。方法引數依位置傳遞：

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	int score_index = -1;
	int add_index = -1;
	int identity_index = -1;
	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> candidate = field_info<Record>(info.qualified_name, i);
		if (candidate.name() == "score")
		{
			score_index = i;
		}
	}
	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> candidate = method_info<Record>(info.qualified_name, i);
		if (candidate.name() == "add_score")
		{
			add_index = i;
		}
		if (candidate.name() == "identity")
		{
			identity_index = i;
		}
	}
	if (score_index < 0 || add_index < 0 || identity_index < 0) return 1;

	field_info<Record> score = field_info<Record>(info.qualified_name, score_index);
	method_info<Record> add = method_info<Record>(info.qualified_name, add_index);
	method_info<Record> identity = method_info<Record>(info.qualified_name, identity_index);
	Record record = Record();
	score.set<int>(record, 10);
	print("score={}", score.get<int>(record));
	print("add_score={}", add.invoke<int>(record, 5));
	print("identity={}", identity.invoke_static<int>(21));
	return 0;
}
```

`get<V>` 和 `set<V>` 操作實例欄位；`invoke<R>` 呼叫實例方法，`invoke_static<R>` 呼叫靜態方法。選定成員的值型別或回傳型別必須與 `V` 或 `R` 完全相符。完整參考文件列出不支援的簽章和各後端限制。

## 練習

1. 為 `Record` 增加一個靜態方法，並用 `is_static()` 找到它。
2. 使用 `base_count()` 和 `base_name(i)` 顯示 `Record` 的直接基底類別。
3. 使用 `enum_member_count()`、`enum_member_name(i)` 和 `enum_member_value(i)` 顯示 `RecordState` 的成員。
4. 編寫一個只列印帶有 `@data` 元資料欄位的工具。

完整規則請見[反射](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/reflection.md)；檢查中繼資料的範例請見 [`examples/reflection.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection.vs)，可執行操作範例請見 [`examples/reflection_executable.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection_executable.vs)。宿主介面描述和腳本反射是兩個層次，前者見[綁定目錄](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/bindings.md)。

本教學主線到此結束。需要時可查閱[工具鏈參考](../../../User/zh-TW/toolchain/index.md)或[標準函式庫參考](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/index.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/advanced/13-reflection.md) · [fr-FR](../../fr-FR/advanced/13-reflection.md) · [ja-JP](../../ja-JP/advanced/13-reflection.md) · [ko-KR](../../ko-KR/advanced/13-reflection.md) · [zh-CN](../../zh-CN/advanced/13-reflection.md) · [zh-TW](13-reflection.md)
