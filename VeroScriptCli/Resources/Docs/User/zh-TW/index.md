# 命令列工具

命令列程式名稱為 `veroscript`。它可以執行腳本、只做編譯檢查、執行原始碼字串、進入互動模式、啟動 DAP 偵錯器、建置套件、校驗綁定目錄和產生 AOT 原始檔。

## 初始化專案

```sh
veroscript project init
veroscript project init --path "./MyProject"
veroscript build --path "./MyProject"
```

初始化會補齊 `veroscript-project.json` 與 `veroscript-build.json`，版本為 `0.1.1`，
相依為空，預設可攜式應用程式目標輸出 `app.vsapp`。建立預設建置設定時，若缺少入口，
也會產生最小 `main.vs`。既有一般檔案顯示為 `Kept`，不會覆寫；既有建置設定保留自己的入口，
不會額外產生 `main.vs`。檔案目標被目錄或符號連結佔用時會傳回錯誤。重複執行可補齊缺少的設定。
`project --help` 與 `project init --help` 只顯示說明，不寫入檔案。

省略 `--path` 時使用目前工作目錄。相對路徑以命令啟動目錄為基準，也支援絕對路徑及
`--path=<目錄>`；不存在的目錄會建立。目錄名稱會正規化為有效的小寫 ASCII 專案名稱，必要時
使用 `my-project`，目錄本身不會重新命名。初始化只在本機產生檔案，不安裝相依或執行指令碼。
`veroscript.lock` 於成功安裝相依後產生，`.veroscript/` 由套件管理作業按需建立。
執行建置需使用包含建置服務的 CLI。參閱[最小專案範例](../../../Examples/Project/README.md)。

## 命令形式

```powershell
# Run a source script or application package
veroscript [options] hello.vs [args...]
veroscript [options] DemoApp.vsapp [args...]

# Check without executing
veroscript --check hello.vs

# Execute source text
veroscript -e 'print(1 + 2);'

# Enter the REPL
veroscript -i

# Generate C++17 AOT source (the default target)
veroscript aot hello.vs -o generated.cpp

# Generate standalone Python source
veroscript aot --target=python hello.vs -o generated.py

# Generate a deterministic C# source bundle
veroscript aot --target=csharp hello.vs --output-dir generated --bundle-name App

# Validate or format a binding catalog
veroscript bindings validate catalog.json
veroscript bindings format catalog.json -o formatted.json

# Inspect a package
veroscript package inspect DemoApp.vsapp
```

帶有 `string[] args` 的 `main` 會接收腳本路徑之後的參數；腳本路徑本身不包含在 `args` 中。無參數 `main` 會忽略額外參數。 `int main` 的回傳值成為處理程序退出碼，`void main` 正常完成時回傳成功碼。

CLI 的處理程序退出碼直接反映腳本的最終結果碼：`int main` 的傳回值會被傳回，執行 `exit(n)` 時 `n` 優先，`void main` 或執行到 `int main` 末端時為 `0`。這與嵌入 API 的 `FRunResult` 語意不同；嵌入宿主應把 `bSuccess` 作為「引擎是否正常完成」的判斷，再單獨讀取 `ExitCode`。

## 常用選項

| 選項 | 作用 |
| --- | --- |
| `-e <source>` / `--eval <source>` | 執行原始碼字串；不能與 `-i`、`--check` 或監視模式同時使用。 |
| `-i` / `--interactive` | 進入 REPL。 |
| `--repl-strict` | 要求與 `-i` 一起使用；當最後一條輸入失敗時以非零碼退出。 |
| `-I <path>` / `--import-path <path>` | 追加模組搜尋路徑；可重複使用。 |
| `--stdlib-path <path>` | 新增標準庫搜尋根目錄。 |
| `--implicit-import <file>` | 為每個編譯來源隱式匯入指定檔案；可重複使用。它不修改原始檔案，也不會自動重新匯出宣告。 |
| `--no-circular-imports` | 拒絕循環模組依賴；預設允許循環相依性。 |
| `--allow-circular-imports` | 明確復原允許循環模組依賴的預設行為。 |
| `--allow-project-wide-access` | 允許在專案範圍記憶體取非匯出宣告；應謹慎使用。 |
| `--binding-catalog <file>` | 載入宿主綁定目錄；可以重複使用和合併。普通腳本執行或檢查可用，不能與 `-e`、`-i` 或監視模式組合。 |
| `-D name[=value]` / `--define name[=value]` | 定義物件巨集或條件編譯符號。 |
| `--macro NAME(params)=body` | 定義結構化宿主風格函式巨集；`--predefined-macro` 是相容別名。 |
| `--check` | 編譯並報告診斷，不執行腳本；不能與 `-e`、`-i` 或監視模式組合。 |
| `--lang <tag>` | 選擇診斷語言；標籤必須能解析到目前載入目錄中的文化目錄。預設資源由發行套件隨附的文化目錄決定；自訂目錄可以提供更多語言。 |
| `-o <path>` / `--output <path>` | AOT 或綁定目錄格式化的輸出路徑。 |
| `--output-dir <path>` | C# 或 Python AOT 多檔案 bundle 目錄。 |
| `--bundle-name <name>` | 原始碼 bundle 名稱；省略時使用輸入檔名。 |
| `--manifest <path>` | C# bundle manifest 路徑；省略時使用輸出目錄。 |
| `--target <name>` | AOT 目標：`cpp`（預設）、`csharp` 或 `python`。 |
| `-h` / `--help` / `-?` | 顯示幫助。 |
| `--version` | 顯示版本、平臺和 JIT 能力資訊。 |

## 執行後端和快取

| 選項 | 作用 |
| --- | --- |
| `--backend=vm` 或 `--backend vm` | 只使用位元組碼虛擬機器。 |
| `--backend=mixed` 或 `--backend mixed` | 支援時使用 JIT，否則回退到 VM；這是預設後端。 |
| `--backend=jit` 或 `--backend jit` | 需要相容的 JIT 模組，可明確指定或由模組環境自動選擇；積極編譯符合條件的程式碼，不支援的函式在進入前退回 VM。 |
| `--bc-cache=off` / `--bc-cache=on` | 關閉或啟用 `.vsbc` 位元組碼快取；預設為啟用。 |
| `--bc-cache-dir <path>` | 指定共享位元組碼快取目錄；未指定時使用平臺使用者快取目錄。 |
| `--vm-backend <id\|file\|directory>` / `--vm-backend=<id\|file\|directory>` | 選擇直譯器：隨附 `vs-bytecode`、相容程式庫檔案／目錄，或啟用套件支援時的已安裝後端 ID；與執行原則獨立。 |

完整 Runtime 在 `Vm/` 提供相符直譯器，部署 CLI 時須保留此目錄。`--vm-backend=vs-bytecode` 選擇隨附版本。其他單純 ID 僅在啟用套件支援時，從明確選定的已安裝環境解析；安裝本身不會選取。檔案／目錄路徑相對啟動目錄解析，須與 Runtime 相容。`--vm-backend` 選擇直譯器，`--backend=vm|mixed|jit` 選擇執行原則，`--jit-backend` 選擇 JIT 實作。指令碼編譯處理器也需要 VM；不執行這類處理器的一般檢查不會執行入口程式。

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

偵錯使用 VM。`--watch` 在監視的檔案變更時依選定的執行策略重新執行。

## 監視模式

`--watch` 在來源檔案或相關模組變更後重新執行腳本。 `--watch-once` 等同於監視模式，並在首次非零執行結果後退出。檢查模式、評估模式、REPL、偵錯模式和套件檔案不能與監視模式組合；已建置的套件也不支援 `--watch`。

## 偵錯

標準輸入輸出 DAP：

```powershell
veroscript --debug hello.vs
```

TCP DAP：

```powershell
veroscript --debug --dap-port=4711 hello.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 hello.vs
```

`--dap-port` 的範圍為 `1`–`65535`；`--dap-bind` 指定監聽位址，預設值為 `127.0.0.1`，必須與 `--debug --dap-port` 一起使用。偵錯模式透過 DAP 的 `launch` 或 `attach` 傳遞程式和參數；命令列形式中的腳本路徑會預置程式，路徑後面的命令列參數則會成為該請求的預設腳本參數。 `veroscript-dap` 是同一偵錯主機的薄封裝。詳細規則請見[偵錯](../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/debugging.md)。

## 套件、AOT 與綁定目錄

應用程式套件和函式庫套件使用 `--emit-package` 建置，`--package-file` 可以重複添加來源模組，`--package-version` 設定無符號版本號，`--strip-package-debug` 移除來源檔案和偵錯行表資訊。這些選項不能與 `-e`、`-i`、`--check`、監視或偵錯模式組合。詳見[套件與發佈](packages.md)。

`aot` 子指令需要來源腳本。預設目標是 C++17，並以 `-o`/`--output` 產生翻譯
單元。`--target=python` 會產生 Python 3.11-3.15 獨立原始碼，`--target=csharp` 會
產生適用於 .NET 8/9 的 C# 原始碼。Python 與 C# 也支援以 `--output-dir` 和
`--bundle-name` 產生原始碼 bundle。結果是原始碼而非已連結的執行檔。請參閱
[C++ AOT](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/aot.md)、[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/aot.md) 與 [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/aot.md)。

`bindings validate` 校驗目錄，`bindings format` 規範化目錄；後者可透過 `-o`/`--output` 輸出至新檔案。詳見[宿主綁定](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/bindings.md)。

## 診斷和環境變數

```powershell
veroscript --lang zh-TW --check script.vs
veroscript --version
```

常用環境變數包括：

| 變數 | 作用 |
| --- | --- |
| `VERO_PATH` | 模組搜尋路徑；Windows 使用 `;`，Unix 使用 `:`。 |
| `VERO_STDLIB_PATH` | 標準函式庫搜尋根目錄。 |
| `VERO_CACHE_DIR` | 位元組碼快取目錄。 |
| `VERO_LANG` | 診斷語言，例如目前目錄中已載入的任意規範 BCP 47 語言標籤。 |
| `VERO_LOCALIZATION_DIR` | 本地化目錄。 |
| `VERO_MAX_CALL_DEPTH` | 調整總遞歸呼叫深度上限。 |

命令列選項覆蓋對應的環境設定；不受支援的系統語言或 `VERO_LANG` 會回退到英文，但明確指定的不受支援 `--lang` 值會作為參數錯誤被拒絕。需要查看目前安裝版本時使用 `veroscript --version`。

本地化目錄必須使用 `localization/<culture>/<component>.json` 佈局；目錄校驗、搜尋順序和模組級目錄見[診斷本地化](../../../../../VeroScript/Resources/Docs/User/zh-TW/localization.md)。

## 效能測量時選擇後端

VM、mixed 和 JIT 優先後端具有相同的語言語意。mixed 會讓符合條件的程式碼使用最佳化層，同時保留 VM 回退；JIT 優先會更早要求編譯，但不保證每個函式都會被編譯。

測量效能時請使用非偵錯的一般執行。偵錯使用 VM。位元組碼快取影響編譯時間，不影響腳本穩定執行時的實際工作成本。比較時應固定工作負載、程序生命週期、快取狀態和後端。

## Unicode 路徑與參數

Windows CLI 接收 Unicode 行程參數，並在分派前轉換為 UTF-8，因此非 ASCII 指令碼路徑、匯入模組路徑和傳給 `main(string[] args)` 的參數能夠保留原文。原始碼檔案和本地化 JSON 使用 UTF-8；讀取重新導向 stdout 或 stderr 的工具應按 UTF-8 解碼這些串流。

## 後端選擇

<a id="cli-backend-policy"></a>

`vm`、`mixed` 和 `jit` 選擇執行策略，與 JIT 後端選擇相互獨立。Runtime 不內建 JIT。存在已安裝模組環境時，省略 `--jit-backend` 會繼承 `services.jit.backend`；未設定時預設為 `auto`，按後端 ID 升序嘗試已啟用的候選，選擇第一個相容且可用的後端。`--jit-backend=auto` 覆寫設定，`--jit-backend=none` 停用 JIT。也可以明確指定後端 ID、程式庫檔案或目錄；無效的明確選擇會報錯。`auto` 遇到無效候選 ID 或同一後端 ID 的作用中提供者衝突時也會報錯。自動選擇需要模組環境。停用 JIT 或自動選擇找不到可用後端時，`mixed` 使用 VM，強制 `jit` 在執行前失敗。CLI 選項須放在腳本路徑之前。偵錯使用 VM。不支援的程式碼可在進入前退回；進入後的失敗不會重播副作用。

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

```sh
veroscript --vm-backend=auto --jit-backend=auto main.vs
veroscript --jit-backend=none --backend=mixed main.vs
```

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
```

## 指令碼編譯擴充

請參閱[指令碼編譯擴充](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/compiler.md#source-extension-activation)。

<a id="cli-compiler-options"></a>

`--compiler-config <key=value>` 每次新增一個字串設定項，不同鍵可重複指定；值不按 JSON 解析，重複鍵會被拒絕，--compiler-instructions 與 --compiler-timeout 限制擴充工作，--no-compiler-imports 禁止原始碼啟用但保留宿主設定的擴充。擴充使用編譯 VM、協作取消及受限服務，一般執行階段匯入不會取得編譯權限。產生的程式碼仍接受一般語言規則檢查；獨立匯出執行產生的業務程式碼，不保留擴充 VM。處理器撰寫請見開發指南。

[指令碼編譯擴充](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/compiler.md)

擴充整體預設預算為 1,000,000 條 VM 指令及 2,000 毫秒。0 表示關閉對應預算；這些限制不能強制中斷阻塞的原生回呼。

`install -r <file>` / `install --requirement=<file>`: [相依列表檔案](../../../../../VeroScriptPackageManager/Resources/Docs/User/zh-TW/index.md).

## 原生模組命令

已安裝且啟用、宣告原生 `cli` 角色的套件，可在選定的專案或元件環境直接提供命令。使用套件實際登錄的命令與選項名稱。選擇其他專案時，將 `--path <directory>` 放在模組選項之前。

本機程式庫使用可重複的 `--cli-module <file>`。相對路徑以啟動目錄為基準；載入器接受程式庫檔案，不掃描目錄或 PATH。Windows 使用 `.dll`，Linux/macOS 使用相符的 `.so`/`.dylib`。引數順序為環境/後端選項及程式庫選擇、模組選項、命令或腳本；後續引數原樣傳遞。

模組命令優先於同名檔案；執行該檔案時使用 `./name`。內建名稱與重複登錄名稱不能被覆寫。模組命令不能與 check、eval、REPL、watch、debug/DAP 或位元碼輸出模式組合。`--help` 列出登錄項目，`<command> --help` 不設定或執行命令，只顯示用法；登錄過程仍會載入原生模組。輸出及描述由模組提供，`--lang` 將所選語言傳給回呼，不會自動翻譯模組文字。Core CLI 說明與部分驗證訊息仍為英文。

```text
veroscript my-command arg
veroscript --path ./project --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll my-command --help
```

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
