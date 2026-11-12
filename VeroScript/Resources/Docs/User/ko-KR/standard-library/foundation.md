# 표준 라이브러리 기반

<a id="foundation-imports"></a>

core.vs 또는 개별 모듈을 가져옵니다. std::io::read_*에는 io.vs가 필요합니다. 환경, 시계, 콘솔 함수는 런타임이 직접 제공합니다. 정규화된 이름이 import를 대신하지 않습니다.

parse/try_parse는 고정 폭 정수, float, double, bool 및 별칭을 지원합니다. 외곽 ASCII 공백 제거 후 전체를 검사합니다. 부호는 하나이며 unsigned는 -0도 거부합니다. 정수 전용 base는 2..36으로 접두사를 제거하지 않습니다. 부동 소수점은 표현 가능한 유한 십진수만, bool은 true/false만 허용합니다. 일반 형식/범위 오류에서 try_parse는 false이며 출력은 유지됩니다. 잘못된 인수, 한도, 취소는 실행 오류입니다.

문자열 위치는 UTF-8 바이트 오프셋입니다. replace는 겹치지 않는 일치를 치환하고 빈 패턴이면 원문을 반환합니다. split은 빈 조각을 보존하며 trim과 대소문자 변환은 ASCII 규칙입니다. UTF-8은 엄격히 검사하고 NUL/BOM/줄바꿈을 보존합니다. region은 buffer/view/readonly_view의 실제 오버로드를 뜻합니다.

<a id="io-contract"></a>

정상 EOF만 빈 optional을 반환합니다. 빈 줄은 값이 있는 빈 문자열이며 개행 없는 마지막 줄도 반환합니다. MaxTextBytes는 LF와 끝의 CR 하나를 제외한 줄 내용을 세며 줄 내부의 CR은 포함합니다. LF와 끝의 CR 하나를 제거하고 token은 끝 구분자를 남깁니다. 기존 read_line의 EOF는 빈 문자열이고 엄격한 token의 EOF는 오류입니다. read_bytes는 한도 초과를 잘라내지 않고 오류로 처리하며 명시적 0은 빈 파일만 허용합니다. 메모리 대여를 검증합니다. 실패 전 파일 변경은 되돌리지 않으며 flush는 영속성을 보장하지 않습니다.

경로는 디스크 접근 없이 native/posix/windows 어휘 규칙으로 처리합니다. Windows C:a와 /a는 절대 경로가 아니며 완전한 UNC는 절대 경로입니다. 출력은 /를 사용합니다. NUL, 불완전 UNC, 장치 접두사는 거부합니다. 정규화가 샌드박스 포함을 증명하지 않습니다. sum_as는 각 원소를 R로 변환한 뒤 검사된 덧셈을 수행하고 빈 배열을 거부합니다. accumulate_as는 빈 배열에서 초깃값을 반환합니다. R은 명시적인 bool 외 기본 수치형입니다. clamp는 암시적 혼합 타입, 역순/NaN 경계를 거부하며 입력 NaN은 보존합니다.

동기화 팩터리는 비공개 채널 역할을 만듭니다. 일반 send/recv/용량 변경은 거부하고 닫기와 조회는 허용합니다. mutex는 비재귀적이며 논리 실행기가 소유합니다. 타 실행기의 해제는 오류, 자기 try/시간 제한 획득은 false이고 소유자 종료 시 닫힙니다. semaphore는 숫자 카운터를 저장하고 초과 해제를 거부합니다. synchronized는 준비된 스냅샷을 게시하여 산술 오류 시 값을 보존합니다. 하드웨어 무잠금은 보장하지 않습니다.

작업 별칭은 Core 상태를 공유합니다. await는 worker 종료 후 유일한 결과를 소비하며 실패/누락/복수 결과는 오류입니다. await_for의 빈 optional은 시간 초과뿐이며 작업은 활성 상태를 유지합니다. 소비 전 준비 실패는 결과를 보존합니다. 그룹은 null/무효/중복 능력을 먼저 검사하며 부분 실패 시 이미 소비된 작업은 복원하지 않고 size는 남은 범위입니다. when_all은 배열을 유지하며 작업 상태를 소비합니다. cancel_all은 먼저 모두에게 취소를 요청하고 실제 종료까지 worker를 보유합니다. 차단된 호스트 호출을 강제로 중단하지 않습니다.

호스트 MaxTextBytes/MaxReadBytes/MaxSplitParts는 작업별 uint64이며 0은 추가 제한 없음입니다. worker가 설정을 상속하고 네이티브 처리는 현재 예산과 취소를 블록 단위로 검사합니다. reverse/rotate/shuffle은 중단 후 원소 다중집합을 보존합니다. 관찰 경로는 clone 없이 준비한 순열을 한 번 게시합니다. shuffle 대상과 난수 엔진의 동일성은 거부하고 소비된 난수는 복원하지 않습니다. 독립 AOT는 Core 서비스가 필요한 연산을 출력 전에 명확히 거부하며 사용하지 않은 import는 허용합니다.

[문자열 모듈](string.md#integer-formatting)을 참조하세요.

setenv(name, "")는 존재하는 빈 값을 설정합니다. 삭제는 unsetenv이며 없는 변수 삭제도 성공합니다. try_getenv는 없음과 빈 값을 구분합니다. 이름은 비어 있거나 =/NUL을 포함할 수 없고 값은 NUL을 포함할 수 없습니다. 이름 대소문자는 Windows/POSIX 규칙을 따릅니다.

## API

```text
// string.vs
T std::strings::parse<T>(string text);
T std::strings::parse<T>(string text, int base);
bool std::strings::try_parse<T>(string text, ref T output);
bool std::strings::try_parse<T>(string text, ref T output, int base);
std::memory::buffer std::strings::encode_utf8(string text);
string std::strings::decode_utf8(region bytes);
bool std::strings::is_valid_utf8(string text);
bool std::strings::is_valid_utf8(region bytes);
// io.vs
optional<string> std::io::try_read_line();
optional<string> std::io::try_read_token();
std::memory::buffer std::io::read_bytes(string path);
std::memory::buffer std::io::read_bytes(string path, int64 max_bytes);
void std::io::write_bytes(string path, region source);
void std::io::append_bytes(string path, region source);
// path.vs: each query has both one- and two-argument overloads
bool std::filesystem::is_absolute(string path);
bool std::filesystem::is_absolute(string path, std::filesystem::path_style style);
string std::filesystem::root_path(string path);
string std::filesystem::root_path(string path, std::filesystem::path_style style);
string std::filesystem::lexically_normal(string path);
string std::filesystem::lexically_normal(string path, std::filesystem::path_style style);
// path_style: native, posix, windows
// numeric.vs / math.vs: R explicit, T inferred
R std::numeric::accumulate_as<R, T>(array<T> values, R initial);
R std::numeric::sum_as<R, T>(array<T> values);
T std::clamp<T>(T value, T low, T high);
// runtime-provided
optional<string> std::environment::try_getenv(string name);
void std::environment::unsetenv(string name);
```

## SDK

```text
C++: FEngineConfig.StandardLibraryLimits;
     VeroScriptSession::SetStandardLibraryLimits(FStandardLibraryLimits);
     VeroScriptVm::SetStandardLibraryLimits(FStandardLibraryLimits);
C:   vs_interop_session_set_stdlib_limits(session, &limits);
     vs_interop_stdlib_limits { struct_size, reserved=0, max_text_bytes,
                                max_read_bytes, max_split_parts };
C#:  VeroScriptSession.SetStandardLibraryLimits(VeroScriptStandardLibraryLimits);
Python: Session.set_stdlib_limits(max_text_bytes=0, max_read_bytes=0, max_split_parts=0);
WebAssembly: runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts);
```

[동시성 모듈 — 작업 취소 요청](concurrent.md#concurrency-aot-boundary)을 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/foundation.md) · [fr-FR](../../fr-FR/standard-library/foundation.md) · [ja-JP](../../ja-JP/standard-library/foundation.md) · [ko-KR](foundation.md) · [zh-CN](../../zh-CN/standard-library/foundation.md) · [zh-TW](../../zh-TW/standard-library/foundation.md)
