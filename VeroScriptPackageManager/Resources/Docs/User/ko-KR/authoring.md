# 배포물 작성

`pack`은 `veroscript-package.json`을 우선 읽고, 없으면 `veroscript-project.json`의 `packageManager.distribution`(객체 또는 프로젝트 기준 JSON 파일)을 사용합니다. 프로젝트 배포 시 별도의 `veroscript-package.json`을 생성하고 원본 프로젝트 설정을 제외합니다. 파일 필터에는 생성 매니페스트와 모든 선언된 진입 파일이 포함되어야 합니다.

## 매니페스트와 의존성

ZIP 루트에 veroscript-package.json을 둡니다. 이름은 소문자/숫자와 단일 하이픈으로 된 2–64자이며 예약 장치 이름은 제외합니다. 버전은 major.minor.patch와 선택적 사전 버전이며 빌드 메타데이터는 없습니다. 요구 사항은 정확한 버전 또는 공백으로 나눈 비교식 교집합입니다. 실행 시 경로, 대상, 식별 및 전체 의존성 그래프도 검사하므로 스키마만으로 유효성을 판정하지 마세요.

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

용도는 runtime/compile/development입니다. --development는 개발 의존성, 선택 의존성은 `packages.<name>.enabled=true`가 필요합니다. 사전 버전은 명시적 요구 또는 --prerelease로 선택합니다. 한 환경의 정규 이름은 버전과 출처를 하나만 갖습니다. 적용 가능한 runtime 루트만 배포되며 간접 설치가 다른 선언에 직접 접근 권한을 주지 않습니다.

## 리소스, 입력과 표시 문구

entries.modules는 공개 import 이름, resources는 이름 있는 리소스 그룹을 선언합니다. 패키지 경로는 이식 가능한 상대 경로이며 이탈, 링크, 장치 별칭을 허용하지 않습니다. 선택 리소스 부재와 영수증 소유 파일의 삭제/변경을 구분하고 무결성 오류를 정상 부재로 취급하지 마세요.

현지화 루트에는 정규화된 BCP 47 디렉터리와 en-US JSON 기준이 필요합니다. 번역은 키를 생략할 수 있지만 추가, 파일 간 중복 또는 formatId 변경은 금지됩니다. 일반 문자열은 중괄호까지 리터럴이며 서식 메시지는 pattern과 formatId=vero.named를 명시합니다. 표준 키는 package.displayName, package.description, install.before/install.after, `inputs.<name>.description`이며 package.name과 package.version을 받습니다. 스크립트/컴파일러 진단 언어는 바꾸지 않습니다.

```json
{
	"package.displayName": "Demo Math",
	"install.after": {
		"pattern": "{package.name} {package.version}",
		"formatId": "vero.named"
	}
}
```

입력은 string/boolean/integer/enum 형식을 유지합니다. 일반 입력은 `--input=name=<JSON>`, 민감 문자열은 --secret-env=name=ENV_NAME을 사용합니다. 비밀은 해당 작업에만 남기며 선언적 템플릿에 쓰지 않습니다. 우선순위는 `<package>@<version>.<input>`, `<package>.<input>`, 단독 이름입니다. CLI 기본값은 비대화형입니다.

## 설치 처리와 한도

선언적 copy/template는 새 generated 영역에만 씁니다. 스크립트 훅에는 --allow-hooks와 내보낸 비제네릭 void entry(std::package::install_context)가 필요합니다. 허용 리소스와 생성 영역 쓰기 외 파일/환경/콘솔/태스크/주소/시계 서비스 및 패키지 네이티브 등록은 사용할 수 없습니다. 취소와 VM 예산은 협력적이며 OS 샌드박스가 아닙니다.

기본값은 훅 1,000,000명령, 2,000ms, 리소스 텍스트 서비스 작업 16MiB, 훅 버퍼 64MiB, 표시 65,536바이트이며 전체 힙 제한이 아닙니다. 훅에는 형식화된 입력과 독립 표시 스냅샷이 전달됩니다. 신뢰하는 사용자 프로세서는 버전과 구현 식별을 갖는 호스트 서비스입니다. 생성에 영향을 주는 설정 변경 후에는 재설치해야 합니다. uninstall --skip-hooks는 경고를 내는 명시적 복구 옵션입니다.

훅 컴파일은 Core의 `constexpr`, `if constexpr`, 제네릭 및 소스/`.vslib` 가져오기를 사용합니다. 각 컴파일이 공유하는 상수 평가 예산은 기본 작업량 1,000,000, 호출 깊이 64, 표현식 깊이 256, 평가 메모리 16 MiB, 캐시 8 MiB입니다. 호스트는 `FLimits.HookConstantEvaluationLimits`로 설정하며 캐시를 끄는 `MaxCacheBytes`만 0을 허용합니다. 사전 검사, 실행 전 컴파일 및 제거 훅에 같은 정책을 적용합니다. 컴파일은 패키지 작업 전체의 기한과 취소를 따르며 2,000 ms VM 제한은 실행 단계에 별도로 적용합니다. 수치와 버퍼 제한도 실행 시 정책입니다.

## 레지스트리와 게시

프로젝트 설정과 잠금 파일에 공식 소스가 없으면 CLI는 기본적으로 https://veroscript.org/api를 사용합니다. --registry-api는 이 대체 주소를 바꾸며 packageManager.registries.official은 프로젝트 소스를 설정합니다. 임베딩 호스트는 계속 FClientOptions.OfficialApi 또는 프로젝트 소스를 명시적으로 설정합니다. HTTPS는 인증서를 검사하며 --allow-http는 명시적 개발 용도입니다. --credential-env는 Authorization 값(예: Bearer YOUR_TOKEN, 필드 이름 Authorization: 제외)을 읽습니다. --credential-scope를 생략하면 자격 증명은 --registry-api 또는 내장 공식 API로 제한됩니다. 해당 출처와 경로 범위에 속하는 URL에만 자격 증명을 보냅니다. 범위 밖의 프로젝트 API나 미러에는 --credential-scope를 명시하세요. 자격 증명을 잠금이나 소스 매니페스트에 저장하지 마세요.

설정된 미러에는 trustedMirror=true가 필요하며 --mirror는 해당 명령에서 공식 소스만 허용합니다. allowOriginFallback의 기본값은 false입니다. 출처나 범위가 바뀌는 리디렉션은 민감한 헤더를 제거하며 HTTPS를 하향 전환하지 않습니다.

## 추가 플랫폼 게시

publish로 버전을 만들거나 같은 버전에 다른 플랫폼 아티팩트를 추가합니다. 제공하는 모든 아카이브는 패키지 이름, 버전, 의존성 및 충돌 조건이 같아야 합니다. 매니페스트 대상은 서로 겹치거나 기존 게시물의 대상과 겹치면 안 됩니다. OS나 아키텍처 제약이 다르면 대상을 구분할 수 있지만 최소 버전이나 필수 기능의 차이만으로는 구분할 수 없습니다. 이식 가능한 아티팩트는 모든 대상과 겹칩니다.

기존 아티팩트의 ID, 바이트 및 메타데이터는 변경할 수 없습니다. 동일한 아티팩트는 다시 업로드하지 않고 재사용합니다. 기존 대상의 바이트를 바꾸려면 새 패키지 버전이 필요합니다. 아카이브 파일 이름만 바꿔도 다른 대상이 되지는 않습니다. 네트워크 결과가 불확실하면 원본 아카이브와 게시 캐시를 보관하고 같은 파일로 다시 시도하세요. 클라이언트는 레지스트리를 확인한 후 성공을 알립니다.

한 명령에 같은 아카이브를 두 번 넣지 마세요. 파일 이름이 달라도 바이트가 같은 입력 두 개는 거부됩니다.

```text
veroscript install example-math@0.1.1
veroscript publish ./library.vspkg
veroscript publish ./gui-0.1.1-windows-x64.vspkg ./gui-0.1.1-linux-x64.vspkg
veroscript publish ./gui-0.1.1-macos-arm64.vspkg
veroscript install example-math@0.1.1 --registry-api=https://your-host/api/v1
```

[패키지 스키마](../../../Schemas/veroscript-package.schema.json) · [현지화 스키마](../../../Schemas/veroscript-package-localization.schema.json) · [매니페스트 예제](../../../Examples/Local/library/veroscript-package.json)

## 네이티브 항목과 CLI 패키지

`entries.native`에는 `id`, `path`, `linkage`, `roles`가 필수입니다. 선택적인 `componentVersion`은 네이티브 설명자 버전을 검증하며 bindings/compiler 항목 분석에는 이 필드와 `catalog`가 모두 필요합니다. 지원 역할은 `bindings`, `compiler`, `provider`, `cli`이며 역할 집합이 질의한 라이브러리 설명자와 일치해야 합니다. 네이티브 패키지는 OS, 아키텍처, 환경, Core 빌드와 `abis.native-extension`이 일치하는 대상 식별 정보가 필요합니다. 해당 `veroscript env --json`에서 값을 얻고 패키지 버전만으로 추정하지 마세요.

`cli` 항목은 CLI 호스트 서비스에 명령과 옵션을 등록합니다. 선택 환경에 설치하고 활성화하면 라이브러리 선택 없이 등록 이름으로 바로 사용합니다. 비활성화한 패키지는 명령 탐색에 참여하지 않습니다. CLI 전용 항목은 분석 및 서비스 없는 호스트에서 건너뜁니다. bindings/compiler 복합 역할은 일반 분석 카탈로그와 컴포넌트 버전이 계속 필요합니다. 설치는 일반 불변 배포물과 로더 메타데이터를 검증합니다.

[명령 사용법](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md)

VeroScript 공식 설치 패키지 이름은 `vs-` 접두사를 사용합니다. 예를 들어 패키지 이름은 `vs-asmjit`이며 JIT 백엔드 ID는 계속 `asmjit`입니다. 공식 레지스트리는 승인된 공식 게시자에게 이 접두사를 예약합니다. 로컬 또는 비공개 소스의 같은 접두사는 공식 출처를 증명하지 않으므로 소스와 아티팩트를 검증해야 합니다.

네이티브 ID는 ASCII 소문자와 숫자를 단일 하이픈으로 구분하며 처음과 끝에 하이픈을 쓰지 않습니다. 스키마는 2–64자를 허용하지만 네이티브 설명자의 64바이트에는 NUL 종료가 포함되므로 실제 모듈은 최대 63자를 사용하세요. 선언 ID는 설명자 및 예상 ID와 같아야 합니다. `entries.backends[].module`은 선언된 네이티브 공급자 ID이며 호스트 서비스 ID가 아닙니다.

## 공식 백엔드 및 GUI 배포물

JIT 및 스크립트를 지원하는 기반 모듈 배포물은 제품 디렉터리가 포함된 이식 가능한 ZIP/tar와 함께 설치 가능한 `.vspkg`를 제공합니다. 루트에 `veroscript-package.json`이 있는 아카이브만 설치할 수 있습니다. 기본 설치 대상은 현재 프로젝트입니다. `--path`는 상대 또는 절대 프로젝트 경로를 받으며 `--root`는 Runtime/SDK 환경을 선택합니다. `vs-asmjit`의 JIT 백엔드 ID는 `asmjit`입니다.

```text
veroscript install ./vs-asmjit-0.1.1-win-x64.vspkg --path ./MyProject
veroscript --path ./MyProject --jit-backend asmjit --backend jit ./MyProject/main.vs
```

GUI 어댑터는 기존 호스트 서비스 `veroscript.gui.tools`를 사용합니다. 호환되는 GUI 지원 CLI/Runtime 또는 GuiScript를 명시적으로 조립한 호스트를 사용하세요. 설치만으로 순수 Core에 GUI가 내장되지는 않습니다. SDL3, RmlUi, Stb, FontBackend는 별도로 설치하고 선택하는 제공자입니다. 오프라인 설치에서는 프로젝트 의존성 또는 명령 인수에 의존 아카이브를 명시하세요. 인접 파일이 자동으로 의존성 소스가 되지는 않습니다.

## 백엔드 항목과 예약 선택 값

`entries.backends[]`에는 `service`와 `id`가 필요하며 패키지 ID 문법(소문자 ASCII 영숫자를 단일 하이픈으로 구분하는 2–64자)을 따릅니다. `auto`와 `none`은 선택용 예약 값이므로 백엔드 ID로 사용할 수 없습니다. 두 형식 중 하나만 사용하세요. `module`은 선언된 `provider` 역할 네이티브 항목을 참조하며 `path`와 `contract`를 제외합니다. 또는 `path`와 `contract`로 전용 VM/JIT 라이브러리를 지정하고 `module`을 제외하며 `service`와 `contract`를 모두 `vm` 또는 모두 `jit`로 설정합니다. 네이티브 대상과 ABI 검사도 적용됩니다. 프로젝트 `services`에서 `backend`를 생략하면 자동 선택을 상속하고, 명시적 빈 값이나 `none`은 비활성화하며 `auto`는 자동 후보를 요청합니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md) · [en-US](../en-US/authoring.md) · [fr-FR](../fr-FR/authoring.md) · [ja-JP](../ja-JP/authoring.md) · [ko-KR](authoring.md) · [zh-CN](../zh-CN/authoring.md) · [zh-TW](../zh-TW/authoring.md)
