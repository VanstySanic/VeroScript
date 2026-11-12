# 標準函式庫基礎

<a id="foundation-imports"></a>

字串、I/O、路徑、數值聚合與並行處理需要匯入 core.vs 或相應模組；std::io::read_* 需要 io.vs。環境、時間與主控台入口仍由執行階段直接提供。完整命名空間不能代替模組匯入。

解析支援定寬整數、float、double、bool 及別名。去除外圍 ASCII 空白後必須完整符合；至多一個符號，無符號拒絕負號（含 -0）。明確的 base 僅用於整數，範圍 2..36，不剝離進位前綴。浮點只接受可表示的有限十進位數；bool 僅接受 true/false。一般格式或範圍失敗使 try_parse 傳回 false，輸出不變；非法參數、限額、取消等仍報錯。

字串位置採用 UTF-8 位元組位移。replace 從左至右替換不重疊的符合項目，空模式傳回原文；split 保留空片段。修剪和大小寫轉換使用 ASCII 規則。UTF-8 嚴格拒絕錯誤編碼，保留 NUL、BOM、換行而不替換錯誤位元組。region 表示 buffer、view、readonly_view 的實際多載，並非新型別。

<a id="io-contract"></a>

只有正常 EOF 傳回空 optional；空行是有值的空字串，沒有換行結尾的末行也有值。移除 LF 及末尾一個 CR；MaxTextBytes 依移除後的行內容計量，行內 CR 仍計入；token 保留結尾分隔符。舊 read_line 在 EOF 傳回空字串，嚴格 token 讀取在 EOF 報錯。read_bytes 超出明確上限時報錯而不截斷，0 僅允許空檔案。位元組 I/O 使用受檢記憶體借用。寫入失敗時檔案可能已部分修改，flush 不保證持久化。

路徑查詢僅作詞法處理，不存取磁碟。native 使用宿主語法，posix/windows 為明確的跨平台語法。Windows 的 C:a、/a 並非絕對路徑，完整 UNC 則是，輸出分隔符為 /。拒絕 NUL、不完整 UNC 與裝置前綴；正規化不能證明沙箱包含關係。sum_as 逐項轉成 R 後受檢相加，空陣列報錯；accumulate_as 空陣列傳回初值。R 必須明確指定為非 bool 基本數值型別。clamp 拒絕隱含混合型別、反向或 NaN 邊界，保留 NaN 輸入。

同步工廠建立私有通道角色；通用 send/recv/容量修改拒絕這些角色，關閉與唯讀查詢仍可用。mutex 非遞迴並由邏輯執行器擁有；其他執行器解鎖報錯，自身 try/限時加鎖傳回 false，擁有者退出時關閉仍持有的鎖。semaphore 以計數儲存並拒絕超額釋放。synchronized 透過已準備的快照與提交操作，算術失敗不會清空單元，不保證硬體無鎖。

任務別名共用 Core 狀態。await 等待 worker 完成後消費唯一結果，失敗、缺少或多餘結果均報錯。await_for 只有逾時才傳回空 optional，此時保持作用中；消費前準備失敗保留結果。群組先驗證空、無效和重複能力；部分失敗後已消費任務不回復，size 為剩餘範圍，終結失敗項目從範圍移除。when_all 保留原陣列但消費任務狀態。cancel_all 先要求全部取消，worker 實際退出前仍由 VM 持有；取消不能強制中斷阻塞的宿主呼叫。

宿主按單次操作設定 MaxTextBytes、MaxReadBytes、MaxSplitParts（uint64，0 不額外限制），平台及 VM 上限始終有效，worker 繼承設定。原生分塊工作沿用目前預算與取消。reverse/rotate/shuffle 中斷後保持元素多重集合；有觀察者時私有準備後一次發布，不呼叫 clone。shuffle 拒絕陣列與亂數引擎為同一身分，已消費亂數不回復。獨立 AOT 在產生前明確拒絕需要這些 Core 服務的操作，未使用的標準函式庫匯入仍允許。

請參閱[字串模組](string.md#integer-formatting)。

setenv(name, "") 設定存在的空值；刪除使用 unsetenv，刪除不存在項目也成功。try_getenv 區分缺少與空值。名稱不可為空且不能含 = 或 NUL，值不能含 NUL。Windows 環境名稱沿系統規則不區分大小寫，POSIX 則區分。

## API

```text
// string.vs
T std::strings::parse<T>(string text);
T std::strings::parse<T>(string text, int base);
bool std::strings::try_parse<T>(string text, ref T output);
bool std::strings::try_parse<T>(string text, ref T output, int base);
std::memory::buffer std::strings::encode_utf8(string text);
string std::strings::decode_utf8(region bytes);
bool std::strings::is_valid_utf8(string text);
bool std::strings::is_valid_utf8(region bytes);
// io.vs
optional<string> std::io::try_read_line();
optional<string> std::io::try_read_token();
std::memory::buffer std::io::read_bytes(string path);
std::memory::buffer std::io::read_bytes(string path, int64 max_bytes);
void std::io::write_bytes(string path, region source);
void std::io::append_bytes(string path, region source);
// path.vs: each query has both one- and two-argument overloads
bool std::filesystem::is_absolute(string path);
bool std::filesystem::is_absolute(string path, std::filesystem::path_style style);
string std::filesystem::root_path(string path);
string std::filesystem::root_path(string path, std::filesystem::path_style style);
string std::filesystem::lexically_normal(string path);
string std::filesystem::lexically_normal(string path, std::filesystem::path_style style);
// path_style: native, posix, windows
// numeric.vs / math.vs: R explicit, T inferred
R std::numeric::accumulate_as<R, T>(array<T> values, R initial);
R std::numeric::sum_as<R, T>(array<T> values);
T std::clamp<T>(T value, T low, T high);
// runtime-provided
optional<string> std::environment::try_getenv(string name);
void std::environment::unsetenv(string name);
```

## SDK

```text
C++: FEngineConfig.StandardLibraryLimits;
     VeroScriptSession::SetStandardLibraryLimits(FStandardLibraryLimits);
     VeroScriptVm::SetStandardLibraryLimits(FStandardLibraryLimits);
C:   vs_interop_session_set_stdlib_limits(session, &limits);
     vs_interop_stdlib_limits { struct_size, reserved=0, max_text_bytes,
                                max_read_bytes, max_split_parts };
C#:  VeroScriptSession.SetStandardLibraryLimits(VeroScriptStandardLibraryLimits);
Python: Session.set_stdlib_limits(max_text_bytes=0, max_read_bytes=0, max_split_parts=0);
WebAssembly: runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts);
```

請參閱[並發模組 — 任務取消請求](concurrent.md#concurrency-aot-boundary)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/foundation.md) · [fr-FR](../../fr-FR/standard-library/foundation.md) · [ja-JP](../../ja-JP/standard-library/foundation.md) · [ko-KR](../../ko-KR/standard-library/foundation.md) · [zh-CN](../../zh-CN/standard-library/foundation.md) · [zh-TW](foundation.md)
