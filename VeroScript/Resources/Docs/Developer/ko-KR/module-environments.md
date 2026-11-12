# 모듈 환경과 네이티브 확장

## 책임과 진입점

Core는 해석과 활성 상태 필터링이 끝난 불변 스냅샷을 받고 공통 백엔드 선택 계약을 제공합니다. 프로젝트 설정 읽기, 버전 해석, 설치된 후보 제공은 클라이언트와 호스트가 담당합니다. Core는 다운로드, 압축 해제, 설치 또는 설치 위치와 시스템 경로 검색을 수행하지 않습니다. 패키지를 설치해도 모든 선언이 암묵적으로 가져와지지는 않습니다.

`FModuleEnvironmentDesc`를 구성하고 `FModuleEnvironment::Create`를 호출합니다. 반환된 공유 불변 환경을 `FCompileOptions::ModuleEnvironment`에 설정하고 실행 전에 `VeroScriptVm::SetModuleEnvironment`에 전달합니다. 소스 내보내기는 `FCodegenOptions::ModuleEnvironment`를 사용합니다. 빌드 가능한 공개 SDK 예제는 `Resources/Examples/ModuleEnvironment`에 있습니다.

## 모듈과 범위

`Scopes`는 소유자와 직접 의존성을 정의하고 `EntryOwnerId`는 진입점 범위를 선택합니다. `bHostProvided`는 모든 모듈이 호스트 범위에 접근하도록 명시적으로 허용합니다. `SourceRoots`는 소유권이 있는 정규화된 절대 경로입니다. `Modules`는 공개 가져오기 이름을 모듈 ID, 소유자, 콘텐츠 식별 정보와 파일에 정확히 연결하며, `Source`를 가진 `veroscript-module:/...` 메모리 모듈도 지원합니다.

일반 가져오기는 정확한 매핑을 조회하며 충돌하는 로컬 후보가 있으면 오류를 냅니다. 명시적 `./` 또는 `../`는 가져오는 파일 기준의 상대 경로만 사용하고 루트와 의존성 검증도 받습니다. 대상이 없어도 다른 검색 루트를 탐색하지 않으며 잘못된 매핑을 같은 이름의 파일로 대체하지 않습니다. `import`, `include`, 재내보내기 가져오기와 매크로 탐색은 같은 로더를 사용합니다. 기본적으로 소스 범위를 엄격히 제한하고 환경 및 작업 디렉터리의 암시적 탐색을 끕니다. 표준 라이브러리에도 검색 루트와 소유 범위를 명시해야 합니다.

모듈은 자신의 소스와 선언된 직접 의존성을 사용할 수 있습니다. 재내보낸 인터페이스에는 일반 언어 가시성 규칙이 적용됩니다. 네이티브 선언은 `FBindingOrigin`을 가지며 컴파일 시 소유 범위와 가져오기 가시성을 검사합니다. 공개 API 서명은 반환값 사용에 필요한 네이티브 타입을 노출할 수 있습니다. 이 의존성 격리는 신뢰할 수 없는 네이티브 코드를 실행하는 샌드박스가 아닙니다.

카탈로그와 리소스 계약을 한 번 게시한 뒤에는 선언이나 콜백 식별 정보를 바꾸지 않습니다. `Apply`는 컴파일 입력을 트랜잭션으로 병합하고 충돌하는 활성 카탈로그를 거부합니다. 같은 기본 레지스트리로 공통 스냅샷을 구성하고 독립적인 타입 ID 공간을 사용하는 활성 바인딩을 직접 이어 붙이지 마세요. 명시적 소스 오버레이는 계속 정확한 경로에 적용됩니다.

## 리소스 서비스와 현지화

`FResourceProviderBinding`에는 공급자, 소유자, 안정적인 계약 식별 정보와 선택적인 리소스 전용 `ModuleIds`를 넣습니다. `IResourceProvider`는 논리 요청 `(ProviderId, ModuleId, Group, Path)`, 취소 확인 콜백과 바이트 한도를 받습니다. 경로는 상위 디렉터리 이동이 없는 이식 가능한 상대 경로입니다. 성공했지만 리소스가 없는 결과와 입출력/공급자 실패를 구분합니다. 존재하는 리소스에는 콘텐츠 식별 정보가 필요합니다. 선택적인 물리 절대 경로는 진단 정보이며 공급자를 우회할 권한이 아닙니다.

실행 시 `FModuleEnvironment::ReadResource`, 컴파일 프로세서는 `Context.ReadResource`, 스크립트 컴파일 확장은 `read_resource(provider,module,group,path)`를 사용합니다. 읽은 콘텐츠와 부재 관측은 일반 의존성 기록에 저장되어 변경 시 준비된 결과를 무효화합니다. 공급자는 스레드 안전성과 한도/취소를 보장하고 무제한 읽기를 피해야 합니다. Core가 출력 한도와 식별 정보를 재검사합니다. `MaxResourceBytes`는 단일 읽기를 제한하며 컴파일에는 누적 입력과 의존성 예산도 적용됩니다.

환경을 통해 기존 `I18n::FI18nServices`, 타입이 있는 메시지, 컨텍스트와 한도를 재사용합니다. 패키지 메타데이터 JSON은 패키지 클라이언트가 해석하며 Core는 별도 지역화 형식을 추가하지 않습니다. 네이티브 확장은 `GetService(id,version)`으로 호스트가 제공하는 타입이 있는 서비스를 요청할 수 있습니다. 결정적인 서비스 식별 정보는 프로세서 캐시 키에 들어갑니다.

## 네이티브 등록과 수명

패키지 관리 네이티브 ID(`entries.native[].id`)는 `[a-z0-9]+(?:-[a-z0-9]+)*`를 따릅니다. 스키마는 2–64자를 허용하지만 NUL 종료 `Descriptor.Id[64]`에 들어가는 실제 ID는 ASCII 최대 63자입니다. 설명자, 매니페스트, `ExpectedId`가 같아야 하며 `entries.backends[].module` 및 정적 등록 키 `package:ID`도 같은 ID를 참조합니다. Core 저수준 로더는 길이가 제한된 UTF-8 계약을 검사하며 패키지 이름 문법을 강제하지 않습니다. 제공 모듈은 `veroscript-sdl3`, `veroscript-rmlui`, `veroscript-stb`, `veroscript-font-backend`, `veroscript-gui-tools`를 사용합니다. ID 변경 시 라이브러리, 카탈로그, 환경을 함께 다시 빌드하세요.

서비스 ID, 백엔드 선택 값과 인터페이스 계약 ID는 각자의 프로토콜을 따릅니다. 특히 네이티브 확장 `veroscript-gui-tools`가 요청하는 별도 호스트 서비스 `veroscript.gui.tools`는 `ToolServiceId` 조회 키로 유지됩니다.

`VeroScriptNativeExtensionLoader.h`를 포함합니다. `LoadNativeExtension`은 명시적인 절대 라이브러리 파일을 요구하며 패키지/이름/PATH/작업 디렉터리를 탐색하지 않습니다. 정적으로 연결한 모듈은 같은 질의 함수와 소유자로 `QueryNativeExtension`을 호출합니다. C 내보내기 심벌은 `veroscript_extension_query`, `NativeExtensionApiVersion`은 **0.1.1**입니다. 등록 전에 예상 ID, 버전과 역할을 검증합니다.

고정 레이아웃 질의 설명자는 공개 SDK ABI, Core 구현 식별 정보, 컴파일러/STL/런타임, 아키텍처/플랫폼, RTTI와 예외 모드가 일치해야 합니다. C 질의가 이후 C++ 객체까지 안정적인 C ABI로 만들지는 않습니다. MSVC 동적 확장은 공유 CRT를 요구하며 iOS와 WebAssembly는 정적 등록을 사용합니다. 바이너리 해시는 캐시 식별 정보이지 진위/서명 검사가 아닙니다. 클라이언트가 신뢰한 배포물을 검증하고 불변 파일의 임대를 유지합니다.

`FNativeExtensionRegistry::Add`로 모듈을 준비합니다. `Create`는 `INativeExtensionHost`를 받아 함수, 타입/멤버, 제네릭 템플릿, 별칭, 애너테이션, 컴파일 프로세서와 서비스 공급자를 등록합니다. 확장은 호스트 가상 함수 테이블을 사용하며 자체 정적 Core를 연결하지 않습니다. 등록은 원자적이며 실패하면 이전 스냅샷을 유지합니다. `GetBindings`, `GetProcessors`, `GetProviders`, `GetOwners`, `GetDependencies`, `GetFingerprint`로 해석된 환경을 만듭니다. 사용 가능한 계약은 패키지 버전만으로 추측하지 말고 검증한 로드 설명자와 바이너리 식별 정보가 담긴 `GetDependencies`로 채웁니다. 그 버전은 네이티브 컴포넌트 버전이며 배포 패키지 버전으로 자동 대체되지 않습니다. 모든 네이티브 바인딩 공급자에는 정확한 외부 의존성 계약이 필요합니다.

호스트 서비스는 소유 수명이 있는 버전별 인터페이스입니다. 선언은 등록 중에만 바꿀 수 있습니다. 이후 보유한 서비스는 호스트 객체 생성, 리소스 읽기와 브리지의 컴파일 작업 제출에 사용할 수 있습니다. `MakeHostObject`/`MakeHostObjectNamed`와 예외를 던지지 않는 파괴 콜백을 사용하며 소유권 제어 블록은 Core가 할당합니다. 콜백, 프로세서 팩터리/인스턴스, 공급자, 서비스 인스턴스와 외부로 전달된 객체가 살아 있는 동안 강한 참조가 모듈 코드를 유지합니다. 같은 등록을 소유하는 객체로 향하는 역방향 강한 참조 대신 약한 참조를 사용하세요. 팩터리와 소멸자는 오류/소유권 계약을 지키며 소멸자는 예외를 던지지 않습니다.

공급자 등록은 GUI나 다른 서비스를 초기화하지 않습니다. Core 상위 계층에서 `(ServiceId, BackendId)`를 선택하고 계약을 검증한 뒤 `CreateNativeService`를 호출합니다. 반환된 인스턴스는 소멸할 때까지 환경과 코드를 유지합니다. JIT는 기존 `LoadJitBackend` 프로토콜을 사용합니다.

네이티브 제네릭 템플릿은 기존 레지스트리 계약을 유지합니다. 불변 카탈로그를 게시하기 전에 준비 레지스트리에서 `FHostBindingRegistry::InstantiateGenericHostType`으로 필요한 구체 타입을 인스턴스화합니다. 실행 중에는 게시된 카탈로그를 바꾸지 않으며 스크립트 제네릭은 일반 컴파일러가 인스턴스화합니다.

## 캐시, 패키지와 다시 로드

컴파일 지문에는 그래프 소유권, 소스 식별 정보, 바인딩, 프로세서 버전/설정, 애너테이션과 컴파일 정책이 포함됩니다. 별도 호스트 설정 지문이 없어도 네이티브 등록 설정은 프로세서 식별 정보에 자동으로 들어갑니다. 리소스는 정확한 읽기 의존성을 추가하고 실행 전용 설정에는 별도 실행 식별 정보가 있습니다. 식별할 수 없거나 비결정적인 네이티브 호스트 서비스는 프로세서 캐시를 끕니다. 생성 결과에 영향을 주는 다른 불투명한 콜백/설정에도 호스트가 안정적인 식별 정보를 제공해야 합니다.

`FExternalDependency`는 정확한 ID/버전/ABI/빌드/콘텐츠 식별 정보를 기록합니다. Core는 버전 해결 없이 검증, 병합, 직렬화합니다. 필수 계약은 바이트코드, `.vslib`/`.vsapp`와 연결 이후에도 보존됩니다. 실행 계약이 없거나 불일치하면 초기화나 JIT 실행 전에 실패합니다. 열린 제네릭 패키지 소스는 생산자 범위를 유지하며 가져오는 호스트가 호환 네이티브 바인딩과 매핑된 요구사항을 제공해야 합니다. 패키지 보조 모듈의 소스가 라이브러리에 내장되어도 그 소유자 ID의 범위와 직접 의존성을 유효 환경에 선언해야 합니다. 제품과 바이트코드 버전은 **0.1.1**이며 이전 개발판 바이트코드는 허용하지 않습니다.

독립 AOT는 같은 환경과 생성 선언으로 순수 스크립트 결과를 내보낼 수 있습니다. 대상 런타임 어댑터가 배포와 바인딩 계약을 제공하기 전에는 실행 의존 네이티브 패키지 계약을 거부합니다. 확장 콜백을 관계없는 extern 선언으로 조용히 바꾸지 않습니다.

재로드는 유휴 경계에서 새 환경을 게시합니다. 진행 중인 작업은 이전 스냅샷과 파일 임대를 유지합니다. `NativeExtensionParallelRegistrations`가 없는 모듈의 재등록에는 프로세스 재시작이 필요합니다. 로드한 라이브러리의 제자리 교체는 거부하며 불변 버전 경로를 사용합니다. 네이티브 객체에는 등록 식별 정보가 있으므로 타입 이름이 같아도 이전 객체를 새 등록 콜백에 넘길 수 없습니다. VM/세션 스레드 및 취소 규칙은 그대로 적용되며 차단된 네이티브 콜백은 강제로 중단할 수 없습니다. 기존 워커는 원래 환경을 유지합니다.

## 검증과 통합

`VeroScriptModuleEnvironmentTests`, `VeroScriptNativeExtensionTests`, 공개 헤더 테스트와 `VeroScriptModuleEnvironmentExample`이 Core 계약을 검증합니다. 동적 테스트 라이브러리는 의도적으로 Core를 연결하지 않습니다. 각 배포 대상에서 고유한 로딩과 기기 검증이 필요합니다. 설치와 환경 선택은 패키지 클라이언트, CLI, Interop/언어 어댑터가 담당하며 Core는 해석된 스냅샷을 받습니다.

## 명령줄 호스트 서비스

`CommandLine` 역할(`0x8`, 패키지 표기는 `cli`)은 일치하는 SDK의 `veroscript.cli` 버전 `0.1.1` 서비스를 사용합니다. CLI가 `ICommandLineExtensionService`를 제공하고 네이티브 모듈은 Create 중 `GetService`로 얻어 등록이 닫히기 전에 설명자를 등록합니다. 복합 역할 모듈은 일반 SDK 호스트에 서비스가 없어도 동작해야 합니다. 패키지 클라이언트는 분석 및 서비스 없는 호스트에서 CLI 전용 항목을 건너뜁니다. 옵션 파싱과 명령 디스패치는 CLI가 담당합니다.

[CLI](../../../../../VeroScriptCli/Resources/Docs/Developer/ko-KR/process-and-assets.md)

## 백엔드 선택과 환경 어댑터

`VeroScriptBackendSelection.h`는 `ResolveBackendSelection`, `SelectServiceBackend`, `ResolveExecutionBackends`를 정의합니다. 요청을 생략하면 `SelectedBackends`를 상속하고 저장된 빈 값은 `none`을 뜻합니다. 명시적인 `auto`는 구성된 선택을 재정의합니다. 후보를 ID 순으로 정렬하고 빈 ID, 예약 ID, 중복 ID를 거부합니다. 자동 후보 재시도는 팩터리 생성／준비 실패에만 적용됩니다. 취소는 전파되며 실행 중인 작업을 재실행하지 않습니다.

`FBackendSelectionResult`는 `Selected`, `Disabled`, `Unavailable`, `Failed`와 `BackendId`, 거부된 시도, 오류를 보존합니다. 선택적 자동 서비스가 없어도 환경 구성이 성공할 수 있으므로 성공 여부와 반환된 팩터리를 함께 확인합니다. `FInstalledEnvironment::GetBackendCandidates`는 메타데이터만 읽고 비활성 패키지를 제외합니다. `LoadJitBackend` / `LoadVmBackend`는 선택 결과를 선택적으로 반환하며 `BackendDiagnostics`는 사용할 수 없는 자동 제공자 모듈의 진단을 보존합니다.

`FModuleEnvironmentDesc`는 `SelectedBackends`, `ServiceProviders`, `BackendDiagnostics`, `ExecutionBackends`를 전달합니다. 패키지 어댑터는 불변 해석 콜백과 `ExecutionBackends.Identity`를 제공하고 설치 스냅샷과 코드 소유권을 유지합니다. `AnalysisOnly`는 실행 해석기를 등록하지 않습니다. `FNativeServiceProviderDesc`는 선택 헤더에 선언되며 네이티브 도우미는 생성 전에 서비스／백엔드／계약／버전을 확인합니다. C, C++, C#, Python은 네이티브 Session 해석을 공유하며 별도 패키지 검색을 구현하지 않습니다.

`ResolveExecutionBackends`는 확정 전에 출력 사본을 준비합니다. 명시적인 `VmBackendFactory` / `VmBackendPath`가 환경보다 우선하고 팩터리, 경로, 명시적 해제는 서로 배타적입니다. `FallbackVmBackendFactory` / `FallbackVmBackendPath`는 제품 조립 입력이며 자동 선택으로 사용할 VM이 없을 때만 적용됩니다. 명시적 선택 실패나 비활성화를 대체하지 않습니다. `bDisableJitBackend`는 환경 JIT 기본값을 차단합니다. Session은 제품 대체 VM을 보존하며 `SetFallbackVmBackendFactory`로 변경하고 실행 전 `SetVmBackendPath("auto")`로 복원할 수 있습니다. JIT를 포함한 실행 시작 시 VM 도메인이 고정되며 Clear/reload로 해제되지 않습니다. 팩터리, 인스턴스, 보존 코드가 살아 있는 동안 모듈과 환경 소유자를 유지합니다.

모듈 환경에서 `Origin.ProviderId`가 있는 처리기는 해당 공급자에 접근할 수 있는 소스 소유자 또는 표시되는 공개 `Origin.ModulePath`의 이벤트만 받습니다. 패키지 생산자와 캡처된 소유자 정보는 실제 경로 조회보다 우선합니다. 공급자가 없는 호스트 전역 처리기와 환경 없는 호스트는 기존 범위를 유지합니다. 스크립트 처리기는 `import compiler`나 구성된 루트로 명시적으로 활성화합니다. 이벤트 필터는 소스, 리소스 또는 네이티브 바인딩 의존성 접근을 허용하지 않습니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [en-US](../en-US/module-environments.md) · [fr-FR](../fr-FR/module-environments.md) · [ja-JP](../ja-JP/module-environments.md) · [ko-KR](module-environments.md) · [zh-CN](../zh-CN/module-environments.md) · [zh-TW](../zh-TW/module-environments.md)
