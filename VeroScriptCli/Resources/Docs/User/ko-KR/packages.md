# 패키지 및 배포

VeroScript는 실행 가능한 애플리케이션과 다른 스크립트에서 가져온 라이브러리에 대해 별도의 패키지 형식을 사용합니다.

| 형식 | 목적 |
| --- | --- |
| `.vsapp` | 실행 가능한 애플리케이션 패키지. |
| `.vslib` | 다른 스크립트에서 가져온 바이너리 라이브러리 패키지입니다. |
| `.vsbc` | 배포 패키지가 아닌 바이트코드 캐시 파일입니다. |
| `.vspkg` / `.zip` | 루트에 `veroscript-package.json`이 있는 ZIP 설치 배포물입니다. 두 확장자는 대소문자를 구분하지 않습니다. |

## 애플리케이션 패키지 구축

`--emit-package`을 사용하여 출력 파일을 선택합니다.

```powershell
veroscript --emit-package DemoApp.vsapp main.vs
```

응용 프로그램 패키지에는 일반적으로 해당 항목 모듈과 모듈 종속성이 포함됩니다. 다음을 사용하여 실행하세요.

```powershell
veroscript DemoApp.vsapp
```

## 라이브러리 패키지 구축

라이브러리 소스는 공개 API를 명시적으로 내보내야 하며 일반적으로 애플리케이션 진입점을 제공하지 않습니다.

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
```

다른 스크립트는 일반 `import` 구문을 사용하여 라이브러리를 가져옵니다.

```VeroScript
import "DemoLibrary.vslib";
```

`export`이 없는 선언은 모듈 전용으로 유지되며 패키지 API의 일부가 아닙니다.

## 패키지 옵션

| 옵션 | 목적 |
| --- | --- |
| `--package-file <file>` | 패키지에 소스 모듈을 추가합니다. 반복 가능. |
| `--package-version <N>` | 패키지 버전을 설정합니다. |
| `--strip-package-debug` | 소스 파일 및 디버그 라인 테이블 정보를 제거합니다. |
| `package inspect <package>` | `.vsapp` 또는 `.vslib` 패키지를 검사합니다. |

```powershell
veroscript package inspect DemoLibrary.vslib
```

## 소스 및 바이너리 패키지

개발 중에 `.vs` 소스 모듈을 직접 가져오고 배포를 위해 라이브러리를 `.vslib`로 컴파일합니다. 두 형식 모두 동일한 모듈 가시성 규칙을 사용하지만 바이너리 패키지는 이를 사용하는 런타임 및 바인딩과 호환되는 상태를 유지해야 합니다.

## 확장 패키지 설치

선택적 패키지 관리자는 기본적으로 명령 시작 디렉터리의 프로젝트에 설치합니다. `--path`는 다른 프로젝트를 지정하고, `--root`는 CLI에 속한 검증된 Runtime/Core SDK를, `--root --path`는 다른 검증된 구성 요소를 선택합니다. 상대 경로는 시작 디렉터리를 기준으로 합니다. Runtime은 `Packages`, 프로젝트와 SDK는 `.veroscript/packages`를 사용합니다. 실행, 검사, eval, REPL은 먼저 프로젝트를 찾고 없으면 CLI 자체의 구성 요소만 사용합니다. 사용자 설치 범위나 암시적 환경 병합은 없습니다. AOT도 지원 범위 내에서 같은 선택 규칙을 사용합니다.

확장 배포물은 표준 ZIP 데이터를 사용하며 `.vspkg`와 `.zip` 확장자의 대소문자를 구분하지 않습니다. `pack`의 기본 파일명은 `<이름>-<버전>.vspkg`이며 `.zip` 출력도 명시할 수 있습니다. `install`과 `publish`는 같은 내용과 루트 `veroscript-package.json`을 검증합니다. 손상되거나 잘못된 파일은 이름을 바꿔도 거부됩니다. 다운로드 URL에 확장자는 필요 없으며 해시와 오프라인 캐시 재사용은 실제 바이트를 기준으로 합니다.

```sh
veroscript pack ./library
veroscript pack ./library --output ./library.zip
veroscript pack ./library --output ./library-0.1.1.vspkg
veroscript publish ./library-0.1.1.vspkg
veroscript install ./library.vspkg
veroscript install ./library.zip --path "./My App"
veroscript install ./library.vspkg --root
veroscript install ./library.vspkg --root --path "D:/VeroScript/SDK/Core/0.1.1"
veroscript --path "./My App" "./My App/main.vs"
```

[확장 패키지 설치와 관리](../../../../../VeroScriptPackageManager/Resources/Docs/User/ko-KR/index.md)

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
