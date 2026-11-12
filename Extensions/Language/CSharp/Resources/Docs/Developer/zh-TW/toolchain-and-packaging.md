# C# 工具鏈與包契約

## 嚴格的工具邊界

存在進入點時，C# Transpiler 會降低由 C# 編譯器選定的進入點，而不是依類別名稱猜測。它僅接受宣告於靜態型別中的無參數 `static void Main()` 或 `static int Main()`；進入點含糊、帶有參數或位於執行個體容器時會以 `VSCT2001` 失敗。由於 Binding Catalog v1 尚無宿主索引存取降低，BindGen 會拒絕 C# 索引子。兩個工具都不會悄然省略不支援的進入點或成員，只有驗證成功後才會提交輸出。

## 框架和 SDK

`VeroScriptCSharp` 目標為 `net8.0` 與 `net9.0`，使用 C# 語言版本 12.0，生成 XML 文件並啟用包 API 驗證；執行時包為 `VeroScript.CSharp`。CMake 在構建前檢查已安裝 .NET SDK，併為兩個目標框架執行託管測試。

C# 前端、Transpiler 和 BindGen 專案同樣目標為 `net8.0` 與 `net9.0`，但當前 CMake 工具鏈中其構建期工具需要 .NET 9.0 或更高版本的 SDK。這是工具構建前提，不能與執行時包支援的目標框架混淆。當要求的 .NET 8 或 9 執行時未安裝時，打包的命令列工具採用 `Major` 執行時前滾策略，因此可以在更新且仍受支援的主版本執行時上執行。

## 包內容

執行階段 `.nupkg` 包含 net8.0/net9.0 託管組件、XML API 文件、buildTransitive 整合目標與授權條款；偵錯符號透過獨立 `.snupkg` 分發。原始碼 README.md、API.md 與 MOBILE.md 不隨套件附帶，使用指南透過獨立 Resources 文件取得。原生 Interop 執行階段仍由宿主部署。CMake 將各框架的託管輸出與產生的 NuGet 套件安裝至 SDK 套件區域。

## C# AOT 源 Bundle

`VeroScriptCSharpAot` 同時公開單個生成 C# 編譯單元和確定性的 `FCSharpAotSourceBundle`。Bundle 包含執行時支援、降級程式、入口點的相對原始檔，以及 SDK 專案和機器可讀 `ManifestJson`。`BuildCSharpSourceBundle` 打包已發射的 C#，不改寫生成源。

源、檔案和已檢查程式入口共享載入器控制：匯入路徑、隱式匯入路徑、`Defines`、迴圈匯入策略和專案範圍訪問策略。接受 options 的過載還攜帶宿主目錄和程式碼生成設定。載入、語義檢查、降級或 Bundle 構建失敗不得返回部分可構建 Bundle。相對路徑和清單欄位是輸出契約，絕不本地化。

## 生成註冊檢查

BindGen 打包包含目錄生成、生成註冊編譯與執行時測試、C++ 目錄匯入測試和包驗證。變更生成 C# 源或目錄形狀後，所有生成註冊消費者仍必須能編譯執行。

## 源轉譯

`VeroScript.CSharpTranspiler` 和 `VeroScript.CSharpTranspiler.Tool` 提供嚴格的 C# 到 VeroScript 轉換。專案模式將所有源作為單個 Roslyn 編譯處理，發射語義跨檔案匯入、原始碼對映和確定清單，並原子提交輸出集。預設用真實 VeroScript 驗證器編譯生成源。

## 工具相依套件聲明

C# 前端、BindGen CLI 與 Transpiler CLI 的輸出在 `licenses/` 下保留 Roslyn 原始聲明；前端 NuGet 與兩個 .NET 工具套件也包含這些檔案。它們補充專案的 MIT 聲明。Roslyn 版本變更時同步更新[聲明來源](../../../Licenses/README.md)，保留完整的 `Roslyn-LICENSE.txt` 和 `Roslyn-ThirdPartyNotices.rtf`，包括 RTF 編碼。套件檢查會比較它們與原始聲明的一致性；不要在地化上游授權條款正文。

## 標準容器的泛型參數數量

請參閱[C# 前端 — 標準容器的泛型參數數量](../../User/zh-TW/frontend.md#container-mapping-arity)。

## 整數轉譯與數值支援邊界

Transpiler 保留 C# 預設 unchecked 整數語意：加減乘、取負、遞增遞減、複合指定和整數強制轉型按解析後的位元寬度環繞；位移計數以 31 或 63 遮罩。複合指定在右運算元之前僅求值一次目標及其舊值；自動屬性輸出為欄位。整數除法與餘數在除數為零或有號最小值/-1 溢位時擲回可捕捉的指令碼例外。整數值的 `double` 常數仍保留浮點型別。迴歸測試對同一輸入執行 CLR/VeroScript 並比較實際結果。

請參閱[C# 轉換器 — 整數轉譯與數值支援邊界](../../User/zh-TW/transpiler.md#numeric-and-console-boundaries)。

工具套件發佈檢查會安裝 Transpiler，以它產生原始碼、透過真實編譯器驗證並執行，核對無號除法與餘數、有號整數回繞、布林文字與換行、可捕捉的零除數例外，以及關閉驗證時仍拒絕不支援的 checked 字串常數。

C# AOT 的浮點轉整數轉換向零截斷，並在強制轉換前按二進位值檢查目標範圍，保留可表示的 64 位元整數值，不經過十進位捨入。無號目標拒絕負數輸入，包括負小數；NaN、無窮和超出範圍的值均失敗。回歸檢查在託管執行與 NativeAOT 執行中涵蓋整數邊界值。

請參閱[C# AOT — 明確排除](../../User/zh-TW/aot.md#aot-floating-conversions)。

C# 轉譯器在整數與浮點數的混合比較前套用 Roslyn 的運算元提升，確保進入核心比較前已經完成 C# 的 float 捨入。

數值條件分支套用 Roslyn 選擇的隱式轉換，保留整數位寬與 float 捨入，執行時只求值選中的分支。條件分支中的非常量 64 位元整數轉 float 同樣明確拒絕。

請參閱[C# 執行時與 Interop 介面卡 — 保留呼叫控制代碼的釋放](runtime-and-interop.md#managed-marshalling-maintenance)。

[Android 與 iOS / MAUI](../../User/zh-TW/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

行動封裝仍使用同一專案與套件識別名稱：`VeroScriptIncludeMobile=true` 將桌面 .NET 8/9 與 .NET 10 Android/iOS 目標及已驗證的原生 SDK 一起封裝。

裁剪 `core.vs` 匯入的未使用實作時，已解析的位元組碼呼叫會保留執行相依性，泛型主體會保留自由符號相依性，型別相依性透過 AST 宣告與作用域參照收集，公開型別名稱透過 Core 別名表比對。`std::sqrt`、`std::algorithm::sort` 等呼叫和 `std::delegate` 等型別會保留所需實作。區域名稱、實例成員名稱與一般字串內容不會被視為全域函式參照。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](toolchain-and-packaging.md)
