# 확장 패키지 설치와 관리

프로젝트 루트 의존성에 명시한 소스는 소스를 지정하지 않은 같은 이름의 전이 의존성에도 적용됩니다. 각 버전과 해시 제약은 계속 검사하며 서로 다른 명시적 소스는 오류입니다. 오프라인에서는 로컬 `vs-network` 압축 파일 소스를 먼저 선언한 뒤 `vs-http` 또는 `vs-websocket`을 설치합니다.

`veroscript-project.json`은 프로젝트 의존성, 패키지 활성화 상태와 설정을 저장합니다. 배포 루트는 `veroscript-package.json`, 빌드 그래프는 `veroscript-build.json`을 사용하며 각각의 형식과 스키마를 검증합니다.

선택적 패키지 관리자는 스크립트 라이브러리, 리소스, 네이티브 확장을 명시적인 프로젝트 또는 Runtime/Core SDK에 설치합니다. 배포물은 루트에 veroscript-package.json이 있는 .vspkg/.zip ZIP이며 .vslib 라이브러리, .vsapp 앱, .vsbc 캐시와 다릅니다.

## 환경 선택

명령은 시작 디렉터리를 기본 프로젝트로 사용합니다. --path는 다른 프로젝트, --root는 실행 CLI의 검증된 Runtime/Core SDK, --root --path는 다른 검증된 컴포넌트를 선택합니다. 상대 경로는 시작 디렉터리 기준입니다. 프로젝트/SDK 기본 저장소는 .veroscript/packages, Runtime은 Packages입니다. 사용자 설치 범위, 암묵적 병합, 실행 시 자동 설치는 없습니다.

## 명령

```sh
veroscript env --json
veroscript install ./library.vspkg --path ./app
veroscript install --path ./app --locked --offline
veroscript list --path ./app --json
veroscript show example-math --path ./app
veroscript disable example-math --path ./app
veroscript enable example-math --path ./app
veroscript verify --path ./app
veroscript uninstall example-math --path ./app
veroscript clean --path ./app
veroscript pack ./library --output ./library.vspkg
veroscript --path ./app ./app/main.vs
veroscript deploy --path ./app --output ./deployment
```

install은 해석과 설치, --locked는 잠금 유지, --offline은 네트워크 금지입니다. 원격 결과에는 완전한 캐시가 필요하고 로컬 아카이브는 존재해야 합니다. 잠긴 디렉터리 스냅샷은 원본 삭제 후에도 사용할 수 있습니다. --upgrade와 --locked는 함께 사용할 수 없습니다. list/show는 조회, verify는 무결성 검사입니다. disable은 파일과 버전 잠금을 유지하지만 활성 의존성을 깨지 못하며 enable로 다시 선택합니다. clean은 참조와 활성 대여가 없는 소유 세대만 정리합니다.

pack은 기존 출력을 덮어쓰지 않습니다. 새 출력 경로를 지정하거나 이전 아카이브를 옮긴 후 다시 생성하세요.

## 업그레이드, 제거 및 반복 설치

```sh
veroscript install example-math --path ./app --upgrade
veroscript install --path ./app --upgrade
veroscript uninstall example-math --path ./app
```

첫 명령은 지정한 루트 의존성을, 두 번째는 전체 의존성 그래프를 다시 선택합니다. 별도의 `update` 하위 명령은 없습니다. `--upgrade`는 선언된 버전 제약과 소스를 따르며 `--locked`와 함께 사용할 수 없습니다. 버전 없이 새로 추가한 레지스트리 의존성은 선택한 정확한 버전으로 고정됩니다. 그 고정을 넘으려면 요구사항을 수정하거나 `name@version`을 명시해 설치하세요. 로컬 디렉터리/아카이브 의존성은 계속 로컬 소스를 사용합니다. [로컬 예제](../../../Examples/README.md)에서는 레지스트리 없이 두 업그레이드 명령을 실행할 수 있습니다.

`--upgrade`가 없으면 유효한 잠긴 버전을 우선하고 동일한 패키지 내용을 재사용합니다. 요구사항, 소스 또는 로컬 내용이 바뀌면 다른 해석 결과가 필요할 수 있습니다. 반복 설치도 새 세대를 만들고 설치 작업, 신뢰하는 프로세서와 허용된 훅을 다시 실행합니다. 훅에는 호출할 때마다 `--allow-hooks`가 필요합니다. `--locked`는 잠금 결과를 제한하고 `--offline`은 네트워크를 금지하지만 설치 처리를 생략하지는 않습니다.

`uninstall`은 선언된 루트 의존성을 받습니다. 해당 요구사항과 나머지 루트에 더 이상 필요하지 않은 의존성을 제거하며 공유 의존성은 유지합니다. 간접 의존성만 있는 이름을 직접 제거하는 대신 이를 요구하는 루트를 제거하거나 수정하세요. 참조되지 않는 설치 세대는 활성 대여가 없어진 뒤 `clean`으로 별도 정리할 수 있습니다.

## 실행과 배포

실행, 검사, eval, REPL, watch, 바이트코드 패키징 및 지원되는 소스 AOT가 선택 환경을 사용합니다. 명시하지 않으면 스크립트는 진입점에서, eval/REPL은 시작 위치에서 프로젝트를 찾고 실행 컴포넌트로 폴백합니다. LSP/DAP/Interop 통합은 각 어댑터의 책임이며 CLI 선택자를 자동 공유하지 않습니다. DAP는 지원하지 않는 명시적 선택을 거부합니다. AOT는 대상별 네이티브 브리지가 여전히 필요합니다.

deploy는 새 출력 디렉터리에 활성 runtime 의존성 폐쇄, 리소스, 라이선스, 이식 가능한 로드 스냅샷을 복사합니다. compile/development 전용 의존성은 제외하고 앱/.vsapp는 별도 제공합니다. 교차 대상 설치는 대상 라이브러리를 실행하지 않고 검사·복사합니다. 플랫폼별 검증은 별도로 수행하세요.

설치된 교차 대상을 내보낼 때는 설치에 사용한 전체 대상 설명자를 `deploy --target <file>`에 전달하거나 `FInstalledEnvironment::DeployForTarget`을 호출합니다. 내보내기는 의존성 폐쇄 데이터를 검증하고 복사합니다. 실행을 위해 배포를 불러오려면 호스트 대상과 일치해야 합니다.

## 설정, 오류와 언어

`recovery-conflict`는 관리 파일이 중단된 저널의 이전/새 값 모두와 다름을 뜻합니다. 편집과 `transaction.json`을 보존하며 롤백이 막히면 `recovery-required`일 수 있습니다. 충돌 파일을 백업하고 보존된 저널의 충돌을 해결한 뒤 재시도하세요. 저널만 삭제하지 마세요.

--packages-dir, --cache-dir, --lockfile로 위치를 명시합니다. 기존 저장소 경로 편집으로 설치를 이동하지 말고 새 환경에 재설치하거나 배포하세요. environment-changed는 동시 편집을 보존하므로 다시 읽고 재시도합니다. layout-conflict는 상태/잠금/소유권 경로 중첩, restart-required는 살아 있는 네이티브 구현이 대체 버전과 공존하지 못함을 뜻합니다.

패키지 --lang과 반복 가능한 --fallback-lang은 컴파일러 진단 언어와 별도로 표시 문구를 선택합니다. 관리자의 기본 진단은 현재 영어 메시지와 안정적인 code를 제공하므로 code로 분기하세요. --json은 기계 결과와 메시지를 분리합니다. 선택적 관리자를 끈 빌드는 이 명령들을 제공하지 않습니다.

[배포물 작성](authoring.md) · [C++에서 설치 환경 사용](embedding.md)

## 의존성 목록에서 설치

```sh
veroscript install -r requirements.txt
veroscript install --requirement "config/dependencies.txt" --path ./app
veroscript install -r requirements.txt --root
veroscript install -r requirements.txt --locked --offline
```

`-r FILE`, `-rFILE`, `--requirement FILE`, `--requirement=FILE`은 반복 가능한 `install` 전용 옵션이며 개별 패키지 인수와 함께 사용할 수 있습니다. 파일 이름은 자유롭고 `.txt` 확장자는 필수가 아닙니다. 명령줄 목록 경로는 시작 디렉터리를 기준으로 하며 설치 대상 `--path`／`--root`와 독립적입니다. 목록 안의 중첩 목록과 로컬 배포물은 각각 해당 파일의 디렉터리를 기준으로 합니다. 절대 경로, UTF-8, 공백을 지원하며 공백이나 주석 문자가 있는 경로는 따옴표로 감싸세요. 따옴표 안의 Windows 역슬래시는 그대로 유지되며 슬래시도 사용할 수 있습니다.

UTF-8 예시입니다. LF／CRLF와 선택적인 UTF-8 BOM을 지원합니다.

```text
# Registry requirements use VeroScript versions.
gui==0.1.1
math >= 0.1.1, < 0.2.0 # both comparisons apply
widgets@0.1.1-beta.1
-r "lists/common.txt"
"../archives/local library.vspkg"
tools @ "../tools"
assets @ https://example.com/assets/download
```

논리 행마다 의존성 하나 또는 로컬 `-r`／`--requirement` 포함을 작성합니다. 빈 행과 `#` 주석은 무시하며 행 끝 주석은 따옴표 밖의 공백 뒤에서 시작합니다. 이스케이프되지 않은 마지막 역슬래시는 다음 물리 행을 공백으로 연결한 후 주석을 제거합니다. 같은 이름의 항목은 버전 조건의 교집합을 사용하며 명시적인 출처나 설정이 다르면 실패합니다. 이름이 있는 직접 참조는 실제 매니페스트 이름을 확인합니다. 로컬 경로는 선택한 프로젝트 기준으로 바꾸어 `veroscript-project.json`에 저장하며 URL／아카이브 해시는 기존 방식으로 기록합니다. 다른 프로젝트 의존성은 유지하고 명시한 항목이 해당 루트 의존성을 갱신합니다. 전체 목록은 한 번의 의존성 해석과 설치 트랜잭션을 공유합니다. `--locked`, `--offline`, `--upgrade`와 훅 권한의 의미는 동일합니다. 실패 시 일부만 커밋하지 않으며 목록의 동시 수정은 보류 중인 커밋을 무효화합니다.

비교 연산자는 `==`／`=`, `<`, `<=`, `>`, `>=`이며 쉼표나 공백은 AND입니다. 기존 `name@<조건>`과 `name@*`도 지원합니다. 이름만 쓰면 기존 의존성을 유지하고 새 버전 미지정 레지스트리 루트는 선택 버전으로 고정합니다. 직접 출처는 공백이 있는 `name @ <경로 또는 URL>`로 작성합니다. 슬래시 없는 디렉터리는 `./directory` 또는 따옴표로 패키지 이름과 구분하세요.

pip 사용 방식을 참고하되 VeroScript 이름과 버전을 사용하는 형식입니다. Python extras, 환경 마커, VCS／editable, 제약 파일, `~=`, `!=`, pip 옵션은 지원하지 않습니다. 포함 대상은 로컬 파일이며 환경 변수, `~`, shell 이스케이프, glob을 확장하지 않습니다. 레지스트리, 미러, 인증과 권한은 기존 CLI／호스트 옵션으로 지정합니다. 순환, 잘못된 UTF-8, 문법 오류는 파일／행 번호를 제공하며 읽기, 깊이, 파일／항목 수, 누적 메타데이터, 취소와 기한은 기존 제한을 사용합니다. 목록은 명시적으로 지정해야 하며 일반 `install`은 계속 `veroscript-project.json`을 읽고 텍스트 목록을 자동 탐색하지 않습니다.

### 오프라인 예제

[로컬 예제](../../../Examples/README.md)의 `library`를 `workspace/vendor/library`에 복사하고 다음 두 파일을 만든 뒤 `workspace`에서 실행하십시오. 결과에는 `example-math`가 표시되며 앱의 veroscript-project.json과 잠금 파일에 의존성을 저장합니다.

`workspace/requirements.txt`:

```text
# Local package list
-r lists/local.txt
```

`workspace/lists/local.txt`:

```text
"../vendor/library"
```

```sh
mkdir app
veroscript install -r requirements.txt --path ./app --offline
veroscript install --requirement=requirements.txt --path ./app --locked --offline
veroscript list --path ./app --json
```

포함 순환, 잘못된 인코딩과 리소스 한도 위반은 거부합니다. 가능하면 진단에 원본 파일과 시작 물리 행을 표시합니다. `requirements-changed`는 커밋 전에 목록이 바뀌었음을 뜻하므로 파일을 다시 읽고 재시도하세요.

명시적인 URL 항목에 `--locked` 또는 `--offline`을 사용하면 같은 URL에 기록된 해시를 재사용하며 이름이 있는 참조는 패키지 이름도 일치해야 합니다. 캐시 바이트는 계속 검증하고 알 수 없거나 바뀐 URL에 다른 URL의 캐시 식별자를 사용하지 않습니다. 오프라인 재설치에는 저장된 프로젝트 의존성과 완전하고 검증 가능한 아카이브 캐시가 필요합니다.

프로젝트／상태 생성 전 최초 설치가 중단되어도 `recovery-required`를 반환합니다. 명시적인 패키지 쓰기 작업으로 복구를 완료하세요. 유효하고 완전히 적용된 `committed` 로그는 읽기를 막지 않습니다. `combine-processing`은 생성 결과나 네이티브 로더가 다른 의존성／설정에 대응함을 뜻하므로 필요한 그래프를 한 환경에 설치하세요. `pack`은 동시에 생성된 대상을 포함하여 기존 출력을 바꾸지 않습니다.

패키지 저장소와 관리 파일은 심볼릭 링크 및 특수 항목을 거부합니다. macOS에서는 root 소유 `/tmp`, `/var`, `/etc`가 예상한 `/private/...` 대상을 가리킬 때만 일반 임시 프로젝트/캐시의 상위 경로로 허용됩니다. 패키지 아카이브 항목과 선택한 저장소 아래 링크의 거부 규칙은 유지됩니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md) · [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
