# Python — Mobile SDK

請將標頭子目錄實體化：`includeDirectory` 內的目錄符號連結及 Windows junction／reparse 目錄會被拒絕。`includeDirectory` 本身可解析到 SDK 內的實體目錄；一般 `.h` 檔案連結可保留，但目標必須位於 SDK 內，其內容會納入 SDK 指紋。

本頁面向使用 `Extensions/Language/Python/Resources/Tools/Mobile` 建置與發佈 Python 行動套件的維護者。必須明確提供目標 CPython SDK；主機直譯器與桌面／macOS 二進位檔不能替代目標資源。在新目錄建置，驗證完整檔案清單，並收集目標平台驗收證據後再發佈穩定版本。

Python：宿主 CPython 與目標 major/minor 相同，使用 CMake 3.25+、Android NDK/Ninja 或 macOS/完整 Xcode。目標為帶 GIL 的 CPython 3.13/3.14 release，每個 minor 獨立打包，所有 slice 的完整版本相同；拒絕 debug/free-threaded。SDK 描述檔的相對路徑必須留在自身目錄，欄位為 `schemaVersion`、`platform`、`architecture`、`variant`、`minimumVersion`、`includeDirectory`、`library`、`sysconfigData`。檢查器讀取目標標頭，以 AST 讀取字面量 `build_time_vars`，不執行目標解譯器；檢查平台、架構、最低 OS、Python ABI 及 iOS install name。Android 必須有 arm64-v8a/x86_64（API 26），iOS 必須有 arm64 真機與 arm64/x86_64 模擬器（15.0）。

Python 套件提供擴充功能、facade 和資源，應用程式提供 CPython 及相依項目。Android 使用提取後的 JNI 路徑，iOS 使用簽署的動態 Python 擴充 framework 和 `.fwork`/`.origin`；這與靜態 C ABI XCFramework、被停用的原生 JIT 分別處理。每個 slice 產生新的 build nonce／native identity，`python-mobile.json` 記錄全部檔案摘要、版本、平台／ABI、SDK 指紋及待驗收狀態。

發布前審核完整 inventory 與二進位檔。`device_smoke.py` 拒絕桌面執行，每次重試先使舊證據失效；證據須符合 build ID、準確 Python／產品版本、ABI、架構和真機／模擬器。所有 slice 都須有證據，Android arm64 還須涵蓋 4 KB 和 16 KB 頁環境。受治理的 VM smoke 檢查匯入、回呼、診斷、保留值及釋放。最終簽署應用、相依項目／APK ZIP 對齊、iOS runpath／資源／簽署、取消與應用測試須另行驗收；宿主測試不能替代這些步驟。

[Python](index.md)


## 出版文件

行動套件僅收錄功能檔案、執行資源與授權聲明，不攜帶 README、手冊或範例；集成 User 文件由獨立 Resources 發行物提供。原生資源路徑與裝置驗收要求維持不變。


## 描述檔與重現

以下為 Android arm64 與 iOS 實機描述範例。sysconfigData 名稱僅供示範，必須選擇 SDK 中的實際檔案，不可把其他 ABI 的檔案改名使用。Android x86_64 需修改 `architecture` 與所有對應路徑；iOS 模擬器使用 `variant: simulator`、`architecture: arm64` 或 `x86_64` 及單架構二進位。各切片完整版本必須一致，保留 `@rpath/Python.framework/Python`。

從儲存庫根目錄執行。`--host-python` 選擇宿主直譯器，`--cmake` 選擇建置驅動；`--plan` 驗證 SDK 並輸出命令，不執行建置，Android 規劃仍需 `--ndk`。輸出目錄必須是新目錄，中間建置在暫存目錄進行，每次建置使用新 nonce。除隨套件聲明外，還應保留 CPython 與依存項的授權。

宿主測試 `Extensions/Language/Python/VeroScriptPython/tests/test_mobile.py` 驗證 AST、宿主/目標設定、通用模擬器框架與證據。實機冒煙測試使用受控 VM/Mixed（100,000 指令、5 秒、256 層呼叫、僅允許主控台輸出），拒絕桌面執行，`-O` 下仍有效，並核對原生身分、ABI 與版本。CPython、應用程式測試及最終簽署應用程式審查仍需獨立完成。

```json
{
	"schemaVersion": 1,
	"platform": "android",
	"architecture": "arm64-v8a",
	"variant": "device",
	"minimumVersion": "26",
	"includeDirectory": "include/python3.14",
	"library": "lib/libpython3.14.so",
	"sysconfigData": "lib/python3.14/_sysconfigdata__android_aarch64-linux-android.py"
}
```

```json
{
	"schemaVersion": 1,
	"platform": "ios",
	"architecture": "arm64",
	"variant": "device",
	"minimumVersion": "15.0",
	"includeDirectory": "Python.framework/Headers",
	"library": "Python.framework/Python",
	"sysconfigData": "stdlib/_sysconfigdata__ios_arm64-apple-ios.py"
}
```

[Python](../../User/zh-TW/mobile.md)

## 從描述檔建置

工具實作不等於已完成交叉編譯或裝置驗證。必須明確提供目標 CPython SDK，主控端直譯器與目標 major/minor 相同，目標使用帶 GIL 的 release 建置。Frontend/BindGen/Transpiler/AOT 僅在主控端執行。擴充靜態連結 Core、C++ 支援及 Interop；VM/Mixed 不依賴原生 JIT。

Android 需要 NDK/Ninja 及兩個 ABI 的 SDK，檢查 libpython、擴充的 LOAD/RELRO 16 KB 對齊與相容的 `libc++_shared.so`。iOS 需要 macOS 與完整 Xcode。通用模擬器 Python.framework 須透過 `xcrun lipo -thin` 等方式拆成描述檔需要的 arm64/x86_64 單架構輸入，保留 `@rpath/Python.framework/Python`。

在存放庫根目錄執行，封裝命令依目標平台**選擇一個**；角括號路徑替換為實際 SDK 或新的輸出位置。檢查器不會執行目標 Python。

```text
python Extensions/Language/Python/Resources/Tools/Mobile/mobile_sdk.py --sdk <sdk.json> --check-host
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py android --sdk <android-arm64/sdk.json> --sdk <android-x86_64/sdk.json> --ndk <ndk-directory> --out-root <new-output-directory>
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py ios --sdk <ios-device/sdk.json> --sdk <ios-simulator-arm64/sdk.json> --sdk <ios-simulator-x86_64/sdk.json> --out-root <new-output-directory>
```

## Android 部署

將各 `slices/<target>/jniLibs/<abi>/` 複製至應用程式的 `src/main/jniLibs/<abi>/`，並依 CPython 提供方要求部署目標 libpython 及相依項。整個應用程式使用一個相容的 `libc++_shared.so`，不能以 `pickFirst` 掩蓋不相容。將對應切片的 `python/veroscript/` 放入應用程式 Python 資源；若原為 APK assets，先解出至實際目錄。保留 `stdlib/`、`localization/`、`py.typed`。

載入器需要已解出的實際檔案路徑。設定應用程式 Gradle 模組，並在首次 import 前設定直譯器：

```kotlin
android {
    defaultConfig { minSdk = 26 }
    packaging { jniLibs { useLegacyPackaging = true } }
}
```

```python
import os
import sys

os.environ["VEROSCRIPT_ANDROID_NATIVE_LIB_DIR"] = native_library_dir
sys.path.insert(0, application_python_directory)
import veroscript
```

`native_library_dir` 來自已安裝應用程式的絕對 `context.applicationInfo.nativeLibraryDir`，須確認程式庫檔案存在。`application_python_directory` 是包含 `veroscript/` 的部署目錄。載入器以 `veroscript._veroscript` 名稱載入 `libveroscript_python.so`。請透過安裝後 APK 驗證，不能以建置機器路徑代替。

## iOS 部署

將外部 **Python.xcframework** 與產出的 **VeroScriptPython.xcframework** 以 **Embed & Sign** 加入應用程式。Xcode 選擇真機或通用模擬器架構；未簽署擴充由應用程式嵌入階段簽署。runpath 包含 `@executable_path/Frameworks`，保留以下配置：

```text
Application.app/
  Frameworks/Python.framework/Python
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript.origin
  python/veroscript/__init__.py
  python/veroscript/_veroscript.fwork
  python/veroscript/stdlib/...
  python/veroscript/localization/...
```

另行設定 CPython 及其標準程式庫，將 `<app-bundle>/python` 加入模組搜尋路徑，關閉唯讀 Bundle 的位元組碼寫入。`AppleFrameworkLoader` 讀取 `_veroscript.fwork`，兩個模擬器架構共用 marker 與 `.origin`。未調整 origin 時不能移至 `app_packages`；不要將 `.so` 放入資源目錄，也不要再次轉換已準備的 marker。

## 稽核與平台證據

稽核將完整清單、facade、標準程式庫及所有語言目錄與對應版本原始碼比較；檢查 CPython 初始化符號、相依項、Android ABI/API/16 KB 對齊、iOS 平台/最低系統/framework/Info.plist/marker 及 XCFramework 成員。額外原生酬載即使已加入雜湊清單也會被拒絕。應在對應 checkout 或 sdist 的 `_native` 根目錄執行，不能僅以套件內雜湊證明資源齊全。

先執行一般稽核。將 `device_smoke.py` 複製至應用程式測試架構，在實際行動直譯器的工作執行緒向 `run` 傳入可寫報告路徑。匯出所有切片與 Android arm64 兩種頁面環境的報告，依實際數量重複 `--evidence`。

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory>
```

```python
from device_smoke import run
run(writable_report_path)
```

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory> --require-device-tests --evidence <report-1.json> --evidence <report-2.json> --evidence <report-3.json>
```

新建置需要新證據。識別使用 `veroscript.HOST_PLATFORM`，產品語意版本使用 `veroscript.__version__`，不能以核心機型名稱或帶位元組碼資訊的 `RUNTIME_VERSION` 替代。`python-mobile.json` 中的 `pending` 不代表已通過驗收。Android 發行前檢查全部原生相依項，執行 `zipalign -v -c -P 16 4 <application.apk>`，並測試經 AAB 發行流程產生的簽署應用程式。iOS 檢查簽署、資源、runpath 及真機和兩個模擬器 ABI 的匯入。保留 CPython/相依項授權，另行完成 CPython、應用程式測試與應用程式商店政策確認。

## 建置身分與 Android 程式庫路徑

產品版本來自 `VeroScriptVersion.h`，不能用 CPython 版本或拼接後的執行時期字串替代。Android 應確認原生程式庫已擷取至 `ApplicationInfo.nativeLibraryDir`，再於首次 `import veroscript` 前將其絕對路徑傳給 `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR`。

<a id="android-build-contract"></a>

Android 建置明確啟用 `VEROSCRIPT_BUILD_APPLICATION=ON`，保留 `python/veroscript/` 中的 `licenses/application/miniz/LICENSE`、`licenses/application/json/LICENSE.MIT`、`licenses/application/sha256/LICENSE`。工具鏈驗證優先比較已解析的 NDK `ANDROID_ABI`／`ANDROID_PLATFORM_LEVEL`，否則使用 CMake Android／系統欄位；舊 NDK 的 `CMAKE_SYSTEM_VERSION=1` 不是目標 API。16 KB 檢查驗證 LOAD 對齊，以及按頁擴展的 RELRO 保護不覆蓋 RELRO 外可寫資料；不要求每個 RELRO 末端均 16 KB 對齊，支援的 NDK 共用 STL 可以帶未對齊的 RELRO 尾端。

<a id="compiler-nesting-acceptance"></a>

## 原生編譯堆疊

嵌入宿主必須在原生堆疊至少為 8 MiB 的工作執行緒執行編譯。Android JNI 與 Apple dispatch 的小型堆疊可能在剖析器回傳巢狀上限診斷前溢位。Session 必須在同一工作執行緒建立、使用和銷毀，不要隱式遷移既有 Session 或回呼。請在建立應用程式 Python 工作執行緒前設定堆疊，或由原生宿主提供。device_smoke.run 也須在此執行緒執行；既有四組驗收涵蓋 200 層正常執行、512 層上限診斷及後續重用。新證據需含 compilerNestingChecked=true，缺少此檢查的舊證據不被接受。

`compilerNestingChecked` 是驗收中繼資料，不是公開 Session API。`verify_mobile.py::verify_package` 在 schemaVersion 1 及匹配的建置／執行階段／ABI／slice／產物識別之外，要求該欄位嚴格為布林 true；run 僅在 exercise_runtime 成功後記錄它。Python Session／C ABI 編譯不經過 Mobile::FRunner 的堆疊適配。下方範例的 report_file 須為應用程式擁有的可寫目錄內的絕對路徑；該監督函式會等待結果，應在非 UI 執行緒呼叫。模擬回歸只驗證邏輯，不驗收目標二進位檔。

```python
import threading
from concurrent.futures import ThreadPoolExecutor
import device_smoke

def validate_on_worker(report_file):
	threading.stack_size(8 * 1024 * 1024)
	with ThreadPoolExecutor(max_workers=1) as worker:
		return worker.submit(device_smoke.run, report_file).result()
```

<!-- docs-navigation -->

---

[開發者文件](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](mobile.md)
