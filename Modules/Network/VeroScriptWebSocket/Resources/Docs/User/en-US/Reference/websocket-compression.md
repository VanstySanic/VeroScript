# WebSocket compression

CompressionSupported(service) reports build capability, bCompression requests negotiation (default false), CompressionInfo() reports the actual immutable result; a peer may decline and ordinary messages continue. Result survives connection/context close. Compression windows default 15 in both directions, configured range 9–15; no-context-takeover flags default false, Level=8 (0–9), MemoryLevel=4 (1–9). Client/server prefixes refer to protocol direction, not local/remote. Unnegotiated info is disabled with zero windows.

An 8-bit compressor window is unsupported and never silently changed to 9. Server skips offers requiring its own 8-bit output, client rejects such selections. Invalid local config is InvalidArgument. Before codec allocation, negotiation checks names, grammar, duplicates, values, offer constraints and capabilities; unknown/duplicate/missing/unsupported choices are Protocol. Server chooses first supported offer in arrival order, including comma lists, multiple fields and quoted numbers.

Payload/message/queue limits use application bytes, decompressed on receive; a tiny compressed input cannot bypass them. Excess is Limit, bad DEFLATE/frame Protocol, clean close Closed, native read Read, idle TimedOut. The first terminal reason is retained after complete queued messages drain. Each negotiating connection reserves 1 MiB of context quota before codec allocation; release on declined negotiation, otherwise retain through actual codec lifetime. This is a conservative managed reservation, not exact process peak.

With compression compiled out, compression_supported is false and enabling it returns Unsupported; ordinary ws/wss remains. The installed dependency descriptor reports websocketCompression. Script make_compression_options exposes client/server_window_bits, level, memory_level and no_context_takeover getters/setters; options.set_compression_options snapshots them. connection.compression exposes enabled/window/takeover results. Compression does not change text/binary/empty/fragment/control semantics.

## Examples

```VeroScript
websocket::compression_options compression = websocket::make_compression_options();
compression.set_server_window_bits(12);
compression.set_client_no_context_takeover(true);
websocket::options options = websocket::make_options();
options.set_compression(true);
options.set_compression_options(compression);
// Query after a successful connection.
websocket::compression_info actual = connection.compression();
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](websocket-compression.md) · [fr-FR](../../fr-FR/Reference/websocket-compression.md) · [ja-JP](../../ja-JP/Reference/websocket-compression.md) · [ko-KR](../../ko-KR/Reference/websocket-compression.md) · [zh-CN](../../zh-CN/Reference/websocket-compression.md) · [zh-TW](../../zh-TW/Reference/websocket-compression.md)
