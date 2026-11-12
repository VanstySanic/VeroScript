# Network — ネットワークリファレンス

フロントエンドの導入とバックエンド選択の共通ルールは [Network](../index.md#backend-selection) で定義します。

vs-network 0.1.1 は context、operation、URL/DNS、TCP/UDP、pipe、file、共有 TLS を提供します。Http/WS は別 module です。native ID network、script Network、CMake VeroScript::Network。native SDK は Core 不要、Bindings は対応 Core が必要です。protocol は対応 Network に依存し、offline 導入ではその local archive を明示します。

FUrl::TryResolve は authority 付き network URL を RFC3986 §5 で解決します。path の文字どおり ./.. のみ正規化し、連続 slash、percent、query/fragment の点は保持します。空 query と欠落は別、空 reference は query を継承し fragment を消します。空 path は /、g:h のような opaque URI は拒否、失敗時出力保持です。byte 単位で一回 encode/decode、plus/space は form 明示時のみ、NUL を保持します。

background は context ごとに I/O thread 一つ、HostDriven は作成 thread で有界 PollIo が必要です。PollIo は callback を呼ばず再帰不可。Dispatch は作成 thread の batch、各 operation は登録/実行一回です。terminal と queue 登録は別なので継続して駆動します。queue allocation 失敗の登録は再試行でき、batch allocation 失敗は 0/Allocation で queue/quota を保ちます。文字列不足でも error code を保ちます。

Wait は観測のみで 0 Completed、1 WaitTimedOut（取消しない）、2 RequiresPoll。期限 ms は -1 無限、0 即時、提出から queue を含みます。完了/取消は一 terminal、Result<T> は成功した型一致時のみです。Shutdown は操作取消と登録 resource close、外部 result/Owner は残れても I/O を再開しません。

MaxRetainedBytes 既定 256 MiB は管理 buffer/pipe/queue/protocol body/result を数え、terminal/Shutdown 後も保持値は解放扱いしません。MemoryUsage と retained_bytes/peak_retained_bytes/memory_limit は現在/peak/上限で、RSS/OS/backend/allocator/caller copy/一時再配置の完全上限ではありません。executor Allocation/Internal は context を閉じて待機を起こし、なお Shutdown/破棄が必要です。回収は新しい operation list/診断文字列を必要としません。native DNS/file は論理取消後も残り、PollIo 内 deferred close も物理終了を待つ場合があります。

TCP は同時 read 一つと MaxQueuedWrites/MaxBufferBytes による有界 serial write queue。待機 write 取消はそれだけ、活動 I/O 取消は socket と他操作を閉じます。FTcpListener は async accept/idempotent close。UDP は空 datagram、実受信/元長/切り捨てを区別します。broadcast/multicast は interface を明示し、実配信は platform/network 次第です。

parse_url/resolve_url は scheme/host/port/path/query/fragment/has_query/has_fragment/explicit_port/ipv6/authority/target/to_string/without_fragment。percent_encode/decode は form 指定。create_context/resolve/connect/listen/bind_udp、send/receive/accept/close と型付き operation を使います。done/state/cancel/error/error_code/message_id/wait、検査付き int64 count、結果型を確認します。handle は context/code を実回収まで保持します。RetainCodeOwner は shared 所有権（null alias 含む）で重複排除し、生 pointer/control block なしは Owner ではありません。

Network を選択したプロトコルモジュールより先に読み込み、パッケージ依存関係を宣言します。Boost プロバイダーに対応する `VeroScriptBoostRuntime` を Windows ホストの隣またはネイティブモジュール配置に保持します。iOS は同じホストイメージ内の静的登録を使用します。これらのネイティブ API はブラウザー/Node 通信や言語の await/future を提供しません。

- [完了通知とプログラムの寿命](completion.md)
- [診断とローカライズ](diagnostics.md)
- [ファイルと原子的出力](files.md)
- [有界バイトストリーム](streaming.md)
- [TLS 設定](tls.md)

## 既定値

| API | 値 |
| --- | --- |
| `FContextOptions::MaxOperations` | `1024` |
| `MaxCallbacks` | `1024` |
| `MaxConnections` | `256` |
| `MaxQueuedWrites` | `64` |
| `MaxBufferBytes` | `16 MiB` |
| `MaxRetainedBytes` | `256 MiB` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
