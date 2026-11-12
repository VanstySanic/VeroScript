# 有界バイトストリーム

VeroScriptNetworkStream.h は所有する有界 ring と reader/writer で、VM 借用はありません。copy は各論理 producer/consumer の cursor を共有します。context 付き TryCreateBytePipe は全容量を quota に算入し、なしでは native caller が総量管理します。数量は検査付き int64、容量は size_t にも収めます。

Progress=0（非空要求で 0byte 不可）、WouldBlock=1（未消費、yield）、End=2（生産終了/queue 空）、Failed=3（構造化 error）。空要求は EOF 検査ではありません。Write は部分受理、WriteAll は全部かゼロ、全容量超過は即失敗です。Finish は生産終了、Fail は残 byte を読んでから error。reader Cancel/最後の解放は queue を捨て通知、未完 writer 最後の解放は Closed。両端/storage 回収まで Owner を保ちます。

script create_byte_pipe は 1byte–16 MiB。read/write は各 ≤64 KiB、NUL 保持、空 string ではなく state/error_code/error で EOF を判断します。buffered/capacity は int64、writer.finish/fail と reader.close を使います。WouldBlock で scheduler に戻し、HostDriven は owner で poll します。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
