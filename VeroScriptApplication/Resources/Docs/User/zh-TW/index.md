# 應用容器與執行服務

使用標準 ZIP `.vsapp` 容器散發可執行應用。容器包含 `veroscript-app.json`、`veroscript-app.files.json`，入口通常是已編譯的 `program/entry.bin`，也支援原始碼入口。`.vslib` 保存已編譯程式庫。可安裝散發套件 `.vspkg/.zip` 使用獨立的根資訊清單 `veroscript-package.json`，使用前需先安裝。

使用 `veroscript application.vsapp` 或 `veroscript-runtime application.vsapp -- 參數` 執行；`--check` 僅檢查。相同內容可組成目錄、ZIP 或可選 PE/ELF 啟動器。Runtime、相依環境與啟動器獨立選擇；載入不安裝套件、不執行安裝鉤子，也不搜尋 PATH。

C++ 使用 `VeroScriptSession::LoadApplication`，或 `CreateApplicationServices()`、`FApplication::Open`、`Prepare`。原始碼入口透過 `FPreparedApplication::CompileSource` 保留宏、隱式匯入及編譯選項。C# 使用 `VeroScriptSession.LoadApplication`，Python 使用 `Session.load_application`；C ABI 使用對應的 application 服務與成對銷毀函式。宿主選項 JSON 的路徑必須為絕對路徑，格式見 Schema。 `VeroScriptSession::LoadFile` 和 `VeroScriptEngine::RunFile` 同樣識別應用容器及目錄。`ReadEntryPackage` 僅檢查映像，不載入原生工廠。

請參閱[C++ SDK — 整合順序](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/index.md#explicit-vm-selection)。

清單校驗每個檔案的長度、SHA-256 與執行模式，拒絕多餘／缺少檔案、路徑逃逸、連結、損壞 ZIP 與超預算輸入。內容路徑為 UTF-8，最多 511 位元組。可寫快取與應用分離；所有權收據與跨行程租約防止覆蓋使用中的內容。

可選 libsodium 提供 XChaCha20-Poly1305 IETF 與 Ed25519；外層 ZIP 保持可讀。信任金鑰與獨立簽章門檻由消費端指定，失敗不退回明文。目錄與單檔程式使用內容簽章，平台程式碼簽章另行處理；金鑰不得放入發行物。原生委託至多一次並轉送參數、輸出和退出碼。

`VeroScriptSession::ReloadApplication` 同時暫存程式碼與環境，失敗回復。遵循 Session 所屬執行緒及資源生命週期；取消無法強制中斷阻塞的宿主回呼。SDK 以 `find_package(VeroScriptApplication 0.1.1 EXACT CONFIG REQUIRED)` 接入。

重新組裝已簽章應用時，必須明確指定新產物的簽章政策。

GUI 應用程式同樣使用此 `.vsapp` 容器。`gui-compile` 提供編譯後的 GUI 映像與資源，GUI 檢查／預覽與通用 CLI inspect／檢查／執行讀取相同檔案。執行時需要啟用 GUI 的宿主，以及手動選擇的相容提供者。

## 主機選項與簽章

使用 `--application-options /absolute/host-options.json` 選擇主機設定。C ABI 提供 `vs_interop_application_services_create`、`vs_interop_application_read`、`vs_interop_session_load_application` 與對應銷毀函式。`cacheRoot`、`stdlibSearchPaths`、`cryptoLibrary`、`keyring` 路徑必須為絕對路徑。信任欄位包含 `requireSignature`、`minimumSignatures`、`requiredKeys`、`policyIdentity`，依據由使用方提供，不可由待載入應用決定。套件位元組 API 亦接受一般應用 ZIP。

XChaCha20-Poly1305 IETF 可保護已編譯映像、程式庫與明確選取的資源；外層 ZIP 與中繼資料仍可讀。不支援加密原始碼匯入或加密安裝 ZIP。libsodium 適配器使用絕對路徑原生程式庫。Ed25519 陣列含 1–16 筆紀錄，綁定種類、精確長度、SHA-256 與金鑰 ID／版本。內部簽章為 `veroscript-app.files.json.sig.json`，產物簽章為 `<filename>.sig.json`。刪除簽章不能解除必要原則；同一金鑰別名不計為獨立簽署者，未知／不受信任的金鑰不能滿足門檻。

委派不傳遞原生回呼或記憶體秘密。此服務本身不提供 GUI、行動平台生命週期或商店核准。替換程式碼前釋放外部指令碼值根；原生繫結目錄變更時使用 `LoadApplication` 與新的應用生命週期。SDK 連結 `VeroScript::VeroScriptApplication`；Runtime 描述符識別 Core、OS、CPU、環境及 ABI，與套件安裝描述符獨立。

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

## 獨立 Runtime 執行後端選項

宿主選項放在應用路徑前，應用參數放在 `--` 之後。`--vm-backend`、`--jit-backend` 選擇執行服務，與平台／圖形／GUI／圖像／字型／文字版面提供者分別設定。它們接受 `auto`、`none`、相符檔案／目錄，或在套件環境提供解析器時接受已安裝 ID。自動 VM 選擇可使用隨附產品後備，明確停用不會被覆寫。下列命令先檢查／準備應用而不呼叫入口，再停用 JIT 執行。GUI 容器仍需相符且啟用 GUI 的宿主與配套提供者。

```sh
veroscript-runtime --check --vm-backend auto --jit-backend none /absolute/application.vsapp
veroscript-runtime --vm-backend auto --jit-backend none /absolute/application.vsapp -- argument1
```

應用程式內容與快取路徑須使用真實檔案系統項目。macOS 僅允許 root 所有、分別解析到 `/private/tmp`、`/private/var`、`/private/etc` 的系統別名 `/tmp`、`/var`、`/etc`。其他符號連結與特殊項目仍會被拒絕；這不允許應用程式內容或快取內部出現連結。

<!-- docs-navigation -->

---

[使用者文件](../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
