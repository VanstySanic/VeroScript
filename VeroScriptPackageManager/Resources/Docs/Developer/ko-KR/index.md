# 패키지 관리자 유지보수

클라이언트는 취득, ZIP, 해석, 영수증, 트랜잭션, 게시, 환경 조합을 맡습니다. Core는 해석된 모듈/리소스/네이티브 계약을 소유하며 HTTP/아카이브/해석기와 독립입니다. 서버는 계정, 권한, 불변 저장소, 정책과 인덱스를 담당합니다.

## 빌드와 소유권

데스크톱은 VEROSCRIPT_BUILD_PACKAGE_MANAGER=ON, embedded/mobile/Wasm/Python-only는 기본 OFF입니다. 고정된 정적 curl, miniz, nlohmann/json, PicoSHA2와 라이선스는 모듈에 둡니다. VEROSCRIPT_PACKAGE_MANAGER_USE_SYSTEM_CURL 또는 기존 CURL 대상은 CURL 7.85+와 일치하는 실행 의존성이 필요합니다. 내장 curl 소스는 SHA-256으로 고정합니다.

VeroScriptPackageManager 설치 구성 요소는 헤더, 라이브러리, 스키마, 의존성 식별 정보와 법적 고지를 내보냅니다. 예제와 README는 설치하지 않으며 6개 언어 문서는 Resources로 게시합니다. 내장 curl의 빌드 구성 요소도 설치하며 Core SDK 패키징은 두 단계를 모두 실행합니다. 이후 게시할 Core SDK 버전과 실제 준비된 Core 라이브러리의 SHA-256으로 의존성을 확정합니다. 심볼 제거 이후에도 적용하며 네이티브 제품 버전이나 이전 빌드 해시만으로는 충분하지 않습니다. 외부 Core SDK 소비자 검사는 HTTP 전송도 생성하여 ZIP/HTTP 링크 의존성을 검증합니다. 소스 API의 영어 진단 및 구조화된 code는 패키지 표시 문구와 분리해 유지합니다.

## 트랜잭션과 수명

소스와 생성물을 스테이징한 뒤 불변 내용을 게시하고 복구 저널로 요구/잠금/상태/소유권을 커밋합니다. 직전에 계획 입력과 저장소 식별을 재검사하고 외부 편집을 보존하며 오래된 계획을 거부합니다. 복구/롤백에 취소된 토큰을 이어 쓰지 않으며 불완전 트랜잭션의 읽기는 거부합니다.

각 세대는 프로세스 간 파일 대여를 보유합니다. clean은 영수증, 모든 대상 설정, 소유권, 내용, 대여를 확인하고 수정/외부 파일을 보존합니다. 게시된 고아 세대도 완전한 소유 영수증과 대여 정보가 있어야 회수합니다. 버려진 스테이징은 호스트가 관리합니다. 콜백은 동기 차용이며 같은 환경에 재진입하지 마세요. 최종 소멸자까지 스냅샷/모듈 대여를 유지합니다.

재생 전 전체 저널의 허용된 고유 대상, string/null 데이터, 예산과 현재 파일을 검사합니다. 파일은 `old` 또는 `new`와 같아야 하며 다른 내용은 `recovery-conflict`로 저널과 외부 편집을 보존합니다. 각 쓰기 전에도 재검사하며 이미 복원된 값은 건너뜁니다. 롤백이 막히면 `recovery-required`로 트랜잭션을 유지합니다. 편집을 백업하고 기록된 값으로 되돌려 복구한 뒤 편집을 재적용합니다. `JournalRecoveryTests`는 부분 커밋, 롤백, 누락, 잘못된 저널, 중복 및 적용 중 편집을 검사합니다.

## 훅 컴파일

각 사전 검사/실행 전 컴파일은 `FLimits.HookConstantEvaluationLimits`를 Core의 `FCompileOptions.ConstantEvaluationLimits`에 전달합니다. 호스트 정책이며 JSON이나 잠금 파일에 저장하지 않습니다. 작업량, 호출/표현식 깊이와 평가 메모리는 양수여야 하며 캐시만 0을 허용합니다. 작업 전체의 기한, 취소, 호스트 분석 관찰자와 원래 진단을 보존합니다. VM 실행 제한은 독립적입니다. `ConstantHookTests`는 소스/라이브러리 가져오기, 제네릭의 버린 분기, uint64, 예산, 일회성 취소, 캐시 없는 재시도 및 제거 트랜잭션을 검증합니다.

## 검증

VeroScriptPackageManagerTests를 빌드하고 VeroScriptPackageManagerCliTests 및 VeroScriptPackageManagerSdkTests를 실행합니다. 불변 소스, 오프라인 캐시, 대상/선택 용도, 아카이브/이름, 인증 범위, 언어/서식, 형식/비밀 입력, 훅 예산, 중단/동시 편집, 정리 대여, 분석/정적/동적 네이티브 등록, 명시 JIT와 배포를 검증합니다. 다른 Core의 SDK 소비자를 부정 사례로 확인하고 플랫폼/장치는 별도 검증합니다.

```sh
cmake -S . -B build/package-manager -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVEROSCRIPT_BUILD_PACKAGE_MANAGER=ON
cmake --build build/package-manager --config Release --target VeroScriptPackageManagerTests VeroScriptCli
ctest --test-dir build/package-manager -C Release --output-on-failure -R VeroScriptPackageManager
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

[레지스트리 프로토콜](registry-protocol.md) · [사용자](../../User/ko-KR/index.md) · [스키마](../../../Schemas) · [예제](../../../Examples)

## 의존성 목록 파일

`PackageRequirements.cpp`는 환경 변경 전에 위치 인수와 `FRequest.RequirementFiles`를 확장합니다. UTF-8 검증, 순환 감지, 소스/시작 줄 진단 및 MaxManifestBytes/MaxMetadataBytes/MaxFiles/MaxDependencyDepth/MaxEdges 한도를 유지합니다. 소스와 설정이 호환될 때만 제약을 결합합니다. 입력 해시를 보관하여 커밋 전에 다시 확인하며 동시 편집은 requirements-changed로 실패합니다. 파일이 훅을 허용하거나 레지스트리 정책을 바꿀 수 없습니다. CLI 짧은/긴 옵션, C++, 중첩·따옴표 경로, 제약 해결 및 locked/offline 반복을 검증합니다. 프로토콜 필드와 진단 코드는 유지하며 목록 내용은 사용자 입력이고 번역 카탈로그가 아닙니다.

## 의존성 목록 서비스

`FRequest.RequirementFiles`는 CLI `-r`／`--requirement`와 같은 설치 전용 서비스입니다. 최상위 상대 경로에는 `Environment.WorkingDirectory`가 필요하고 중첩 경로는 해당 파일을 기준으로 합니다. `Private/PackageRequirements.cpp`는 관리 파일 쓰기 전에 입력을 해석하고 `FLimits`／`FWork`로 UTF-8 읽기, 포함, 항목 수와 누적 메타데이터를 제한하며 커밋 전에 해시를 재확인합니다. 정규화한 의존성과 출처는 `veroscript-project.json`／`veroscript.lock`에 저장되므로 일반 잠금 프로젝트에는 원본 목록이 필요 없지만 명시적인 `-r` 재실행에는 목록과 로컬 아카이브가 필요합니다. 코어, 레지스트리 엔드포인트와 형식 버전은 바뀌지 않습니다. 라이브러리, CLI 및 설치 SDK 검증에 [목록 사용법](../../User/ko-KR/index.md)을 포함하세요.

## 환경 및 출력 무결성

읽기 전용 선택은 프로젝트나 상태가 생성되지 않았어도 중단된 최초 설치를 거부합니다. 남아 있는 `committed` 로그는 식별 정보, 대상, 모든 현재 파일의 바이트와 `new` 일치를 검증한 뒤 읽을 수 있습니다. 읽기 작업은 로그를 삭제하지 않습니다. 최초 쓰기 전에 가장 긴 단계의 로그 예산을 확보합니다.

온라인 갱신은 산출물의 `signatures`도 변경 불가능한 잠금 메타데이터로 취급합니다. `pack`과 `deploy`는 기존 대상을 바꾸지 않고 준비한 출력을 원자적으로 게시합니다. 파일은 안전한 하드 링크를 사용할 수 있지만 디렉터리는 시스템의 교체 없는 이름 변경을 지원해야 합니다. 미지원 작업은 실패하며 덮어쓰기로 전환하지 않습니다.

조합은 컴파일 전용 의존성이 제외된 배포를 포함하여 스냅샷의 검증된 의존성 간선을 사용합니다. 설치 처리 결과에는 자신의 설정과 선택한 전체 의존성 문맥이 일치해야 합니다. 설치 처리가 없는 네이티브 로더는 실제로 참조하는 외부 라이브러리 제공 패키지의 변경 불가능한 식별 정보만 확인합니다. 불일치 시 `combine-processing`을 반환하며 필요한 그래프를 한 환경에 설치해야 합니다. 무관한 스크립트 의존성 변경은 로더를 무효화하지 않습니다.

배포는 각 패키지 설정을 패키지 값과 프로젝트 스냅샷 양쪽에 보존하며 Overlay로 재사용해도 일치시킵니다. 출력 디렉터리의 끝 구분자를 허용합니다. `list`와 `show`는 상태 파일 생성 전 최초 설치 중단도 거부합니다. 커밋 직전에 쓴 레코드와 보존할 레코드 전체를 다시 확인하며, 호스트 콜백 없이 원래 기한을 유지합니다. 롤백은 외부 편집을 보존하고 이미 쓴 레코드와 충돌하면 복구 로그를 남깁니다.

## 애플리케이션 및 빌드 통합

`VeroScriptPackageRuntime`은 고정 환경, 배포, 리소스, 네이티브 등록을 제공하며 HTTP, 의존 해석, 설치, 훅, 게시를 포함하지 않습니다. `FClientOptions.ArtifactDecoder`는 배포 파일 신뢰를, `FCoreEnvironmentOptions.ArtifactDecoder`는 보호된 라이브러리를 처리합니다. 개인 키와 콜백은 잠금 파일에 저장하지 않으며 서명 정책은 캐시에도 적용됩니다.

배포 기본 용도는 runtime이고 소스 앱은 runtime＋compile을 유지합니다. 소비자는 `AcquireDeploymentDirectoryLease` 공유 임대를, 게시자는 교체 전에 독점 임대를 유지합니다. `Build::FServices`는 `Deploy`와 `LeaseDeployment` 콜백을 제공합니다.

레지스트리 URL은 변경하지 않습니다. 선택적 `signatures`는 1~16개 비어 있지 않은 배열이며 distribution, 실제 크기, SHA-256, 알고리즘, 키 식별자를 결합합니다. 업로드 신청, 게시, 조회, 확인, 복구, 잠금, 미러가 같은 기록을 보존해야 합니다. 서버가 크기와 해시를 계산하고 기존 출력은 변경하지 않습니다. 같은 버전 추가는 메타데이터 일치, 대상 비중복, 원자성, 멱등성을 보장합니다. 개인 키는 전송하지 않으며 미지원 필드는 명확히 거부합니다.

`veroscript install name --application-options /absolute/trust-options.json`으로 신뢰 설정을 지정합니다. 로컬 서명은 `<archive>.sig.json`입니다. 직접 URL의 별도 서명 주소를 추측하지 않고 레지스트리 기록이나 명시적으로 받은 로컬 파일을 사용합니다. 설치 ZIP은 읽을 수 있는 상태를 유지하며 앱 컨테이너와 구분합니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md) · [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
