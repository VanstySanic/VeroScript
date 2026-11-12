# Python AOT

`veroscript aot --target=python` 會將通過編譯檢查的 VeroScript 程式轉換成可讀的
Python 原始碼。產物包含必要的相容 runtime，執行時不載入 VeroScript VM、
`VeroScriptInterop` 或 `veroscript` 擴充套件。

## 執行邊界

產物需要使用者提供 CPython 3.11–3.15，不會打包 Python 解譯器、Python SDK、
宿主繫結或 VeroScript 原生函式庫。若要在 Python 應用程式中嵌入 runtime，請使用
[Python SDK](index.md)。

產生的 worker 使用 OS 執行緒。腳本全域變數與靜態欄位由每個 worker 的快照初始化；
worker 之間的明確同步能力是 channel。task ID 只屬於建立它的 worker，不能在其他
worker 中 join 或 cancel。

## 單檔輸出

```powershell
veroscript aot --target=python app.vs -o app.py
python app.py
```

應用程式 import 不在入口檔案旁邊時，可以重複指定 `-I`/`--import-path`。只有產生
成功後才會替換輸出檔案。

## 原始碼 bundle

```powershell
veroscript aot --target=python app.vs --output-dir app_python --bundle-name app
python app_python
python -m app_python
```

bundle 包含 `__main__.py`、`__init__.py`、`_veroscript_runtime.py`、
`_veroscript_program.py` 與 `python-aot.manifest.json`。完整 import 圖會先經過檢查；
包含無關檔案的目錄不會被替換。

所有檔案先暫存，再分階段替換一般目錄；最終路徑不可為符號連結或 Windows 目錄聯接。
提交期間會先移開原目錄，輸出路徑可能短暫不存在；提交失敗時嘗試復原原目錄。

## 支援範圍與限制

支援標量、字串、容器、函式、namespace、import、enum、class、struct、interface、
轉換、例外、`switch`、`algorithm`/`numeric`/`random` 以及標準 concurrent task
wrapper。`ref`/`out`、使用者開放 generic、overload 集合、destructor、多重具體基底、
class default、重複 private type 名稱、`goto`/label、`switch` fallthrough、宿主繫結、
source lambda 與區域 `static` 宣告會被拒絕，且不會留下部分輸出。

函式值宣告須明確使用 `fn(...) -> T` 型別，暫不支援函式值的 `auto` 推斷。
支援命名空間限定的函式值。`Super` 接收者與可變參數函式值會被拒絕；可變參數函式的直接呼叫仍然支援。

`shared<T>` / `handle<T>` 包裝型別、可呼叫物件形式的工作入口與執行式反射也會
被拒絕。這些執行階段功能可透過 Python SDK 使用。

關閉時會關閉既有通道，並在同一等待期限內等待 worker 及其衍生的各代工作結束。關閉期間新建的通道初始即為關閉狀態。

產生程式使用 `VERO_JOIN_TIMEOUT_MS` 限制 worker 關閉時的等待時間，預設為
30000 毫秒。worker 呼叫 `exit(n)` 時只會使該工作失敗，不會終止父程序。

若目標是獨立且可檢查的 Python 原始碼產物，請使用 Python AOT；若需要虛擬機器
或 JIT、動態載入、宿主繫結、反射、熱重載、保留呼叫物件或執行時期套件操作，
請使用 [Python SDK](index.md)。Python 前端、BindGen 和轉換器是獨立的原始碼工具鏈。

## 執行階段記憶體支援

獨立 AOT 目前不支援 `std::memory` 的查詢、緩衝區、檢視及相關型別。實際呼叫或使用這些型別時，產生器會回報明確錯誤；僅匯入未使用的 `memory.vs` 或 `core.vs` 仍可產生。

[Runtime SDK](index.md)

獨立 Python AOT 支援 `std::console` 文字輸出、終端能力與尺寸查詢、顏色、游標和行控制。輸出重新導向時返回無終端能力、零尺寸和控制失敗；非法名稱、模式或座標仍會報錯。未知主控台內建函數會被產生器明確拒絕。

Windows 顏色、樣式重設、清屏、清行及游標控制使用原生主控台 API，無需開啟 ANSI 模式。尺寸和座標對應可見視窗；清屏不會刪除視窗之外的歷史內容。

## 屬性原始碼匯出

屬性設定比較是受支援的唯讀 builtin 特例：`has_same_configuration(ref other)` 不建立可寫入別名。`try_get()` 可用，`try_get(ref output)` 仍不支援；各操作界線見 attribute 參考。

[std::attribute](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/attribute.md)

<a id="native-bundle-api"></a>

原生 SDK 包含 `VeroScriptPythonAot.h`；`FPythonAotSourceBundle` 提供 `Files`、`ManifestJson`，透過 `EmitPythonSourceBundleFromSource`／`EmitPythonSourceBundleFromFile` 產生，再由 `WritePythonSourceBundle` 暫存替換目錄。`BuildPythonSourceBundle` 可打包已產生的 Python。Python 控制包裝的 `veroscript_executable` 選擇 CLI 執行檔或 SDK 根／bin 目錄；未指定時依序查找 `VEROSCRIPT_EXECUTABLE`、`VEROSCRIPT_SDK` 和 PATH。這個外部 CLI 包裝與產生的獨立程式是不同層次。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](aot.md)
