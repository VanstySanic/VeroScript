# 응용 프로그램 빌드 및 패키징

`--build-file <파일>`은 임의의 설정 파일명과 절대 경로 또는 명령 시작 디렉터리 기준 상대 경로를 지원합니다. `--path`가 없으면 설정 파일의 부모가 프로젝트 루트가 되며, 지정하면 해당 루트가 입력 경로의 기준입니다. 기본 파일명은 `veroscript-build.json`입니다.

선택 모듈 `VeroScriptBuild`는 엄격한 `veroscript-build.json`을 읽습니다. 설정 경로는 지정한 프로젝트 루트를, CLI 경로는 명령을 시작한 디렉터리를 기준으로 해석합니다.

`--profile <이름>`은 `profiles`에 선언한 프로필을 선택하며 기본값은 `release`입니다. `--target <이름>`으로 대상을 선택하고 여러 번 지정할 수 있습니다. 대상을 명시하지 않으면 설정의 `defaultTargets`를 사용합니다.

```sh
veroscript build --path /absolute/project --plan --json
veroscript build --path /absolute/project --build-file /absolute/config/release.json --plan --json
```

이 명령은 사용자 코드를 실행하지 않고 계획을 검사합니다. 실행하려면 `--plan`을 제거하고, 대상 이름이나 `--target`으로 대상을 선택합니다. 종속 패키지는 먼저 설치하고 잠가야 합니다.

library, application, distribution, deployment, bundle, custom, group은 각각 `.vslib`, `.vsapp`, `.vspkg/.zip`, 배포 환경, 배포 조합, 사용자 단계 및 대상 그룹을 처리합니다. 출력 참조는 `{ "target": "library", "output": "main" }`이며 소스 트리 파일에는 `path`를 추가합니다.

빌드 중 앱 검증은 빌드 머신이 아닌 선택한 대상의 플랫폼과 ABI를 사용합니다. 선언한 구성 요소가 있으면 다른 대상용 결과를 준비할 수 있지만 네이티브 구성 요소의 검증과 실행은 해당 대상에서 별도로 해야 합니다.

Runtime, 종속 환경, 실행기는 각각 선택 사항입니다. 소스 응용 프로그램은 runtime과 compile 용도를 모두 유지합니다. PE/ELF 단일 파일은 같은 ZIP 내용을 재사용하며 다른 플랫폼 포장은 사용자 공급자가 처리합니다. 소유권 영수증, 해시와 임대가 외부 변경을 보호하고 대상별 원자적 출판을 보장합니다.

프로세스와 스크립트 단계는 명시적 허가가 필요합니다. 스크립트는 선언 입력 읽기, 임시 출력 쓰기, 로그용 build 함수와 VM 제한을 사용합니다. 네이티브 프로세스는 OS 샌드박스가 아닙니다. 키 파일을 배포물에 넣지 마세요.

소스 앱은 실제 패키지 콘텐츠로 검증하며 매크로와 옵션을 저장합니다. 개발용 검색 경로 대신 files/modules 및 명시적 환경을 사용하세요. 편집기 오버레이는 생성 소스로 저장해야 합니다. `Resources/Examples/generated`는 생성기 예제입니다. 디렉터리와 단일 실행 파일은 콘텐츠 서명을 사용합니다.

서명된 앱을 다시 구성할 때는 새 콘텐츠의 서명 정책을 명시해야 합니다.

## 단계, 출력 및 캐시

`--allow-process`와 `--allow-script`는 실행 시 해당 단계를 허용합니다. `--plan`은 프로세서, 사용자 단계, 설치 훅, 하위 프로세스를 실행하지 않습니다. 프로세스는 셸 없이 실행 파일과 인수 배열을 받습니다. 스크립트는 `build_read_text(input, relative)`, `build_write_text(output, relative, text)`, `build_message(text)`를 사용합니다. 가져오기는 스크립트 디렉터리, 선언된 입력, 명시적 표준 라이브러리 루트에 한정됩니다. 표준 파일, 환경, 콘솔, 작업, 주소, 비결정성 서비스는 비활성화되며 명령 수, 시간, 깊이, 메모리, 수치 제한이 적용됩니다. 네이티브 공급자/프로세스는 신뢰하는 기능이며 OS 샌드박스가 아닙니다.

`Resources/Examples/hello`를 쓰기 가능한 위치에 복사하세요. 출력은 `dist/<profile>/<target-identity>/<target>/`에 있으며 이식 가능한 식별자는 `portable`입니다. 외부 또는 수정된 파일을 대체하지 않습니다. 같은 볼륨에서 대상마다 원자적으로 게시하며 이후 실패가 이미 게시된 독립 대상을 되돌리지는 않습니다. 아티팩트 사용 중 반환된 임대를 유지하세요. 캐시는 설정, 고정 입력, 상위 콘텐츠, 공급자 식별자, 출력 해시에 의존합니다. 사용자 단계는 결정성과 도구 체인 식별자를 명시해야 합니다. 동적 프로세서와 신뢰에 민감한 컴파일은 보수적으로 다시 실행합니다. 입력 변경 후에는 새 계획을 만드세요.

임베디드 호스트가 `script` 단계를 실행하려면 `FServices::Compile.ScriptCompileExtensions.VmBackendFactory` 또는 `VmBackendPath`로 VM을 명시적으로 제공해야 합니다. 두 옵션은 배타적이며 누락 시 `script-vm`을 보고합니다. 데스크톱 `veroscript build`는 제품에 구성된 VM을 사용하면서 권한, 명령 수, 기한, 취소와 메모리 제한을 유지합니다. VM 경로나 내용이 바뀌면 캐시가 무효화됩니다. 사용자 팩터리로 스크립트 단계 캐시를 재사용하려면 안정적인 `FServices::Identity`가 필요합니다.

## 구성 요소와 보호

소스 앱은 `["runtime", "compile"]`, 컴파일 앱은 보통 `["runtime"]` 용도가 필요합니다. `bundle.runtime`은 검증된 `veroscript-runtime.json`에 나열된 파일만 복사하며 SDK 전체는 복사하지 않습니다. 실행기는 명시적인 호환 템플릿을 사용하며 PATH 검색이나 컴파일러 호출을 하지 않습니다. 템플릿은 계획 시 고정되며 변경하면 계획/캐시가 무효화됩니다. PE/ELF는 네이티브 구성 요소를 검증된 캐시에 구체화하고 Mach-O 등은 사용자 공급자로 처리합니다. 네이티브 의존성이 다르면 플랫폼/ABI별 출력이 필요합니다.

아티팩트 참조는 선언된 입력 필드에서만 해석하고 공급자 `options`는 불투명 JSON입니다. 입력 재보고로 고정 다이제스트를 바꿀 수 없습니다. 호스트 import/stdlib 루트는 프로젝트 기준이며 대상 루트보다 먼저 검색합니다. 보호 정책은 변환 전에 기존 리소스 서명을 검사합니다. 재암호화는 기존 봉투를 한 번 복호화하며 서명 바이트 변경에는 새 정책이 필요합니다. 새 서명은 기존 서명을 대체하고 변경 없는 복사는 서명을 유지합니다. 검증 실패 시 게시하지 않습니다. `--crypto-library <absolute-library> --keyring <absolute-file>`을 사용하고 키 파일을 배포물에 포함하지 마세요. 레지스트리 `publish`는 설치 배포물만 받습니다. `Resources/Schemas/veroscript-build.schema.json`과 Application 가이드를 참고하세요.

Windows Runtime 목록에는 준비된 MSVC 재배포 라이브러리와 라이선스 고지가 포함됩니다. Runtime을 포함하는 디렉터리／ZIP 앱은 이 파일들을 실행기 옆에도 배치합니다. 단일 파일 실행기에도 네이티브 로더의 사전 조건이 적용됩니다. 동적 CRT 빌드는 대상 시스템에 맞는 VC++ Redistributable이 필요합니다. 이 사전 조건을 없애려면 관련 네이티브 구성 요소를 일관된 정적 CRT 설정으로 소스에서 빌드하세요.

<!-- docs-navigation -->

---

[사용자 문서](../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
