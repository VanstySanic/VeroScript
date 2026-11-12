# 애플리케이션 컨테이너와 실행 서비스

실행 가능한 앱은 표준 ZIP `.vsapp` 컨테이너로 배포합니다. `veroscript-app.json`, `veroscript-app.files.json` 및 보통 컴파일된 진입점 `program/entry.bin`을 포함하며 소스 진입점도 지원합니다. `.vslib`는 컴파일된 라이브러리입니다. 설치 배포물 `.vspkg/.zip`은 별도의 루트 `veroscript-package.json`을 사용하며 사용 전에 설치해야 합니다.

`veroscript application.vsapp` 또는 `veroscript-runtime application.vsapp -- 인수`로 실행합니다. `--check`는 진입점을 실행하지 않습니다. 같은 콘텐츠로 디렉터리, ZIP, 선택적 PE/ELF 실행 파일을 만들 수 있습니다. Runtime, 의존 환경, 실행기는 각각 선택하며 로드 시 설치나 PATH 검색을 하지 않습니다.

C++에서는 `VeroScriptSession::LoadApplication` 또는 `CreateApplicationServices()` → `FApplication::Open` → `Prepare`를 사용합니다. 소스 진입점은 저장된 매크로와 옵션을 적용하는 `FPreparedApplication::CompileSource`로 컴파일합니다. C#은 `VeroScriptSession.LoadApplication`, Python은 `Session.load_application`을 제공합니다. C ABI 소유 핸들은 대응하는 파괴 함수로 해제합니다. JSON 옵션 경로는 절대 경로여야 합니다. `VeroScriptSession::LoadFile`과 `VeroScriptEngine::RunFile`도 응용 프로그램 컨테이너와 디렉터리를 인식합니다. `ReadEntryPackage`는 네이티브 팩터리를 로드하지 않고 이미지를 검사합니다.

[C++ SDK — 통합 순서](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/index.md#explicit-vm-selection)을 참조하세요.

모든 파일의 길이, SHA-256, 실행 모드를 검증하며 추가 파일, 잘못된 경로, 링크, 손상된 ZIP, 예산 초과를 거부합니다. 내부 UTF-8 경로는 최대 511바이트입니다. 쓰기 가능한 캐시는 앱과 분리되며 소유권 영수증과 프로세스 간 임대를 사용합니다.

선택적 libsodium 어댑터는 XChaCha20-Poly1305 IETF와 Ed25519를 제공합니다. 외부 ZIP은 읽을 수 있는 상태로 유지됩니다. 신뢰 키와 독립 서명자 임계값은 소비자가 정하며 실패 시 평문으로 대체하지 않습니다. 디렉터리와 단일 실행 파일에는 콘텐츠 서명을 사용하고 OS 코드 서명은 별도 단계에서 수행합니다. 키 파일을 배포물에 포함하지 마세요.

Runtime 위임은 한 번만 수행하며 인수, 출력, 종료 코드를 전달합니다. `VeroScriptSession::ReloadApplication`은 코드와 환경을 함께 변경하고 실패 시 복원합니다. Session 소유 스레드와 수명 규칙을 지키세요. 취소는 차단된 네이티브 콜백을 강제로 중단하지 않습니다. SDK는 `find_package(VeroScriptApplication 0.1.1 EXACT CONFIG REQUIRED)`로 사용합니다.

서명된 앱을 다시 구성할 때는 새 콘텐츠의 서명 정책을 명시해야 합니다.

GUI 응용 프로그램도 같은 `.vsapp` 컨테이너를 사용합니다. `gui-compile`이 컴파일된 GUI 이미지와 리소스를 제공하며 GUI 검사/미리 보기와 일반 CLI inspect/검사/실행은 같은 파일을 읽습니다. 실행하려면 GUI 지원 호스트와 직접 선택한 호환 제공자가 필요합니다.

## 호스트 옵션과 서명

`--application-options /absolute/host-options.json`으로 호스트 설정을 선택합니다. C ABI는 `vs_interop_application_services_create`, `vs_interop_application_read`, `vs_interop_session_load_application`과 대응 파괴 함수를 제공합니다. `cacheRoot`, `stdlibSearchPaths`, `cryptoLibrary`, `keyring` 경로는 절대 경로여야 합니다. 신뢰 옵션은 `requireSignature`, `minimumSignatures`, `requiredKeys`, `policyIdentity`이며 로드할 앱이 아닌 소비자가 제공합니다. 패키지 바이트 API는 일반 앱 ZIP도 읽습니다.

XChaCha20-Poly1305 IETF는 컴파일 이미지, 라이브러리, 명시적으로 선택한 리소스를 암호화하고 외부 ZIP과 메타데이터는 읽을 수 있게 둡니다. 암호화한 소스 가져오기와 설치 ZIP은 지원하지 않습니다. libsodium 어댑터에는 절대 경로 네이티브 라이브러리를 제공합니다. Ed25519 배열은 종류, 정확한 길이, SHA-256, 키 ID/버전을 결합한 1–16개 레코드입니다. 내부 서명은 `veroscript-app.files.json.sig.json`, 아티팩트 서명은 `<filename>.sig.json`입니다. 서명 삭제로 필수 정책을 끌 수 없습니다. 같은 키의 별칭은 독립 서명자로 세지 않으며 알 수 없거나 신뢰하지 않는 키는 임계값을 충족하지 못합니다.

위임은 네이티브 콜백이나 메모리의 비밀을 전달하지 않습니다. 이 서비스 자체는 GUI, 모바일 수명 주기, 스토어 승인을 제공하지 않습니다. 코드 교체 전에 외부 스크립트 값 루트를 해제하고 네이티브 카탈로그 변경 시 `LoadApplication`과 새 앱 수명을 사용하세요. SDK는 `VeroScript::VeroScriptApplication`에 링크합니다. 런타임 설명자는 Core, OS, CPU, 환경, ABI를 식별하며 패키지 설치 설명자와 독립적입니다.

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

## 독립 Runtime 실행 백엔드 옵션

호스트 옵션은 애플리케이션 경로 앞에, 앱 인수는 `--` 뒤에 둡니다. `--vm-backend`와 `--jit-backend`는 실행 서비스를 선택하며 platform/graphics/GUI/image/font/text-layout 제공자와 독립적입니다. `auto`, `none`, 호환 파일／디렉터리 또는 패키지 환경에 해석기가 있을 때 설치된 ID를 받습니다. 자동 VM은 동봉 제품 대체 VM을 사용할 수 있지만 명시적 비활성화는 덮어쓰지 않습니다. 다음 명령은 진입점을 호출하지 않고 검사／준비한 뒤 JIT를 비활성화하여 실행합니다. GUI 컨테이너에는 호환되는 GUI 지원 호스트와 제공자도 필요합니다.

```sh
veroscript-runtime --check --vm-backend auto --jit-backend none /absolute/application.vsapp
veroscript-runtime --vm-backend auto --jit-backend none /absolute/application.vsapp -- argument1
```

애플리케이션 콘텐츠와 캐시 경로에는 실제 파일시스템 항목을 사용합니다. macOS에서는 root 소유 시스템 별칭 `/tmp`, `/var`, `/etc`가 각각 `/private/tmp`, `/private/var`, `/private/etc`로 해석될 때만 허용됩니다. 다른 심볼릭 링크와 특수 항목은 계속 거부하며 콘텐츠나 캐시 안의 링크를 허용하는 규칙은 아닙니다.

<!-- docs-navigation -->

---

[사용자 문서](../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
