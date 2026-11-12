# VeroScript 설치 및 사용

운영 체제와 아키텍처에 맞는 패키지를 선택하세요. Runtime은 스크립트를 실행하며 CLI, LSP, DAP를 포함합니다. SDK는 호스트 애플리케이션에 VeroScript를 임베딩할 때 사용합니다. 설치 관리자는 선택한 구성 요소를 설치하고, 포터블 아카이브는 원하는 디렉터리에 압축 해제할 수 있습니다.

## 제품 선택

| 제품 | 용도 |
|---|---|
| Runtime | 스크립트 실행, CLI/LSP/DAP 및 표준 라이브러리 |
| Core SDK | Core·Tooling C++ 헤더, 라이브러리 및 CMake 패키지 |
| Interop SDK | 네이티브 및 다른 언어 호스트용 C ABI |
| C++ SDK | C++ 어댑터. 동일 빌드의 Core SDK 필요 |
| C# SDK | .NET 어댑터 및 일치하는 네이티브 Interop 런타임 |
| Python SDK | CPython 어댑터 및 Python 도구 |
| WebAssembly SDK | JavaScript 호스트용 WebAssembly 런타임 및 TypeScript API |

Runtime은 SDK를 설치하지 않습니다. 설치 관리자는 패키지에 포함된 도구, AOT와 심볼을 제공합니다. 문서와 예제는 Resources에서 별도로 받으십시오. SDK의 기본 경로는 버전별로 나뉘어 병렬 설치가 가능합니다. 제품 버전이 다를 수 있으므로 각 SDK의 의존성과 ABI 요구 사항을 따르십시오.

`Release/<business-route>/<target>/<version>/`에서 데스크톱 설치 관리자는 `installers`, 포터블 Runtime 또는 모듈 아카이브는 `archives`, 개발 SDK는 `sdks`, 생태계 패키지와 편집기 확장은 `packages`, 디버그 심볼은 `symbols`에서 받습니다. 공통 배포는 `any`, 포터블 WebAssembly SDK는 `wasm32`를 사용합니다. 선택적 Modules는 별도로 받으며 독립 설치 관리자는 제공하지 않습니다.

## 설치, 업데이트, 이전

Windows에서는 `.exe` 설치 관리자를 실행합니다. Linux에서는 `.tar.gz`를 풀고 안의 프로그램을 실행하며, macOS에서는 `.dmg`를 열고 안의 `.app`을 실행합니다. 마법사에서 기능 구성 요소 선택, 설치 위치 선택, 표시되는 경우 파일 연결 설정, 내용 확인 순으로 진행한 뒤 설치합니다. 다음과 이전 버튼으로 페이지를 이동하며, 구성 요소가 많으면 여러 페이지로 나누어 창 전체를 스크롤할 필요가 없습니다. 문서와 예제는 설치 옵션으로 제공하지 않습니다. 설치 관리자는 자체 포함형이므로 대상 PC에 별도 .NET Runtime이 필요하지 않습니다. Runtime PATH 통합은 기본적으로 꺼져 있으며, 켠 뒤에는 새 터미널을 여십시오.

설치 관리자는 현재 구성 요소 대상 디렉터리에서만 같은 제품의 유효한 기록을 찾습니다. 일치하는 설치를 찾으면 업데이트 모드로 들어가며, 같은 제품 버전을 다시 패키징한 경우에도 적용됩니다. 업데이트는 사용자가 만든 파일을 보존합니다. 새 패키지 파일이 사용자 파일과 충돌하면 실패하므로 충돌 파일을 옮긴 뒤 다시 시도하십시오. 설치된 선택적 구성 요소를 선택 해제하면 관리 파일만 제거하고 사용자 파일은 보존합니다. PATH 옵션을 끄면 해당 설치 기록이 소유한 항목만 제거합니다.

기존 설치를 감지하면 아직 직접 변경하지 않은 구성 요소, PATH 및 지원되는 파일 연결 선택에만 레코드의 기본값을 적용합니다. 업데이트와 제거 확인 화면에는 설치가 소유한 PATH 항목을 제거하는 경우 그 작업을 명시합니다.

설치 루트는 마법사에서 선택한 상위 디렉터리이며 `SDK/Core/<version>` 같은 구성 요소 디렉터리가 아닙니다. 기본 제품의 구성 요소 디렉터리에는 버전이 포함되므로 같은 루트에서도 다른 제품 버전을 함께 설치할 수 있습니다. 설치 관리자는 다른 버전을 자동 검색하거나 이전하거나 제거하지 않습니다. 이전 버전을 교체하려면 새 버전을 설치하고 확인한 뒤 이전 버전의 원래 설치 관리자와 기록으로 이전 버전을 제거하십시오. 설치 루트를 바꾸려면 새 루트에 설치하고 자신의 파일을 복사한 뒤 원래 기록으로 이전 설치를 제거하십시오. 관리되는 구성 요소 디렉터리를 직접 옮기지 마십시오.

## Windows 네이티브 런타임 파일

Windows 패키지는 네이티브 프로그램과 라이브러리 옆에 일치하는 Visual C++ 런타임 DLL을 포함합니다. 아카이브 전체를 풀거나 설치 관리자를 사용하고, 배포할 때 이 DLL과 `licenses/MicrosoftVisualCpp/NOTICE.txt`를 해당 프로그램 또는 네이티브 라이브러리와 함께 유지하세요. 네이티브 프로그램 실행 파일, Interop DLL 또는 Python 확장만 복사하면 필요한 종속 파일이 누락될 수 있습니다. 운영 체제는 여전히 Universal CRT를 제공해야 합니다.

## 복구 및 제거

각 설치 구성 요소 루트에는 `.veroscript-install.json`이 있습니다. 복구에는 원래 제품 패키지가 필요하며 버전, 플랫폼, 아키텍처, 매니페스트 지문이 레코드와 정확히 일치해야 합니다. 따라서 원래 설치 관리자를 보관하십시오. 제거는 내장 payload를 다시 읽지 않으며 복구와 제거 모드는 함께 지정할 수 없습니다.

아래 작업 중 하나를 선택하세요. 설치 관리자와 레코드 경로를 실제 경로로 바꾸세요. 각 명령은 해당 창을 열고 확인 후 실행됩니다.

복구만 실행:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --repair-record $record
```

제거만 실행:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --uninstall-record $record
```

제거는 관리 파일과 이 설치가 여전히 소유한 PATH 항목, 연결 및 아이콘 등록 값만 제거하고 사용자 파일이 있는 디렉터리는 남깁니다. 중단된 작업 뒤에는 레코드를 읽거나 복구/제거할 때 먼저 트랜잭션을 복구합니다. 복구를 우회하려고 `.veroscript-transaction-*.json`, staging 또는 backup 디렉터리를 수동 삭제하지 마십시오.

## CMake에서 SDK 사용

압축을 푼 Core SDK 루트를 `CMAKE_PREFIX_PATH`에 추가합니다.

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScript)
```

Core SDK는 `VeroScript::VeroScript`와 `VeroScript::VeroScriptTooling`을 제공합니다. 가져온 대상은 공개 헤더의 컴파일 요구 사항을 전파합니다. JIT 백엔드는 별도 모듈이며 Core는 `VeroScript::asmjit`를 내보내지 않습니다. C++/AOT 라이브러리는 별도 C++ 제품입니다. 설치 관리자 배포에서는 `<install-root>/SDK/Core/<version>`을 `CMAKE_PREFIX_PATH`에 추가합니다. `<install-root>`는 마법사에서 선택한 설치 루트입니다. 예제는 별도로 받으십시오. 현재 포터블 SDK나 설치 프로그램에 포함되지 않습니다.

C++ SDK에는 일치하는 Core SDK와 `Runtime` 디렉터리가 필요합니다.

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScriptCpp)
```

`VeroScriptCppConfig.cmake`는 C++ 패키지 빌드 시 기록된 Core SDK 버전을 정확히 요구하고 Core 라이브러리 SHA-256 빌드 식별자를 검증합니다. Core 버전과 C++ 제품 버전은 다를 수 있습니다. C++ 릴리스 매니페스트에 기록된 Core 의존성을 사용하세요. 버전 번호만 같다고 빌드가 호환되는 것은 아닙니다. 포터블 패키지는 `<cpp-package>/Runtime`, 기본 설치는 `<install-root>/SDK/Cpp/<version>/Runtime`을 추가합니다. 해당 구성 요소가 설치되어 있으면 `COMPONENTS Tools Aot`를 요청하세요.

Core SDK 도구는 `bin`을 기준으로 `assets/stdlib`과 `assets/localization`을 찾습니다. Windows Core SDK 루트에서 별도로 받은 Resources 배포본의 예제를 실행할 수 있습니다. 절대 경로를 실제 위치로 바꾸십시오.

```powershell
.\bin\veroscript.exe --bc-cache=off 'D:\Packages\Resources\Scripts\examples\standard_library_algorithms.vs'
```

## 오프라인 문서 및 예제

일치하는 Resources 배포본을 별도로 받으십시오. `Resources/Docs/README.md`를 열고 언어를 선택한 뒤 상대 링크로 레퍼런스와 튜토리얼을 읽으십시오. 독립 문서 ZIP은 압축을 푼 패키지 안의 `docs/README.md`를 사용합니다. Resources는 `Resources/Scripts/examples`에 스크립트 예제도 제공합니다. 현재 SDK 압축 파일과 설치 프로그램에는 문서/예제 구성 요소가 포함되지 않습니다. 온라인 소스 참조에는 네트워크가 필요하며 예제에는 일치하는 SDK와 의존성이 필요할 수 있습니다.

## 설치 관리자 언어

설치 관리자는 영어, 프랑스어, 일본어, 한국어, 중국어 간체·번체를 제공합니다. 창에서 언어를 선택하거나 시작할 때 `--language`를 지정하세요. 기본적으로 시스템 UI 언어를 따르며 일치하는 지원 언어가 없으면 영어를 사용합니다. 이 설정은 설치 관리자 UI에만 적용됩니다. 스크립트 진단에는 CLI `--lang` 또는 SDK 언어 API를 별도로 사용하세요. 아래 Windows 예제의 경로와 버전은 다운로드한 파일에 맞게 바꾸세요.

```powershell
& 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe' --language ko-KR
```

## Linux/macOS 설치

Linux에서는 맞는 `linux-x64` 또는 `linux-arm64`의 `.tar.gz` 설치 관리자를 풀고 안의 프로그램을 실행합니다. macOS에서는 맞는 `macos-x64` 또는 `macos-arm64`의 `.dmg`를 열고 안의 설치 관리자 `.app`을 실행합니다. 그래픽 데스크톱이 필요합니다. 복구와 제거는 Windows와 같은 기록 옵션을 받습니다. 아래 예제는 Linux용입니다. macOS에서는 `installer`를 원래 설치 관리자의 `<installer>.app/Contents/MacOS/VeroScriptInstaller`로 지정하고 읽기 전용 이미지의 앱에서는 `chmod`를 생략합니다. 예제 경로는 실제 경로로 바꾸십시오. 포터블 Runtime/SDK 아카이브를 풀 때도 실행 권한을 보존하십시오.

아래 작업 중 하나를 선택하세요. 설치 관리자와 레코드 경로를 실제 경로로 바꾸세요. 각 명령은 해당 창을 열고 확인 후 실행됩니다.

복구만 실행:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --repair-record "$record"
```

제거만 실행:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --uninstall-record "$record"
```

## 라이선스 고지

프로젝트의 전체 [MIT 라이선스](../../../../../LICENSE)는 이 문서에 포함됩니다. 의존성 고지는 다운로드한 설치 프로그램에서 확인할 수 있습니다. 각 고지 옵션을 단독으로 실행하십시오. 원문을 출력하고 설치 창을 열지 않은 채 종료합니다. 예제 경로를 실제 다운로드한 파일로 바꾸십시오. 고지는 원래 언어를 유지하며 `--language`는 설치 화면의 언어를 별도로 선택합니다.

이 예제는 Windows PowerShell 5.1에서 고지 문자가 올바르게 표시되도록 일시적으로 UTF-8을 사용한 뒤 터미널의 원래 인코딩을 복원합니다.

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$noticeEncoding = [Console]::OutputEncoding
try {
	[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
	& $installer --license | Out-Host
	& $installer --third-party-licenses | Out-Host
}
finally
{
	[Console]::OutputEncoding = $noticeEncoding
}
```

<a id="file-associations"></a>

## Windows 파일 연결

Windows 설치 패키지에 Runtime과 해당 아이콘이 있으면 Runtime 선택 시 파일 연결 옵션을 사용할 수 있습니다. 새 설치에서는 꺼져 있습니다. `.vs`와 `.vsapp`을 해당 Runtime으로 실행하도록 등록하거나 `.vspkg` 아이콘만 선택할 수 있습니다. 연결된 `.vs`나 `.vsapp`을 열면 파일을 실행합니다. `.vspkg` 옵션은 패키지를 실행하거나 설치하지 않습니다.

등록은 현재 사용자에게 실행 처리기와 아이콘을 추가합니다. 기본 앱은 Windows 설정에서 직접 선택하세요. 처리기를 등록해도 시스템 기본 앱은 지정되지 않습니다. 선택한 설정 작업은 설치 성공 후 해당 페이지 열기를 요청하며, 열기 실패나 불완전한 등록은 결과 페이지에 표시합니다. Windows는 선택한 기본 앱의 아이콘을 표시할 수 있습니다.

업데이트는 직접 변경하지 않은 연결 항목에 기존 기록의 설정을 사용합니다. 옵션 해제, 복구, 롤백과 제거는 다른 등록 및 이후 사용자 변경을 보존하고 이 설치가 소유한 변경되지 않은 값만 제거하거나 복원합니다. 다른 플랫폼이나 필요한 Runtime 파일이 없는 패키지에서는 이 옵션을 제공하지 않습니다.

`.vspkg` 아이콘은 기존 아이콘 등록이 없을 때만 추가합니다. 기본 앱 옵션을 해제해도 이번 작업의 설정 열기 요청만 취소하며, Windows에서 확인한 선택을 초기화하지 않습니다. 제거 시 보호된 기본 앱 기록을 수정하거나 이전 기본 앱의 자동 복원을 보장하지 않습니다. 연결 소유 정보가 없는 이전 설치 기록으로는 연결을 정리하지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
