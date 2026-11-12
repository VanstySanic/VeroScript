# 相互運用 SDK

## JIT 統計の互換性

`vs_interop_session_get_last_jit_stats` では、`struct_size` が 0 の場合は legacy form となり、元の statistics prefix だけを書き込みます。current caller は appended counter を受け取るため `sizeof(vs_interop_jit_stats)` を渡します。`native_typed_loop_executions` は native typed-loop kernel の invocation を数え、source-visible runtime failure を報告して終了した invocation も含みます。これは success counter ではありません。

`native_leaf_executions`、`native_cfg_executions`、`bridge_executions`、`linear_executions`、`vm_trampoline_executions` は、通常の実行可能エントリに実際に入った回数を数えます。エラーや制御移動で終了した呼び出しも含みます。対応する `*_entries` はコンパイル成果物の数です。これらは直近の実行の統計であり、キャッシュヒットではコンパイルなしで実行できます。`scalar_native_fast_path_calls` はホストのネイティブ高速呼び出しのみを数えます。直接呼び出しと型付きループは入れ子のイベントなので、独立した関数呼び出し数として加算しないでください。

C API は集計統計を公開します。任意の関数別診断スナップショット、フォールバック理由、計時は現在 Core/C++ で提供され、C の JIT ポリシーはこの診断収集を有効にしません。

`VeroScriptInterop` は、C、Python、C#、Rust などのアダプター向けの言語非依存プロセス ABI
です。C++ オブジェクトのレイアウトや STL 型に依存せず、VeroScript のセッション、値、
呼び出し、バインディング、パッケージを公開します。C++ から直接組み込む場合は、まず
[ホスト統合](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/embedding.md)を参照してください。言語をまたぐ連携や安定した動的ライブラリ
境界には、この SDK を使用します。

関数シグネチャ、構造体レイアウト、定数、機能の提供状況は、同梱される公開ヘッダーで定義
されます。このページでは契約と利用パターンを説明します。完全な宣言一覧はヘッダーを
参照してください。

## ABI とインストール

SDK は、C11 および C++17 で有効な `VeroScriptInterop.h` パブリック ヘッダーを提供します。

```c
#define VS_INTEROP_ABI_VERSION UINT32_C(0x00000101)
```

現在の基準は ABI 0.1.1 です。上位 16 ビットはメジャー、次の 8 ビットはマイナー、
下位 8 ビットはパッチ バージョンを表します。バインディングは ABI バージョンの完全一致を要求します。記述子と出力構造
`struct_size` で始まります。互換性のあるリビジョンでは最後にフィールドが追加されます。修正済み
文字列ビューとキー/値ペアには意図的に `struct_size` がありません。
レイアウトはヘッダーによって固定されます。

SDK パッケージは、ランタイム ライブラリ、C11 ヘッダー、および CMake インポート ターゲットを
提供します。アダプターには VeroScript C++ ヘッダーや STL ABI は必要ありません。

```cmake
find_package(VeroScriptInterop CONFIG REQUIRED)
target_link_libraries(MyAdapter PRIVATE VeroScript::VeroScriptInterop)
```

Windows では SDK の実行 DLL、翻訳リソース、実行時の法的通知をホスト実行ファイルの隣に配置し、アプリ配布時にも保持してください。`VeroScriptInterop.dll` だけでは実行依存関係が不足します。統合サンプルは文書やソース参照から別途取得してください。

## 不透明なオブジェクト モデル

インターフェイスは不透明なハンドルを使用します。アダプターはそのレイアウトを検査してはなりません。
システム アロケータを使用してモジュールが所有するメモリを解放します。

|ハンドル |目的 |
| --- | --- |
| `vs_interop_session` |分離されたロード、コンパイル、実行、および呼び出しセッション。 |
| `vs_interop_value` |スクリプトのスカラー、オブジェクト、コンテナ、関数、またはホスト オブジェクトの値。 |
| `vs_interop_callable` |現在のネイティブ コールバックを超えて保持されるスクリプト呼び出し可能。 |
| `vs_interop_call_context` |ネイティブ コールバック引数を読み取り、参照引数を書き込み、コールバック ローカルのネストされた呼び出しを実行します。 |
| `vs_interop_buffer` |モジュールが所有するエラー テキスト、パッケージ バイト、またはシリアル化出力。 `vs_interop_buffer_destroy` でリリースします。 |
| `vs_interop_package` |検証済みのインメモリ `.vsapp`/`.vslib` パッケージ。 |
| `vs_interop_intercept_context` |レシーバー、解決されたターゲット、およびインスタンス インターセプターの引数。 |
| `vs_interop_generic_type_context` |具体的な汎用ホスト値のタイプ、メンバー、およびプロトコルを定義します。 |
| `vs_interop_external_serialize_context` / `deserialize_context` |外部オブジェクトのシリアル化および逆シリアル化コールバックのコンテキスト。 |

最小の C ワークフローは、「セッションの作成 → 構成の適用 → ソースまたはファイルのロード
→ 実行 → 診断の読み取り → 破棄」です。

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

スクリプトは C 文字列エスケープを通じて渡されます。実際のスクリプトは以下に従う必要があります。
Tab インデント、独自の行の中括弧、および
`int main` エントリ ポイント。

## ステータス、診断、終了セマンティクス

すべての呼び出しは明示的な `vs_interop_status` 値を返します。一般的なステータスには次のようなものがあります。

|定数 |意味 |
| --- | --- |
| `VS_INTEROP_STATUS_OK` |通話が完了しました。 |
| `VS_INTEROP_STATUS_INVALID_ARGUMENT` |ハンドル、ポインター、文字列ビュー、または配列引数が無効です。 |
| `VS_INTEROP_STATUS_WRONG_THREAD` |セッションまたはオブジェクト スレッドのルールに違反しました。 |
| `VS_INTEROP_STATUS_ENGINE_ERROR` |読み込みまたは実行に失敗しました。 |
| `VS_INTEROP_STATUS_TYPE_MISMATCH` |値の種類またはホスト タイプが一致しません。 |
| `VS_INTEROP_STATUS_CALLBACK_ERROR` |ホスト コールバックが失敗を報告しました。 |
| `VS_INTEROP_STATUS_UNSUPPORTED` |現在のビルドではこの機能は提供されていません。 |
| `VS_INTEROP_STATUS_OUT_OF_MEMORY` / `BUFFER_TOO_SMALL` / `INTERNAL_ERROR` |リソース、出力バッファ、または内部障害。 |

`vs_interop_run_result` には、`struct_size`、`success`、`exit_requested`、
`exit_code`、および `diagnostic_count`:

- `success` は、`exit_code` がゼロかどうかではなく、通常のコンパイル/実行を報告します。
- `int main` が成功すると、ゼロ以外の終了コードが返される場合があります。
- `exit_requested` は、`exit(0)` を含む明示的な `exit(n)` によってのみ設定されます。
- 通常の `int main` リターンでは `exit_requested` は設定されません。
- 失敗した場合、`exit_code` は通常のスクリプト終了コードではなく、エンジンまたは API エラー コードです。

`vs_interop_session_diagnostic_count` を使用し、
`vs_interop_session_get_diagnostic` 診断を読み取ります。各診断には、
`message`、`message_id`、`file_label`、`line`、および `column`。引数は次のとおりです
診断引数 API と `format_diagnostic` を使用して読み取り、フォーマットできます。
返された文字列ビューの有効期間は、それを所有するセッション操作の規則に従います。

## ローカリゼーション

相互運用のローカリゼーションは、コアによってプロセス全体の単一状態として
管理され、ローカリゼーション操作自体はスレッドセーフです。各
`vs_interop_session` に独立した設定があるわけではありません。言語を変更すると、
変更後にコアがフォーマットする診断（`vs_interop_session_format_diagnostic` を
含む）にも影響します。

|機能 |目的 |
| --- | --- |
| `vs_interop_locale_set_language` |正規または正規化可能な BCP 47 タグからロードされた言語を選択します。成功すると `VS_INTEROP_STATUS_OK` を返します。 |
| `vs_interop_locale_get_language` |実際に一致した正規カルチャ タグ (`en-US` や `zh-CN` など) を返します。 |
| `vs_interop_locale_translate` | ロード済み catalog で翻訳した UTF-8 をモジュール所有バッファーで返し、不明文はそのままコピーします。`vs_interop_buffer_destroy` で解放します。 |
| `vs_interop_locale_get_available_languages` |現在ロードされている正規カルチャ タグを含むモジュール所有の UTF-8 JSON バッファを返します。 |
| `vs_interop_locale_load_catalog_directory` |完全なプロセス全体のローカリゼーション ツリーを検証し、置き換えます。 |
| `vs_interop_locale_merge_catalog_directory` |ツリーを検証し、アクティブなカタログにマージします。 `override_existing != 0` は既存のメッセージ ID を置き換えます。 |

ディレクトリは、[診断ローカリゼーション](../../../../../VeroScript/Resources/Docs/User/ja-JP/localization.md) で説明されている
`<culture>/<component>.json` レイアウトを使用し、完全な `en-US` ベースラインを
含める必要があります。カタログの検証または読み込みに失敗すると、
`load_catalog_directory` と `merge_catalog_directory` は
`VS_INTEROP_STATUS_ENGINE_ERROR` を返し、モジュール所有のエラー バッファを
`out_error` に書き込みます。無効な引数、割り当て失敗、内部例外には対応する
ステータスが使用されます。エラー バッファと利用可能言語の JSON バッファは
`vs_interop_buffer_destroy` で解放してください。`vs_interop_locale_get_language`
の文字列ビューはアダプターが所有しないため、保持する場合はコピーしてください。

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

`vs_interop_locale_get_available_languages` は、現在ロードされている正規カルチャ タグを含む JSON 配列を返します。アダプターはこの配列を使用して言語セレクターを設定してください。
ホスト ファイルシステムを再度スキャンする必要はありません。

アダプターは、これらのオプションのエクスポートを呼び出す前に
`vs_interop_get_capabilities()` に `VS_INTEROP_CAP_LOCALIZATION` が含まれていることを
確認してください。カタログはセッション作成前にも、プロセス実行中にも読み込めます。
複数のスレッドまたはセッションで 1 つのプロセスを共有するホストは、1 回の UI
バッチで言語が混在しないよう、言語変更を許可する時点を定める必要があります。

## セッション構成

`vs_interop_session_apply_config` は `vs_interop_config` を受け入れます:

|フィールド |目的 |
| --- | --- |
| `import_paths` |通常のモジュール検索パス。 |
| `implicit_import_paths` |ファイルはすべてのソース モジュールに暗黙的にインポートされます。これにより、ソース宣言が編集または再エクスポートされることはありません。 |
| `script_args` | `main(string[] args)` に渡される引数。 |
| `defines` |オブジェクトのようなマクロまたは条件付きコンパイル定義。 |
| `stdlib_search_paths` |標準ライブラリの検索ルート。 |
| `source_overlays` |メモリ内パス/ソースの上書き。 |
| `bytecode_cache_directory`、`use_bytecode_cache` |バイトコードキャッシュの場所とスイッチ。 |
| `max_call_depth` |実行ごとの再帰/呼び出しの深さの制限。 `0` は環境またはデフォルトを使用します。 |
| `max_instructions` |実行ごとの VM 命令数の上限。`0` は制限なしです。 |
| `timeout_milliseconds` |実行ごとの実時間制限。`0` は制限なしです。 |
| `execution_backend` | `VM = 0`、`Mixed = 1`、`JIT = 2`。 |
| `predefined_macros` |名前、パラメータ、本体、ソース名、および可変個引数フラグを備えた構造化された関数のようなマクロ。 |
| `allow_circular_imports` |循環インポートが許可されるかどうか。デフォルトで有効になっています。 |
| `allow_project_wide_access` |エクスポートされていない宣言をプロジェクト全体で共有できるかどうか。デフォルトでは無効になっています。 |
| `enable_execution_control` |協調的キャンセル トークンを設定します。設定が正常に適用されると、以前のキャンセル要求をリセットします。 |
| `capability_policy_enabled` |標準ライブラリの副作用ポリシーを有効にします。 |
| `allowed_script_capabilities` |`FILE_READ`、`FILE_WRITE`、`FILE_DELETE`、`ENVIRONMENT_READ`、`ENVIRONMENT_WRITE`、`CONSOLE_INPUT`、`CONSOLE_OUTPUT`、`TASK_SPAWN`、`PROCESS_DIRECTORY_CHANGE` のビットマスク。有効時のみ使用します。`chdir` には `ENVIRONMENT_WRITE` と `PROCESS_DIRECTORY_CHANGE` の両方が必要です。 |

`max_instructions` と `timeout_milliseconds` は実行ごとの協調的な制限で、`0` は無制限です。非ゼロ制限または `enable_execution_control` は VM または対応する制御付き JIT 経路で協調的な検査を行います。制御付き Mixed は既定で VM を使い、対象負荷の測定後に `vs_interop_jit_policy.enable_controlled_mixed` で有効化できます。明示的な JIT 選択はこの設定と独立です。選択したバックエンドの `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION` と実行統計 `controlled_native_executions` を確認してください。未対応または VM が低コストと判定された入口は、検査を維持して VM にフォールバックします。`vs_interop_session_request_cancel` はスレッドセーフで、実行制御が無効なら `VS_INTEROP_STATUS_INVALID_ARGUMENT` を返します。実行中のネイティブコールバックを強制終了しません。要求が戻るまでセッションを保持してください。

`capability_policy_enabled` が非ゼロの場合、標準ライブラリの組み込み関数には `allowed_script_capabilities` のビットだけが許可されます。ホストコールバックはこのポリシーの対象外なので、ホスト自身が認可を管理してください。任意のガバナンス機能に依存する前に `VS_INTEROP_CAP_EXECUTION_GOVERNANCE` を確認します。

`vs_interop_jit_policy` は、混合バックエンドのホットネスしきい値、最大値を制御します
コンパイル試行、キャッシュ容量、および積極的コンパイルのバジェット。
`vs_interop_session_get_last_jit_stats` は最新のコンパイル、キャッシュ、
ディスパッチ、フォールバック、バジェットスキップ、高速パス、
`direct_native_calls` のカウンターを返します。これは VM の値ブリッジを
通らず別のネイティブエントリに到達した世代単位の JIT 呼び出し数です。

### 再利用可能なセッションコード キャッシュ

最初に `VS_INTEROP_CAP_SESSION_JIT_CACHE` を確認してください。このオプション機能は `vs_interop_jit_cache_limits`、`vs_interop_jit_cache_stats`、およびセッションの上限設定/取得と統計取得関数を公開します。渡すまたは受け取る拡張可能な構造体では、必ず `struct_size` を初期化します。既定の上限はネイティブコード 8 MiB、保持ペイロード全体 32 MiB で、ゼロの上限は該当リソースの保持を無効化します。このキャッシュはバイトコード キャッシュとは別で、条件を満たす通常の `Run`/`EvalOnce`/`CallFunction` コンテキストのコンパイル済みコードだけを保持します。統計は安定した累積コンテキスト値であり、ライブまたはプロセス全体のメモリ計測ではありません。再構成またはコンテキストの無効化で保持コードは消去され、リソース上限ではスクリプトの意味を変えず安全に VM 実行へフォールバックします。

再利用には `vs_interop_jit_policy.reuse_session_code` が非ゼロ（既定値）で、有効バックエンドが VM 以外である必要があります。`max_compile_attempts` または `max_cached_entries` がゼロなら、その計数は無制限ですが、ネイティブコードと保持ペイロードの上限は有効です。バインディングのリビジョンが安定している必要があります。永続セッションとホットリロードセッションは、プログラム世代に対応するコードを再利用できます。ネイティブバインディング、インスタンスインターセプター、変更コールバックの変更は保持コードを無効化します。デバッグ、永続状態の初期化、上限付きクリーンアップでは VM を使用します。対応する制御付き実行は互換キャッシュと制御識別情報を保持し、繰り返し呼び出しで累積コンパイル予算をリセットしません。

### 公開マクロとパッケージ

`defines` と `predefined_macros` は各ソースモジュールの前処理を初期化します。
`#define` は非公開で、`#define_export` はモジュールの最終的な定義を公開します。
明示的な `import` はパス文字列の直後から公開マクロを有効にし、`export import` と
`#include` は再エクスポートも行います。`implicit_import_paths` は宣言のみを
インポートします。`source_overlays` にも同じ規則が適用されます。公開関数形式
マクロは呼び出し側の環境で展開され、非公開の補助マクロは捕捉しません。
異なる由来の同名マクロは競合するため、意図的に置換する前に `#undef` を使います。
循環するソースの依存関係の閉包に `#define_export` が含まれる場合、非アクティブな
分岐内の指令でも `allow_circular_imports` にかかわらず拒否されます。

C ABI で構築したライブラリパッケージは `strip_debug_info` のどちらの値でも公開
マクロを保持します。条件付きエクスポートは構築時に確定し、利用側の定義では
再評価されません。アプリケーションパッケージは公開マクロを持ちません。
バイトコードは固定の `VSBC` 識別子と独立した形式バージョンを持ち、ロード時に完全一致が
必要です。`VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` を含むパッケージと
キャッシュはソースから再構築してください。公開 C ABI は 0.1.1 のままです。
独立した JIT バックエンドも現在のコアで再構築してください。ヘッダーの指紋や
レイアウトが非互換のモジュールはロード時に拒否されます。

## 型と値

`vs_interop_value_create_array` は要素型を正規化して各値を検証します。オブジェクトスロットは、宣言された基底クラスを継承するかインターフェースを実装するスクリプトオブジェクトを、入力値自身のモジュールメタデータで判定して受け入れます。コンテナー型は不変で、`array<Derived>` を `array<Base>` に代入できるようにはならず、数値やコンテナーの暗黙変換も追加しません。

`vs_interop_native_type_desc` は、戻り値の型、パラメータ、
ホスト関数とメンバーのコンテナ型引数。パブリックネイティブ
タイプには次のものがあります。

|カテゴリー |型識別子 |
| --- | --- |
|スカラー | `VOID`、`BOOL`、`INT32`、`UINT32`、`INT64`、`UINT64`、`FLOAT`、`DOUBLE`、`INT8`、`UINT8`、`INT16`、 `UINT16`、`STRING`。 |
|ホストと列挙型 | `HOST_OBJECT`、`ENUM`; name フィールドには、ホスト タイプ、列挙型名、または関数シグネチャが含まれます。 |
|コンテナ | `ARRAY`、`MAP`、`SET`、`QUEUE`、`STACK`、`ORDERED_MAP`、`DEQUE`、`CHANNEL`、`MULTI_MAP`、`ORDERED_MULTI_MAP`、`PRIORITY_QUEUE`、 `MULTI_SET`、`ORDERED_MULTI_SET`。 |
|呼び出し可能 | `FUNCTION`。 |

`vs_interop_value_kind` は、`OBJECT`、`NATIVE_ARRAY`、
`WSTRING`、`FUNCTION`、および `HOST_OBJECT`。固定幅の数値は、
異なる種類: `FLOAT` は binary32、`DOUBLE` は binary64、および `INT8`/`UINT8`/
`INT16`/`UINT16` は、
アダプター。

スカラーには、ペアの `vs_interop_value_set_*`/`get_*` 関数を使用します。 UTF-8テキスト
長さで区切られた `vs_interop_string_view` を使用します。ワイド文字列は UTF-16 を使用します
コード単位と明示的な長さ。値の作成、複製、リセット、および
破棄できます。`clone` は値ラッパーの参照セマンティクスをコピーしますが、
コンテナやオブジェクトのペイロードを自動的にディープコピーするものではありません。
完全な runtime 値を変更可能な宛先へコピーするには
`vs_interop_value_assign(destination, source)` を使用します。container と object の
identity semantics を保持し、読み取り専用の borrowed destination は拒否されます。

配列、キュー、スタック、両端キュー、マップ、セット、チャネルには専用の作成機能があります。
カウント、読み取り、包含、送信、受信、およびクローズの操作。キーをマップして設定する
言語でサポートされている正確な型である必要があります: 符号付き/符号なし整数、UTF-8/UTF-16
文字列、スコープ付き列挙型、またはホスト値。ホスト値はデフォルトで ID を使用します。または
登録された等価性/ハッシュ スタンプ プロトコル。マップキーが欠落している場合は成功を返します
`out_found == 0` を使用し、出力値を上書きしません。受信元
開いたチャネルからの受信はブロックする場合があります。チャネルを閉じると、
待機中の受信が解除されます。

## ネイティブ バインディングと呼び出し

アダプターは以下を登録できます。

- コンパクトなネイティブ関数の場合は `vs_interop_session_register_native`。
- 構造化された戻り値の型、パラメーター、ドキュメント、パラメーター名、注釈、および汎用メタデータの場合は `vs_interop_session_register_host_function`。
- `vs_interop_session_register_host_type`、`vs_interop_session_register_host_member`、および `vs_interop_session_register_host_protocol` (列挙型、構造体、クラス、コンストラクター、メソッド、プロパティ、フィールド、静的メンバー、演算子、クローン作成、等価性、ハッシュ、およびサブクラス ファクトリ)。

パラメータ フラグはビット フラグです: `VS_INTEROP_PARAM_REF = 0x01` および
`VS_INTEROP_PARAM_CONST_REF = 0x02`。 `const ref` には参照ビットが含まれている必要があります。
ホスト コールバックは、引数の読み取り、変更可能な引数へのアクセス、取得を行うことができます。
`host_this` とプログラム生成、およびコールバックローカル呼び出しの実行
スクリプト関数の、または `vs_interop_call_context` を通じて呼び出し可能に保持されます。

スクリプト関数、メソッド、コンストラクター、フィールド、およびグローバル呼び出しでは、以下を使用します。

|能力 |代表的な機能 |
| --- | --- |
|自由関数とメソッド | `vs_interop_session_call_function`、`vs_interop_session_call_method`。 |
|インターセプターバイパス | `vs_interop_session_call_method_bypassing_interceptor`。 |
|クエリ | `vs_interop_session_has_function`、`vs_interop_session_has_method`。 |
|オブジェクトとメンバー | `vs_interop_session_construct_object`、`vs_interop_session_get_field`、`vs_interop_session_set_field`、`vs_interop_session_get_global`、`vs_interop_session_set_global`。 |
|保持された呼び出し可能項目 | `vs_interop_session_retain_callable`、`vs_interop_callable_invoke`、`vs_interop_callable_destroy`、`vs_interop_callable_is_valid`。 |

成功した呼び出しは最終的な `ref`/`out` 値をハンドルに書き戻すため、引数
ハンドルは書き込み可能である必要があります。値引数と `const ref` 引数は書き戻されません。
引数はスクリプト実行前に検証されるため、スクリプトの副作用が発生した後に
無効な引数で失敗することはありません。ネイティブ コールバック コンテキストから
借用した読み取り専用の値は、保持またはネストされた呼び出しに渡す前に複製してください。

## リフレクション、インターセプト、およびエクステンション ポイント

セッションでは 2 つの JSON ビューをエクスポートできます。

- `vs_interop_session_export_bindings_json` には、関数、ホスト タイプ、メンバー、
  コンテナのメタデータ、パラメータ フラグ、パラメータ名、ドキュメント、アノテーションが
  含まれます。IDE、LSP、静的チェックのアダプターで使用できます。
- `vs_interop_session_export_reflection_json` には、ロードされたモジュールの型、フィールド、
  メソッド、グローバル、宣言メタデータが含まれ、ランタイム ツールで使用できます。

注釈語彙は、名前とサポートされる引数を説明します。注釈インスタンスは、どの宣言が
実際にその注釈を持つかを記述します。両者は別個のデータ概念です。

セッションでは以下を登録することもできます。

- `vs_interop_session_set_instance_call_interceptor`、`UNHANDLED`、`HANDLED`、または `FAILED` を返します。 `proceed` は、すでに解決されたターゲットとオーバーロードを使用します。
- `vs_interop_session_set_mutation_callback` (オブジェクト タイプ、フィールド、値、および変更世代を伴うフィールドの書き込み、コンテナの追加、削除、設定、およびクリア)。
- ホスト提供のクラスのデフォルトの場合は `vs_interop_session_apply_class_defaults`。
- 同期コールバックで作成されたホスト値タイプの場合は `vs_interop_session_register_generic_host_type_template` および `vs_interop_session_instantiate_generic_host_type`。
- 透過的なホスト エイリアスの場合は `vs_interop_session_register_type_alias`。
- 外部エンコード、デコード、および再帰参照トラバーサル用の `vs_interop_session_register_external_serializer`、`vs_interop_session_serialize_object`、`vs_interop_session_deserialize_object`、および `vs_interop_session_enumerate_object_references`。

外部デシリアライザーは、一時トランザクションに名前付きフィールドの割り当てを追加できますが、
スクリプト オブジェクト ストレージに直接アクセスすることはできません。不明なホスト パッケージの
メタデータはバイト単位で保持されます。

## パッケージとホットリロード

<a id="package-container-format"></a>

`.vslib` は Core のライブラリーイメージです。生の Core アプリケーションイメージは、標準 ZIP `.vsapp` コンテナー内のコンパイル済みペイロードです。Application 対応ビルドではパッケージのバイト API は通常のアプリ ZIP も受け入れ、アプリのビルドは ZIP コンテナーを返します。非対応ビルドでは生の Core イメージを返します。環境、署名、保護を明示的に設定する場合は Application 読み込み API を使用してください。署名対象の正規バイト列は、保持されたコンテナーなら元のコンテナーバイト列、生イメージなら Core の正規バイト列です。解析だけでは発行元の信頼性は確認されません。

相互運用機能は、メモリ内の `.vsapp` および `.vslib` パッケージを読み取り、
検証、構築、実行できます。

- `vs_interop_package_read` は、コアの形式、バージョン、および構造の検証を実行します。
- `vs_interop_package_get_kind`、`vs_interop_package_get_name`、`vs_interop_package_get_version`、`vs_interop_package_metadata_count`、および `vs_interop_package_get_metadata` はパッケージ情報を読み取ります。
- `vs_interop_package_get_canonical_signed_bytes` は、プラットフォームで安定した借用バイトを返します。ホストは信頼境界を越える前に署名を検証する必要があります。
- `vs_interop_session_load_package` は、アプリケーションまたはライブラリ パッケージをロードします。
- `vs_interop_session_reload_package` はトランザクション的にリロードし、成功した場合にのみモジュールを消費します。
- `vs_interop_session_build_package_file` はメモリ内にパッケージのバイトを構築します。

以前の開発版の形式で生成したパッケージとバイトコードキャッシュは、現在のランタイムで使用する前にソースから再構築してください。`vs_interop_package_read` は古い形式を拒否します。

`vs_interop_session_set_hot_reload_enabled` でソースのホットリロードを有効にします。
`vs_interop_session_reload` と `vs_interop_session_reload_package` は、次回の実行で
初期化する候補を準備できます。`VS_INTEROP_CAP_RELOAD_STATUS` がある場合は
`vs_interop_reload_status.struct_size` を設定し、作成元スレッドで
`vs_interop_session_get_reload_status` を呼び出します。`PENDING` では候補が
準備され、旧世代が確定したままです。`ROLLED_BACK` は旧世代への復元を示します。
`APPLIED_WITH_ERRORS` では、確定後の実行や後処理でエラーが発生しても新世代が
残ります。`RESET_REQUIRED` では、セッションをクリアしてプログラムを読み込み直します。
`vs_interop_session_clear` がロールバック未完了を報告した場合はセッションを
破棄して作成し直します。同じ操作は `operation_id` で追跡できます。

互換性のあるグローバル変数、フィールド、オブジェクトの同一性は移行できます。
互換性のない型変更、使用中のチャネル、キャプチャを持つクロージャは移行を妨げる
ことがあります。モジュールを置換する前に、そこに属する所有 C 値を解放してください。
ネイティブ側の副作用はスクリプトのトランザクションの対象外です。

## スレッド、借用、および破壊

アダプターは次の境界を守る必要があります。

- セッションの作成、通常のセッション API、および最終的な破棄は作成者スレッドに限定されます。
- 間違ったスレッドを破棄すると、ハンドルを消費せずに `VS_INTEROP_STATUS_WRONG_THREAD` が返されます。
- `owner_thread_only == 0` で保持された呼び出し可能オブジェクトは、ホストがセッションへのすべてのアクセスをシリアル化する場合にのみ、別のスレッドから呼び出すことができます。セッション自体はまだスレッドセーフではありません。
- スクリプトの実行によってトリガーされるコールバックは VM ワーカー スレッドで発生し、重複する可能性があるため、外部ランタイムは独自のランタイム ロックを取得し、共有状態を同期する必要があります。
- コールバック コンテキスト、文字列ビュー、コンテナ要素ビュー、およびホスト メタデータ引数は、文書化されたコールバックまたは操作の有効期間のために借用されます。文字列または値を保持する前にコピーしてください。
- モジュールが所有する文字列、バイト、ビューはアダプターによって解放されてはなりません。モジュールが所有する `vs_interop_buffer` オブジェクトは、`vs_interop_buffer_destroy` でのみ解放されます。
- 外部コールバックは、C ABI 全体で例外をアンワインドしてはなりません。エラーをコンテキストに保存し、`VS_INTEROP_CALLBACK_ERROR` を返します。
- 値、オブジェクト、およびコンテナの有効期間は、VeroScript 参照セマンティクスに従います。 C 構造体のコピーはディープコピー操作ではありません。

## 機能のネゴシエーションと互換性

`vs_interop_get_capabilities()` は、パッケージ、ホット リロード、ホスト バインディング、バインディング JSON、
リフレクション JSON、インスタンス インターセプト、ミューテーション コールバック、コンテナ値、保持される
呼び出し可能オブジェクト、汎用ホスト タイプ、外部シリアル化、JIT 診断、ローカリゼーション、個別の数値
プリミティブ、コンテナ列挙、実行ガバナンス、再利用可能なセッション コード キャッシュ、明示的なバックエンド選択、
一回の初期化に対応するオプションの機能ビットを報告します。最初に ABI を確認し、オプションのエクスポートを
呼び出す前に機能を確認してください。機能の検出は `VS_INTEROP_ABI_VERSION` を変更しません。

`vs_interop_get_capabilities()` は `VS_INTEROP_CAP_JIT_BACKEND_SELECTION` などの
プロセス API 機能を報告します。暗黙のバックエンドはないため、グローバル結果には
`VS_INTEROP_CAP_NATIVE_JIT` と `VS_INTEROP_CAP_NATIVE_TYPED_LOOPS` を設定しません。
選択したセッションの能力は `vs_interop_session_get_jit_backend_info` の
`native_code`、`VS_INTEROP_JIT_CAP_NATIVE_CODE`、
`VS_INTEROP_JIT_CAP_NATIVE_TYPED_LOOPS` で確認します。プラットフォーム文字列だけでは
ネイティブ能力を判定できません。
ABI 0.1.1 で追加された `vs_interop_jit_stats` の `cost_model_vm_fallbacks` と
`native_typed_loop_executions` は、呼び出し側が十分に大きい `struct_size` を渡した場合だけ見えます。
古い呼び出し側は従来の prefix を安全に保持します。

互換性ルールにより、既存の署名、列挙値、公開済みの構造体レイアウトは変更されません。新しい構造体
フィールドは末尾に追加され、その可視範囲は `struct_size` で保護されます。アダプターは、プラットフォーム
ごとのレイアウトをハードコードする代わりに、`vs_interop_get_runtime_version` と
`vs_interop_get_host_platform` を診断情報として記録できます。

SDK は Python、C#、Rust、Unreal、または AOT のポリシーを規定せず、これらの
ランタイムもバンドルしません。言語コアを変更することなく、公開された
`VeroScriptCpp` の埋め込みサーフェスに依存します。ブラウザーで安全に使える
メモリ内エントリポイントについては [WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ja-JP/index.md) を、フィールド単位の
バインディング カタログ形式については [ホスト バインディング](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/bindings.md) を参照してください。

## ABI の値種別

ABI 0.1.1 には `NULL`、`MULTI_MAP`、`ORDERED_MULTI_MAP`、`PRIORITY_QUEUE`、
`MULTI_SET`、`ORDERED_MULTI_SET` の値種別が追加されました。
VeroScript の `nullptr` には `vs_interop_value_set_null` と
`vs_interop_value_is_null` を使用します。null は無効なハンドルではなく、
独立した値です。

`vs_interop_value_create_sequence` は `PRIORITY_QUEUE` に対応します。
`vs_interop_value_create_map_kind` は通常の map と multi-map を作成し、
multi-map は重複するキーと値の関連をすべて保持します。通常の map は
重複キーの値を置き換えます。`vs_interop_value_create_set_kind` は set と
multi-set を作成し、multi-set は重複要素を保持します。

`vs_interop_value_set_host_object_ex` の
`vs_interop_host_object_is_type` callback はホストオブジェクトのサブタイプ
互換性を検査します。callback は例外を送出せず、借用した型名 view を保持してはなりません。

## 実行モード

`VS_INTEROP_RUN_EVAL` は増分評価の意味を維持します。
`VS_INTEROP_CAP_EVAL_ONCE` で確認できる `VS_INTEROP_RUN_EVAL_ONCE` は、
`main` を呼ばずにモジュール初期化を一度実行し、選択した JIT バックエンドによる
初期化の実行を許可します。`VS_INTEROP_RUN_SCRIPT` はさらに `main` を呼びます。
実行ポリシーは別に設定します。バックエンド未選択時は Mixed が VM を使い、
強制 JIT は実行前に失敗します。

通常のセッションでは、実行するたびに新しい状態を初期化します。EvalOnce は後続の
実行の初期化を省略しません。ホットリロードのセッションでは、既存の永続状態の規則に
従って初期化済みのグローバル変数を保持します。状態を保持する初期化は VM を
使用し、対応する実行では選択したバックエンドを使用します。

## JIT の明示選択

実行ポリシーと backend 選択は別です。インストール済みモジュール環境を渡さない場合、Runtime の既定モジュールはありません。未選択 mixed は VM、強制 jit は実行前失敗です。所有スレッドで Session が待機中に `vs_interop_session_set_jit_backend` を使い、別途導入した互換ファイル/ディレクトリを選択します。`vs_interop_session_set_jit_backend(session, (vs_interop_string_view){NULL, 0})` は選択解除です。変更失敗で前の選択を保持します。`vs_interop_session_get_jit_backend` / `vs_interop_session_get_jit_backend_info` は実際の識別/ネイティブ能力を示し、`backend-services` は Core サービス契約のみです。デバッグ、状態を保持する初期化、上限付きのクリーンアップには VM を使います。制御付き Mixed は既定で VM を使い、`enable_controlled_mixed` で有効化できます。明示的な JIT はこの設定と独立です。対応する制御付き入口は JIT を利用でき、未対応または VM を優先する入口は検査を維持して VM にフォールバックします。検索と ABI は JIT ガイドを参照してください。

問い合わせ前に struct_size を設定します。文字列ビューは選択済みファクトリーから借用するため、backend 変更/Session 破棄前にコピーします。

[JIT](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/ja-JP/index.md)

## 管理メモリ

ホスト API：`vs_interop_memory_*` / `vs_interop_session_set_memory_limits`。所有権、上限、ビューの失効、リースについては [`std::memory`](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md) を参照してください。

追加機能は `VS_INTEROP_CAP_MEMORY` で確認します。既存の構造体と ABI バージョンは変更しません。コンテキストは一つだけ指定します：所有スレッド上で停止中の `session`、またはネイティブコールバック内の `call`。実行中の session 再入は `BUSY`、コールバック操作には実行 VM の制限が適用されます。制限・統計・リースの `struct_size` を設定してください。上限 0 は無制限です。設定変更、Run、Clear はホストが保持する領域の計上を維持します。

`from_array` は uint8 配列を要求し、`to_array` と memory `clone` は独立したコピーです。ポリシー有効時のアドレス取得には `VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS` が必要です。ID と範囲内のバイト操作は影響を受けません。書込リースは競合するバイトアクセスを、読取リースはバイト変更を排除します。すべてのリースはサイズ変更と無効化を防ぎます。ネイティブ処理を同期してから token を一度だけ解放してください。解放は別スレッドや session 破棄後も可能です。外部登録が成功すると `owner` を引き受け、最後の解放スレッドでコールバックを一度呼び出します。失敗時はホストが所有責任を持ちます。最終解放までコールバックとユーザーデータを保持してください。

## コンパイル注釈処理と診断の重大度

`vs_interop_get_compile_capabilities()` で
`VS_INTEROP_COMPILE_CAP_ANNOTATION_PROCESSING` を確認します。ネイティブ C++ ホストは
一致する Core SDK とコンパイラー/STL ABI を使い、`VeroScriptInteropAnnotation.h` の
`CreateAnnotationProcessingService` と凍結済みレジストリーで不透明サービスを作成します。
C11 ホストはハンドルだけを渡します。`vs_interop_annotation_service_retain` は別の所有
ハンドルを返すため、それぞれを `vs_interop_annotation_service_release` で解放します。
アイドル状態の作成スレッドで `vs_interop_session_set_annotation_service` を呼びます。
セッションが設定をコピーするため、渡したハンドルはすぐ解放できます。ApplyConfig は
サービスを保持し、NULL を渡すと明示的に消去します。コンパイルやサービス解放の
コールバックからの設定再入は拒否されます。すべてのハンドルと接続セッションが解放する
まで、ネイティブコードとコールバックデータを保持します。ハンドルはシリアライズできません。

診断の既存レイアウトと C ABI 0.1.1 は互換性を保ちます。
`vs_interop_session_get_diagnostic_severity` で重大度を取得します。
`VS_INTEROP_DIAGNOSTIC_SEVERITY_` の接尾辞 `ERROR`、`WARNING`、`INFORMATION`、`HINT`
はそれぞれ 0、1、2、3 です。無効な添字や異なるスレッドの問い合わせは出力を変更しません。
コンパイル成功時も注記を含められるため、status と success を確認してください。
注記の後に失敗した場合、last-error は主となるエラーを報告します。

## variant

<a id="exported-value-lifetime"></a>

スクリプトに依存する公開値はセッションのルートに登録されます。読み取り・使用は作成スレッドで行い、他スレッドの解放は所有スレッドの安全点に清掃を予約します。モジュールの置換・再読み込み・消去前にすべてのスナップショットを解放してください。終了は依存ラッパーを無効化し言語の清掃を実行します。独立した基本値ボックスは終了後も使えます。C# object/dynamic や任意の Python オブジェクトへの自動変換はありません。

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

代入先が variant と確定した位置では具体値を暗黙にボックス化できます。読み取りは cast<T> または try_cast<T> を明示し、variant_is<T> は変換せず格納型を厳密に調べます。optional<T> と try_cast<T> は Core 組み込みで core.vs のインポートは不要です。空の variant に対する cast<T> は失敗し、try_cast<T> は空の variant や通常の変換不一致で空の optional を返します。複製失敗、キャンセル、制限超過はエラーのままです。

値は HOST_OBJECT のまま、numeric_kind は0その他/利用不可、1bigint、2rationalを確認し、閉じた値や別threadのscript値は0です。VS_INTEROP_CAP_NUMERICSを確認します。numeric_parse/to_text/convertはidle owner Sessionか活動native callの借用contextを正確に一つ指定します。UTF-8は明示長、to_textはstring値、convertは固定整数/float/doubleのVS_INTEROP_NATIVE_*、exact_floatは0/1、失敗出力保持です。limits.struct_sizeとreserved=0を初期化し、0は有限Core既定値でprocessheap上限ではありません。

`vs_interop_session_set_numeric_limits`
`vs_interop_value_numeric_kind`
`vs_interop_numeric_parse`
`vs_interop_numeric_to_text`
`vs_interop_numeric_convert`

## モジュールに依存する値の所有権

<a id="value-domain-confinement"></a>

ハンドルを所有しても値がセッションから独立するわけではありません。スクリプトのオブジェクト／関数とモジュール値を格納できる可変コンテナーは、空でもセッション作成スレッドに限定され、終了後は利用できません。ホスト作成のストレージはセッションでの初回使用時に関連付けられ、共有エイリアスや複製も所有関係を維持します。VM ワーカーのコールバックが外部へ渡せるのは独立した所有値だけです。variant／関数用スロットを持つ空のコンテナーも独立していません。終了後も含め使用済みハンドルを解放してください。

## Session のバックエンド選択引数

`vs_interop_session_set_vm_backend` / `vs_interop_session_set_jit_backend`

これらの Session セッターは、対応するファイル／ディレクトリ、または指定環境にリゾルバーがある場合はインストール済みバックエンド ID を受け取ります。`auto` は環境候補を要求し、VM では保持した製品の代替も使用できます。`none` またはネイティブの空文字列は選択を無効にし、`{NULL, 0}` もその空要求になります。新規セッションでバックエンド指定を省略すると環境による選択が可能になり、明示的な解除とは異なります。既存セッションの再設定で VM 選択を省略した場合は、その実行ドメインを保持します。明示選択に失敗した場合は以前のバックエンドを保持します。作成スレッドの待機中に設定し、VM は実行前に変更してください。Clear/reload でも実行ドメインの固定は解除されません。JIT の選択と VM/Mixed/JIT 実行方針は独立です。

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

<a id="host-memory-ownership"></a>

## ホスト共通のメモリ所有権

Session のメモリ API でバッファ、有界ビュー、バイトスナップショットを作ります。待機中は Session の所有スレッドを使い、登録済みネイティブ callback 内ではその実行コンテキストを使います。ハンドルのコピーはアクセスドメインを移動しません。

割り当て前に上限を設定します。0 は無制限です。バッファごとの上限は容量、保持総量は所有容量と外部バイトに適用され、拡張のピークも含みます。使用量未満への変更は失敗し、設定を変えません。Run、Clear、reload はホストが保持する領域を解放せず、計上をリセットしません。これはバイト保存の上限であり、heap 全体の上限や sandbox ではありません。

書き込み貸出は他のバイトアクセスを禁止します。全貸出は resize と無効化を防ぎ、読み取り貸出は書き込みを防ぎます。ネイティブ処理を終えてから解放してください。別スレッドや Session 終了後も貸出を解放できますが、終了済み Session で新しい操作はできません。外部領域は最後の別名または貸出が解放されるまで所有者を保持し、無効化は全別名を失効させます。生アドレスは所有権を保持しません。

オブジェクト ID は uint64 を保ち、寿命を延ばしません。ポリシー有効時のアドレス照会にはホストのメモリアドレス権限が必要です。引数の元の参照先はそのネイティブ callback 内でのみ照会し、値コピーには元の参照アドレスがありません。callback 終了後にコンテキストを保持しないでください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
