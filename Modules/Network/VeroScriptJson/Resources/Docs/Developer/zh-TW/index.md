# JSON 維護

資料層負責 DOM、文字與串流。Schema 使用記錄修補的 jsoncons 1.10.0 及獨立 QuickJS-ng 0.17.0 正規表示式/Unicode 庫，不含 JavaScript 執行期。維持精確數值配接。C++ 對映不依 Core；腳本對映歸模組處理器。簡單結構體模組使用縮減支援，含匯入、泛型、繼承、註解、可執行成員或複雜預設值時使用完整支援。維持宣告順序、所屬範圍和封閉型別身分。

使用模組 CMake 入口。根整合由 VEROSCRIPT_BUILD_JSON 控制，靜態登記/動態擴充由 VEROSCRIPT_JSON_BUILD_BINDINGS 控制。匹配的已安裝 Core SDK 可獨立建置繫結而不重編 Core。原生目標為 VeroScript::Json，COMPONENTS Bindings 提供 VeroScript::JsonBindings；Bindings 安裝元件須配合資料層和授權。動態模組位於 lib/veroscript/modules。登記須使用實際實作身分，不可只看產品版本。

請參閱[網路基礎: 原始碼職責 — 原始碼建置與原生模組註冊](../../../../../VeroScriptNetwork/Resources/Docs/Developer/zh-TW/index.md#network-execution-assembly)。

允許執行時，分別驗證原生資料、靜態/動態登記、原始碼模組與二進位庫使用。VEROSCRIPT_JSON_SCHEMA_CORPUS 選鎖定 Schema suite/格式語料，未知 format 在註解模式執行；VEROSCRIPT_JSON_NORMALIZATION_CORPUS 選 Unicode 17 NormalizationTest.txt，VEROSCRIPT_JSON_CORPUS 選 JSONTestSuite。資料只透過 Resources/Tools/Tests 明確工具取得，正常建置不下載。記錄實際語料/原始碼/工具鏈身分與結果，不以舊數量認證現行程式、其他宿主、JIT/AOT、行動或瀏覽器。

維護六語言 json.json、穩定 ID/參數及 share/VeroScriptJson/Resources/Localization 部署。json profile 取代目標舊項，不可連續疊加。Unicode 表由 Resources/Tools/Unicode/generate_schema_unicode.py 從固定輸入產生，不把 Python 帶入產品執行期。保留原始聲明與來源清單。

```sh
cmake -S Modules/Network/VeroScriptJson -B build/json -DCMAKE_BUILD_TYPE=Release
cmake --build build/json --config Release
ctest --test-dir build/json -C Release --output-on-failure
cmake --install build/json --config Release --prefix /absolute/sdk
```

[User](../../User/zh-TW/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/zh-TW/GENERAL.md)
