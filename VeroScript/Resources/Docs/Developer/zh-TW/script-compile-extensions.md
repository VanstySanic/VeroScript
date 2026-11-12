# 腳本編譯擴充

擴充使用一般 VeroScript 模組，透過 std::compiler 註冊註解說明和處理器。核心在處理業務原始碼前載入擴充，執行時使用生成後的業務位元碼。

## 載入

業務入口前部，在註釋、空白、BOM 或其他 compiler 匯入之後寫：

```vs
import compiler "builder.vs";
```

路徑必須為字面量。一般 import 提供輔助程式；import compiler 執行註冊並宣告註冊依賴，循環會拒絕。後續業務模組只能使用入口或宿主已載入的擴充。擴充不能依賴自己的未註冊語法。

宿主設定 `FCompileOptions::ScriptCompileExtensions.Roots`，每項包括 Path 和可選 RegistrationEntry。Loader、C++ Session 與 Codegen 共用設定。bAllowSourceImports=false 關閉原始碼啟用，仍保留指定根。覆寫層、匯入與標準庫沿用既有載入規則。

## 註冊與服務

擴充匯入 compiler.vs，並匯出根模組自己的註冊入口：

```vs
import "compiler.vs";
export void register_extension(std::compiler::registration reg) {}
```

無引數工廠回傳 fn(std::compiler::context,std::compiler::output)->void。每個任務建立新的 VM 狀態，註冊及工廠建立各一次，回呼狀態跨階段沿用。原生與腳本處理器共用排序及衝突檢查；單獨註冊說明不賦予功能。

Context 提供階段、事件、模組、宣告、參數、預設值、修飾符、泛型、註解、註釋與位置。Output 支援 add_declaration、edit_source、replace_declaration、complete_body、replace_body、set_metadata、report_diagnostic 和 submit_operation。完整操作攜帶子中繼資料及來源對應，仍經核心權限、Snapshot、衝突和語義檢查。

registration、context 的服務能力與 output 僅於目前呼叫有效。擁有的唯讀值可保留；舊 scope 不可修改後續批次。UTF-8 範圍不得切分字元。泛型於宣告處理一次，再正常代入；使用者正文、虛方法覆寫與預設正文沿用共用管線。

初始化、註冊、工廠和回呼共用總預算與取消控制。編譯 VM 禁止主控台、直接檔案及環境操作、任務、目錄變更、原生位址和時鐘。額外原生繫結由宿主明確提供；未宣告的非決定性禁止重用準備快取。這不是不受信任原生程式的完整沙箱。

configuration 與 read_dependency 回傳擁有的唯讀資料。資源路徑以目前擴充根為基準。原始碼、套件、設定、服務契約及存在或缺少的依賴均納入快取複核。

## 套件與接入

CompileScriptCompileExtensionSource 編譯但不執行註冊。BuildPackage 建立 Library，將 CompileExtensionManifest 複製到選項。載入在初始化前驗證入口、版本、服務指紋、繫結與依賴。業務套件重放生成操作，不執行生產者擴充。

C++ 使用 ScriptCompileExtensions；C 使用 vs_interop_session_set_script_compile_options；C# 使用 SetScriptCompileExtensions；Python 使用 set_script_compile_extensions。設定僅在建立執行緒、閒置會話修改。CLI 支援 --compiler-extension、--compiler-config、--no-compiler-imports、--compiler-instructions、--compiler-timeout 及 compiler-package source.vs output.vslib。

工具與原始碼 AOT 使用相同準備程式，獨立輸出不保留處理器 VM。JIT 執行業務程式，編譯擴充始終使用 VM。快取在宿主原始碼／匯入鍵之外，複核設定及所有依賴。

範例：[builder.vs](../../../Examples/ScriptCompileExtensions/builder.vs)、[main.vs](../../../Examples/ScriptCompileExtensions/main.vs)。

<!-- docs-navigation -->

---

[開發者文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

Language: [en-US](../en-US/script-compile-extensions.md) · [fr-FR](../fr-FR/script-compile-extensions.md) · [ja-JP](../ja-JP/script-compile-extensions.md) · [ko-KR](../ko-KR/script-compile-extensions.md) · [zh-CN](../zh-CN/script-compile-extensions.md) · [zh-TW](script-compile-extensions.md)
