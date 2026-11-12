# Python 執行時與靜態工具

## 型別化迴圈統計

`native_typed_loop_executions` 統計原生型別化迴圈核心的呼叫次數，包括回報原始碼可見執行階段錯誤後結束的呼叫。它是執行可觀測性計數器，而不是成功計數器。

## 執行時介面卡

`VeroScriptPython` 經 `VeroScriptInterop` 公開 `Session`、`Value`、`Package`、保留可呼叫物件、`HostParameter`、宿主函式和宿主類。文件規定的會話和可呼叫物件受建立執行緒限制且必須顯式關閉。允許時原生操作釋放 GIL，回撥重新獲取 GIL 並可能隨時間併發進入，因此回撥擁有狀態需要外部同步。

公共執行時還包括 `ExternalReference` 及其引用種類、`GenericHostClass`、`available_languages`、`get_language`、`set_language` 和 `merge_catalog_directory`。語言發現來自已部署目錄樹。目錄合併會驗證基線，並區分執行時診斷與宿主產品 UI 文字；包裝器不得翻譯 API 名、目錄鍵或引用種類值。

`Value` 為 array、map、ordered map、set、queue、stack 和 deque 使用顯式元素型別；列舉返回擁有的快照。有符號 `char`/`int8` 在 Python 中表示為 `-128..127` 的 `int`；字元 map 鍵和 set 元素在快照、查詢及 `to_python()` 中保持該表示，越界輸入會被拒絕而非截斷。變更回撥異常在原生變更後報告，不會回滾變更。

## 靜態前端

`VeroScriptPythonFrontend` 解析帶型別 Python 宣告而不匯入或執行輸入模組，記錄源範圍、函式、類、enum、欄位、引數、decorator、文件、匯入和規範化型別註解。不支援的頂層執行和巢狀語句須以結構化診斷失敗。

`VeroScriptPythonBindGen` 消費 AST，發射 Binding Catalog 後設資料、每輸入一個 API 引用和專案清單。`VeroScriptPythonTranspiler` 將支援的型別子集降級為 VeroScript，解析提供的專案相對匯入，發射原始碼對映，並驗證完整生成樹。BindGen 和 Transpiler 還會拒絕不符合 VeroScript ASCII 識別字規則或屬於 `do` 等語言關鍵字的生成名稱；即使關閉編譯器驗證，該檢查仍然生效。Transpiler 同時執行核心的 32 層語句巢狀限制，並將 Python `pass` 發射為合法的空 VeroScript 區塊。名稱以下劃線 `_` 開頭的類別和頂層函式不會匯出，也不能被生成的公開契約引用。發射的數值常數必須是有限值，並落在核心無後綴有符號 32 位整數常值範圍內。Python list、set 和 dictionary 常值僅允許作為型別化宣告的初始化器，以符合核心的大括號初始化語法。在型別化子集具備可空型別契約之前，Optional 註解和 Python 身分比較都會被拒絕。`break` 和 `continue` 必須位於發射出的外圍 `while` 迴圈內。

受支援的整數 `IntEnum` 和 `IntFlag` 運算式會被安全求值，並發射為核心 enum 語法要求的有符號 64 位常值。

只有當 `self` 是執行個體方法接收者時，才會將它重寫為 `this`；名為 `self` 的一般識別字保持不變。`None` 僅在宣告為 `-> None` 的函式或建構函式中發射為無傳回值的 `return;`。

非 `void` 函式必須在所有靜態支援的路徑上傳回值。帶型別註記的區域變數必須直接宣告在函式主體中；為避免 Python 與 VeroScript 的作用域產生差異，不允許在 `if` 或 `while` 區塊內宣告。

多檔案專案會在發射前拒絕匯出模組變數與其他模組變數或型別的衝突，以符合核心符號命名空間。

## 會話執行契約

Python `Session` 映象原生生命週期，入口包括 `load_source`、`load_file`、`load_package`、`reload_package`、`build_package_file`、`run`、`reload`、`diagnostics` 和 `set_input`。`set_hot_reload_enabled` 控制保留狀態的過載，`request_cancellation` 可從另一執行緒協作請求取消，不能強制中斷活躍宿主回撥。`set_jit_policy` 和 `jit_stats` 與核心 VM 一樣公開自適應後端策略和最近執行統計。`RuntimeCapabilities` 解碼 `CAPABILITIES` 中的 Core 服務能力。實際選擇的模組透過 `session.get_jit_backend_info()` 查詢：`native_code` 表示原生程式碼生成，`capabilities` 是 `JitBackendCapabilities` 遮罩，包含 `NATIVE_CODE`、`PORTABLE_EXECUTION`、`NATIVE_TYPED_LOOPS` 和 `DIRECT_SCALAR_CALLS`，不能從 `HOST_PLATFORM` 推斷。`cost_model_vm_fallbacks` 和 `native_typed_loop_executions` 分別統計最近執行中對應的決策與執行次數。

Python 配接器將 `reuse_session_code`、`enable_controlled_mixed`、快取限制和統計轉送至原生 Session。資格與世代記帳統一維護在 [C++ Session](../../../../../Cpp/Resources/Docs/Developer/zh-TW/session.md#session-jit-cache)，Python API、預設值與配額含義維護在 [Python Session](../../User/zh-TW/session.md#session-jit-cache)。配接器不另設非零計數前提，亦不一律禁止受治理執行重用。

## 值、呼叫與 channel

`Value.channel` 建立無界或有界 channel。`send`、`try_send`、`send_for` 分別為阻塞、非阻塞和定時傳送；`receive`、`try_receive`、`receive_for` 為對應接收，`close_channel` 關閉同步端點。channel 值是顯式跨 worker 共享機制，普通指令碼全域性仍為 worker 本地。

呼叫麵包括 `has_function`、`has_method`、`call`、`call_method`、`call_method_bypassing_interceptor`、`construct_object`、`retain_callable`、`get_field`、`set_field`、`get_global`、`set_global` 和 `apply_class_defaults`。繞過入口攔截器不會禁用方法體發起巢狀呼叫的攔截器。

## 宿主繫結、反射與持久化

註冊入口含 `register_function`、`register_annotation`、`register_type_alias`、`register_generic_host_type`、`instantiate_generic_host_type`、`register_host_class`、`register_host_enum`、`register_host_protocol` 和 `register_host_member`。反射和繫結匯出使用 `export_reflection_json`、`export_bindings_json`。

`register_serializer`、`serialize_object`、`deserialize_object` 與 `enumerate_object_references` 遵循 Interop 外部序列化和借用引用規則。`set_mutation_callback` 報告欄位/容器變更；`set_instance_call_interceptor` 接收已解析目標，可繼續或處理呼叫。API、JSON 鍵、型別名和診斷均保持原樣。

## Python 執行緒邊界

阻塞、定時和非阻塞 channel 操作在原生 channel 操作期間釋放 Python GIL。這避免等待 channel 阻塞無關 Python 執行緒，但不會使 `Session` 或 `Value` 普遍執行緒安全；會話操作仍遵守原生所有者執行緒規則。

`request_cancellation` 是唯一明確可從其他 Python 執行緒安全呼叫的 `Session` 操作，呼叫方必須保留 `Session` 到原生請求返回。原生回撥異常會記錄並在呼叫邊界後轉回 Python。JIT 統計對映將 `direct_native_calls` 作為獨立計數器。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/runtime-and-tools.md) · [fr-FR](../fr-FR/runtime-and-tools.md) · [ja-JP](../ja-JP/runtime-and-tools.md) · [ko-KR](../ko-KR/runtime-and-tools.md) · [zh-CN](../zh-CN/runtime-and-tools.md) · [zh-TW](runtime-and-tools.md)
