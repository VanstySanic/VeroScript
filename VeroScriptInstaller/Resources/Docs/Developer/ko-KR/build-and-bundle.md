# 설치 관리자 빌드 및 번들 계약

## 빌드 대상

App과 Tool은 .NET 9를 대상으로 하며 `VeroScriptInstaller.Core`를 사용합니다. App은 언어 카탈로그를 리소스로 포함하고 번들을 단일 실행 파일의 자체 추출 콘텐츠로 포함합니다. Tool은 GUI 없이 번들 아카이브를 만들고 검증합니다.

`VeroScriptInstaller/global.json`은 .NET SDK `9.0.100`과 `latestFeature` 롤포워드를 선택합니다. 예제는 저장소 루트에서 시작하며 `Push-Location`으로 Installer 디렉터리에서 SDK를 결정합니다. .NET 10 모바일 워크로드가 있어도 .NET 9 SDK를 설치해 두세요. C# 모바일 도구 체인은 별도입니다.

```powershell
Push-Location VeroScriptInstaller
try
{
	dotnet build App/VeroScriptInstaller.csproj -c Release -warnaserror
	dotnet build Tool/VeroScriptInstaller.Tool.csproj -c Release -warnaserror
	dotnet run --project Tests/VeroScriptInstaller.Tests.csproj -c Release
}
finally
{
	Pop-Location
}
```

Tests는 회귀 테스트 실행 파일입니다. `dotnet run`을 사용하고 `dotnet test`의 검색 결과를 전제하지 마세요. App 빌드만으로는 배포 페이로드가 포함되지 않으므로 배포 스크립트를 사용하세요.

## 전체 Windows 배포

전체 파이프라인은 Release의 `win-x64` 및 `win-arm64`를 지원합니다. CMake, MSVC, 전체 제품용 .NET 8/9 SDK, Node.js/npm 및 WebAssembly 재빌드용 Emscripten이 필요합니다. `Extensions/Platform/WebAssembly/VeroScriptWebAssembly`에서 `npm ci`를 실행하세요. 브라우저 테스트에는 Playwright Chromium 같은 사용 가능한 브라우저도 필요합니다.

```powershell
.\Resources\Tools\Packaging\package-veroscript-all.ps1 -ConfigurePreset windows-msvc-vs2026 -Runtime win-x64
```

Windows ARM64에서는 `-Runtime win-arm64`와 `-ConfigurePreset windows-msvc-vs2026-arm64` 같은 일치하는 프리셋을 지정하고 MSVC ARM64 도구와 Windows SDK를 설치하세요. 전체 게시에는 Python Wheel 매트릭스를 포함한 네이티브 실행 검증이 있으므로 필요한 모든 ARM64 CPython 인터프리터가 설치된 네이티브 Windows ARM64 호스트를 사용해야 합니다. x64에서 네이티브 결과물만 교차 컴파일하면 이러한 검증을 완료할 수 없습니다. `win-arm64`는 네이티브 ARM64를 뜻하며 ARM64EC나 에뮬레이션된 x64를 포함하지 않습니다.

Visual Studio에 맞는 프리셋을 선택하거나 생략하여 자동 선택하세요. 기본 출력은 `Release`입니다. `-SkipBuild`는 기존 네이티브·Web 출력을 재사용하지만 관리 앱 게시와 준비된 WebAssembly SDK 검증은 수행합니다. 기존 출력의 소스와 구성이 확인된 경우에만 사용하세요. 결과물은 `Release/<business-route>/<target>/<version>/`에 두고 각 버전에 필요한 `archives/`, `sdks/`, `packages/`, `installers/`, `symbols/`를 만듭니다. 공유 배포는 `any`, WebAssembly SDK는 `wasm32`를 사용합니다. 매니페스트, 체크섬 및 색인으로 계속 관리합니다. 제품 버전은 독립적입니다. Core/C++ 의존성, 잠금, 버전 정리 및 롤백은 [배포 규칙](release-layout.md)을 참조하세요.

## Windows 네이티브 런타임 의존성

MSVC Release 패키징은 `vs_install_windows_runtime`을 통해 선택된 도구 체인의 재배포 가능 DLL을 설치합니다. `VeroScriptWindowsRuntime` 컴포넌트는 Runtime 루트에 배치하며 Interop, C++ 도구 및 Python은 각각의 설치 컴포넌트를 사용합니다. 패키지 조립 시 SDK 및 관리 런타임 안의 네이티브 진입점 옆에도 DLL을 배치합니다. 시스템 디렉터리에서 복사하지 말고 도구 체인의 재배포 파일을 사용하세요. 디버그 CRT와 Universal CRT는 포함하지 않습니다.

배포 검증은 네이티브 아키텍처를 확인하며 각 네이티브 진입점 옆에 `msvcp140.dll`, `vcruntime140.dll`, x64 대상에서는 추가로 `vcruntime140_1.dll`을 요구합니다. ARM64는 네이티브 CRT를 사용하며 ARM64 재배포 구성 요소에 포함된 x64용 `vcruntime140_1.dll`을 제외합니다. 같은 디렉터리에 `licenses/MicrosoftVisualCpp/NOTICE.txt`도 필요합니다. 원본 `Resources/Tools/CMake/WindowsRuntimeNotice.txt`를 별도의 제삼자 고지로 유지하세요. 프로젝트 MIT 라이선스로 대체하지 마세요. 의존성 누락은 게시를 실패시키고 MSVC 재배포 파일 누락은 Windows Wheel 구성을 실패시킵니다. Universal CRT는 운영 체제의 사전 요구 사항입니다.

## Python 게시 사전 요구 사항

전체 배포에서 Python을 선택하면 파이프라인 실행 전에 CPython 3.11–3.15 구성을 준비하세요. `-SkipBuild` / `--skip-build`에서도 Wheel과 소스 배포본을 빌드하고 검증합니다. 인터프리터 선택, 사전 검사 및 Wheel별 ABI와 SDK의 단일 네이티브 ABI 차이는 [Python 매트릭스 계약](../../../../../Extensions/Language/Python/Resources/Docs/Developer/ko-KR/toolchain-and-packaging.md)을 참조하세요.

## 번들 계약

매니페스트는 제품 ID, 플랫폼, 아키텍처, 선택 구성 요소, 상대 경로 및 해시를 정의합니다. 설치·복구는 현재 소스 트리가 아니라 번들을 사용합니다. 확장 번들에는 `manifest.json`과 `payload/`가 있으며 포터블 Runtime/SDK 아카이브와 다릅니다.

```powershell
Push-Location VeroScriptInstaller
try
{
	$tool = 'Tool/VeroScriptInstaller.Tool.csproj'
	dotnet run --project $tool -c Release -- build-bundle --output ../.tmp/runtime-installer-bundle --version 0.1.1 --profile runtime --source-runtime D:/Packages/Runtime
	dotnet run --project $tool -c Release -- verify --bundle ../.tmp/runtime-installer-bundle
	dotnet run --project $tool -c Release -- pack-bundle --bundle ../.tmp/runtime-installer-bundle --output ../.tmp/runtime-installer-bundle.zip
}
finally
{
	Pop-Location
}
```

`D:/Packages/Runtime`은 CLI/LSP/DAP, 표준 라이브러리 및 지역화 리소스를 포함한 준비된 페이로드 예시입니다. 프로필에 맞는 `--source-*` 옵션을 사용하세요. [제품 정의](../../../Examples/Products/README.md) 및 [자동화](automation.md)에서 Tool 설치, 이전, 복구 및 제거를 설명합니다.

## 단일 파일 번들 전송 형식

`App/InstallerBundleContent.targets`는 `InstallerBundleArchive`를 각각 최대 1 GiB인 콘텐츠 조각 최대 32개로 나누며 `VeroScriptInstaller.EmbeddedBundle.zip.0000`부터 번호를 붙입니다. 단일 파일 게시 도구가 `IncludeAllContentForSelfExtract`로 포함하므로 별도 다운로드 파일이 아닙니다. 어셈블리 메타데이터에 전체 ZIP의 `VeroScriptInstaller.BundleSha256`과 `VeroScriptInstaller.BundlePartCount`를 기록합니다.

`EmbeddedInstallerBundle`은 추출된 조각을 열고 링크 및 잘못된 개수/크기를 거부한 뒤 전체 ZIP SHA-256을 확인합니다. 전체 아카이브를 메모리에 담거나 다른 ZIP으로 복사하지 않고 탐색 가능한 스트림을 제공합니다. 번들 압축 해제 위치는 Linux에서 `AppContext.BaseDirectory/VeroScriptInstaller.Unpack`, 그 외에는 `Path.GetTempPath()/VeroScriptInstaller`입니다. 추출과 매니페스트 검증도 유지합니다. 게시 대상은 단일 실행 파일의 누락이나 빈 파일을 거부하지만 이 검사만으로 대용량 설치 프로그램의 대상 검증을 통과한 것은 아닙니다.

실패하면 보통 소유한 배포 준비 디렉터리를 삭제합니다. Windows 전체 게시의 `-KeepFailedStage` 또는 Unix 전체 게시의 `--keep-failed-stage`로 실패 디렉터리를 진단용으로 보존하고 경로를 출력합니다. 성공한 준비 디렉터리는 삭제합니다. 보존된 파일은 후보이며 재사용 전 소스, 구성과 해시를 다시 확인합니다.

## 검증 및 서명

내장·외부 번들에 동일한 매니페스트, 경로, 해시, 구성 요소 및 플랫폼 검증을 적용합니다. 아카이브 구조, 지문 또는 설치 레코드 스키마를 바꾸면 복구, 이전, 제거 및 패키지 테스트를 함께 갱신하세요. 구성 요소에는 현재 API와 바이너리가 있어야 합니다. Core 헤더, C++ 세션과 CMake 패키지, Python 확장, 타입 정보 및 일치하는 Interop DLL이 대상입니다. 최종 검증은 세션 캐시 API, 개별·NuGet C# 어셈블리, 심볼 패키지, 두 WebAssembly SDK 및 선언도 확인합니다.

파이프라인은 생성된 설치 관리자에서 `--verify-localization`과 `--verify-embedded-bundle`을 실행하며 서명 후에도 확인합니다. 종료 코드를 확인하세요. GUI 수용 테스트를 대신하지 않습니다. 언어 검증은 여섯 카탈로그, 여덟 프로필, 구성 요소 키, 비어 있지 않은 값, 형식 인수, 부분 외부 오버레이, 중국어 문자 체계 및 언어 전환을 확인합니다. 내장 번역 누락을 영어 대체로 숨기면 안 됩니다.

지역화 키는 매니페스트 지문의 일부입니다. 번들과 앱을 함께 재빌드하고 복구용 원래 설치 관리자를 보관하세요. 버전이 같아도 지문이 다른 재빌드는 복구에서 거부되고 일반 업데이트에서만 사용할 수 있습니다.

서명은 선택 사항입니다. 필수로 하려면 `VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT`와 `VEROSCRIPT_TIMESTAMP_SERVER`를 설정하고 전체 파이프라인에 `-RequireSignedInstallers`, 단일 제품에 `-RequireSignature`를 지정하세요. 최종 체크섬은 서명 후 생성됩니다. 별도 최종 검증에도 `-RequireSignedInstallers`를 지정하여 모든 설치 관리자의 Authenticode 상태가 `Valid`인지 확인하세요.

## Linux/macOS 배포

전체 릴리스와 개별 구성 요소는 [제품별 게시 규칙](release-layout.md)을 따릅니다. 네이티브 Runtime/SDK 아카이브는 실행 권한을 보존하는 `.tar.gz`, WebAssembly는 ZIP을 사용합니다. Linux 설치 관리자는 `.tar.gz`, macOS 설치 관리자는 표준 `.app`을 포함하는 `.dmg`를 사용합니다. 설치 관리자는 자체 포함형입니다. Linux는 절대 경로 `XDG_DATA_HOME`을 따릅니다. Python ABI와 Linux 호환성은 기록한 빌드 환경에 따라 달라집니다.

```bash
bash Resources/Tools/Packaging/package-veroscript-all.sh --target linux-x64
python3 Resources/Tools/Packaging/verify-veroscript-release.py \
  --release-directory Release --target linux-x64
```

`target`: `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`.

`win-x86`를 포함한 데스크톱 교차 대상 조립은 `Resources/Tools/Packaging/package-veroscript-desktop-cross.py --help`를 확인하고 실제 대상 빌드와 일치하는 종속성을 입력합니다. 빌드/아카이브 검증과 대상 실행 검증은 별도로 기록합니다.


## 문서 패키지와 통합 보기

[문서 유지보수 — 검증과 출판](../../../../../Resources/Docs/Developer/ko-KR/index.md#integrated-publication-views)을 참조하세요.

전체 릴리스는 Resources를 통해 User 보기를 `Resources/Docs/`에 게시하며 `Resources/Scripts`, `Resources/Assets`를 나란히 배치합니다. 현재 Runtime/SDK/확장 압축 파일과 설치 프로그램에는 작성된 문서와 예제를 포함하지 않습니다. 페이로드 정책은 Installer Developer의 `release-layout.md`를 참조하십시오.

공통 안내, Overview, Standards, Tutorials는 Resources/Docs에, 상세 원본은 각 모듈의 Resources/Docs에 유지합니다. 게시 시 `.tmp/docs-collection-*` 임시 모음을 구성하고 검증한 뒤 삭제합니다. 소스 Docs에 생성 모음을 남기지 않습니다.

## 포함된 라이선스 고지

Core 프로젝트는 저장소 루트의 `LICENSE`와 `VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt` 전체를 포함합니다. App과 Tool은 `--license` 또는 `--third-party-licenses`를 단독 인자로 받아 GUI나 번들을 초기화하기 전에 종료합니다. 고지는 지역화 카탈로그가 아닌 원문 리소스이므로 번역하거나 줄이지 마십시오. Windows 및 Unix 게시 도구는 설치 프로그램을 게시하기 전에 두 명령의 출력을 원본 고지와 비교합니다. 의존성을 변경하면 [고지 출처와 유지관리 기록](../../../Licenses/README.md)을 갱신하고 실행 파일을 다시 빌드한 뒤 두 검사를 다시 실행하십시오. 이전 설치 프로그램에는 기존 고지가 그대로 남습니다.

사용자 지정 제품은 Tool의 --definition을 사용하며 제품 PowerShell 게시기는 구성된 VeroScript 제품 이름을 요구합니다. InstallerProductDefinition.ReleaseGroup(JSON releaseGroup)이 Modules이면 새 설치 프로그램/번들 생성을 거부합니다. 제품 정의 가이드의 완전한 페이로드 예제와 별도 모듈 아카이브 정책을 참고하세요.

- [배포 구성 및 게시 규칙](release-layout.md)

Windows Python 페이로드 계약에는 패키지 진입점, 형식 스텁, `py.typed` 마커와 일치하는 Interop DLL이 필요합니다. 네이티브 확장만으로는 충분하지 않습니다.

<a id="publication-paths"></a>

`veroscript_business_release.guard_tree`는 링크된 배포 루트와 항목을 거부합니다. macOS에서만 root 소유 `/tmp`, `/var`, `/etc`가 각각 정확히 `/private/tmp`, `/private/var`, `/private/etc`를 가리키면 상위 경로로 허용합니다. 일반 임시 배포 경로를 허용하되 링크된 출력 루트나 컬렉션 내부 링크는 허용하지 않습니다. 경로 검사 변경 시 이 구분을 보존하세요.

## 독립 설치 관리자 다운로드

Windows Runtime 설치 관리자는 `package-veroscript-installer.ps1`, 일치하는 Linux/macOS 호스트에서는 `package-veroscript-installer.sh`를 사용합니다. 기본 관리 출력은 `Release/Runtime/<target>/<version>/archives/`의 Runtime 아카이브를 읽습니다. 준비된 펼친 페이로드는 `-RuntimePackageDir` 또는 `--runtime-package-dir`로 전달할 수 있습니다. 이름은 `VeroScript<business>Installer-<version>-<target>`에 Windows의 `.exe`, Linux의 `.tar.gz`, macOS의 `.dmg`를 붙입니다. Linux는 실행 파일을 담은 아카이브 하나, macOS는 앱을 담은 디스크 이미지 하나를 게시합니다. 내장 페이로드 검사는 [다운로드 검증](release-validation.md#installer-download-validation)을 참조하십시오.

## Windows 실행 파일 아이콘 입력

Windows의 `veroscript`, `veroscript-runtime`, `veroscript-launcher`, `veroscript-lsp`, `veroscript-dap`는 소스 CMake 도우미 `vs_use_product_icon`으로 `Resources/Assets/icons/veroscript-icon.ico`를 내장합니다. CLI 대상 선언은 `PRODUCT_ICON`으로 활성화합니다. 이 실행 파일 대상에는 RC 언어와 Windows 리소스 컴파일러가 필요합니다. 루트 프로젝트는 RC가 있으면 탐지하고 활성화합니다. 소스 도우미를 재사용하는 상위 프로젝트는 Windows 실행 파일에 RC를 활성화해야 합니다. 라이브러리 전용 구성은 RC 없이 설정할 수 있습니다.

ICO를 바꾸면 해당 실행 파일을 다시 빌드합니다. `VeroScriptProductIcon.rc.in`은 아이콘 SHA-256을 기록해 생성된 RC의 변경이 Visual Studio 증분 빌드도 갱신하도록 합니다. C/C++ 옵션은 RC에 전달되지 않도록 언어별로 제한합니다. Installer 앱은 프로젝트 리소스로 ICO와 256px PNG를 별도로 사용하고 macOS 설치 관리자 번들은 ICNS를 사용합니다. 생성과 형식의 세부 사항은 [자산 가이드](../../../../../Resources/Assets/icons/README.md)에서 관리합니다. `python Resources/Tools/Packaging/veroscript_icons.py check --require-tracked`는 관리되는 소스를 검사합니다. 배포 감사는 모든 빌드 호스트에서 Windows PE 아이콘 그룹을 읽고 프레임을 관리되는 ICO와 비교할 수 있으며 대상 실행 파일을 시작하지 않습니다. 실제 셸 표시와 대상 플랫폼 검증은 별도로 기록합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/build-and-bundle.md) · [fr-FR](../fr-FR/build-and-bundle.md) · [ja-JP](../ja-JP/build-and-bundle.md) · [ko-KR](build-and-bundle.md) · [zh-CN](../zh-CN/build-and-bundle.md) · [zh-TW](../zh-TW/build-and-bundle.md)
