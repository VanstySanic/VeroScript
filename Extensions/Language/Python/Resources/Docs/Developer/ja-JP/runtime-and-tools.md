# Python runtime と static tool

## typed-loop 統計

`native_typed_loop_executions` は native typed-loop kernel の invocation を数えます。source-visible runtime failure を報告して終了する invocation も含まれます。これは execution observability の counter であり、success counter ではありません。

## ランタイムアダプター

`VeroScriptPython` は `VeroScriptInterop` 上で `Session`、`Value`、`Package`、retained callable、`HostParameter`、host function/class を公開します。documented session/callable は creator-thread confined で明示 close が必要です。native operation は可能なとき GIL を release し、callback で reacquire します。callback は時間を通じて concurrent に入るため、callback-owned state は外部で同期します。

`ExternalReference` と kind、`GenericHostClass`、`available_languages`、`get_language`、`set_language`、`merge_catalog_directory` も public surface です。language は deployed catalog tree から検出します。catalog merge は baseline を検証し、runtime diagnostic と host-product UI text を区別します。wrapper は API name、catalog key、reference-kind value を翻訳しません。

`Value` は array、map、ordered map、set、queue、stack、deque に explicit element type を用い、enumeration は owned snapshot を返します。signed `char`/`int8` は `-128..127` の Python `int` として表現されます。character map key と set element は snapshot、lookup、`to_python()` でもこの表現を保持し、範囲外の入力は切り詰めず拒否します。mutation callback exception は native mutation 後に報告し、mutation を rollback しません。

## 静的フロントエンド

`VeroScriptPythonFrontend` は input module を import/execute せず typed Python declaration を parse し、source range、function/class/enum/field/parameter/decorator/doc/import/normalized type annotation を記録します。unsupported top-level execution/nested statement は structured diagnostic で失敗します。`VeroScriptPythonBindGen` は AST から Binding Catalog、input ごとの API reference、project manifest を、`VeroScriptPythonTranspiler` は supported typed subset の VeroScript、relative import 解決、source map、complete generated tree validation を提供します。BindGen と Transpiler は、ASCII VeroScript identifier でない生成名や `do` などの language keyword も、compiler validation が無効な場合を含めて拒否します。Transpiler は core の statement nesting 上限 32 も適用し、Python の `pass` を有効な空の VeroScript block として出力します。名前が `_` で始まる class と top-level function は非 export であり、生成される public contract から参照できません。出力される数値定数は有限で、core の suffix なし signed 32-bit integer literal の範囲に収まる必要があります。Python の list、set、dictionary literal は、core の brace-initializer grammar に合わせ、typed declaration の initializer としてのみ使用できます。typed subset が nullable-type contract を持つまでは、Optional annotation と Python identity comparison を拒否します。`break` と `continue` には、それらを囲む出力済み `while` loop が必要です。

supported integer `IntEnum` / `IntFlag` expression は安全に評価され、core enum grammar が要求する signed 64-bit literal として出力されます。

`self` が `this` に書き換えられるのは instance method の receiver である場合だけで、通常の `self` identifier はそのまま保持されます。`None` は `-> None` function または constructor の値なし `return;` としてのみ出力されます。

non-`void` function は、静的にサポートされるすべての path で値を返す必要があります。annotated local は function body の直下で宣言する必要があり、Python と VeroScript の scope の相違を防ぐため `if` / `while` block 内での宣言は拒否されます。

multi-file project では、export される module variable と別の module variable または type との衝突を、core の symbol namespace に合わせて出力前に拒否します。

## セッション実行の契約

`Session` は `load_source`、`load_file`、`load_package`、`reload_package`、`build_package_file`、`run`、`reload`、`diagnostics`、`set_input` を提供します。`set_hot_reload_enabled` は state-preserving reload を制御し、`request_cancellation` は別 thread から cooperative cancellation を要求しますが active host callback を強制停止しません。`set_jit_policy` と `jit_stats` は core VM と同じ adaptive policy/last-run stats を公開します。`RuntimeCapabilities` は `CAPABILITIES` の Core サービスを解釈します。選択したモジュールは `session.get_jit_backend_info()` で確認します。`native_code` はネイティブコード生成、`capabilities` は `JitBackendCapabilities` の `NATIVE_CODE`、`PORTABLE_EXECUTION`、`NATIVE_TYPED_LOOPS`、`DIRECT_SCALAR_CALLS` を表し、`HOST_PLATFORM` とは独立しています。`cost_model_vm_fallbacks` と `native_typed_loop_executions` は対応する last-run の判断と実行を数えます。

Python アダプターは `reuse_session_code`、`enable_controlled_mixed`、キャッシュ上限、統計をネイティブ Session に渡します。対象条件と世代別集計は [C++ Session](../../../../../Cpp/Resources/Docs/Developer/ja-JP/session.md#session-jit-cache)、Python API の既定値とクォータの意味は [Python Session](../../User/ja-JP/session.md#session-jit-cache) で管理します。アダプター側でゼロ以外の回数制限を要求したり、制限付き実行の再利用を一律に無効化したりしません。

## value、call、channel

`Value.channel` は bounded/unbounded channel を作ります。`send`/`try_send`/`send_for` と `receive`/`try_receive`/`receive_for` は blocking/nonblocking/timed operation、`close_channel` は endpoint を閉じます。channel は cross-worker sharing の明示的機構で、通常 script global は worker-local です。

call surface は `has_function`、`has_method`、`call`、`call_method`、`call_method_bypassing_interceptor`、`construct_object`、`retain_callable`、`get_field`、`set_field`、`get_global`、`set_global`、`apply_class_defaults` を含みます。entry interceptor を bypass しても method body の nested call interceptor は無効になりません。

## host binding、reflection、persistence

registration/inspection API は `register_function`、`register_annotation`、`register_type_alias`、`register_generic_host_type`、`instantiate_generic_host_type`、`register_host_class`、`register_host_enum`、`register_host_protocol`、`register_host_member`、`export_reflection_json`、`export_bindings_json` を含みます。`register_serializer`、`serialize_object`、`deserialize_object`、`enumerate_object_references` は external-serialization/borrowed-reference contract に従います。`set_mutation_callback` は mutation を、`set_instance_call_interceptor` は resolved target を受け取り continue/handle を選べます。API/JSON/type/diagnostic token は source のままです。

## Python のスレッド境界

channel operation は native operation 中 GIL を release しますが、`Session`/`Value` 全体を thread-safe にしません。owner-thread rule は維持されます。`request_cancellation` だけが別 Python thread から明示的に安全であり、caller は native request が返るまで session を保持します。native callback exception は call boundary 後に Python へ変換され、JIT mapping の `direct_native_calls` は独立 counter です。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/runtime-and-tools.md) · [fr-FR](../fr-FR/runtime-and-tools.md) · [ja-JP](runtime-and-tools.md) · [ko-KR](../ko-KR/runtime-and-tools.md) · [zh-CN](../zh-CN/runtime-and-tools.md) · [zh-TW](../zh-TW/runtime-and-tools.md)
