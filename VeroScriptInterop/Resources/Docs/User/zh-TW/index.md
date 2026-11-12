# Interop SDK

## JIT 統計相容性

對於 `vs_interop_session_get_last_jit_stats`，`struct_size` 為零表示舊版呼叫形式，只會寫入原有統計欄位前綴；目前呼叫端必須傳遞 `sizeof(vs_interop_jit_stats)` 才能接收附加計數器。`native_typed_loop_executions` 統計原生型別化迴圈核心的呼叫次數，包括回報原始碼可見執行階段錯誤後結束的呼叫。它不是成功計數器。

`native_leaf_executions`、`native_cfg_executions`、`bridge_executions`、`linear_executions` 和 `vm_trampoline_executions` 統計一般可執行入口的實際進入次數，包括因錯誤或控制轉移而退出的呼叫。對應的 `*_entries` 欄位統計編譯產物。這些計數屬於最近一次執行，命中熱快取時可以沒有編譯次數而仍有執行次數。`scalar_native_fast_path_calls` 僅統計宿主原生快速路徑呼叫。直接呼叫和型別化迴圈呼叫屬於巢狀事件，不應相加作為獨立函式呼叫次數。

C API 提供彙總統計。選用的逐函式診斷快照、回退原因與計時目前透過 Core/C++ 提供，C JIT 策略不會啟用該診斷收集。

`VeroScriptInterop` 是針對 C、Python、C#、Rust 以及其他語言適配器的語言無關處理程序 ABI。它把 VeroScript 的會話、值、呼叫、綁定和套件能力包裝成不依賴 C++ 物件佈局與 STL 類型的 C 介面。需要在 C++ 內部直接嵌入時，應優先閱讀[宿主整合](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/embedding.md)；當需要跨語言或穩定的動態函式庫邊界時，使用本頁所述 SDK。

函式簽名、結構佈局、常數以及功能可用性以隨 SDK 提供的公用標頭檔為準。本頁說明契約和使用方式；完整宣告清單請直接查閱該標頭檔。

## ABI 與安裝

SDK 提供的 `VeroScriptInterop.h` 公用頭檔同時相容於 C11 和 C++17。目前 ABI 常數為：

```c
#define VS_INTEROP_ABI_VERSION UINT32_C(0x00000101)
```

目前基線為 ABI 0.1.1。高 16 位元編碼主版本，中間 8 位元編碼次版本，低 8 位元編碼修補版本；繫結要求 ABI 版本完全一致。結構化描述和輸出結構以 `struct_size` 開頭；相容擴展透過在結構尾部追加欄位完成。固定的字串視圖和鍵值對視圖不帶 `struct_size`，其佈局由頭檔固定。

安裝 SDK 後即可取得執行庫、C11 頭檔和 CMake 匯入目標。適配器不需要依賴 VeroScript 的 C++ 頭檔或 STL ABI。

CMake 消費者可以使用：

```cmake
find_package(VeroScriptInterop CONFIG REQUIRED)
target_link_libraries(MyAdapter PRIVATE VeroScript::VeroScriptInterop)
```

Windows 上應把 SDK 執行階段 DLL、本地化資源與執行階段法律聲明部署於宿主執行檔旁；分發應用程式時保留這些檔案。只複製 `VeroScriptInterop.dll` 會遺漏必要相依項目。整合範例請透過文件或原始碼參考另行取得。

## 核心物件模型

介面使用不透明控制代碼。適配器不能讀取控制代碼內部佈局，也不能使用系統 `free` 釋放模組擁有的記憶體。

| 控制代碼 | 作用 |
| --- | --- |
| `vs_interop_session` | 隔離的載入、編譯、執行和呼叫會話。 |
| `vs_interop_value` | 腳本標量、物件、容器、函式或宿主物件值。 |
| `vs_interop_callable` | 可跨目前原生回呼保留的腳本呼叫物件。 |
| `vs_interop_call_context` | 原生函式回呼期間讀取參數、寫回參考參數或發起回呼局部巢狀呼叫。 |
| `vs_interop_buffer` | 模組擁有的錯誤文字、套件位元組或序列化結果；只能用 `vs_interop_buffer_destroy` 釋放。 |
| `vs_interop_package` | 已校驗的 `.vsapp`/`.vslib` 記憶體套件。 |
| `vs_interop_intercept_context` | 實例方法攔截器的接收者、目標方法和參數。 |
| `vs_interop_generic_type_context` | 泛型宿主值類型工廠定義特定類型、成員和協定。 |
| `vs_interop_external_serialize_context` / `deserialize_context` | 外部物件序列化與反序列化回呼上下文。 |

最小的 C 呼叫流程是「建立會話 → 應用程式設定 → 載入來源或檔案 → 執行 → 讀取診斷 → 銷毀」：

```c
#include "VeroScriptInterop.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static vs_interop_string_view view(const char* text)
{
	vs_interop_string_view result;
	result.data = text;
	result.size = (uint64_t)strlen(text);
	return result;
}

int main(void)
{
	vs_interop_session* session = NULL;
	vs_interop_session_options options = {0};
	options.struct_size = sizeof(options);

	if (vs_interop_session_create(&options, &session) != VS_INTEROP_STATUS_OK)
	{
		return 1;
	}

	const vs_interop_string_view source = view(
		"int main()\n"
		"{\n"
		"\tprint(\"hello\");\n"
		"\treturn 0;\n"
		"}");
	vs_interop_run_result load = {0};
	load.struct_size = sizeof(load);

	const vs_interop_status load_status = vs_interop_session_load_source(
		session,
		source,
		view("main.vs"),
		&load);

	if (load_status != VS_INTEROP_STATUS_OK || !load.success)
	{
		vs_interop_session_destroy(session);
		return 2;
	}

	vs_interop_run_result run = {0};
	run.struct_size = sizeof(run);
	const vs_interop_status run_status = vs_interop_session_run(
		session,
		VS_INTEROP_RUN_SCRIPT,
		&run);

	const int result = run_status == VS_INTEROP_STATUS_OK && run.success ? 0 : 3;
	vs_interop_session_destroy(session);
	return result;
}
```

範例中的腳本透過 C 字串轉義傳入；實際腳本仍應使用項目約定的 Tab 縮排、獨佔一行的左花括號和 `int main` 入口。

## 狀態、診斷和退出語意

所有回傳狀態都是顯式的 `vs_interop_status`。常用狀態包括：

| 常數 | 意義 |
| --- | --- |
| `VS_INTEROP_STATUS_OK` | 呼叫完成。 |
| `VS_INTEROP_STATUS_INVALID_ARGUMENT` | 控制代碼、指標、字串視圖或陣列參數無效。 |
| `VS_INTEROP_STATUS_WRONG_THREAD` | 呼叫違反會話或物件的執行緒約束。 |
| `VS_INTEROP_STATUS_ENGINE_ERROR` | 引擎執行或載入失敗。 |
| `VS_INTEROP_STATUS_TYPE_MISMATCH` | 值種類或宿主類型不符。 |
| `VS_INTEROP_STATUS_CALLBACK_ERROR` | 宿主回呼報告失敗。 |
| `VS_INTEROP_STATUS_UNSUPPORTED` | 目前建置不支援該能力。 |
| `VS_INTEROP_STATUS_OUT_OF_MEMORY` / `BUFFER_TOO_SMALL` / `INTERNAL_ERROR` | 資源、輸出緩衝區或內部錯誤。 |

`vs_interop_run_result` 包含 `struct_size`、`success`、`exit_requested`、`exit_code` 和 `diagnostic_count`：

- `success` 表示編譯/執行是否正常完成，不表示 `exit_code` 是否為零；
- 成功的 `int main` 可以傳回非零退出碼；
- `exit_requested` 只表示腳本明確地呼叫了 `exit(n)`，包含 `exit(0)`；
- 普通的 `int main` 回傳不會設定 `exit_requested`；
- 失敗時 `exit_code` 是引擎或 API 錯誤碼，不能當作腳本正常退出碼解釋。

診斷透過 `vs_interop_session_diagnostic_count`、`vs_interop_session_get_diagnostic` 讀取。每項包含 `message`、`message_id`、`file_label`、`line` 和 `column`；診斷參數可用 `diagnostic_argument_count`、`diagnostic_argument` 和 `format_diagnostic` 取得。傳回的字串視圖必須遵循對應會話操作的生命週期。

## 本地化

Interop 的本地化狀態由核心處理程序統一維護，並且本地化操作本身是執行緒安全的；它不是每個 `vs_interop_session` 獨立的一份設定。一個行程中的語言切換會影響之後由核心格式化的診斷，包括 `vs_interop_session_format_diagnostic`。

| 函式 | 作用 |
| --- | --- |
| `vs_interop_locale_set_language` | 根據規範或可規範化的 BCP 47 標籤選擇已載入的語言；成功傳回 `VS_INTEROP_STATUS_OK`。 |
| `vs_interop_locale_get_language` | 傳回目前實際符合的規範文化標籤，例如 `en-US` 或 `zh-CN`。 |
| `vs_interop_locale_translate` | 透過已載入的程序級目錄傳回模組所有的 UTF-8 翻譯，未知英文原樣複製。用 `vs_interop_buffer_destroy` 釋放。 |
| `vs_interop_locale_get_available_languages` | 傳回由模組擁有的 UTF-8 JSON 陣列緩衝區，其中包含目前已載入的規範文化標籤。 |
| `vs_interop_locale_load_catalog_directory` | 校驗並取代完整的處理程序級本地化目錄樹。 |
| `vs_interop_locale_merge_catalog_directory` | 校驗目錄並把它合併到目前目錄樹；`override_existing != 0` 時覆寫已有訊息 ID。 |

目錄必須遵循[診斷本地化](../../../../../VeroScript/Resources/Docs/User/zh-TW/localization.md)中的 `<culture>/<component>.json` 佈局，並包含完整的 `en-US` 基準。目錄校驗或載入失敗時，`load_catalog_directory` 和 `merge_catalog_directory` 傳回 `VS_INTEROP_STATUS_ENGINE_ERROR`，並透過 `out_error` 傳回由模組擁有的錯誤緩衝區；無效參數、記憶體不足或內部異常會傳回對應的其他狀態。呼叫方必須使用 `vs_interop_buffer_destroy` 釋放錯誤緩衝區和可用語言 JSON 緩衝區。 `vs_interop_locale_get_language` 傳回的字串視圖不是呼叫方擁有的記憶體，適配器應在需要長期儲存時複製內容。

```c
/* The view(...) helper from the minimal workflow above is reused here. */
int ConfigureLocale(void)
{
	vs_interop_buffer* error = NULL;
	if (vs_interop_locale_load_catalog_directory(
			view("localization"),
			&error) != VS_INTEROP_STATUS_OK)
	{
		if (error != NULL)
		{
			vs_interop_buffer_destroy(error);
		}
		return 1;
	}

	if (vs_interop_locale_set_language(view("zh-CN")) != VS_INTEROP_STATUS_OK)
	{
		return 2;
	}
	return 0;
}
```

`vs_interop_locale_get_available_languages` 傳回包含目前已載入規範文化標籤的 JSON 陣列。適配器應使用傳回的陣列填滿語言選擇器，而不需要重複掃描宿主檔案系統。

適配器應先檢查 `vs_interop_get_capabilities()` 是否包含 `VS_INTEROP_CAP_LOCALIZATION`，再呼叫這些可選匯出。語言目錄可以在建立會話前加載，也可以在執行期間加載；但如果多個執行緒或會話共享一個處理程序，宿主仍應明確約定語言切換時機，避免讓同一批使用者介面同時看到不同語言。

## 會話設定

`vs_interop_session_apply_config` 接收 `vs_interop_config`。該結構的公開欄位如下：

| 欄位 | 作用 |
| --- | --- |
| `import_paths` | 普通模組搜尋路徑。 |
| `implicit_import_paths` | 對每個來源模組隱式匯入的檔案；不會修改來源檔案，也不會自動重新匯出宣告。 |
| `script_args` | 傳給 `main(string[] args)` 的腳本參數。 |
| `defines` | 物件巨集或條件編譯定義。 |
| `stdlib_search_paths` | 標準函式庫搜尋根目錄。 |
| `source_overlays` | 記憶體中的路徑/原始碼覆蓋項。 |
| `bytecode_cache_directory`、`use_bytecode_cache` | 位元組碼快取目錄和開關。 |
| `max_call_depth` | 單次執行的遞歸/呼叫深度上限；`0` 使用環境變數或預設值。 |
| `max_instructions` | 單次執行的 VM 指令數上限；`0` 表示不限制。 |
| `timeout_milliseconds` | 單次執行的牆上時鐘時間上限；`0` 表示不限制。 |
| `execution_backend` | `VM = 0`、`Mixed = 1`、`JIT = 2`。 |
| `predefined_macros` | 結構化函式巨集，包括名稱、參數、函式體、來源名稱和可變參數標誌。 |
| `allow_circular_imports` | 是否允許循環匯入，預設允許。 |
| `allow_project_wide_access` | 是否允許在專案範圍內存取非匯出宣告，預設為關閉。 |
| `enable_execution_control` | 安裝協作式取消令牌；成功套用設定時會重設先前的取消要求。 |
| `capability_policy_enabled` | 啟用標準函式庫副作用能力策略。 |
| `allowed_script_capabilities` | `FILE_READ`、`FILE_WRITE`、`FILE_DELETE`、`ENVIRONMENT_READ`、`ENVIRONMENT_WRITE`、`CONSOLE_INPUT`、`CONSOLE_OUTPUT`、`TASK_SPAWN` 和 `PROCESS_DIRECTORY_CHANGE` 的位元遮罩。僅在啟用策略時使用；`chdir` 同時需要 `ENVIRONMENT_WRITE` 和 `PROCESS_DIRECTORY_CHANGE`。 |

`max_instructions` 和 `timeout_milliseconds` 是單次執行的協作式限制；`0` 表示不限制。非零限制或 `enable_execution_control` 要求 VM 或符合條件的受控 JIT 路徑執行協作式檢查。受控 Mixed 預設使用 VM；量測目標負載後，可用 `vs_interop_jit_policy.enable_controlled_mixed` 明確啟用原生路徑，明確 JIT 選擇獨立於此開關。查詢所選後端的 `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION`，並用 `controlled_native_executions` 確認實際執行；不支援或成本模型優先選擇 VM 的入口仍保留治理檢查。`vs_interop_session_request_cancel` 為執行緒安全；未啟用執行控制時傳回 `VS_INTEROP_STATUS_INVALID_ARGUMENT`，不會強制終止已執行的原生回呼。呼叫返回前必須保持工作階段有效。

當 `capability_policy_enabled` 非零時，標準函式庫內建函式只能使用 `allowed_script_capabilities` 授予的能力；宿主回呼不受此策略約束，必須自行執行授權檢查。依賴這些可選治理能力前，應檢查 `VS_INTEROP_CAP_EXECUTION_GOVERNANCE`。

`vs_interop_jit_policy` 可設定混合後端的熱點閾值、最大編譯嘗試數、快取容量和預編譯預算；`vs_interop_session_get_last_jit_stats` 傳回最近一次執行的編譯、快取、分派、回退、預算跳過、快速路徑和 `direct_native_calls` 統計。`direct_native_calls` 表示在同一代 JIT 程式碼中繞過虛擬機器值橋、直接到達另一個原生入口的呼叫次數。

### 工作階段可重用程式碼快取

應先檢查 `VS_INTEROP_CAP_SESSION_JIT_CACHE`。這項可選能力公開 `vs_interop_jit_cache_limits`、`vs_interop_jit_cache_stats`，以及工作階段的快取上限設定/讀取和統計讀取函式。對每個傳入或接收的可擴充結構都要初始化 `struct_size`。預設上限為 8 MiB 原生程式碼與 32 MiB 完整保留承載；將上限設為零會禁止保留該資源。此快取與位元組碼快取彼此獨立，只為符合條件的一般 `Run`/`EvalOnce`/`CallFunction` 內容保留已編譯程式碼。其統計是已穩定的累積內容值，而非即時或程序層級記憶體計量。重新設定或內容失效會清除保留程式碼；達到資源上限會安全回退到 VM 執行，不改變指令碼語意。

要重用程式碼，`vs_interop_jit_policy.reuse_session_code` 必須非零（預設值），且有效後端不是 VM。`max_compile_attempts` 或 `max_cached_entries` 為零表示對應計數不限額，保留程式碼仍受原生程式碼和完整載荷上限約束。繫結修訂號必須穩定。持久工作階段與熱重載工作階段可以重用與程式世代對應的程式碼。原生繫結、執行個體攔截器或變更回呼發生變更時，保留程式碼失效。偵錯、持久狀態初始化與有界清理使用 VM。符合條件的受控執行可重用匹配的快取及治理身分；重複呼叫不會重設上下文的累計編譯配額。

### 公開巨集與套件

`defines` 和 `predefined_macros` 為每個來源模組提供前置處理初始巨集。`#define` 為
私有巨集，`#define_export` 公開模組的最終巨集定義。明確 `import` 在路徑字串之後
啟用公開巨集；`export import` 和 `#include` 還會再匯出這些巨集。
`implicit_import_paths` 僅匯入宣告。這些規則同樣適用於 `source_overlays`。
公開函式式巨集在呼叫端環境中展開，不擷取私有輔助巨集。不同來源的同名公開巨集
會衝突；有意替換前先使用 `#undef`。來源匯入環的相依閉包中出現任何
`#define_export`（包括非作用中分支的指令）時，都會被拒絕，不受
`allow_circular_imports` 影響。

C ABI 建置的程式庫套件在兩種 `strip_debug_info` 設定下都保留公開巨集。條件匯出
在建置時確定，使用端的巨集定義不會重新選擇匯出分支；應用程式套件不攜帶公開
巨集介面。位元組碼使用固定 `VSBC` 標識及獨立格式版本，載入要求版本完全相符；
含 `VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` 的套件與快取須從
原始碼重新建置。公開 C ABI 仍為 0.1.1。獨立 JIT 後端也應使用目前核心重建；
載入器會拒絕標頭指紋或配置不相容的模組。

## 類型和值

`vs_interop_value_create_array` 正規化元素型別並逐一檢查值。物件槽位接受繼承所宣告基底類別或實作所宣告介面的指令碼衍生物件，依來源值自身的模組中繼資料判斷。容器宣告仍保持不變性：不會因此讓 `array<Derived>` 可指派給 `array<Base>`，也不新增隱含數值或容器轉換。

Interop 的類型描述以 `vs_interop_native_type_desc` 遞歸來表達宿主函式或成員的傳回類型、參數類型及容器類型參數。公共原生類型包括：

| 類別 | 類型識別 |
| --- | --- |
| 標量 | `VOID`、`BOOL`、`INT32`、`UINT32`、`INT64`、`UINT64`、`FLOAT`、`DOUBLE`、`INT8`、`UINT8`、`INT16`、`UINT16`、`STRING`。 |
| 宿主與枚舉 | `HOST_OBJECT`、`ENUM`。名稱欄位用於宿主類型名、枚舉名或函式簽名。 |
| 容器 | `ARRAY`、`MAP`、`SET`、`QUEUE`、`STACK`、`ORDERED_MAP`、`DEQUE`、`CHANNEL`、`MULTI_MAP`、`ORDERED_MULTI_MAP`、`PRIORITY_QUEUE`、`MULTI_SET`、`ORDERED_MULTI_SET`。 |
| 可呼叫物件 | `FUNCTION`。 |

值控制代碼的 `vs_interop_value_kind` 也區分 `OBJECT`、`NATIVE_ARRAY`、`WSTRING`、`FUNCTION` 和 `HOST_OBJECT`。目前固定寬度數值為獨立值種類：`FLOAT` 是 binary32，`DOUBLE` 是 binary64；`INT8`、`UINT8`、`INT16`、`UINT16` 不應在適配器中退化為 `INT32` 或無符號 C `int`。

標量使用成對的 `vs_interop_value_set_*`/`get_*` 函式；字串使用長度明確的 UTF-8 `vs_interop_string_view`，寬字串使用 UTF-16 單元和長度。值控制代碼可建立、複製、重設和銷毀；`clone` 複製值包裝器的參考語意，不會自動深複製容器或物件載重。使用 `vs_interop_value_assign(destination, source)` 可將完整執行階段值複製到可寫目標，同時保留容器和物件身分語意；唯讀借用目標會被拒絕。

陣列、佇列、堆疊、雙端佇列、映射、集合和通道透過專門的建立、計數、讀取、包含、傳送、接收和關閉函式操作。映射和集合鍵必須是語言支援的精確類型：有符號/無符號整數、UTF-8/UTF-16 字串、限定枚舉和宿主值。宿主值預設按身份比較，也可以註冊帶有相等和雜湊戳的協定。映射查找不存在的鍵時傳回成功並把 `out_found` 設為 `0`，不會覆蓋輸出值；打開通道接收可能阻塞，關閉通道會喚醒等待者。

## 原生綁定與呼叫

適配器可以註冊三類資訊：

- `vs_interop_session_register_native`：快速註冊原生函式；
- `vs_interop_session_register_host_function`：使用結構化傳回類型、參數、檔案、參數名稱、註解和泛型元資料註冊函式；
- `vs_interop_session_register_host_type`、`vs_interop_session_register_host_member`、`vs_interop_session_register_host_protocol`：註冊枚舉、結構體、類別、建構子、方法、屬性、欄位、靜態成員、運算子、複製、相等、雜湊和子類別工廠。

宿主函式和成員的參數標誌是位元標誌：`VS_INTEROP_PARAM_REF = 0x01`、`VS_INTEROP_PARAM_CONST_REF = 0x02`。 `const ref` 必須同時包含引用位元。宿主回呼可以從 `vs_interop_call_context` 讀取參數、讀取可變參數、取得 `host_this` 和程式代際，並呼叫回呼局部的腳本函式或可保留 callable。

呼叫腳本函式、方法、建構函式、欄位和全域量的入口分別是：

| 能力 | 代表函式 |
| --- | --- |
| 自由函式/方法 | `vs_interop_session_call_function`、`vs_interop_session_call_method`。 |
| 攔截繞過 | `vs_interop_session_call_method_bypassing_interceptor`。 |
| 查詢 | `vs_interop_session_has_function`、`vs_interop_session_has_method`。 |
| 物件與成員 | `vs_interop_session_construct_object`、`vs_interop_session_get_field`、`vs_interop_session_set_field`、`vs_interop_session_get_global`、`vs_interop_session_set_global`。 |
| 保留 callable | `vs_interop_session_retain_callable`、`vs_interop_callable_invoke`、`vs_interop_callable_destroy`、`vs_interop_callable_is_valid`。 |

呼叫參數控制代碼必須可寫，因為成功呼叫會把 `ref`/`out` 的最終值寫回原控制代碼；值參數和 `const ref` 參數不會被寫回。呼叫開始前會先驗證參數，驗證失敗不會產生腳本副作用。回呼上下文所取得的唯讀值是藉用值，需要在回呼結束後保留時先 `clone`。

## 反射、攔截與擴展能力

會話可以匯出兩類 JSON：

- `vs_interop_session_export_bindings_json`：函式、宿主類型、成員、容器元資料、參數標誌、參數名稱、檔案和註解，適用於 IDE、LSP 和靜態檢查；
- `vs_interop_session_export_reflection_json`：已載入模組的類型、欄位、方法、全域量和宣告訊息，適合執行階段工具。

綁定描述中的註記詞彙表和宣告上的註解實例是兩種不同資料：詞彙表描述工具可以認識的名稱和參數，實例表示註解實際附著到了哪個宣告。

另外也可以按會話報名：

- `vs_interop_session_set_instance_call_interceptor`：攔截實例方法，支援 `UNHANDLED`、`HANDLED` 和 `FAILED`；回呼中的 `proceed` 使用已解析的目標，適合實作觀測或權限層；
- `vs_interop_session_set_mutation_callback`：接收欄位寫入、容器增加、刪除、設定和清空事件，並攜帶物件類型、欄位、值和物件修改代際；
- `vs_interop_session_apply_class_defaults`：把宿主提供的類別預設值套用到腳本物件；
- `vs_interop_session_register_generic_host_type_template` 和 `vs_interop_session_instantiate_generic_host_type`：由同步工廠以特定類型參數建構宿主值類型；
- `vs_interop_session_register_type_alias`：註冊透明宿主類型別名；
- `vs_interop_session_register_external_serializer`、`vs_interop_session_serialize_object`、`vs_interop_session_deserialize_object` 和 `vs_interop_session_enumerate_object_references`：存取宿主物件的外部編碼、解碼和遞歸參考遍歷。

外部反序列化回呼只能為臨時交易新增命名欄位賦值，不能直接取得腳本物件儲存。未知的宿主套件元資料會依位元組保留。

## 套件與熱重載

<a id="package-container-format"></a>

`.vslib` 是 Core 程式庫映像；原始 Core 應用程式映像是標準 ZIP `.vsapp` 容器內的已編譯載荷。啟用 Application 支援時，套件位元組 API 也接受一般應用 ZIP，應用建置會傳回 ZIP 容器；未啟用時則傳回原始 Core 映像。需要明確設定主機環境、簽章或保護選項時，請使用 Application 載入 API。保留應用容器時，套件的正規簽章位元組是原始容器位元組；原始映像則使用 Core 正規位元組。解析成功不代表發行者可信。

Interop 可以在記憶體中讀取、校驗、建置和執行 `.vsapp`/`.vslib`：

- `vs_interop_package_read` 解析記憶體位元組並執行核心格式、版本和結構校驗；
- `vs_interop_package_get_kind`、`vs_interop_package_get_name`、`vs_interop_package_get_version`、`vs_interop_package_metadata_count` 和 `vs_interop_package_get_metadata` 讀取套件資訊；
- `vs_interop_package_get_canonical_signed_bytes` 傳回平臺穩定的借用位元組範圍；跨越信任邊界時，宿主必須先驗證簽章；
- `vs_interop_session_load_package` 載入應用程式或函式庫套件；
- `vs_interop_session_reload_package` 以事務方式重載套件，只有成功後才消費模組；
- `vs_interop_session_build_package_file` 在記憶體中建立套件位元組。

舊開發版產生的套件與位元組碼快取使用舊格式標識，須從原始碼重新建置後才能供目前執行階段使用；`vs_interop_package_read` 會拒絕舊版配置。

用 `vs_interop_session_set_hot_reload_enabled` 啟用來源檔案熱重載。
`vs_interop_session_reload` 和 `vs_interop_session_reload_package` 可先暫存候選模組，
在下一次執行時初始化。若有 `VS_INTEROP_CAP_RELOAD_STATUS`，設定
`vs_interop_reload_status.struct_size`，並在建立執行緒呼叫
`vs_interop_session_get_reload_status`。`PENDING` 表示候選模組已暫存，舊世代仍是
已提交世代。`ROLLED_BACK` 表示已還原舊世代。`APPLIED_WITH_ERRORS` 表示候選模組已
提交，之後的執行或清理發生錯誤。`RESET_REQUIRED` 表示需清除工作階段並重新載入程式。
若 `vs_interop_session_clear` 回報回復未完成，請銷毀並重新建立工作階段。
可用 `operation_id` 關聯同一次重載的各階段快照。

相容的全域變數、欄位和物件身分可以遷移。不相容的型別變更、活動通道和擷取閉包可能
阻止遷移。取代模組前先釋放依賴該模組的 C 自有值。宿主原生副作用不屬於指令碼交易。

## 執行緒、借用和銷毀規則

這是適配器必須遵守的邊界：

- 會話建立、普通會話 API 和最終銷毀都限制在建立執行緒；
- 錯誤執行緒銷毀傳回 `VS_INTEROP_STATUS_WRONG_THREAD`，不會消費控制代碼；
- `owner_thread_only == 0` 的保留 callable 可以在其他執行緒呼叫，但前提是宿主串列該會話的全部存取；會話本身仍不是並發安全性物件；
- 腳本回呼可能來自 VM 工作執行緒並且可能並發執行，Python 等外部執行階段必須在回呼中取得自己的執行階段鎖，並同步共用狀態；
- 回呼上下文、字串視圖、容器元素視圖和宿主元資料參數都是藉用數據，只在頭檔案規定的回呼或操作期間有效；需要跨越該邊界時複製字串或克隆值；
- 不要釋放模組擁有的字串、位元組和視圖；模組產生的 `vs_interop_buffer` 只能使用 `vs_interop_buffer_destroy`；
- 外部回呼無法讓異常穿過 C ABI；應把錯誤寫入上下文並回傳 `VS_INTEROP_CALLBACK_ERROR`；
- 物件、容器和值的生命週期遵循 VeroScript 的引用語意，適配器不能透過複製 C 結構體來推斷深複製。

## 能力協商與相容性

`vs_interop_get_capabilities()` 傳回可選能力位，包括：套件、熱重載、宿主綁定、綁定 JSON、反射 JSON、實例攔截、修改回呼、容器值、保留 callable、泛型宿主類型、外部序列化、JIT 診斷、本地化、獨立數值基本類型、容器遍歷、執行治理、可重用工作階段程式碼快取、明確後端選擇和單次初始化。適配器應先檢查 ABI，再根據能力位元決定是否呼叫可選匯出；能力發現不改變 `VS_INTEROP_ABI_VERSION`。

`vs_interop_get_capabilities()` 回報行程 API 功能，包括
`VS_INTEROP_CAP_JIT_BACKEND_SELECTION`。執行階段沒有隱含後端，因此全域結果不設定
`VS_INTEROP_CAP_NATIVE_JIT` 或 `VS_INTEROP_CAP_NATIVE_TYPED_LOOPS`。應查詢
`vs_interop_session_get_jit_backend_info`，使用 `native_code`、
`VS_INTEROP_JIT_CAP_NATIVE_CODE` 與 `VS_INTEROP_JIT_CAP_NATIVE_TYPED_LOOPS`
判斷目前工作階段所選後端的能力；不能透過平台字串推斷原生支援。ABI 0.1.1 追加到 `vs_interop_jit_stats` 的
`cost_model_vm_fallbacks` 與 `native_typed_loop_executions` 只有呼叫端提供足夠大的
`struct_size` 才可見；舊呼叫端會安全保留原有前綴。

公共介面的相容性規則為：不改變已有函式簽章、枚舉值和已發佈結構版面設定；新增結構欄位只能追加，並由 `struct_size` 控制可見範圍。適配器應使用 `vs_interop_get_runtime_version` 和 `vs_interop_get_host_platform` 記錄診斷訊息，而不要硬編碼平臺佈局。

Interop SDK 不規定 Python、C#、Rust、Unreal 或 AOT 的特定策略，也不把它們的執行階段打包進函式庫。它依賴 `VeroScriptCpp` 的公開嵌入能力，但不改變語言核心。Web 場景的瀏覽器安全入口請見 [WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-TW/index.md)，綁定目錄 JSON 的欄位級格式請參考[宿主綁定](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/bindings.md)。

## ABI 的值種類

ABI 0.1.1 包含 `NULL`、`MULTI_MAP`、`ORDERED_MULTI_MAP`、`PRIORITY_QUEUE`、
`MULTI_SET` 與 `ORDERED_MULTI_SET` 值種類。使用
`vs_interop_value_set_null` 與 `vs_interop_value_is_null` 表示 VeroScript
的 `nullptr`；null 是獨立的真實值，不是無效控制代碼。

`vs_interop_value_create_sequence` 支援 `PRIORITY_QUEUE`。
`vs_interop_value_create_map_kind` 可建立一般映射與多重映射；多重映射保留
每一筆重複的鍵值關聯，一般映射遇到重複鍵時會取代既有值。
`vs_interop_value_create_set_kind` 可建立一般集合與多重集合；多重集合保留
重複元素。

`vs_interop_value_set_host_object_ex` 接受可選的
`vs_interop_host_object_is_type` 回呼，用於在多載或型別驗證期間判斷宿主
物件子型別是否相容。回呼不得拋出例外，也不得保存借用的型別名稱視圖。

## 執行模式

`VS_INTEROP_RUN_EVAL` 保留增量求值語意。`VS_INTEROP_CAP_EVAL_ONCE`
宣告的 `VS_INTEROP_RUN_EVAL_ONCE` 只執行一次模組初始化，不呼叫 `main`，
並允許所選 JIT 後端執行初始化。`VS_INTEROP_RUN_SCRIPT` 還會呼叫 `main`。
執行策略另行設定；未選擇後端時，Mixed 使用 VM，強制 JIT 在執行前失敗。

一般工作階段每次執行都會初始化新的狀態；EvalOnce 不會讓後續執行略過初始化。
熱重載工作階段保留已初始化的全域變數。保留狀態的初始化使用 VM，符合條件的執行使用所選後端。

## JIT 明確選擇

執行策略與後端選擇分別設定。未提供已安裝模組環境時，Runtime 不提供預設模組：未選後端時 mixed 使用 VM，強制 jit 在執行前失敗。工作階段閒置時，在所有者執行緒使用 `vs_interop_session_set_jit_backend` 選擇獨立安裝且相容的程式庫檔案或目錄；`vs_interop_session_set_jit_backend(session, (vs_interop_string_view){NULL, 0})` 清除選擇。取代失敗保留先前後端。`vs_interop_session_get_jit_backend` / `vs_interop_session_get_jit_backend_info` 查詢實際身分與原生能力；`backend-services` 僅表示 Core 服務契約。偵錯、保留狀態的初始化和有界清理使用 VM。受控 Mixed 預設使用 VM，可透過 `enable_controlled_mixed` 啟用；明確選擇 JIT 不依賴該開關。符合條件的受控入口可使用 JIT，不支援或優先選擇 VM 的入口保留檢查並回退到 VM。檔案/目錄搜尋與 ABI 規則見 JIT 指南。

查詢前設定結構的 struct_size。文字檢視借用所選工廠儲存，在更換後端或銷毀工作階段前複製。

[JIT](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/zh-TW/index.md)

## 受管理記憶體

宿主介面：`vs_interop_memory_*` / `vs_interop_session_set_memory_limits`。所有權、配額、檢視失效與租借規則請參閱 [`std::memory`](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)。

透過 `VS_INTEROP_CAP_MEMORY` 查詢此增量擴充；既有結構配置和 ABI 版本不變。每次只能提供一種上下文：所有者執行緒上閒置的 `session`，或原生回呼期間有效的 `call`。執行中重入 session 傳回 `BUSY`；回呼操作使用目前 VM 的執行治理檢查。限制、統計及租借結構須先設定 `struct_size`。配額為 0 表示無限制。套用設定、Run 和 Clear 均保留宿主持有區域的配額計數。

`from_array` 要求 uint8 陣列；`to_array` 和 memory `clone` 產生獨立位元組副本。策略啟用時，位址查詢要求 `VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS`，物件 ID 和有界位元組存取不受影響。寫租借排斥競爭的位元組存取，讀租借排斥位元組修改，所有租借均阻止調整大小和失效操作；同步完成原生工作後，恰好釋放一次 token。釋放允許跨執行緒及在 session 銷毀後進行。外部區域註冊成功後接管 `owner`，在最終釋放執行緒呼叫一次釋放回呼；失敗時仍由宿主負責 owner。回呼程式碼及使用者資料必須保持有效直到最終釋放。

## 編譯註記處理與診斷級別

透過 `vs_interop_get_compile_capabilities()` 查詢
`VS_INTEROP_COMPILE_CAP_ANNOTATION_PROCESSING`。原生 C++ 宿主使用
`VeroScriptInteropAnnotation.h` 的 `CreateAnnotationProcessingService` 及已凍結登錄表
建立不透明服務，需要配套 Core SDK 及一致的編譯器/STL ABI；C11 宿主僅傳遞服務控制代碼。
`vs_interop_annotation_service_retain` 傳回另一個獨立擁有的控制代碼，各自使用
`vs_interop_annotation_service_release` 釋放。在閒置的建立執行緒上呼叫
`vs_interop_session_set_annotation_service` 附加服務。工作階段複製設定，因此宿主可立即
釋放傳入控制代碼；一般 ApplyConfig 保留服務，傳入 NULL 才明確清除。編譯回呼或服務
釋放回呼中的設定重入會被拒絕。原生程式碼和回呼資料必須存活至所有控制代碼及附加
工作階段都釋放服務。控制代碼僅限本處理程序，不能序列化。

現有診斷結構配置和 C ABI 0.1.1 保持相容。透過
`vs_interop_session_get_diagnostic_severity` 查詢級別，
`VS_INTEROP_DIAGNOSTIC_SEVERITY_` 前綴的 `ERROR`、`WARNING`、`INFORMATION`、`HINT`
分別為 0、1、2、3。無效索引或錯誤執行緒查詢不修改輸出。成功編譯也可攜帶非錯誤
診斷，應檢查結果 status 和 success；非錯誤診斷之後發生失敗時，last-error 文字選擇
主要錯誤。

## variant

<a id="exported-value-lifetime"></a>

匯出的腳本所屬值登記為工作階段根。讀取與使用必須在建立執行緒進行；其他執行緒釋放包裝器僅排入佇列，由擁有者安全點清理。替換、重載或清空模組前釋放所有快照；關閉工作階段會使依賴模組的包裝器失效並執行語言清理。獨立基本值盒在關閉後仍可使用。不自動映射 C# object/dynamic 或任意 Python 物件。

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

目標已確定為 variant 時允許具體值隱式裝箱。讀取須明確 cast<T> 或 try_cast<T>；variant_is<T> 精確檢查儲存型別，不做轉換。optional<T> 與 try_cast<T> 是 Core 內建功能，不需匯入 core.vs。對空 variant 執行 cast<T> 會失敗；try_cast<T> 遇到空 variant 或一般轉換不符時回傳空 optional。複製失敗、取消和限額仍為錯誤。

值仍為 HOST_OBJECT；numeric_kind 驗0其他/不可用、1bigint、2rational，已關閉或跨執行緒腳本值回0。檢查 VS_INTEROP_CAP_NUMERICS。numeric_parse/to_text/convert 必須且只能傳閒置所屬執行緒 Session 或活動原生呼叫借用 context 之一。文字含明確 UTF-8 長度，to_text 寫普通字串，convert 為固定整數/float/double 的 VS_INTEROP_NATIVE_* 與 exact_float 0/1，失敗保留輸出。初始化 limits.struct_size、reserved=0；零選有限 Core 預設，不是處理序堆積上限。

`vs_interop_session_set_numeric_limits`
`vs_interop_value_numeric_kind`
`vs_interop_numeric_parse`
`vs_interop_numeric_to_text`
`vs_interop_numeric_convert`

## 模組相關值的所有權

<a id="value-domain-confinement"></a>

擁有值控制代碼不代表其承載內容脫離工作階段。指令碼物件／函式及具有模組值欄位的可變容器即使為空，也受工作階段建立執行緒約束，並在工作階段關閉後無法使用。主程式建立的儲存空間首次被工作階段使用時建立此歸屬，共用別名和複本保留此歸屬。VM 工作執行緒回呼只能匯出獨立擁有的值；包含 variant／函式欄位的空容器也不是獨立值。使用後仍須釋放擁有的控制代碼，包括工作階段已關閉的情況。

## Session 後端選擇參數

`vs_interop_session_set_vm_backend` / `vs_interop_session_set_jit_backend`

這些 Session 設定方法接受相符檔案／目錄；提供的模組環境含解析器時，也接受已安裝後端 ID。`auto` 要求環境候選，VM 還可使用保留的產品後備。`none` 或原生空字串停用選擇；`{NULL, 0}` 對應該空要求。新建工作階段未明確選擇時可繼承環境；省略與明確清除不同。重新設定現有工作階段時，省略 VM 選擇會保留既有執行域。明確選擇失敗保留先前後端。僅在建立執行緒的閒置工作階段設定；VM 必須在執行前更換，因為 Clear／reload 不解除執行域固定。JIT 選擇與 VM／Mixed／JIT 執行策略獨立。

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

<a id="host-memory-ownership"></a>

## 主程式共用記憶體擁有權

透過 Session 記憶體 API 建立緩衝區、有界檢視與位元組快照。Session 閒置時在所屬執行緒操作；在註冊的原生回呼內使用目前活躍的記憶體環境。複製值控制代碼不會移轉記憶體存取域。

配置前設定限額，0 表示無限。單一緩衝區限額針對容量，保留總量包括擁有容量、外部位元組及擴充時的峰值。降低至目前占用以下會失敗並保留原設定。Run、Clear、重新載入不會釋放宿主保留的區域，也不歸零核算。限額只約束位元組儲存，不是整個堆積限額或完整沙箱。

寫入租借排斥其他位元組存取；所有租借阻止 resize 和失效操作，讀取租借排斥寫入。完成原生工作後才釋放租借。可在其他執行緒及 Session 銷毀後釋放，但已銷毀 Session 不能用於新操作。外部儲存保留擁有者直到最後一個別名或租借釋放；失效操作使所有別名失效。原始位址觀察不保留擁有權。

物件 ID 保留 uint64 精度，不延長物件存留期。啟用原則時，位址查詢需要宿主記憶體位址能力。僅在原生回呼內查詢引數的原始參考目標；傳值副本沒有原始參考位址。不得將回呼環境保留至回呼之外。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
