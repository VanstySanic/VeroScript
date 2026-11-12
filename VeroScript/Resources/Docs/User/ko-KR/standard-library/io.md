# 입출력 모듈

모듈 파일: `io.vs`

다음을 사용하여 가져옵니다.

```VeroScript
import "io.vs";
```

## 파일 및 디렉터리

| 기능 | 설명 |
| --- | --- |
| `std::io::read_file(string path)` | 완전한 텍스트 파일을 읽습니다. |
| `std::io::write_file(string path, string content)` | 텍스트 파일을 덮어씁니다. |
| `std::io::append_file(string path, string content)` | 누락된 경우 텍스트를 추가하고 파일을 생성합니다. |
| `std::io::read_bytes(string path)` | 전체 파일을 소유 바이트 버퍼로 읽습니다. |
| `std::io::read_bytes(string path, int64 max_bytes)` | 명시한 최대 바이트 수로 읽고 초과하면 오류를 냅니다. |
| `std::io::write_bytes(string path, region source)` | 버퍼 또는 뷰의 바이트로 덮어씁니다. |
| `std::io::append_bytes(string path, region source)` | 버퍼 또는 뷰의 바이트를 덧붙입니다. |
| `std::filesystem::exists(string path)` | 경로가 존재하는지 테스트합니다. |
| `std::filesystem::delete_file(string path)` | 파일을 삭제합니다. |
| `std::filesystem::is_file(string path)` | 일반 파일인지 확인합니다. |
| `std::filesystem::is_directory(string path)` | 디렉터리인지 확인합니다. |
| `std::filesystem::remove_dir(string path)` | 빈 디렉터리 하나를 삭제합니다. |
| `std::filesystem::list_dir(string path)` | 디렉토리 항목 이름의 배열을 반환합니다. |
| `std::filesystem::mkdir(string path)` | 디렉터리와 해당 상위 항목을 만듭니다. |
| `std::filesystem::cwd()` | 현재 작업 디렉터리를 반환합니다. |
| `std::filesystem::chdir(string path)` | 현재 작업 디렉터리를 변경합니다. |

`std::filesystem::exists`는 경로가 없음을 확인한 경우에만 `false`를 반환하며 검사 오류는 보고합니다. `std::filesystem::delete_file`은 파일과 심볼릭 링크를 삭제할 수 있지만 실제 디렉터리는 거부합니다. `std::filesystem::chdir`은 호스트 프로세스 전체의 작업 디렉터리를 바꾸므로 다른 세션에도 영향을 줍니다. 환경 쓰기와 프로세스 디렉터리 변경 권한이 모두 필요합니다. C++ 호스트는 `FScriptCapabilities::bAllowProcessDirectoryChange`, C·C#·Python 호스트는 스크립트 권한 비트 `PROCESS_DIRECTORY_CHANGE`를 사용합니다.

액세스 가능한 파일 시스템 범위는 호스트 권한과 현재 작업 디렉터리에 따라 다릅니다. 읽기, 쓰기, 삭제, 디렉터리 생성 또는 디렉터리 변경 실패가 런타임 시 보고됩니다.

`std::filesystem::is_file`과 `std::filesystem::is_directory`는 심볼릭 링크를 따라가며 경로가 없으면 `false`를 반환합니다. 파일 읽기 권한이 필요하고 다른 검사 오류는 보고됩니다. `std::filesystem::remove_dir`에는 파일 삭제 권한이 필요합니다. 경로가 없으면 `false`, 파일·비어 있지 않은 디렉터리·디렉터리 심볼릭 링크이면 오류입니다. 재귀적으로 삭제하지 않습니다.

## 표준 입력

| 기능 | 설명 |
| --- | --- |
| `std::io::read_line()` | 후행 줄 바꿈 없이 한 줄을 읽습니다. EOF에서 빈 문자열을 반환합니다. |
| `std::io::try_read_line()` | 정상 EOF에서만 빈 optional을 반환합니다. |
| `std::io::try_read_token()` | 다음 토큰 또는 정상 EOF에서 빈 optional을 반환합니다. |
| `std::io::pause()` | 하나의 입력 라인을 기다립니다. 비대화형 호스트에서 EOF 시 즉시 반환됩니다. |
| `std::io::read_token()` | 공백으로 구분된 다음 토큰을 읽습니다. |
| `std::io::read_string()` | 문자열 토큰을 읽습니다. |
| `std::io::read_int()` | 정수 토큰을 읽습니다. |
| `std::io::read_double()` | 부동 소수점 토큰을 읽습니다. |
| `std::io::read_bool()` | 부울 토큰을 읽습니다. |

토큰 입력 함수는 공백으로 구분된 값을 사용합니다. 전체 줄이 중요한 경우에는 `std::io::read_line`을 사용하고 값을 개별적으로 읽어야 하는 경우에는 입력된 `read_*` 함수를 사용합니다.


- [표준 라이브러리 기반](foundation.md)

`region`은 buffer, view, readonly_view 오버로드를 뜻합니다. 파일 읽기는 빈 디렉터리도 거부합니다. 바이트 I/O 제한, 명시적 최대값 0과 EOF 계약은 [기반 참조](foundation.md#io-contract)를 따릅니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/io.md) · [fr-FR](../../fr-FR/standard-library/io.md) · [ja-JP](../../ja-JP/standard-library/io.md) · [ko-KR](io.md) · [zh-CN](../../zh-CN/standard-library/io.md) · [zh-TW](../../zh-TW/standard-library/io.md)
