# 15: 프로젝트 옵션과 진단 언어 설정

CLI 옵션으로 가져오기 경로, 정의, 스크립트 인수를 지정합니다. 이 설정은 VeroScript 언어 의미를 변경하지 않습니다.

## 학습 파일 준비

일치하는 Runtime과 제공되는 VM/언어 리소스를 사용합니다. 쓰기 가능한 학습 디렉터리에 `main.vs`, `src`/`shared`를 만들고 아래 세 파일을 저장합니다. 모든 명령을 해당 디렉터리에서 실행합니다. import 경로와 매크로는 호출마다 적용되므로 `main.vs` 검사/실행 명령마다 반복합니다.

### main.vs

```VeroScript
import "score.vs";

int main(string[] args)
{
#ifdef FEATURE_SCORE
	print("score={}, argc={}", scoreValue(), len(args));
#else
	print("feature disabled");
#endif
	return 0;
}
```

### src/score.vs

```VeroScript
import "settings.vs";

export int scoreValue()
{
	return baseValue() + 2;
}
```

### shared/settings.vs

```VeroScript
export int baseValue()
{
	return 40;
}
```

`FEATURE_SCORE=1`과 `demo 42`를 사용하면 `score=42, argc=2`를 출력하고 0을 반환합니다. 매크로 정의를 빼면 `feature disabled` 분기를 선택합니다. 검사 성공 시 `main`을 호출하지 않고 0을 반환합니다.

## 1단계: 컴파일 옵션 설정

`-I`로 가져오기 경로를, `-D`로 정의를 지정합니다. 여러 값은 옵션을 반복해서 지정합니다. CLI는 옵션을 직접 읽습니다.

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

## 2단계: 실행 백엔드 선택

이 수업에서는 Runtime에 포함된 VM을 사용하므로 선택적 JIT 패키지가 필요하지 않습니다. 아래 명령은 `demo`와 `42`를 `main.vs`에 전달하고 스크립트의 일반 출력과 종료 결과를 유지합니다. 다른 정책과 모듈 환경 선택은 [CLI 백엔드 참조](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md#cli-backend-policy)를 확인하세요.

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

## 3단계: 실행 전 확인

실행 전에 진입점을 확인하세요.

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

`main(string[] args)`은 스크립트 경로 뒤에 인수를 받습니다. 경로 자체는 `args`에 없습니다. CLI 프로세스 종료 코드는 `int main`, `void main` 또는 `exit(n)`의 최종 결과에서 나옵니다.

## 4단계: 진단 언어 선택

`--lang`을 사용하여 하나의 CLI 호출에 대한 언어를 선택합니다.

```powershell
veroscript --lang ko-KR --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript --lang en-US --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

현재 PowerShell 세션에 대해 `VERO_LANG`을 설정할 수도 있습니다.

환경 변수 예제는 검사 후 이전 `VERO_LANG` 값을 복원합니다.

```powershell
$tutorialPreviousLanguage = $env:VERO_LANG
try
{
	$env:VERO_LANG = "ko-KR"
	veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
	$env:VERO_LANG = $tutorialPreviousLanguage
}
```

언어 태그는 배포된 카탈로그에서 검색됩니다. 누락된 번역은 영어로 표시되며, 지원하지 않는 명시적 `--lang` 값은 인수 오류입니다.
잘못된 `static class` 및 `static struct` 선언을 포함한 파서 선언 진단은 CLI와 LSP에서 동일한 안정적 ID와 카탈로그를 사용합니다. 도구 로직에는 진단 ID를 사용하고 번역된 메시지는 표시 텍스트로 처리하세요.

## 5단계: 프로젝트 현지화 디렉터리 제공

배포판의 전체 `localization/` 트리를 프로젝트에 복사하고 완전한 `en-US` 기준 카탈로그를 유지하세요. 아래 구조는 언어 카탈로그를 보여 주며, 복사한 다른 파일도 유지해야 합니다.

```text
localization/
  en-US/
    language.json
    ...
  ko-KR/
    language.json
    ...
```

`VERO_LOCALIZATION_DIR`로 프로젝트 트리를 명시적으로 선택하세요. 실행 파일 옆의 카탈로그는 작업 디렉터리의 자동 검색보다 우선합니다. 이 예제는 명령 실행 후 이전 설정을 복원합니다.

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang ko-KR --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

`en-US`는 완전한 기준을 정의합니다. 다른 하위 태그를 포함한 정규 BCP 47 디렉터리에는 영어 트리에 있는 파일과 메시지 ID만 포함할 수 있습니다. 번역 파일이나 키가 없으면 영어로 폴백합니다. 파일은 문자열 값을 가진 평면 UTF-8 JSON 객체여야 하며 중복 키와 후행 내용이 없어야 합니다. 언어를 추가하려면 선택한 트리에 정규 디렉터리와 JSON 파일을 추가하세요.

## 연습

1. 실행 명령에서 `-D FEATURE_SCORE=1`을 빼고 `feature disabled`를 확인합니다. 정의를 복원하고 `demo 42`를 전달해 `score=42, argc=2`를 확인합니다.
2. 세 가지 인수를 항목에 전달하고 `main(string[] args)`에서 인쇄합니다.
3. 프로젝트 트리를 선택한 상태에서 `ko-KR/language.json`의 `diag.expected_expression`을 삭제하고 아래 명령을 실행하여 영어 “expected expression”이 표시되는지 확인하세요.

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang ko-KR -e 'int main() { return +; }'
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

[CLI 참조](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md) · [Localization](../../../../../VeroScript/Resources/Docs/User/ko-KR/localization.md)

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/toolchain/15-workspace-and-localization.md) · [fr-FR](../../fr-FR/toolchain/15-workspace-and-localization.md) · [ja-JP](../../ja-JP/toolchain/15-workspace-and-localization.md) · [ko-KR](15-workspace-and-localization.md) · [zh-CN](../../zh-CN/toolchain/15-workspace-and-localization.md) · [zh-TW](../../zh-TW/toolchain/15-workspace-and-localization.md)
