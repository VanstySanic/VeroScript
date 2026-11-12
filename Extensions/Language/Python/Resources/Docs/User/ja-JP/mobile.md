# Android/iOS の Python

対象 CPython のマイナーバージョンに一致する組み込み SDK を使用します。Android は GIL 有効の CPython 3.13/3.14 リリースビルド、arm64-v8a/x86_64、API 26 以上です。iOS は同じ Python バージョン、iOS 15 以上、実機 arm64 とシミュレーター arm64/x86_64 です。デスクトップ wheel では代用できません。CPython と依存ライブラリはアプリが用意します。統合は実験段階です。配備前にシミュレーター／エミュレーターと実機で検証してください。

Android は対応 slice の `jniLibs/<abi>/` と対象 CPython ライブラリを配置します。ネイティブ依存間で互換性のある一つの `libc++_shared.so` を使います。`stdlib/`、`localization/`、`py.typed` を含む `python/veroscript/` をアプリ所有ディレクトリに展開します。最初の import 前に `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR` をインストール済みアプリの絶対パス `ApplicationInfo.nativeLibraryDir` に設定し、`veroscript/` の親を `sys.path` に追加します。JNI ライブラリの展開には `useLegacyPackaging = true` が必要です。APK asset 名はライブラリの実パスではありません。

iOS は対応する `Python.xcframework` と `VeroScriptPython.xcframework` を埋め込み署名します。Bundle の `python/veroscript/`、`_veroscript.fwork`、framework の `.origin` を保持します。`<app-bundle>/python` を `sys.path` に追加し、読み取り専用 Bundle への bytecode 書き込みを無効にします。対象 CPython の Apple framework loader が拡張を読み込みます。デスクトップ `.so` で代用せず、origin を変更せずに資源を移動しないでください。

facade の import 時に付属カタログを読み込みます。その後 `set_language(tag)` で言語を選びます。言語はプロセス共通です。標準ライブラリを facade の隣に保ち、キャッシュはアプリ所有の書き込み可能な実ディレクトリに置きます。Session の作成、設定、実行、終了は同じワーカースレッドで行い、終了後に UI へ結果を渡します。ホスト callback は権限と取消を管理します。ネイティブ JIT は付属しません。実行制限を使う場合は VM を指定します。

```python
import threading
from concurrent.futures import ThreadPoolExecutor
from veroscript import Backend, ScriptCapabilities, Session, set_language

threading.stack_size(8 * 1024 * 1024)

def run_script():
	set_language("zh-CN")
	output = []
	with Session(backend=Backend.VM, print_callback=output.append,
		max_instructions=100_000, timeout_milliseconds=5_000,
		max_call_depth=256, enable_execution_control=True,
		capability_policy_enabled=True,
		allowed_script_capabilities=ScriptCapabilities.CONSOLE_OUTPUT) as session:
		session.set_memory_limits(16 * 1024 * 1024, 32 * 1024 * 1024)
		loaded = session.load_source('import "core.vs"; void main() { print(42); }')
		if not loaded.success:
			raise RuntimeError(loaded.diagnostics)
		result = session.run()
		if not result.success:
			raise RuntimeError(result.diagnostics)
	return output

with ThreadPoolExecutor(max_workers=1) as worker:
	output = worker.submit(run_script).result()
```

[Session](session.md) · [Localization](packages.md)

ソースのコンパイルは、8 MiB 以上のネイティブスタックを持つワーカーで実行してください。同じワーカーで Session を作成、使用、破棄します。アプリケーションの Python スレッドを開始する前にスタックを設定してください。バインディングは既存の Session やコールバックを別のスレッドへ移動しません。

<a id="mobile-compiler-thread"></a>

このモバイル組込みプロファイルでは、コンパイラに 8 MiB 以上のネイティブスタックを用意します。`threading.stack_size` はアプリの Python ワーカーを作る前に設定し、新しい Python スレッドだけに適用されます。既存のネイティブ／JNI／dispatch スレッドには適用されないため、ホストがワーカーを提供する場合はそこでスタックを設定します。Session の生成・使用・終了は同じワーカーで行い、既存 Session の自動移動はありません。例は結果を待つため UI スレッド外で実行してください。出力は `["42"]` です。ネイティブスタックはバッファや VM 実行のクォータとは独立しています。

<!-- docs-navigation -->

---

[ユーザー文書](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
