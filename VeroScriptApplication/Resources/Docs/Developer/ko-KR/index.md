# Application 서비스 유지 관리

## 구조와 경계

`VeroScriptApplication`은 Core 상위 서비스입니다. Core는 컴파일 이미지와 `.vslib`를 인코딩하고 Application은 표준 ZIP `.vsapp`, 설명자, 목록, 준비 및 콘텐츠 수명을 관리합니다. ZIP, 암호 어댑터, GUI 통합은 Core 외부에 둡니다. `VeroScriptApplicationPackages`는 HTTP 없는 `VeroScriptPackageRuntime`을 명시적 고정 환경에 연결합니다. 앱 열기가 의존성 해결이나 설치를 수행해서는 안 됩니다.

`BuildApplication`은 봉인된 콘텐츠 파일을 반환하고 `WriteArchive`가 외부 ZIP을 직렬화합니다. 원시 이미지 오버로드는 바이트와 메타데이터를 유지해야 합니다. `FApplication::Open`은 저장 콘텐츠를 검증합니다. `ReadEntryPackage`는 공급자나 환경 팩터리를 로드하지 않고 컴파일 메타데이터를 검사합니다. `Prepare`는 환경과 바이트 소유자를 유지하며 `FPreparedApplication::CompileSource`는 소유한 이식 가능 소스 기본값을 적용합니다. Session 로드와 다시 로드는 이 공통 경로를 사용해야 합니다.

## 무결성, 소유권과 정책

디렉터리, ZIP, 단일 파일에서 목록, 경로 정규화, 로컬 ZIP 헤더, 파일 모드, 크기/압축 해제 제한 및 해시 검사를 일치시키세요. 추가 파일, 경로 이탈, 대소문자 충돌, 특수 파일을 거부합니다. 쓰기 가능한 캐시는 소유 기록과 협력 프로세스 임대를 유지하며 외부 디렉터리를 거부합니다. 네이티브 콘텐츠 사용자는 구체화된 파일의 임대를 유지합니다. 다시 로드는 코드와 환경을 함께 준비하고 실패 시 모두 복원합니다. 네이티브 카탈로그 변경은 새 앱 수명을 요구합니다.

보호 서비스는 Core에 평문을 전달하기 전에 콘텐츠를 인증합니다. 서명 신뢰와 독립 키 임계값은 소비자가 정하며 필수 서비스가 없으면 평문 대체 없이 실패합니다. 서명된 콘텐츠 재구성에는 새 서명 정책을 명시해야 합니다. 암호 모듈 소유자를 유지하고 비밀 키나 콜백을 직렬화하지 마세요. 콘텐츠 서명과 플랫폼 서명을 분리하고 소스 기본값에 개발 전용 경로를 넣지 마세요.

## 통합과 검증

루트 빌드가 선택 모듈을 제어하며 `CMakeLists.txt`는 Application과 패키지 어댑터를 별도로 내보냅니다. Core 식별자, SDK 내보내기, `Resources/Schemas`, Interop/언어 어댑터, CLI, 런타임 위임을 함께 유지하세요. 루트에서 활성화하는 `VeroScriptApplicationTests`와 선택적 `VeroScriptApplicationPackageServicesTests`는 컨테이너/준비 및 환경 통합을 다룹니다. 손상, 신뢰 실패, 소유자 수명, 롤백, 소스 컴파일, 이동 실행, 디렉터리/ZIP/단일 파일 동등성을 검토하세요. 부트스트랩, 플랫폼 서명, 기기 실행에는 별도 기록된 검증이 필요하며 정적 문서 검사로 이를 입증할 수 없습니다.

`BuildApplication`은 이미 생성된 Core 응용 프로그램 이미지도 받아 바이트와 호스트 메타데이터를 보존합니다. `FApplicationServices::PreparePackage`는 환경 준비 후 Session 로드 전에 컴파일된 진입점의 호스트 서비스를 설정하며 이미지 검사만으로는 호출되지 않습니다. `ConfigurePackageApplicationServices`의 선택적 `ProvidersReady` 콜백은 등록된 제공자, 유지된 환경과 스냅샷을 소유하는 백엔드 선택 함수를 임베딩 어댑터에 전달합니다. 선택 우선순위는 호스트 옵션, 응용 프로그램 서비스 설정, 프로젝트 기본값이며 내장 배포의 기본값과 명시적 비활성화도 유지합니다. GUI는 이 훅으로 표준 `.vsapp` 컨테이너를 사용하며 Core에 GUI 또는 ZIP 의존성을 추가하지 않습니다. `PreparePackage`는 바인딩과 리소스만 준비하며 진입 프로그램을 실행해서는 안 됩니다.

<!-- docs-navigation -->

---

[사용자 가이드](../../User/ko-KR/index.md) · [개발자 문서](../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
