# Bounded byte streams

VeroScriptNetworkStream.h provides an owned bounded ring with reader/writer endpoints and no borrowed VM state. Copies share one cursor per logical consumer/producer. TryCreateBytePipe(context,capacity,...) accounts full ring capacity against context limits; without context native callers manage totals. Read/Write quantities are checked int64; capacity also fits size_t.

States: Progress=0 (nonempty requests never claim progress with zero bytes), WouldBlock=1 (zero consumed; yield), End=2 (producer finished and buffer drained), Failed=3 (structured error). Empty requests do not probe EOF. Write may partially accept; WriteAll is all-or-nothing, and a block larger than total capacity fails rather than waits forever. Finish ends production; Fail drains queued bytes before error. Reader Cancel/final release discards bytes and notifies producer; final unfinished writer release makes the reader Closed. Owner remains through both endpoints/storage.

Script Network::create_byte_pipe(context,capacity) allows 1 byte–16 MiB. pipe.reader/writer share endpoints. read(count)/write(bytes) transfer at most 64 KiB per call, preserving NUL; inspect state/error_code/error rather than treating an empty string as EOF. buffered/capacity are int64. writer.finish/fail and reader.close control termination. On WouldBlock yield, and in HostDriven drive context.poll on its owner.

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
