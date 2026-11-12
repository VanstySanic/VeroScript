# 개발자 문서 — VS Code

에디터/네이티브 공통 책임과 여섯 언어 유지보수는 [공통 에디터 통합](../../../../../../../../Resources/Docs/Developer/ko-KR/editors.md)에서 관리합니다.

Core, Tooling, VM, JIT 및 GUI 공급자는 C++ 언어 모드를 포함하여 일치하는 네이티브 소스/빌드 구성을 사용해야 합니다. 제품 버전만 같다고 ABI 호환성이 보장되지는 않습니다. SDK 통합 테스트는 정적 제네릭, handle 및 중첩 형식 수신자의 완성과 호버를 검증합니다. 선택적인 `GetLastJitDiagnostics` 스냅샷은 C++ 호스트 API이며 현재 LSP/DAP 통신 프로토콜에는 노출되지 않습니다.

UI 메시지는 `vscode.l10n.t`와 `Resources/Localization/Bundles`를 사용합니다. `Resources/Localization/<locale>/manifest.json`에서 `package.nls` 파일을 생성하며 인수 번호와 매니페스트 참조를 유지합니다.

Node.js 22.18.0+와 데스크톱 VS Code 1.106.0+를 사용하고 모듈에서 아래 명령을 실행합니다. 단위 테스트는 설정/UI, 통합 테스트는 데스크톱 Electron/VS Code와 현재 Runtime이 필요합니다. domain fixture는 VM을 실행하지 않습니다. package:contents/VSIX 라이선스를 확인하고 네이티브, 원격 호스트, 실제 앱 domain은 별도로 검증합니다.

```sh
npm ci
npm run typecheck
npm run build
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
```

데스크톱 Extension Host 테스트 모음은 하나씩 실행합니다. 여러 VS Code 테스트 창을 동시에 열면 완성 확인, 실행 취소 및 Enter 테스트에 필요한 편집기 포커스가 서로 바뀔 수 있습니다.

네이티브 인수 테스트는 CLI 호출마다 180초를 허용하며 사용하지 않는 호스트 매크로를 넣지 않아 인수 전달을 전체 GUI 콜드 컴파일과 분리합니다. 매크로 구성은 전용 구성／프로토콜 테스트에서 다루며 실제 GUI 컴파일은 별도 인수 검증을 거칩니다. 이 테스트 예산은 LSP나 디버그 제한 시간을 바꾸지 않습니다. 실제 프로세스 인수를 비교하기 전에 JIT 테스트 경로를 정규화하세요.

[Standards](../../../../../../../../Resources/Docs/Standards/ko-KR/GENERAL.md)



## 호스트 통합 및 domain 프로토콜

C++, UE 등의 호스트는 호환 TCP DAP와 실제 네이티브 바인딩을 제공해야 합니다. 임베딩 예제의 RunDapTcp는 Stdio.LiveBindings와 VeroScriptDapTcp::Run을 사용합니다. 수명, 실행 스레드, 연결 종료 후 수신 대기는 호스트가 관리합니다. 플러그인은 스크립트 VM을 디버깅하며 네이티브 코드는 해당 디버거를 사용합니다. domain 모드는 호스트가 준비한 브리지가 필요하며 이미 실행 중인 JIT VM을 인계받을 수 없습니다. 편집용 bindingCatalogPaths, 가져오기, 매크로, 표준 라이브러리를 호스트와 맞추세요. 메타데이터는 로컬 Runtime에 실제 콜백을 등록하지 않습니다. 연결 명령은 컴파일 설정을 호스트에 맡기며 재정의는 launch.json에서 명시합니다. 설정 범위 밖의 호스트 전용 의존 소스는 직접 저장하세요. 주소는 원격 환경을 포함한 확장 호스트에서 해석됩니다.

호스트는 IVeroScriptLiveDebugDomain을 제공하고 initialize에서 veroscriptSupportsDomainAttach: true를 선언해야 합니다. 선언이 없으면 program으로 전환하지 않고 실패합니다. 초기 연결/initialize 제한은 10초입니다. 출력에 식별 정보와 불투명한 리비전을 기록하며 소스 자동 비교나 LSP 동기화는 제공하지 않습니다. 변수 편집은 veroscriptCanSetVariable을 따르고 지원되지 않는 일시 정지/편집은 오류를 반환합니다. 연결 종료는 terminateDebuggee: false를 보내며 연결이 끊겨도 자동 재연결하지 않습니다. 취소, 수명, 안전 지점은 호스트가 관리하며 실행 전에 브리지를 준비하고 현재 단일 컨텍스트의 디버그 호출을 직렬화합니다. 실제 UE 통합은 별도 검증이 필요합니다.

호스트 플래그는 단계별 실행, 표현식 평가, 조건부 중단점도 제어하며, 생략된 플래그는 미지원으로 처리합니다. 플러그인은 사용 가능한 VS Code 기능 정보를 갱신하고 지원하지 않는 요청을 명시적으로 거부합니다. 일반 중단점은 계속 사용할 수 있습니다. 호스트가 veroscriptCanTerminate를 선언해도 중지/연결 해제 시 취소는 호스트가 관리합니다. 호스트의 `terminated` 이벤트 후에도 `terminateDebuggee: false`로 편집기의 마지막 `disconnect` 절차를 완료합니다. 대기 중인 작업은 한 번만 실패하고 늦은 응답과 중복 종료 이벤트는 무시합니다. 전송 연결이 이미 닫혔다면 마지막 연결 해제 요청에 로컬로 응답합니다.

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/Developer/ko-KR/workspace-and-debug.md)

## 메모리 검사 계약

DAP `memoryReference`와 `veroMemory` 필드는 JavaScript의 안전한 정수 범위를 넘는 값도 불투명한 십진 문자열로 보존합니다. 검사 하위 항목은 읽기 전용이며 핸들은 현재 일시 정지 동안만 유효합니다. 호스트의 기능 선언을 전달하고 메타데이터만으로 `readMemory`/`writeMemory` 지원을 추론하지 마세요. 통합 테스트는 네이티브 메모리 타입의 편집, 실행, 검사, 디버깅 및 도메인 메타데이터 전달을 확인합니다. 도메인 프로토콜 테스트 대역은 실제 호스트 VM을 검증하지 않습니다.

## 클라이언트 작업 처리와 양방향 DAP

연속된 편집기 이벤트는 최신 문서 상태 동기화를 한 번으로 합칩니다. 처리 중 도착한 이벤트와 명시적 재시작 요청은 다음 처리로 유지하며 현재 처리가 실패해도 계속합니다. 도메인 DAP는 로컬 이벤트를 추가하고 출력 메시지 번호를 다시 부여하므로 역방향 요청 응답의 `request_seq`를 호스트의 원래 번호로 복원해야 합니다. 응답 순서가 바뀌어도 대응을 유지합니다. 프로토콜 테스트 대역은 터미널이나 하위 디버깅 세션을 시작하지 않고 번호 대응을 검증합니다.

## 네이티브 모듈 소스 연동

`ModuleSources`는 언어 클라이언트 경계에서 URI를 변환하고 공개된 `moduleSourceMethod`를 호출합니다. 편집기 URI는 원래 프로토콜 URI, 서버 수명 및 컴파일 지문을 유지하며 서버에는 원래 URI만 보냅니다. 읽기 전용 뷰는 소스 오버레이나 쓰기 가능한 편집 대상이 되지 않습니다. 없거나 종료되거나 취소되거나 변경된 스냅샷을 거부하고 응답 대기 중 변경도 확인합니다. 모듈 심볼과 진단은 소유 클라이언트에 속합니다. 자동 재시작 시 클라이언트 객체와 컴파일 지문이 같아도 새 소스 수명을 시작합니다.

`VEROSCRIPT_TEST_MODULE_LSP`를 `VeroScriptModuleEnvironmentToolingTests` 실행 파일의 절대 경로로 설정하면 Extension Host에서 네이티브 `--lsp` 테스트 대역을 실행합니다. 생략 시 건너뛰므로 네이티브 환경 검증으로 볼 수 없습니다. 기본 Runtime 서버는 네이티브 시작 옵션을 사용하며 처리기 JSON 설정이나 CLI 패키지 환경 자동 주입은 추가하지 않습니다. DAP `sourceReference`와 고정 소스 응답은 VS Code 표준 처리를 따릅니다.

어노테이션 이름 비교, 표준 표기 및 시그니처 매개변수 파싱은 네이티브 도구가 담당하며 여러 줄 라벨, 중첩 제네릭과 쉼표가 포함된 리터럴을 지원합니다. 결과는 SDK를 통해 전달합니다. 통합 테스트는 클라이언트를 교체하지 않고 같은 카탈로그의 `caseSensitive` 정책을 변경하며, VS Code에서 `source.organizeImports` 편집을 적용해 정확한 범위, CRLF 유지와 멱등성을 확인합니다.

## 변수 표시와 정확한 수치 형식

SDK와 도메인 브리지를 통해 각 DAP 변수의 전체 `presentationHint`, 특히 `attributes: ["readOnly"]`를 보존합니다. `supportsSetVariable`과 `veroscriptCanSetVariable`은 세션 작업 기능을 나타내며 모든 변수를 쓰기 가능하게 만들지는 않습니다. 최종 검증은 네이티브 코드가 담당합니다. 회귀 사례는 변경 불가능한 `variant`의 하위 항목, 중첩 값, 조사식 하위 항목과 일반 지역 변수 편집을 다루며, 도메인 편집 기능의 활성·비활성 상태에서 정보 전달을 확인합니다.

정확한 수치 형식은 네이티브 이름과 문자열 값을 유지합니다. 문맥별 TextMate 사례는 `std::bigint`, `std::rational`, `std::numeric::rounding_mode`를 다루며 마지막 이름을 키워드로 예약하지 않습니다. 추가하거나 수정한 사례는 일치하는 도구로 형식 검사와 실제 Extension Host/VSIX 검증을 실행하기 전까지 미검증 상태이며, 이전 보고서로 새 네이티브 소스의 검증을 대신할 수 없습니다.

## 동반 확장 도구 API

`Public/api.d.ts`가 API 1을 정의합니다. `getToolchain(scopeUri, sourceFile?)`은 기본 확장과 같은 Runtime, 컴파일 옵션, 환경, 실행 백엔드ㆍ인수, 소스 범위를 확인합니다. scope URI는 설정, 선택적 소스는 파일 변수를 결정합니다. CLI 버전을 조회할 수 있으므로 명시적 실행 때만 사용하세요. `compileArguments`에는 명령ㆍ진입점ㆍ실행 백엔드가 없습니다. GUI 검사／컴파일은 JIT 모듈 없이 VM을 사용합니다. VSIX에 `Public` 선언을 포함하고 테스트의 `ready`／`snapshot`을 유지하세요. 일치하는 Runtime으로 실제 Extension Host와 설치된 VSIX에서 이 API를 검증하세요. 네이티브 GUI 실행에는 별도 검증이 필요합니다.

인터프리터 구현은 별도의 BytecodeVm 모듈에 있습니다. 이동한 편집기 테스트 환경에서도 `Vm/`을 포함한 Runtime 전체를 유지합니다. 네이티브 LSP/DAP 시작 코드가 기본 팩터리를 제공하며, 편집기는 새 프로토콜 필드를 추가하거나 사용자 호스트의 팩터리를 추측하지 않습니다. SDK 회귀 테스트는 추론된 함수 캡처의 호버·완성·시그니처·정의, UTF-16 범위, 불완전한 편집 후 현재 수신 객체의 호버를 검증합니다.

네이티브 모듈 테스트 대역을 이동할 때는 `VEROSCRIPT_TEST_VM_BACKEND`도 Runtime의 `Vm/` 디렉터리에 있는 호환 네이티브 VM 모듈의 절대 경로로 설정하십시오. 이 설정은 테스트 대역의 빌드 시 기록된 VM 경로를 재정의합니다. 일반 Runtime LSP/DAP 프로그램은 네이티브 기본 팩터리를 계속 사용합니다.

publisher는 `package.json`의 선언을 따르며 GUI 종속성은 기본 확장의 식별자와 일치해야 합니다. 루트 README를 세부 정보 페이지로 포함하고 제외된 저장소 파일로 연결하지 마십시오. 기능 패키지의 문서 예외는 `extension/readme.md`만 허용합니다. Marketplace 게시에는 해당 등록 publisher의 권한과 검증 완료가 필요하며 매니페스트 변경으로 publisher가 등록되지는 않습니다.

파일 아이콘은 [공유 생성기](../../../../../../../../Resources/Assets/icons/README.md#regeneration)로 재생성합니다. 빌드 시 [공유 256px 제품 PNG](../../../../../../../../Resources/Assets/icons/veroscript-icon-256.png)를 확장 표시 아이콘으로 복사합니다.

<!-- docs-navigation -->

---

[개발자 문서](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
