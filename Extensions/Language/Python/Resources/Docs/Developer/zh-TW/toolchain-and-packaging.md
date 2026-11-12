# Python 工具鏈與包契約

## 嚴格的靜態工具契約

目前端發現類別中重複宣告欄位、列舉值、屬性或方法時，會回報 `PYF0003`；僅允許一個與既有屬性相符的 setter。Transpiler 僅允許將 `None` 用作可呼叫項回傳型別，要求每個建構函式宣告 `-> None`，執行個體接收者必須是無預設值的位置參數 `self`，且方法 decorator 只允許 `@staticmethod`。

在執行時繫結前，BindGen 還會驗證完整 Binding Catalog 與實際 Python 可呼叫項。它會拒絕重複建構函式或可見成員名稱、欄位/getter/setter signature 不相符，以及必要位置參數 shape 與目錄不完全一致的建構函式、函式、方法和屬性存取子。這些失敗不會部分註冊繫結，也不會取代已產生的成品。

## 原生擴充套件

`VeroScriptPython` 是 CPython 擴充套件，不是嵌入式 Python 發行版。CMake 要求 Python 3.11 至 3.15 的 CPython 直譯器和 `Development.Module`；拒絕其他 Python 實現，不能悄然生成不相容擴充套件。

擴充套件連結 `VeroScriptInterop`，使用平臺 CPython ABI 字尾，並將 Interop 庫放在可匯入包旁。構建還在包旁部署匹配的 `localization/` 和 `stdlib/` 樹，使測試和本地匯入不依賴全域 PATH 或庫路徑改變。

## 工具與元件

前端、BindGen、Transpiler 和 Python AOT 包作為 Python 工具元件安裝。其 CMake 目標使用選定直譯器測試，並安裝相對 site-package 路徑以使暫存字首可重定位。執行時 wheel 包含原生擴充套件、Interop 庫、Python 源、型別資訊、標準庫和 Catalog 資源。

## 釋出驗證

分發驗證檢查可匯入性、原生庫位置、版本一致性、包內容、wheel/原始碼分發行為和受支援直譯器/平臺矩陣。只能在倉庫原始碼樹中工作的包不是有效釋出產物。

## Wheel 矩陣出版

Python 產品完整出版從 `Extensions/Language/Python/pyproject.toml` 推導直譯器矩陣；`requires-python` 與 `cibuildwheel` 選擇必須一致，目前涵蓋 CPython 3.11–3.15。請預先準備與目標作業系統和架構相符的五個原生直譯器；自動探索使用帶版本號的命令及 Windows `py` 啟動器。完整出版可透過 Windows 的 `-PythonExecutables` 或 Unix 上重複的 `--python-executable` 參數指定路徑，也可設定 `VEROSCRIPT_PYTHON_EXECUTABLES` 清單，Windows 以 `;` 分隔、Unix 以 `:` 分隔。缺少版本、偵錯建置、自由執行緒直譯器或架構不符都會在預先檢查階段失敗；不會自動安裝直譯器。

以下命令從儲存庫根目錄檢查 Windows x64 直譯器集合，不進行建置。請選擇與原生宿主相符的目標；獨立入口使用重複的 `--python` 參數指定直譯器路徑。

```text
python Resources/Tools/Packaging/build-veroscript-python-wheels.py --target win-x64 --check-interpreters
```

管線建立一份原始碼發行套件，並從該套件在獨立的原始碼及建置目錄中建置每個 wheel。每個 wheel 都在對應直譯器的全新環境中安裝，並執行 `Extensions/Language/Python/Resources/Tools/Packaging/test-wheel.py` 選取的全部已安裝套件測試及 `pip check`。完整出版要求 wheel ABI 集合齊全、原始碼發行套件存在、標籤與原生識別資訊一致，並包含授權文件。產品清單的 `pythonWheelVersions` 記錄矩陣，離開原始碼儲存庫也可稽核；部分元件出版維持部分狀態。選擇 Python 時，`-SkipBuild` 或 `--skip-build` 仍會建置並驗證此矩陣。

Python sdist 僅存一份，位於 `Release/Extensions/Language/Python/any/<version>/packages/`；wheel 按真實平台／ABI 標籤選取，僅在標籤允許時共用清單。目標索引共同參照 sdist 與 wheel 清單。即使檢出目錄乾淨，也要檢查實際 sdist 成員沒有 `.cxx`、`.gradle` 或 `.externalNativeBuild` 快取：`pyproject.toml` 已明確排除，原始碼發行校驗也會拒絕它們。保留獨立解壓縮後建置所需的原始碼、標頭、建置腳本、中繼資料和法律聲明。

SDK／安裝程式中的原生模組保留主建置的單一 CPython ABI。Linux wheel 保留實際原生標籤；矩陣本身不代表已符合 `manylinux` 或 `musllinux` 相容性。

## 本地化維護

各 Python 工具維護自身的 `Resources/Localization/<culture>/` 目錄，只有多個工具共用的訊息歸入 Python 區域。部署時合併目錄，不能重複定義 `python_tools.*` ID。檔案必須是 UTF-8 JSON 物件，ID 唯一，值為非空白字串。損壞的語言目錄會被略過，並嘗試下一符合的候選。

英文目錄定義可用訊息 ID 和位置參數契約。翻譯可以調整 `{0}`、`{1}` 等編號參數的順序，但必須保留相同的參數索引集合；不支援具名欄位、屬性存取或索引存取。字面大括號使用 `{{` 和 `}}` 逸出。範本缺漏或無效時回退到實際載入的英文目錄，格式化失敗時再使用內建英文。出版前須通過 Frontend 本地化回歸測試及六語言部署驗證。

格式錯誤的 JSON、重複訊息 ID、非字串或空白值、無效 UTF-8 會使對應語言目錄被拒絕；僅翻譯中存在的 ID 會被忽略。

<a id="python-package"></a>

Python 工具組維護 `Extensions/Language/Python/pyproject.toml` 中的 PEP 517 專案。從儲存庫根目錄執行下列命令可建置並審查本地 wheel／原始碼發行候選；從原始碼安裝使用 `python -m pip install ./Extensions/Language/Python`。原始碼發行包含所需原生輸入，但仍需要所選 CPython 的開發檔案和原生工具鏈。`VEROSCRIPT_PYTHON_INSTALL_DIR` 使用相對安裝目標以保持 CMake 暫存可遷移。建置設定中的完整 wheel 矩陣時，從儲存庫根執行 `cibuildwheel`；建置與檢查都不會上傳發行物。版本中繼資料取自 `VeroScriptVersion.h`，須與載入的執行階段一致。

```sh
python -m pip install build
python -m build Extensions/Language/Python --outdir .tmp/python-dist
python Extensions/Language/Python/VeroScriptPython/Resources/Tools/Packaging/validate_distribution.py .tmp/python-dist --expected-version 0.1.1
```

## 發行中繼資料核驗

wheel 及原始碼發行套件的 `Requires-Python` 上下界須符合完整宣告矩陣，目前為 `>=3.11,<3.16`。部分發佈亦會驗證每份已提供的產物。Windows 擴充模組副檔名須符合 `win32`、`win_amd64` 或 `win_arm64` 及所選 CPython ABI。macOS wheel 的最低版本標籤須滿足擴充模組及隨附 Interop 程式庫各自要求的最低 OS 版本。跨目標組包保留已設定的編譯器、工具鏈、產生器平台及部署設定，並在編譯擴充模組前核對目標 CPython 標頭。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](toolchain-and-packaging.md)
