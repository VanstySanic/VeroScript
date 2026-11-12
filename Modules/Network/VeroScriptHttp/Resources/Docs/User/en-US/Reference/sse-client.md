# SSE client

FSseClient runs over bounded HTTP streams with one logical caller of Poll/accessors/Close, no concurrent calls on one object. Background/HostDriven keep normal I/O driving, without per-connection workers or I/O-thread script calls. Connect reads options.Request.Url and permits bodyless GET only: no upload/factory/length/multipart/raw body. Accept is text/event-stream, Last-Event-ID is managed across reconnects; request CA/proxy/explicit limits remain.

Defaults: unlimited total timeout, 60 s idle covering heads/body; ordinary HTTP’s 30 s total is not silently applied. Explicit Request replacement preserves that request’s own timeout/totals. Total body bytes default unlimited, ring 256 KiB, line 64 KiB, event 1 MiB, Poll ≤64 KiB. Context counts ring/parser storage, caller-owned event copies are separate. Borrowed FSseWork checks cancellation at start/before chunks with zero cost, then charges actual consumed bytes in ≤8 KiB blocks without double-charging suffixes. False is Cancelled, callback exceptions Callback; script cancellation/budget failure propagates as VM failure.

Poll: 0 Pending (I/O or per-call budget), 1 EventReady (only state that commits Out), 2 Reconnecting, 3 Closed, 4 Failed. Require HTTP 200 and text/event-stream; 204 disables reconnect. Wrong status/type retains ResponseHead. EOF never fabricates an incomplete event.

Reconnect defaults unlimited, initial 3 s, maximum 60 s. Resolve/connect/read/write/timeout may retry, protocol/config/parse limit/allocation do not. MaxReconnects counts lifetime reconnects, 0 disables. On exhaustion clean EOF is Closed, network failure Failed. Server retry persists and is capped; IDs commit only on complete blank-line separators, including ID-only updates, and later ID-less events inherit. Close is idempotent. New requests start only in Poll; no automatic business replay.

Script make_sse_options sets limits/reconnect/idle_timeout (-1 disables)/request (URL argument overrides). connect_sse/connect_sse_with_options returns poll/close/event_data/event_type/event_id/last_event_id/response_status/reconnect_count/reconnect_remaining/error_code/error. event_* requires the last poll=1. Server buffered create_response/reply_response is distinct from ongoing reply_stream with text/event-stream.

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](sse-client.md) · [fr-FR](../../fr-FR/Reference/sse-client.md) · [ja-JP](../../ja-JP/Reference/sse-client.md) · [ko-KR](../../ko-KR/Reference/sse-client.md) · [zh-CN](../../zh-CN/Reference/sse-client.md) · [zh-TW](../../zh-TW/Reference/sse-client.md)
