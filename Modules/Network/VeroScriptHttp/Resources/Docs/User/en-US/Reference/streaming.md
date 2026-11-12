# Bounded byte streams

SendStream(request,buffer_bytes) returns an operation, Body and TryHead: pending head returns false without error, a ready head is immutable, and Body is not duplicated in response.Body. Final success includes BodyBytes/trailers. Queue capacity is at least 16 KiB and at most the context single-buffer cap; body limits are independent. Drain during transfer, not after waiting, to avoid backpressure deadlock. Full curl chunks pause/replay without double counting; upload/download pause independently, other connections progress. Reader cancellation/release cancels the request; queued bytes drain before a later terminal error. Quotas do not cap all TLS/curl/HTTP2/OS storage.

Upload is a single-consumer reader, optional UploadLength; unknown HTTP/1.1 length uses backend framing. MaxUploadBytes defaults to 64 MiB. Short known input fails; WouldBlock pauses until write/Finish. One-shot streams cannot be reused, combined with buffered/multipart/HEAD bodies or automatic redirects. Native UploadFactory permits explicit replay; multipart can combine buffered/stream parts. The backend manages Content-Length/Transfer-Encoding and rejects conflicts. Script set_upload(reader,length=-1), set_upload_limit, send_stream, stream.operation/body/head_ready/head and response.body_bytes expose the contract. VM work checks remain bounded; yield on WouldBlock and drive HostDriven I/O.

ReplyStream snapshots response headers/fixed trailers and consumes one BodySource; nonempty response.Body conflicts with a reader. States 0 Uncommitted, 1 HeadersSent, 2 BodyStreaming, 3 Finished, 4 Aborted. BodyBytes counts fully written blocks. Completion means local acceptance, not remote application processing; sent bytes cannot roll back. HTTP/1.1 uses chunked for unknown length/trailers, otherwise Content-Length; HTTP/1.0 unknown length closes, no trailers. HEAD sends representation headers only, 204/304 no body and cancels input. Wrong length is Protocol, excess MaxStreamBodyBytes Limit. Framing/Trailer headers are rebuilt; trailers cannot carry framing/connection/routing fields. Invalid precommit arguments do not consume the exchange. Transfer chunks are at most 16 KiB, separately charged from pipe capacity. Failure/cancel/timeout/source close ends the connection. ResponseTimeout is idle, options.Timeout total from submission; long SSE can explicitly disable total but still obey byte limits. Script reply_stream(response,reader,length,total_timeout) uses -1 for unknown/unlimited, same-context pipe, operation/state/body_bytes; add_trailer/set_trailer precede submission.

Server bStreamRequests defaults false. Enabled Accept returns after headers, with empty Request.Body, single-reader BodyStream and BodyOperation; final request_body_result supplies body_bytes and trailers, without mutating early headers. Expect:100-continue precedes reading. RequestStreamBufferBytes defaults 64 KiB; MaxStreamBodyBytes limits totals, chunks at most 16 KiB/capacity. Drain or cancel the reader. Next requests wait for both body and response; old exchanges cannot reply to later requests. Input/output streams progress independently. Script server_options controls stream_requests/request_stream_buffer_bytes/max_stream_body_bytes, normal body/header/pending/time limits and TLS by value. Buffered exchanges reject body-stream access; strings preserve NUL.

server.Shutdown(grace,operation_options) stops new connections/requests and pending Accepts, closes unaccepted/idle work, and lets delivered exchanges drain within grace without another request. Expiry forces close (bForced); ClosedConnections counts active connections at shutdown start, not physical resource release. Grace is 0–365 days; operation total deadline also applies. Cancel/timeout/Close ends work with its original error. Concurrent shutdown reports State; already closed repeats successfully with 0 connections. Retain the server handle; destruction is Close. Script shutdown(grace_ms,total_timeout_ms), shutdown_forced/shutdown_connections and case-insensitive header/trailer queries follow typed-result rules.

## Examples

```VeroScript
Network::byte_pipe output = Network::create_byte_pipe(context, 65536);
http::response_builder response = http::create_response(200, "");
response.set_header("Content-Type", "text/event-stream");
http::response_stream_operation sending = exchange.reply_stream(response, output.reader(), -1, -1);
Network::writable_stream writer = output.writer();
writer.write(http::encode_sse("hello", "", ""));
// On WouldBlock yield; retry the unaccepted suffix on a later tick.
writer.finish();
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
