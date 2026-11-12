# Registry protocol

Registry archive validation must locate `veroscript-package.json` at the ZIP root. A project configuration cannot replace it. API routes, JSON format identifiers and format version `0.1.1` remain the same.

The client protocol is 0.1.1. Use a configured API base, HTTPS, UTF-8 JSON and matching sourceId. This describes the interface a registry must implement; it is not a hosted server.

The official CLI API base is `https://veroscript.org/api`, with logical `sourceId` `veroscript.official`. Append the routes below to this base. Project/lockfile sources and explicit API or mirror options retain their existing selection rules; embedding hosts configure their own source.

| Method | Route | Contract |
| --- | --- | --- |
| GET | `/search?q=...` | `sourceId + search JSON` |
| GET | `/packages/{name}` | `veroscript-registry-package` |
| GET | `/packages/{name}/versions/{version}` | `veroscript-registry-version` |
| GET | `/artifacts/{id}/download` | `ZIP` |
| POST | `/uploads` | `veroscript-upload` |
| PUT | `{upload.url}` | `ZIP` |
| POST | `/packages/{name}/versions` | `mode=append + sourceId/name/version/dependencies/conflicts/artifacts/idempotencyKey` |

Version objects use veroscript-registry-version with common dependencies/conflicts and artifacts containing id, size, sha256, manifestSha256, target and requires; blocked is optional. Package objects use veroscript-registry-package with formatVersion/sourceId/name/versions and optional withdrawn/blocked version state. Upload responses use veroscript-upload and uploadId/method=PUT/url, optional headers/expiresAt. Artifact IDs are non-empty ASCII letters/digits plus `.`, `_`, `-`, excluding `.` and `..`. Registry artifacts are ZIP archives and have no `kind` field.

A version is either one portable artifact or mutually exclusive target artifacts, never an ambiguous portable/native overlap. Every artifact agrees on canonical name, version, dependencies and conflicts. sha256 hashes exact ZIP bytes; manifestSha256 hashes the original root veroscript-package.json bytes, including whitespace. .vspkg and .zip use this same protocol.

## Append platforms to an existing version

Use the same `publish` command for initial publication and later platform additions. Supply only the new `.vspkg`/`.zip` files; identical existing artifacts are accepted without another upload. Package name, version, common dependencies and conflicts must match. Existing artifacts retain their IDs, bytes, SHA-256, manifest digest, size, target and requirements. A portable target overlaps every target. Equality constraints on OS, architecture, environment, coreBuild or a shared ABI key can prove disjointness; different minimum versions or required capabilities alone cannot.

The following changes are required in the registry backend:

1. Keep `POST /packages/{name}/versions`; require `mode: "append"`. Create an absent version or atomically merge the supplied upload references into the existing version. This is an additive request, not a replacement list. The [publication request schema](../../../Schemas/veroscript-publication.schema.json) defines the body.
2. Enforce package ownership, uploaded bytes/manifest identity and unchanged common dependencies/conflicts. Preserve existing artifact IDs and immutable fields; reuse an exact existing artifact instead of allocating another ID. Compare incoming targets against all existing and incoming artifacts, including blocked artifacts. Reject modifications, deletions and overlaps with a conflict response; reject appends to blocked/withdrawn versions. A conflict rejects the entire batch.
3. Perform these checks and insertion in one transaction under concurrency. Concurrent disjoint appends may both succeed. The client preflight cannot replace server-side transaction checks.
4. Persist the idempotency key and canonical request. The same key/request returns the original outcome; a changed request is rejected. Upload allocation follows the same rule. Preserve completed upload references for retry; never require the client to persist signed URLs or authorization headers. Upload allocation may renew expired signed transport URLs while retaining the same uploadId; repeating PUT with identical bytes must be safe.
5. Return the complete published version, including stable IDs for old and new artifacts. A response containing only the added artifacts is insufficient. The client checks all previously observed identities and all requested artifacts, and permits only valid non-overlapping concurrent additions.
6. The authoritative version GET returns 404 only for an absent version and returns the current full version after commit. Preflight and recovery use this origin directly with `Cache-Control: no-cache`, bypassing mirrors and installation metadata caches. If a commit response is lost, a complete matching GET confirms success; otherwise upload references and the exact commit request/key remain available for retry.
7. Refresh ETags and CDN/mirror metadata after an append while preserving existing artifact download identities and bytes. Existing target-specific lockfiles remain valid. Withdrawal/blocking are separate mutable status operations, not permission to replace an artifact.

The protocol stays at `0.1.1`; core, Interop and JIT APIs are unchanged. The backend must implement these append semantics before the updated client can publish safely.

Errors contain code/message/requestId and optional structured details. GET supports ETag/If-None-Match and bounded retry. Upload/commit POST must not use blind GET-style retries. Scope credentials to exact hosts/paths and isolate recovery records by logical source, API and artifact set. The server implements authorization, expiry, validation, indexing, storage, withdrawal/blocking, mirror synchronization and auditing.

[Schemas](../../../Schemas) · [Maintenance](index.md)

The bundled HTTP transport revalidates resolver-supplied Authorization against each normalized request URL, including redirects. Custom `IHttpTransport` implementations must honor `FHttpRequest.Credential` on every request leg: an empty result removes Authorization, and origin/path redirect restrictions still apply. Explicit upload authorization supplied by the registry remains separate from registry credential resolution. Queries on an exact credential path remain in scope.

Resolver authorization is also withheld when percent-decoding the path reveals standalone `.` or `..` segments, including encoded separators. Ordinary encoded filenames and UTF-8 paths remain valid.

The official registry must reserve package names beginning with `vs-` for authorized official publishers, including package creation and subsequent publication. AsmJit's canonical package name is `vs-asmjit`; its service backend ID is `asmjit`. Enforce the reservation in server ownership and publication authorization. Clients retain generic package-name validation; a local/private-source name with this prefix is not proof of official origin.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md) · [en-US](registry-protocol.md) · [fr-FR](../fr-FR/registry-protocol.md) · [ja-JP](../ja-JP/registry-protocol.md) · [ko-KR](../ko-KR/registry-protocol.md) · [zh-CN](../zh-CN/registry-protocol.md) · [zh-TW](../zh-TW/registry-protocol.md)
