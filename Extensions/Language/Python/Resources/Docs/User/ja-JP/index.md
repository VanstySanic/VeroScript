# Python SDK

スタンドアロン Python 生成と制限については [Python AOT](aot.md) も参照してください。

`VeroScriptPython` は VeroScript Interop ABI の CPython アダプターです。スクリプトのロードと実行、関数呼び出し、型付きコールバックとクラスの公開、メタデータの検査、パッケージと値の操作を行えます。

VM、mixed、JIT バックエンド、構造化診断、型付きコンテナー、ホストバインディング、ソースオーバーレイ、ホットリロード、JIT 統計、リフレクション、シリアライズ、ミューテーションイベント、ローカライズに対応します。

## インストール

```text
python -m pip install veroscript
```

拡張にはネイティブ Interop ABI の完全一致が必要です。現在の値は `0x00000101` (0.1.1) で、不一致の場合は `veroscript` のインポート時に `ImportError` が発生します。Wheel 内のネイティブライブラリは一組として使用し、別リリースの Interop ライブラリに置き換えないでください。インポート成功後は `veroscript.ABI_VERSION` で ABI、`veroscript.RUNTIME_VERSION` でランタイムのバージョンを確認できます。

Wheel には Python 拡張、対応する Interop ライブラリ、カタログ、型情報、標準ライブラリが含まれます。ネイティブ拡張は CPython のマイナーバージョンとアーキテクチャに依存します。`py.typed` と `_veroscript.pyi` も含まれます。Python コールバックの例外は `CallbackError` に変換され、C ABI を越えて unwind しません。

公開 Wheel は GIL を有効にした標準 CPython 3.11–3.15 に対応します。デバッグ版およびフリースレッド版のインタープリターは対象外です。OS、プロセスのアーキテクチャ、CPython のマイナーバージョンが一致する Wheel を選んでください。Linux Wheel の互換性はビルド元の環境に依存するため、別の Linux ディストリビューションで作られた Wheel が使えるとは限りません。

Python SDK のアーカイブやインストーラーに含まれるネイティブモジュールは、そのパッケージに使用された CPython のマイナーバージョンにのみ対応します。別の対応バージョンを使う場合は、そのインタープリターの環境に一致する Wheel をインストールしてください。

## 最小実行

```Python
from veroscript import Backend, Session

source = """
int main(string[] args)
{
	print("arguments={}", len(args));
	return 0;
}
"""

with Session(backend=Backend.MIXED, script_args=("one", "two")) as session:
	result = session.load_source(source, "main.vs")
	if not result.success:
		raise RuntimeError(result.diagnostics)
	result = session.run()
```

## リファレンス

- [セッションと実行](session.md)：設定、呼び出し、結果、JIT、リロード、スレッド。
- [値とコンテナー](values.md)：スカラー、所有権、コンテナー、保持 callable。
- [ホストバインディング](host-bindings.md)：関数、デコレーター、クラス、コールバック。
- [パッケージ、リフレクション、ローカライズ](packages.md)。
- [Python フロントエンド](frontend.md)：型付き Python の静的 AST。
- [Python BindGen](bindgen.md)：Binding Catalog とプロジェクト成果物。
- [Python トランスパイラー](transpiler.md)：型付き Python から VeroScript への変換。

VeroScript の構文と意味は言語リファレンスを参照してください。このモジュールは Python 境界だけを説明します。

[Android/iOS の Python](mobile.md)

## 管理メモリ

ホスト API：`session.memory` / `current_memory()` / `set_memory_limits`。所有権、上限、ビューの失効、リースについては [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md) を参照してください。

- [ホストから使う管理メモリ](memory.md)

## variant

[相互運用 SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#exported-value-lifetime) を参照してください。

```python
from veroscript import Session, Value
with Session() as session:
	payload = Value(42, kind="int32")
	box = session.variant("int", payload)
	assert box.variant_has_value()
	assert box.variant_type_name() == "int"
	extracted = box.variant_get()
	assert extracted.value == 42
```

`Session.load_application(absolute_path, options=...)` でアプリケーションを読み込み、結果を確認してから `run()` を呼び出します。ホストオプション内のパスは絶対パスにしてください。環境、署名、信頼の設定は [Application の読み込み規約](../../../../../../../VeroScriptApplication/Resources/Docs/User/ja-JP/index.md)に従ってください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
