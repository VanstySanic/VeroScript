# CLI process와 asset 계약

## 진입점

`VeroScriptCli`는 `veroscript`, `veroscript-lsp`, `veroscript-dap`를 위한 thin process layer입니다. ordinary main은 compile/check/run과 package operation을 dispatch합니다. REPL/watch code는 interactive source/file-change orchestration을 추가합니다. LSP/DAP main은 language behavior를 Tooling에 delegate합니다.

CLI는 compiler, VM, debugger, workspace semantics를 중복 구현하면 안 됩니다. subcommand name, option, environment variable, exit code, diagnostic ID, output format은 compatibility token이며 localize하지 않습니다.

## 리소스 해석

resolution은 source root, SDK root, `bin` directory, standard-library directory, localization directory를 구별합니다. executable과 runtime asset은 compatible installation에서 와야 합니다. environment-variable/`PATH` fallback은 discovery behavior이며 installation 혼용 허가가 아닙니다.

## 프로세스 동작

stdout, stderr, diagnostic line handling, input stream, process cancellation, exit-code propagation은 IDE, SDK, shell consumer에 대해 stable해야 합니다. LSP/DAP framing은 Tooling transport implementation의 책임입니다. CLI는 process startup과 termination만 소유합니다.

## Unicode 프로세스 경계

Windows `wmain`은 `VeroScript::Cli::Run` 호출 전에 UTF-16 인수를 한 번 UTF-8로 변환합니다. 인수를 소유하는 문자열과 포인터 배열은 호출 전체에서 유효해야 합니다. 파일 작업은 UTF-8 경로 도우미를 사용합니다. `VeroScriptUnicodeCliTests`는 VM, mixed 및 JIT 우선 모드에서 비 ASCII 소스와 가져오기 경로, 스크립트 인수, 패키지 실행을 확인합니다. 터미널 표시와 파이프 디코딩은 소비자의 책임입니다.

## 등록과 디스패치 계약

Core는 `VeroScriptCommandLine.h`를 공개하며 CLI는 레지스트리, 인자 파서와 디스패치를 소유합니다. 네이티브 `Create`에서 `INativeExtensionHost::GetService("veroscript.cli", "0.1.1", error)`를 얻고 일치하는 `ICommandLineExtensionService`를 사용하여 소유한 `FCommandLineModule` 설명자를 동기 등록합니다. 등록은 디스패치 전에 닫히며 잘못된 선언은 전체 배치를 무효화합니다. 복합 역할 모듈은 CLI가 아닌 호스트에서 서비스가 없어도 동작해야 합니다. 모듈은 명령이나 옵션을 하나 이상 선언해야 하며 옵션을 선언하면 Configure 콜백이 필요합니다.

ID와 이름은 대소문자를 구분하는 ASCII 영숫자/밑줄/하이픈이며 최대 64자이고 앞뒤 하이픈은 금지됩니다. Core 이름은 예약됩니다. 최대 256개 모듈, 모듈당 명령과 옵션 합계 256개이며 설명/사용법/값 레이블은 제어 문자 없이 최대 8192바이트입니다. 옵션은 기본적으로 값이 필수이고 반복할 수 없습니다. 지정한 플래그는 빈 문자열 하나를 제공하고 미지정 옵션은 맵 항목이 없습니다. 값이 옵션처럼 보이면 `--name=value`를 사용합니다.

Configure는 일반 실행/명령 디스패치 전에 모듈 ID 순서로 한 번만 상태를 검증/저장합니다. 자신의 값만 받으며 스크립트를 실행해서는 안 됩니다. 도움말은 구성과 명령 실행을 생략하지만 모듈 로드/등록은 수행합니다. Run은 원래 UTF-8 인자, 언어 태그, 스트림과 선택 환경을 받습니다. 빌린 컨텍스트를 보관하거나 등록 소유 환경으로 강한 역참조를 만들지 마세요. 네이티브 Destroy가 반환하기 전에 워커를 종료합니다. 호스트는 코드를 유지하고 예외를 잡으며 종료 코드 0..255를 검사합니다. 차단되는 네이티브 명령은 취소를 직접 관리합니다.

[NativeModule 소스 예제](../../../Examples/NativeModule/README.md)는 일치하는 SDK 빌드와 패키지 통합을 보여 줍니다. `VeroScriptCliModulesTests`와 `VeroScriptCliRegistryTests`는 인자 파싱, 이름 충돌, Unicode, 수명, 활성화/비활성화와 인자 경계를 검증합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/process-and-assets.md) · [fr-FR](../fr-FR/process-and-assets.md) · [ja-JP](../ja-JP/process-and-assets.md) · [ko-KR](process-and-assets.md) · [zh-CN](../zh-CN/process-and-assets.md) · [zh-TW](../zh-TW/process-and-assets.md)
