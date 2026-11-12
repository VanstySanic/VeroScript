# C++ AOT emitter

`VeroScriptCppAot`는 compile-checked VeroScript program에서 C++를 emit합니다. public API는 source string, file, program, 단일 generated source string과 relative file 및 JSON manifest를 포함하는 `FCppAotSourceBundle` result를 지원합니다.

## bundle 불변 조건

생성 bundle은 의도적으로 하나의 C++ translation unit와 generated `.inl` fragment를 사용합니다. textual output을 독립적으로 link하는 module로 나누면 template instantiation, internal linkage, global initialization ordering이 달라집니다. 따라서 bundle layout은 선택 가능한 formatting이 아니라 output contract입니다.

## 대상과 runtime 지원

emitter는 entry module을 validate하고 entry function `main`의 `void main()`, `int main()`, 또는 대응하는 `string[] args` form만 허용합니다. program이 사용하는 feature에 대해서만 value, container, channel, worker task compatibility support를 emit합니다. AOT worker의 global/static field는 native thread별로 분리되고 channel은 명시적인 synchronized sharing capability입니다.

문자열로 지정한 task entry, 협력적 `std::concurrent::cancel_task()` / `std::concurrent::cancel_requested()`, filesystem 연산 `std::filesystem::is_file()`, `std::filesystem::is_directory()`, `std::filesystem::remove_dir()`를 지원합니다. callable task entry와 실행 가능한 `field_info<T>` / `method_info<T>` reflection은 AOT 진단과 함께 거부합니다.

numeric, ownership, exception, destruction, module 또는 concurrency semantics를 바꾸게 되는 lowering은 program을 거부해야 합니다. compile, eligibility check, lowering step, output write가 실패한 뒤 partial source를 반환해서는 안 됩니다.

생성된 `array<bool>`은 인덱싱과 참조 인수에 실제 `bool&`를 제공하도록 각 요소를 주소 지정 가능한 독립 bool 슬롯에 저장합니다. 비트 압축을 사용하지 않으므로 같은 용량의 `std::vector<bool>`보다 메모리를 더 사용합니다.

## 유지보수

public emitter header, emitter implementation, generated-source test, bundle test, generated-file validation path가 authoritative합니다. 새 runtime helper는 대응하는 VeroScript behavior를 test로 입증하고 helper가 generated bundle 안에서 self-contained일 때만 추가합니다.

[C++ AOT — 사용자 pair 템플릿](../../User/ko-KR/aot.md#cleanup-container-operations)을 참조하세요.

독립 소스의 십진 변환을 `Shared/Private/VeroScriptFloatParsing.h`와 맞추세요. `VeroScriptFloatParsingSource.h`, `MakeRuntimeSupport`, 생성된 `VsFloatParsingDetail`이 독립 출력 경로를 구성합니다. 공유 입력을 이동하면 CMake 읽기 경로와 configure 의존성을 함께 갱신하고 생성 코드 사용자가 비공개 소스 헤더를 필요로 하지 않도록 하세요. 대상 폭, 로캘과 범위는 Core AOT 실행과 별도로 검증합니다.

<a id="standalone-file-paths"></a>

생성한 `VsPathFromUtf8`는 자체 완결성을 유지합니다. Windows의 char8_t와 C++17 u8path 분기 모두 경로를 만들고 네이티브 구분자로 바꾸며 네이티브 UTF-16 단위 248개 이상의 절대 경로에만 확장 접두사를 붙입니다. 완전한 UNC는 UNC 확장 형식을 사용하고 기존 접두사는 유지하며 상대 경로를 절대화하지 않습니다. 변환 전에 내장 NUL을 거부하세요. Unicode, 긴 절대/UNC 경로, 상대 경로와 두 언어 표준 분기의 생성 및 회귀를 함께 유지하고 생성물 소비자에게 비공개 소스 헤더를 요구하지 마세요.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
