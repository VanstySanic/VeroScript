# 레지스트리 프로토콜

레지스트리 아카이브 검증은 ZIP 루트의 `veroscript-package.json`을 읽어야 합니다. 프로젝트 설정으로 대체할 수 없습니다. API 경로, JSON 형식 식별자와 형식 버전 `0.1.1`은 동일합니다.

클라이언트 계약은 0.1.1이며 설정된 API, HTTPS, UTF-8 JSON, 일치하는 sourceId를 사용합니다. 서버가 구현할 계약이며 운영 서버를 의미하지 않습니다.

공식 CLI의 API 기본 주소는 `https://veroscript.org/api`이며 논리 `sourceId`는 `veroscript.official`입니다. 아래 경로를 이 주소에 붙입니다. 프로젝트/잠금 파일 소스와 명시적 API·미러 옵션은 기존 선택 규칙을 따르며 임베딩 호스트는 소스를 직접 설정합니다.

| 메서드 | 경로 | 계약 |
| --- | --- | --- |
| GET | `/search?q=...` | `sourceId + search JSON` |
| GET | `/packages/{name}` | `veroscript-registry-package` |
| GET | `/packages/{name}/versions/{version}` | `veroscript-registry-version` |
| GET | `/artifacts/{id}/download` | `ZIP` |
| POST | `/uploads` | `veroscript-upload` |
| PUT | `{upload.url}` | `ZIP` |
| POST | `/packages/{name}/versions` | `mode=append + sourceId/name/version/dependencies/conflicts/artifacts/idempotencyKey` |

veroscript-registry-version은 공통 dependencies/conflicts와 artifacts를 갖고 각 항목은 id/size/sha256/manifestSha256/target/requires, 선택 blocked를 포함합니다. veroscript-registry-package는 formatVersion/sourceId/name/versions와 선택 withdrawn/blocked 상태를 제공합니다. veroscript-upload 응답은 uploadId/method=PUT/url, 선택 headers/expiresAt입니다. 산출물 ID는 비어 있지 않은 ASCII 영숫자와 `.`, `_`, `-`만 사용하며 `.`과 `..`는 금지됩니다. 산출물은 ZIP 아카이브이며 `kind` 필드를 사용하지 않습니다.

버전은 portable 하나 또는 상호 배타적 대상들로 구성하며 portable/native의 모호한 중첩은 금지합니다. 정규 이름, 버전, 의존성, 충돌이 모두 같아야 합니다. sha256은 정확한 ZIP, manifestSha256은 공백을 포함한 원래 루트 veroscript-package.json 바이트를 대상으로 합니다. .vspkg/.zip은 같은 프로토콜입니다.

## 같은 버전에 플랫폼 추가

최초 게시와 이후 추가에는 같은 `publish` 명령을 사용하고 새 `.vspkg`/`.zip`만 전달합니다. 완전히 같은 기존 산출물은 다시 업로드하지 않습니다. 이름, 버전, 공통 의존성·충돌과 기존 ID, 바이트, 해시, 크기, 대상·요구 사항을 유지합니다. portable 대상은 모든 대상과 겹칩니다. OS, 아키텍처, environment, coreBuild 또는 공통 ABI 키의 서로 다른 동등 조건은 배타성을 증명하지만 최소 버전이나 필요 기능 차이만으로는 증명하지 못합니다.

백엔드에서 필요한 변경:

1. `POST /packages/{name}/versions`를 유지하고 `mode: "append"`를 필수로 받습니다. 없는 버전은 생성하고 기존 버전에는 업로드 참조를 원자적으로 추가합니다. 목록은 교체가 아닌 증분입니다. [게시 요청 스키마](../../../Schemas/veroscript-publication.schema.json)를 따릅니다.
2. 소유권, 업로드 바이트/매니페스트와 공통 메타데이터 불변성을 검사합니다. 기존 ID·필드를 보존하며 완전히 같은 산출물은 ID를 재사용합니다. 차단된 산출물을 포함한 모든 기존 대상과 배치 내부를 비교합니다. 변경·삭제·겹침과 차단/철회 버전 추가를 거부하고 충돌 시 배치 전체를 거부합니다.
3. 동시성 검사와 삽입을 같은 트랜잭션으로 처리합니다. 배타적인 동시 추가는 함께 성공할 수 있습니다. 클라이언트 사전 검사는 서버 트랜잭션 검사를 대신하지 않습니다.
4. 멱등 키와 정규 요청을 저장합니다. 같은 키·요청은 원래 결과를 반환하고 다른 요청은 거부합니다. 업로드 할당에도 적용합니다. 완료 참조를 유지하며 클라이언트에 서명 URL·인증 헤더 저장을 요구하지 않습니다. 같은 uploadId를 유지하면서 만료된 서명 URL을 재발급할 수 있어야 하며 같은 바이트의 PUT 재전송은 안전해야 합니다.
5. 모든 기존/신규 산출물과 안정 ID를 포함한 전체 버전을 반환합니다. 클라이언트는 관찰한 기존 ID와 모든 요청 산출물을 확인하고 유효하며 겹치지 않는 동시 추가를 허용합니다.
6. 원본 버전 GET은 실제로 없을 때만 404를 반환하며 커밋 후 최신 전체 버전을 반환합니다. 사전 검사·복구는 `Cache-Control: no-cache`로 원본을 직접 사용하고 미러·설치 캐시를 우회합니다. 응답 유실은 완전히 일치하는 GET으로 확인하며 미확인 시 참조와 원래 요청·키를 보존해 재시도합니다.
7. ETag와 CDN/미러 메타데이터를 갱신하면서 기존 다운로드 ID·바이트를 보존합니다. 대상별 잠금은 계속 유효합니다. 철회/차단 상태는 산출물 교체 권한이 아닙니다.

프로토콜은 `0.1.1`을 유지하며 코어·Interop·JIT API는 바뀌지 않습니다. 실제 게시 전 백엔드가 위 의미를 구현해야 합니다.

오류는 code/message/requestId와 선택 구조 상세입니다. GET은 ETag/If-None-Match와 제한된 재시도를 지원하며 업로드/커밋 POST에 맹목적 재시도를 적용하지 않습니다. 인증을 정확한 호스트/경로로 한정하고 복구 기록을 출처/API/결과 집합별 분리합니다. 서버는 권한, 만료, 검증, 인덱스, 저장, 철회/차단, 미러 동기화와 감사를 구현합니다.

[스키마](../../../Schemas) · [유지보수](index.md)

기본 HTTP 전송은 리디렉션을 포함한 각 요청의 정규화된 URL에 대해 인증 정보를 다시 확인합니다. 사용자 정의 `IHttpTransport`는 각 요청에서 `FHttpRequest.Credential`을 적용해야 하며, 빈 결과이면 Authorization을 전송하지 않습니다. 기존 출처/경로 리디렉션 제한도 유지합니다. 레지스트리가 제공하는 업로드 서명 인증은 레지스트리 인증 정보와 별도로 처리합니다. 같은 경로의 쿼리 매개변수는 인증 범위에 포함됩니다.

퍼센트 디코딩한 경로에 독립된 `.` 또는 `..` 세그먼트가 나타나면 인코딩된 구분자도 포함하여 확인하고, 해석기가 제공한 인증 정보를 전송하지 않습니다. 일반 인코딩 파일명과 UTF-8 경로는 계속 사용할 수 있습니다.

공식 레지스트리는 `vs-`로 시작하는 패키지 이름의 생성과 후속 게시를 승인된 공식 게시자에게 예약해야 합니다. AsmJit의 정규 패키지 이름은 `vs-asmjit`, 서비스 백엔드 ID는 `asmjit`입니다. 서버의 소유권 및 게시 권한 검사에서 이 규칙을 적용합니다. 클라이언트는 일반 이름 검증을 유지하며 로컬 또는 비공개 소스의 접두사만으로 공식 출처를 판단하지 않습니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md) · [en-US](../en-US/registry-protocol.md) · [fr-FR](../fr-FR/registry-protocol.md) · [ja-JP](../ja-JP/registry-protocol.md) · [ko-KR](registry-protocol.md) · [zh-CN](../zh-CN/registry-protocol.md) · [zh-TW](../zh-TW/registry-protocol.md)
