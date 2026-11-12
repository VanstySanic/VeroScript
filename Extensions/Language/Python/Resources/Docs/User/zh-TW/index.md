# Python SDK

獨立 Python 產生、原始碼 bundle 與限制請參閱 [Python AOT](aot.md)。

`VeroScriptPython` 是面向 CPython 的 VeroScript Interop ABI 配接器。Python 應用程式可以用它載入和執行腳本、呼叫函式、註冊型別化回呼與類別、讀取中繼資料，並處理套件和值。

SDK 支援 VM、mixed 和 JIT 後端、結構化診斷、型別化純量與容器、宿主函式與類別、原始碼覆蓋、熱重載、JIT 策略與統計、反射、套件操作、保留呼叫物件、外部序列化、變更事件與在地化。

## 安裝與執行時期邊界

```text
python -m pip install veroscript
```

擴充要求原生 Interop ABI 完全相符：目前為 `0x00000101`（0.1.1）。不相符時，匯入 `veroscript` 會拋出 `ImportError`。請成套使用 wheel 隨附的原生函式庫，不要替換為其他發行版本的 Interop 函式庫。成功匯入後，`veroscript.ABI_VERSION` 表示 ABI，`veroscript.RUNTIME_VERSION` 表示執行時期版本。

Wheel 包含 Python 擴充、相符的 Interop 函式庫、執行時期目錄、型別資訊與標準庫檔案。原生擴充與 CPython 小版本及處理器架構相關；`py.typed` 與 `_veroscript.pyi` 也會提供給靜態型別檢查器。

Python 回呼會重新取得 GIL；回呼例外會轉換成 `CallbackError`，不會穿越 C ABI 展開。

發行 wheel 支援啟用 GIL 的標準 CPython 3.11–3.15，不支援偵錯版或自由執行緒直譯器。請選擇與作業系統、處理程序架構及 CPython 次版本相符的 wheel。Linux wheel 的相容性受其原生建置環境限制，不能假設其他 Linux 發行版上的 wheel 一定適用於目前系統。

Python SDK 壓縮檔或安裝程式中的原生模組只符合該套件採用的 CPython 次版本。使用其他受支援次版本時，請在對應直譯器的環境中安裝相符的 wheel。

## 最小執行

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

## 文件索引

- [工作階段與執行](session.md)：設定、載入、呼叫、結果、後端、JIT、熱重載與執行緒規則。
- [值與容器](values.md)：純量、型別化容器、通道、所有權與保留呼叫物件。
- [宿主繫結](host-bindings.md)：型別化函式、裝飾器、Python 類別、回呼與變更處理。
- [套件、反射與在地化](packages.md)：套件、中繼資料、反射、序列化與語言目錄。
- [Python 前端](frontend.md)：靜態分析型別化 Python 並產生宣告 AST。
- [Python BindGen](bindgen.md)：產生 Binding Catalog 與專案構件。
- [Python 轉換器](transpiler.md)：將型別化 Python 轉換為經驗證的 VeroScript。

語言參考仍是 VeroScript 語法與語意的權威來源；本模組只說明 Python 邊界。

[Android/iOS 中使用 Python](mobile.md)

## 受管理記憶體

宿主介面：`session.memory` / `current_memory()` / `set_memory_limits`。所有權、配額、檢視失效與租借規則請參閱 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)。

- [宿主中的受管理記憶體](memory.md)

## variant

請參閱[Interop SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#exported-value-lifetime)。

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

使用 `Session.load_application(absolute_path, options=...)` 載入應用程式，確認結果後再呼叫 `run()`。主程式選項中的路徑必須為絕對路徑。環境、簽章及信任設定遵循 [Application 載入約定](../../../../../../../VeroScriptApplication/Resources/Docs/User/zh-TW/index.md)。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
