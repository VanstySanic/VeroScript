# Network — 網路參考

前端安裝與後端選擇的公共規則統一見 [Network](../index.md#backend-selection)。

vs-network 0.1.1 提供 context、operation、URL/DNS、TCP/UDP、位元組管道、檔案及共享 TLS；Http/WebSocket 為獨立模組。原生 ID 是 network、腳本命名空間 Network、CMake 目標 VeroScript::Network。原生 SDK 不依 Core，Bindings 須匹配 Core。協定套件依賴同版 Network，離線安裝須明確指定本地歸檔。

FUrl::TryResolve 依 RFC 3986 §5 解決含 authority 的網路 URL，只正規化路徑字面 ./..，保留重複斜線、百分號、query/fragment 點段。空 query 與缺少不同；空參考繼承 query、移除 fragment；空路徑為 /，g:h 等不透明 URI 拒絕。失敗保留輸出；位元組僅編解碼一次，form 明確啟用 +/空白轉換，字串保留 NUL。

背景模式每 context 一個原生 I/O 執行緒；HostDriven 由建立執行緒限量 PollIo，不執行回呼也不可遞迴。Dispatch 在建立執行緒分批執行，每 operation 只登記/執行一次完成回呼。終態與入佇列分開，須持續推進；佇列配置失敗可重試登記。Dispatch 批次配置失敗回傳 0/Allocation，保留佇列/額度；文字無法配置仍保留錯誤碼。

Wait 僅觀察：0 Completed、1 WaitTimedOut 不取消、2 RequiresPoll。期限為毫秒，-1 無限、0 立即，從提交起含排隊。取消/完成只發布一終態；Result<T> 僅成功且型別符合時可取。Shutdown 取消並關閉登記資源；外部結果/程式碼 Owner 可存活但不恢復 I/O。

MaxRetainedBytes 預設 256 MiB，涵蓋受管緩衝、管道、佇列、協定正文及結果；終態/Shutdown 不提前釋放仍持有資料。MemoryUsage 及 retained_bytes/peak_retained_bytes/memory_limit 為目前/峰值/上限，不含完整 RSS、OS、後端、配置器、呼叫端複本或擴容暫存。執行器 Allocation/Internal 使 context 關閉並喚醒，仍須正常 Shutdown/解構。清理不依賴新操作清單或診斷字串。原生 DNS/檔案工作可能延後物理關閉；PollIo 內延遲 Close 也可能等待底層退出。

TCP 接受一個 read 及 MaxQueuedWrites/MaxBufferBytes 限制的序列 write 佇列；取消未開始 write 只移除它，取消活動 I/O 關閉 socket 及其他操作。FTcpListener 提供非同步 accept/冪等 close。UDP 保留空報文、實際/原始長度與截斷；broadcast/multicast 明確選介面，實際交付依網路平台。

parse_url/resolve_url 提供 scheme/host/port/path/query/fragment/has_query/has_fragment/explicit_port/ipv6/authority/target/to_string/without_fragment；percent_encode/decode 明確 form。create_context/resolve/connect/listen/bind_udp 與 send/receive/accept/close 使用型別化 operation；done/state/cancel/error/error_code/message_id/wait 與受檢 int64 count/結果。所有 handle 保有 context/程式碼至實際清理。RetainCodeOwner 按共享控制區塊去重，含 null 別名；只有裸位址而無控制區塊不是有效 Owner。

先載入 Network，再載入選定協定模組，並宣告套件相依。Boost provider 須保留匹配的 `VeroScriptBoostRuntime`，Windows 主程式可放在可執行檔旁或原生模組部署目錄。iOS 在同一主程式映像內靜態註冊。這些原生 API 不提供瀏覽器/Node 通訊實作或語言層 await/future。

- [完成回呼與程式生命週期](completion.md)
- [診斷與本地化](diagnostics.md)
- [檔案與原子輸出](files.md)
- [有界位元組串流](streaming.md)
- [TLS 設定](tls.md)

## 預設值

| API | 值 |
| --- | --- |
| `FContextOptions::MaxOperations` | `1024` |
| `MaxCallbacks` | `1024` |
| `MaxConnections` | `256` |
| `MaxQueuedWrites` | `64` |
| `MaxBufferBytes` | `16 MiB` |
| `MaxRetainedBytes` | `256 MiB` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](README.md)
