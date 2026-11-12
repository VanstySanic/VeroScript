# 有界位元組串流

VeroScriptNetworkStream.h 提供擁有的有界環及 reader/writer，不借用 VM。複本共用每個邏輯生產/消費端游標。有 context 的 TryCreateBytePipe 計入完整容量；無 context 由原生呼叫端管理總量。數量受檢 int64，容量也須符合 size_t。

Progress=0（非空請求不可零位元組）、WouldBlock=1（未消費，交回排程）、End=2（生產結束且緩衝排空）、Failed=3。空請求不探測 EOF。Write 可部分接收，WriteAll 全接或不接；大於總容量立即失敗。Finish 正常結束；Fail 先排空既有資料再報錯。reader Cancel/最後釋放捨棄並通知生產者，未 Finish 的 writer 最後釋放回報 Closed。Owner 持續至端點與儲存皆釋放。

腳本 create_byte_pipe 為 1 位元組–16 MiB；read/write 每次最多 64 KiB 並保留 NUL，以 state/error_code/error 而非空字串判 EOF。buffered/capacity 為 int64，使用 writer.finish/fail、reader.close。WouldBlock 時讓出排程，HostDriven 由所屬執行緒 poll。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](streaming.md)
