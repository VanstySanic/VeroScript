# 명령줄 도구

명령줄 프로그램 이름은 `veroscript`입니다. 스크립트를 실행하고, 실행하지 않고 컴파일 확인하고, 소스 텍스트를 평가하고, REPL을 입력하고, DAP 디버거를 시작하고, 패키지를 빌드하고, 바인딩 카탈로그를 확인하고, AOT 소스를 생성할 수 있습니다.

## 프로젝트 초기화

```sh
veroscript project init
veroscript project init --path "./MyProject"
veroscript build --path "./MyProject"
```

없는 `veroscript-project.json`과 `veroscript-build.json`을 생성합니다. 버전은 `0.1.1`,
의존성은 빈 상태이며 기본 이식 가능한 애플리케이션 대상은 `app.vsapp`을 출력합니다.
기본 빌드 설정을 새로 만들 때 진입점이 없으면 최소한의 `main.vs`도 생성합니다.
기존 일반 파일은 `Kept`로 표시하고 덮어쓰지 않습니다. 기존 빌드 설정은 자체 진입점을 유지하며
별도의 `main.vs`를 생성하지 않습니다. 파일 대상에 디렉터리나 심볼릭 링크가 있으면 오류입니다.
다시 실행하면 없는 설정을 보완합니다. `project --help`와 `project init --help`는 파일을 쓰지 않고
도움말을 표시합니다.

`--path`를 생략하면 현재 작업 디렉터리를 사용합니다. 상대 경로는 명령 시작 디렉터리를 기준으로
하며 절대 경로와 `--path=<디렉터리>`도 지원합니다. 없는 디렉터리는 생성합니다. 디렉터리 이름을
유효한 소문자 ASCII 프로젝트 이름으로 정규화하고 필요한 경우 `my-project`를 사용하며,
디렉터리 자체의 이름은 변경하지 않습니다. 초기화는 로컬에서만 수행하며 의존성을 설치하거나
스크립트를 실행하지 않습니다. `veroscript.lock`은 설치 성공 시 생성하고 `.veroscript/`는 패키지
관리에 필요할 때 생성합니다. 빌드에는 빌드 서비스를 포함한 CLI가 필요합니다.
[최소 프로젝트 예제](../../../Examples/Project/README.md)를 참조하세요.

## 명령 형식

```powershell
# Run a source script or application package
veroscript [options] hello.vs [args...]
veroscript [options] DemoApp.vsapp [args...]

# Check without executing
veroscript --check hello.vs

# Execute source text
veroscript -e 'print(1 + 2);'

# Enter the REPL
veroscript -i

# Generate C++17 AOT source (the default target)
veroscript aot hello.vs -o generated.cpp

# Generate standalone Python source
veroscript aot --target=python hello.vs -o generated.py

# Generate a deterministic C# source bundle
veroscript aot --target=csharp hello.vs --output-dir generated --bundle-name App

# Validate or format a binding catalog
veroscript bindings validate catalog.json
veroscript bindings format catalog.json -o formatted.json

# Inspect a package
veroscript package inspect DemoApp.vsapp
```

`string[] args`이 포함된 `main`은 스크립트 경로 뒤에 인수를 받습니다. 경로 자체는 `args`에 포함되지 않습니다. 매개변수가 없는 `main`은 추가 인수를 무시합니다. `int main` 반환 값은 프로세스 종료 코드가 됩니다. 정상적으로 완료된 `void main`는 성공을 반환합니다.

CLI 프로세스 종료 코드는 스크립트의 최종 결과입니다. `int main` 값이 반환되고, `exit(n)`이 우선 순위를 가지며, 닫는 중괄호에 도달하는 `void main` 또는 `int main`은 `0`를 생성합니다. 이는 임베딩 API의 `FRunResult` 의미 체계와 다릅니다. 포함 호스트는 `bSuccess`을 사용하여 엔진이 정상적으로 완료되었는지 확인한 다음 `ExitCode`을 별도로 검사해야 합니다.

## 공통 옵션

| 옵션 | 목적 |
| --- | --- |
| `-e <source>` / `--eval <source>` | 소스 텍스트를 실행합니다. `-i`, `--check` 또는 감시 모드와 결합할 수 없습니다. |
| `-i` / `--interactive` | REPL을 입력합니다. |
| `--repl-strict` | `-i`이 필요합니다. 마지막 REPL 입력이 실패하면 0이 아닌 값으로 종료됩니다. |
| `-I <path>` / `--import-path <path>` | 모듈 검색 경로를 추가합니다. 반복 가능. |
| `--stdlib-path <path>` | 표준 라이브러리 검색 루트를 추가합니다. |
| `--implicit-import <file>` | 컴파일된 모든 소스로 파일을 가져옵니다. 반복 가능. 자동으로 소스를 편집하거나 선언을 다시 내보내지 않습니다. |
| `--no-circular-imports` | 순환 모듈 종속성을 거부합니다. 주기는 기본적으로 허용됩니다. |
| `--allow-circular-imports` | 순환 모듈 종속성을 허용하는 기본 동작을 명시적으로 복원합니다. |
| `--allow-project-wide-access` | 내보내지 않은 선언에 대한 프로젝트 전체 액세스를 허용합니다. 아껴서 사용하세요. |
| `--binding-catalog <file>` | 호스트 바인딩 카탈로그를 로드합니다. 반복 가능하고 병합됩니다. 일반적인 확인/실행 모드에서는 사용할 수 있지만 `-e`, `-i` 또는 감시 모드에서는 사용할 수 없습니다. |
| `-D name[=value]` / `--define name[=value]` | 객체와 유사한 매크로 또는 조건부 컴파일 기호를 정의합니다. |
| `--macro NAME(params)=body` | 구조화된 호스트 스타일 함수 매크로를 정의합니다. `--predefined-macro`은 호환성 별칭입니다. |
| `--check` | 실행하지 않고 진단을 컴파일하고 보고합니다. `-e`, `-i` 또는 감시 모드와 결합할 수 없습니다. |
| `--lang <tag>` | 로드된 문화권 디렉터리에서 확인할 수 있는 진단 언어를 선택합니다. 기본 리소스는 배포판에 포함된 문화권 디렉터리로 결정됩니다. 사용자 정의 카탈로그 루트는 더 많은 언어를 제공할 수 있습니다. |
| `-o <path>` / `--output <path>` | AOT 또는 바인딩 카탈로그 형식화를 위한 출력 경로입니다. |
| `--output-dir <path>` | C# 또는 Python AOT 다중 파일 bundle 디렉터리입니다. |
| `--bundle-name <name>` | 소스 bundle 이름이며 생략하면 입력 파일 이름을 사용합니다. |
| `--manifest <path>` | C# bundle manifest 경로이며 생략하면 출력 디렉터리를 사용합니다. |
| `--target <name>` | AOT 대상: `cpp`(기본값), `csharp` 또는 `python`입니다. |
| `-h` / `--help` / `-?` | 도움말을 표시합니다. |
| `--version` | 버전, 플랫폼 및 JIT 기능 정보를 표시합니다. |

## 실행 백엔드 및 캐시

| 옵션 | 목적 |
| --- | --- |
| `--backend=vm` 또는 `--backend vm` | 바이트코드 가상 머신만 사용합니다. |
| `--backend=mixed` 또는 `--backend mixed` | 지원되는 경우 JIT를 사용하고 VM으로 폴백합니다. 이것이 기본값입니다. |
| `--backend=jit` 또는 `--backend jit` | 명시적으로 또는 모듈 환경에서 자동으로 선택한 호환 JIT 모듈이 필요합니다. 대상 코드를 적극적으로 컴파일하고 지원하지 않는 본문은 진입 전에 VM으로 폴백합니다. |
| `--bc-cache=off` / `--bc-cache=on` | `.vsbc` 바이트코드 캐싱을 비활성화하거나 활성화합니다. 기본적으로 활성화되어 있습니다. |
| `--bc-cache-dir <path>` | 공유 바이트코드 캐시 디렉터리를 선택합니다. 생략하면 플랫폼 사용자 캐시가 사용됩니다. |
| `--vm-backend <id\|file\|directory>` / `--vm-backend=<id\|file\|directory>` | 인터프리터 선택: 제공된 `vs-bytecode`, 호환 파일/디렉터리 또는 패키지 지원 시 설치된 ID. 실행 정책과 독립적입니다. |

완전한 Runtime은 호환 인터프리터를 `Vm/`에 제공합니다. CLI 배포 시 이 디렉터리를 유지하세요. `--vm-backend=vs-bytecode`는 제공된 VM을 선택합니다. 다른 단순 ID는 패키지 지원 시 명시적으로 선택한 설치 환경에서만 해석하며 설치만으로 선택되지는 않습니다. 파일/디렉터리 경로는 시작 디렉터리 기준이며 Runtime과 호환되어야 합니다. `--vm-backend`는 인터프리터, `--backend=vm|mixed|jit`는 실행 정책, `--jit-backend`는 JIT 구현을 선택합니다. 스크립트 컴파일 프로세서에도 VM이 필요합니다. 이런 프로세서를 실행하지 않는 일반 검사는 진입 프로그램을 실행하지 않습니다.

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

디버깅은 VM을 사용합니다. `--watch`는 감시 중인 파일이 바뀌면 선택한 실행 정책으로 새로 실행합니다.

## 시청 모드

`--watch`은 소스 파일이나 관련 모듈이 변경되면 스크립트를 다시 실행합니다. `--watch-once`은 감시 모드를 활성화하고 0이 아닌 첫 번째 실행 결과 후에 종료됩니다. 확인 모드, 평가, REPL, 디버깅 및 패키지 파일은 감시 모드와 결합할 수 없습니다. 빌드된 패키지는 `--watch`를 지원하지 않습니다.

## 디버깅

표준 입력 및 출력에 대한 DAP:

```powershell
veroscript --debug hello.vs
```

TCP를 통한 DAP:

```powershell
veroscript --debug --dap-port=4711 hello.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 hello.vs
```

`--dap-port`은 `1`–`65535`를 허용합니다. `--dap-bind`은 수신 주소를 선택하고 기본값은 `127.0.0.1`이며 `--debug --dap-port`가 필요합니다. 디버그 모드에서 DAP `launch` 또는 `attach`은 프로그램과 해당 인수를 제공합니다. 명령줄 스크립트 경로는 프로그램을 미리 지정하고 그 뒤의 명령줄 인수는 해당 요청의 기본 스크립트 인수가 됩니다. `veroscript-dap`은 동일한 디버그 호스트에 대한 씬 별칭입니다. 전체 프로토콜 경계는 [디버깅](../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/debugging.md)을 참조하세요.

## 패키지, AOT 및 카탈로그

`--emit-package`을 사용하여 애플리케이션 및 라이브러리 패키지를 빌드하세요. `--package-file`은 소스 모듈을 반복적으로 추가할 수 있고, `--package-version`는 서명되지 않은 패키지 버전을 설정하며, `--strip-package-debug`은 소스 파일 및 라인 테이블 정보를 제거합니다. 이러한 옵션은 `-e`, `-i`, `--check`, 감시 모드 또는 디버깅과 결합할 수 없습니다. [패키지 및 배포](packages.md)를 참조하세요.

`aot` 하위 명령은 소스 스크립트가 필요합니다. 기본 대상은 C++17이며
`-o`/`--output`으로 변환 단위를 출력합니다. `--target=python`은 Python 3.11-3.15
독립 소스를, `--target=csharp`는 .NET 8/9용 C# 소스를 생성합니다. Python과
C#은 소스 bundle에 `--output-dir`과 `--bundle-name`을 사용할 수 있습니다.
결과는 링크된 실행 파일이 아니라 소스입니다. [C++ AOT](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/aot.md),
[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/aot.md), [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/aot.md)를 참조하십시오.

`bindings validate`은 카탈로그를 확인하고 `bindings format`은 카탈로그를 정규화합니다. 후자는 `-o`/`--output`을 사용하여 새 파일을 작성할 수 있습니다. [호스트 바인딩](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/bindings.md)을 참조하세요.

## 진단 및 환경 변수

```powershell
veroscript --lang en-US --check script.vs
veroscript --version
```

일반적인 환경 변수는 다음과 같습니다.

| 변수 | 목적 |
| --- | --- |
| `VERO_PATH` | 모듈 검색 경로 Windows에서는 `;`, Unix에서는 `:`입니다. |
| `VERO_STDLIB_PATH` | 표준 라이브러리 검색 루트. |
| `VERO_CACHE_DIR` | 바이트코드 캐시 디렉토리. |
| `VERO_LANG` | 현재 카탈로그 루트에서 로드된 표준 BCP 47 태그와 같은 진단 언어입니다. |
| `VERO_LOCALIZATION_DIR` | 현지화 카탈로그 루트. |
| `VERO_MAX_CALL_DEPTH` | 총 재귀 호출 깊이 제한입니다. |

명령줄 옵션은 해당 환경 설정을 재정의합니다. 지원되지 않는 시스템 언어 또는 `VERO_LANG`은 영어로 대체되지만, 명시적으로 지정한 지원되지 않는 `--lang` 값은 인수 오류로 거부됩니다. `veroscript --version`을 사용하여 설치된 버전을 검사합니다.

현지화 루트는 `localization/<culture>/<component>.json` 레이아웃을 사용합니다. 유효성 검사, 검색 순서 및 모듈 수준 카탈로그는 [진단 현지화](../../../../../VeroScript/Resources/Docs/User/ko-KR/localization.md)를 참조하세요.

## 성능 측정 시 백엔드 선택

VM, mixed, JIT 우선 백엔드는 동일한 언어 의미를 구현합니다. mixed는 대상 코드를 최적화 계층에서 실행하면서 VM 폴백을 유지하고, JIT 우선은 더 이른 컴파일을 요청하지만 모든 함수가 컴파일된다는 보장은 없습니다.

성능을 측정할 때는 디버그가 아닌 일반 실행을 사용하세요. 디버깅은 VM을 사용합니다. 바이트코드 캐시는 컴파일 시간에만 영향을 줍니다. 입력, 프로세스 수명, 캐시 상태 및 백엔드를 동일하게 유지하고 비교하세요.

## Unicode 경로와 인수

Windows CLI는 Unicode 프로세스 인수를 받고 처리 전에 UTF-8로 변환합니다. 따라서 비 ASCII 스크립트 경로, 가져온 모듈 경로 및 `main(string[] args)` 인수의 텍스트를 보존합니다. 소스 파일과 지역화 JSON은 UTF-8을 사용하며 리디렉션된 stdout 또는 stderr를 읽는 도구도 해당 스트림을 UTF-8로 디코딩해야 합니다.

## 백엔드 선택

<a id="cli-backend-policy"></a>

`vm`, `mixed`, `jit`는 실행 정책을 선택하며 JIT 백엔드 선택과는 별개입니다. Runtime은 JIT를 포함하지 않습니다. 설치된 모듈 환경이 있으면 `--jit-backend`를 생략할 때 `services.jit.backend`를 상속합니다. 설정이 없으면 기본값 `auto`가 활성 후보를 백엔드 ID 오름차순으로 시도하여 사용 가능한 첫 호환 백엔드를 선택합니다. `--jit-backend=auto`는 설정을 덮어쓰고 `--jit-backend=none`은 JIT를 비활성화합니다. ID, 라이브러리 파일 또는 디렉터리도 명시할 수 있으며 잘못된 명시적 선택은 오류를 냅니다. `auto`도 잘못된 후보 ID 또는 동일한 백엔드 ID를 가진 활성 공급자 충돌을 오류로 보고합니다. 자동 선택에는 모듈 환경이 필요합니다. JIT가 비활성화되었거나 자동 선택으로 사용 가능한 백엔드를 찾지 못하면 `mixed`는 VM을 사용하고 강제 `jit`는 실행 전에 실패합니다. CLI 옵션은 스크립트 경로 앞에 둡니다. 디버깅은 VM을 사용합니다. 지원하지 않는 코드는 진입 전에 VM으로 폴백할 수 있지만 진입 후 실패하면 부수 효과를 다시 실행하지 않습니다.

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

```sh
veroscript --vm-backend=auto --jit-backend=auto main.vs
veroscript --jit-backend=none --backend=mixed main.vs
```

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
```

## 스크립트 컴파일 확장

[스크립트 컴파일 확장](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/compiler.md#source-extension-activation)을 참조하세요.

<a id="cli-compiler-options"></a>

`--compiler-config <key=value>`는 문자열 설정 하나를 추가합니다. 다른 키로 반복할 수 있으며 값을 JSON으로 해석하지 않고 중복 키는 거부합니다. --compiler-instructions와 --compiler-timeout은 작업 예산을 제공합니다. --no-compiler-imports는 설정된 확장을 유지하며 소스 활성화를 막습니다. 확장은 컴파일 VM, 협력적 취소 및 제한된 서비스를 사용합니다. 일반 import만으로 컴파일 권한을 얻지 못합니다. 생성 코드에도 일반 언어 규칙이 적용되며 독립 출력은 확장 VM 없이 생성된 업무 코드를 실행합니다. 프로세서 작성은 Developer 안내를 참고하세요.

[스크립트 컴파일 확장](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/compiler.md)

확장 전체의 기본 예산은 VM 명령 1,000,000개와 2,000ms입니다. 0은 해당 예산을 비활성화하며 차단된 네이티브 콜백을 강제로 중단하지는 못합니다.

`install -r <file>` / `install --requirement=<file>`: [의존성 목록 파일](../../../../../VeroScriptPackageManager/Resources/Docs/User/ko-KR/index.md).

## 네이티브 모듈 명령

설치되고 활성화된 패키지가 네이티브 `cli` 역할을 선언하면 선택한 프로젝트나 컴포넌트 환경에서 그 명령을 바로 사용할 수 있습니다. 패키지가 제공한 실제 명령/옵션 이름을 사용하세요. 다른 프로젝트를 선택하는 `--path <directory>`는 모듈 옵션보다 앞에 둡니다.

로컬 라이브러리는 `--cli-module <file>`로 로드하며 반복할 수 있습니다. 상대 경로는 시작 디렉터리 기준이고 로더는 라이브러리 파일만 받으며 디렉터리/PATH를 탐색하지 않습니다. Windows에서는 `.dll`, Linux/macOS에서는 해당 `.so`/`.dylib`를 사용합니다. 환경/백엔드 옵션과 라이브러리 선택, 모듈 옵션, 명령 또는 스크립트 순서로 배치하며 뒤의 인자는 그대로 전달됩니다.

모듈 명령은 같은 이름의 파일보다 우선합니다. 해당 파일을 실행하려면 `./name`을 사용하세요. 기본 이름과 중복 등록 이름을 사용할 수 없습니다. 모듈 명령은 check, eval, REPL, watch, debug/DAP, 바이트코드 출력 모드와 함께 사용할 수 없습니다. `--help`는 등록 항목을 나열하고 `<command> --help`는 구성/실행 없이 사용법을 표시합니다. 다만 등록 과정에서는 네이티브 모듈을 로드합니다. 출력과 설명은 모듈이 제공하며 `--lang`은 선택한 언어를 콜백에 전달할 뿐 텍스트를 자동 번역하지 않습니다. Core CLI 도움말과 일부 검증 메시지는 영어입니다.

```text
veroscript my-command arg
veroscript --path ./project --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll my-command --help
```

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
