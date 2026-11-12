# 코어 아키텍처와 컴파일 파이프라인

[교체 가능한 VM 백엔드](vm-backends.md)

바이트코드 식별자는 고정된 `VSBC`입니다. 형식 버전은 제품 버전을 따르며 현재 `0.1.1`입니다. 이전 개발 빌드의 산출물은 다시 생성해야 합니다.

코어는 lexer, parser, AST, preprocessor, compiler, bytecode, VM, code generation, JIT, module, package, debugger, host binding, reflection, serialization, concurrency, mutation, localization을 소유합니다.

## 파이프라인

컴파일은 source, overlay, import, package 해석으로 시작합니다. 토큰화와 전처리는 source location을 보존하고, public AST, semantic/type/generic/visibility 검사, debug 정보가 있는 bytecode, module/package, VM/JIT 실행으로 이어집니다. 모든 단계는 code, message, source location, 관련 token/declaration을 포함한 구조화 diagnostic을 반환합니다. 출력 스트림의 오류 한 줄은 result object를 대신하지 않습니다.

## 모듈, 가져오기와 매크로

`VeroScriptModuleLoader`는 program을 merge하기 전에 main module, ordinary/implicit import, stdlib module, package/template module graph를 해석합니다. import path와 stdlib root는 정규화하고 중복을 제거합니다. source overlay는 정규화 path로 disk read를 대체하므로 editor나 Web host가 file을 쓰지 않고 in-memory graph를 compile할 수 있습니다.

preprocessor `-D` definition과 structured predefined macro는 source fingerprint에 영향을 주기 전에 정규화됩니다. host type name은 parse 전에 제공되므로 `Counter c = Counter(1);` 같은 declaration이 host binding catalog로 분류됩니다. circular-import와 project-wide-access policy는 숨은 global compiler state가 아닌 명시 loader setting입니다.

stdlib root 또는 module resource를 추가하면 loader는 인접 `localization/` tree를 merge할 수 있습니다. locale merge는 canonical catalog ID로 key되며 `en-US` fallback을 유지하므로 resource package는 stdlib와 localization directory를 같이 deploy해야 합니다.

## 경계와 의미론

`Public/`는 supported header를, `Private/`는 compiler, parser, VM, JIT, internal data structure를 포함합니다. public-header test는 installed header가 없는 private file에 의존하지 않음을 보장합니다. 외부 tooling/language adapter는 public contract를 사용합니다. compiler AST나 runtime internal이 실제로 필요한 source-tree target은 CMake에서 private include path를 명시적으로 받고 SDK consumer가 아닌 review 대상 friend dependency로 취급합니다.

<a id="core-vm-ownership"></a>

Core는 프로그램, 값, 논리 프레임, 언어 연산, 실행 제어와 디스패치를 소유합니다. 전체 바이트코드 해석 루프는 Modules/Execution/VeroScriptBytecodeVm의 VeroScriptBytecodeVm에 속합니다. 백엔드 ID는 vs-bytecode이고 패키지 이름은 vs-bytecode-vm입니다. Core는 VM이나 JIT를 암시적으로 선택하지 않습니다. Runtime은 Vm/의 공식 동적 라이브러리를 명시적으로 구성하며 Interop, Web 및 모바일 정적 제품도 VM을 명시적으로 주입합니다. JIT: `Modules/Execution/VeroScriptAsmJit`; Core: `Private/JitSupport`.

## 정적 선언, 제네릭과 캡처

static field는 module-owned global로 등록되고 module initialization에서 초기화됩니다. static access는 type receiver나 평가된 object receiver를 사용할 수 있으며, 후자는 backing global을 읽기 전에 side effect를 위해 receiver를 계속 평가합니다. generic type instantiation은 field type을 substitute하고 static storage를 독립 등록합니다.

compiler는 lambda capture와 generic bound를 AST에 기록하고 source location을 bytecode diagnostic으로 전달합니다. static type restriction, capture validity, type-argument bound, inheritance rule은 compiler semantics입니다. frontend, AOT emitter, tooling consumer는 recorded declaration identity를 사용하고 display name에서 규칙을 재구성해서는 안 됩니다.

## 범위별 분석 제어와 의존성 메타데이터

`FAnalysisControlScope`는 호출 스레드의 동기 분석에 쓰는 취소 및 측정 콜백을 소유합니다. 중첩 범위는 이전 제어를 복원합니다. 캡처한 객체는 범위가 끝날 때까지 유효해야 하며, 각 `FAnalysisStage`는 제어 범위보다 먼저 끝나야 합니다. 조건자가 true를 반환하면 `CheckAnalysisCancellation()`이 `FAnalysisCancelled`를 던집니다. 구문 진단과 별도로 처리하며 VM 실행을 취소하지 않습니다. 취소된 렉서는 불완전한 토큰 스트림을 보존하지 않고 다시 분석할 수 있습니다.

`FAnalysisStage`는 같은 스레드에서 경과 밀리초를 보고하며 이름은 단계가 끝날 때까지 유효해야 합니다. 관찰 콜백의 예외는 무시하여 스택 해제 중 취소나 파싱 예외를 덮어쓰지 않습니다. 중첩 단계의 시간은 겹치므로 독립적인 작업 시간처럼 합산하면 안 됩니다.

`VeroScriptModuleLoader::GetLoadedFileStamps()`는 실제 소스나 패키지를 읽기 전에 기록한 수정 시각과 크기를 제공합니다. 메모리 입력과 오버레이에는 디스크 기록이 없습니다. 반환된 참조는 다음 로드 또는 로더 소멸 전까지 유효합니다. 캐시 사용자는 `bValid == false`를 거부하고 AST를 재사용하기 전에 디스크 메타데이터를 다시 확인하여 파싱 중 변경도 검출해야 합니다. 바이트 해시가 아니므로 문서 버전, 옵션, 오버레이는 별도로 무효화해야 합니다.

## 길이와 인덱스

컬렉션 길이와 선형 인덱스는 `int64`를 사용합니다. 기본 저장소는 `size_t`를 유지하며 축소 변환 전에 스크립트 및 플랫폼 범위를 모두 검사합니다. C# 컨테이너는 CLR 제한을 유지합니다. 해당 의미의 바이트코드 식별자는 `0x43425356`(`VSBC`)입니다. 32비트 길이를 반환하는 이전 캐시는 소스에서 다시 컴파일해야 합니다. C++ 호스트 및 AOT 출력도 갱신된 헤더와 표준 라이브러리로 다시 빌드해야 합니다.

## 공개 매크로

[전처리기 — 공개 매크로](../../User/ko-KR/language-basics/preprocessor.md#exported-macro-contract)을 참조하세요.

VSBC 0.1.1는 검증된 공개 매크로 표를 끝에 저장합니다. 캐시와 `.vslib`는 표를 보존하며 `.vsapp`는 빈 표를 기록합니다. 이전 바이너리 패키지도 다시 생성해야 합니다. 전처리는 최종 출력의 가져오기 지점에서 멈추고 대상 인터페이스 준비 후 계속됩니다. 모듈 준비에는 제한된 힙 스택을 사용합니다.

## 컴파일 시점 주석 처리

동결된 네이티브 처리기 레지스트리는 `FCompileOptions.AnnotationProcessing`, C++ Engine/Session, Tooling 또는 `FCodegenOptions`로 전달합니다. `@Name`은 `@Name()`과 같으며 순서가 있는 플래그, 이름 있는 스칼라 인수와 중첩 그룹을 보존합니다. 미등록 주석은 메타데이터로 남습니다. 단일 선언 단계에서 사용자 정의를 연결한 뒤 메서드 본문과 보조 메서드를 생성하고, 일반 검사, 가상 디스패치와 제네릭 치환을 적용합니다. 다른 파일의 정의는 선언과 구현 모듈을 모두 로드해야 합니다. 패키지는 생성 멤버와 열린 템플릿을 보존하므로 소비 시 생산자 처리기가 필요하지 않습니다. Tooling은 읽기 전용 `GeneratedDocuments`를 공개하며 내장 도구가 가상 문서를 표시합니다. 독립 도구는 네이티브 처리기를 자동 로드하지 않고 호스트가 등록합니다. C# / Python은 Interop을 통해 네이티브 서비스 핸들을 연결합니다. VSBC 0.1.1는 구조화된 주석과 생성 작업을 저장하므로 이전 캐시와 바이너리 패키지를 다시 생성해야 합니다. 네이티브 예제는 `VeroScript/Resources/Examples/AnnotationProcessing`에 있습니다.

CLI 회귀는 두 라이브러리의 구체화와 디버그 정보를 제거한 애플리케이션을 검증합니다.

## 호스트 오버로드

[호스트 바인딩 및 바인딩 카탈로그 — 호스트 오버로드](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/bindings.md#host-overload-resolution)을 참조하세요.

## 공통 템플릿 서비스

`VeroScriptTemplate.h`는 순서 있는 Type/Value 스키마, 정확한 형식 상수, 제한된 순수 평가, 인수 인코딩 및 선언／인스턴스 스냅샷을 제공합니다. 컴파일러는 입력 AST를 변경하지 않고 구체화 결과를 소유합니다. 기본값은 선언 문맥에서 바인딩되며 인스턴스 식별에는 외부와 자체 인수가 포함됩니다. VM, JIT, Core AOT는 확정된 명령과 레이아웃을 사용합니다.

`ModuleMeta::EnumerateTemplateDeclarations`는 본문 실행 없이 데이터를 소유하는 열린 선언을 반환합니다. 함수와 형식의 `TemplateInstance`는 자체／외부 인수를 구분합니다. 형식은 정규 이름, 값은 정확한 `@value:` 인코딩을 사용합니다. 접두사를 제거한 뒤 `DecodeTemplateArgument`로 해석하며 double을 거치지 않습니다. 스냅샷의 수명은 반환 객체와 같고 처리기 뷰는 콜백 중에만 유효합니다.

네이티브 `TemplateParameters`와 스크립트 `declaration_info.template_parameters`는 종류, 기본값, 외부 플래그를 공유합니다. 라이브러리는 일반 공개 형식의 열린 메서드와 필요한 비공개 의존성을 보존합니다. 닫힌 앱 로드는 처리기를 실행하지 않습니다. 내부 레이아웃／빌드 식별은 갱신하고 제품과 VSBC 버전은 0.1.1을 유지합니다. AST 소비 모듈은 같은 Core로 다시 빌드해야 합니다. 독립 소스 내보내기는 미지원 구문을 출력 전에 진단합니다.

`VeroScriptTemplateQuery.h`의 `FTemplateCallTypeQuery`는 준비된 불변 프로그램에서 Core의 추론, 기본값, 오버로드 선택으로 시그니처를 조회합니다. 본문을 컴파일하거나 처리기 및 네이티브 호출을 실행하지 않습니다. 가시성과 접근 필터링은 호출자가 담당합니다. 반환 형식 표기는 조회용이며 표시나 직렬화 식별자로 사용하지 않습니다. 프로그램과 바인딩은 조회보다 오래 유지해야 하며 각 인스턴스는 단일 분석 스레드에서 사용하고 취소를 전파합니다.

## 런타임 최적화 경계

제한된 정수 호출 계획은 디버깅, 핫 리로드, 실행 제어가 없는 실행에만 적용하며 호출 깊이 제한을 유지합니다. 인수와 값 캡처는 int여야 합니다. 정수 필드 시험 계산이 실패하면 쓰기를 반영하지 않고 일반 VM으로 돌아가 오류 전의 부분적인 효과를 유지합니다. 변경 관찰자가 있으면 필드 계획을 사용하지 않습니다. 계획은 모듈이나 프로그램 세대를 넘지 않으며 빌린 모듈의 캐시는 실행 경계에서 비웁니다. BigInt 해시는 불변 데이터에 필요할 때 원자적으로 캐시하고, 수치 복사는 별도 래퍼와 사용자 프로토콜 경로를 유지합니다.

FScriptObject의 호스트 기반은 GetHostBase()/SetHostBase()로 접근합니다. 일반 객체는 호스트/복사 생성 보조 기록을 할당하지 않고 복사된 기록은 변경 전에 분리합니다. C++ SDK 소비자와 JIT 백엔드는 일치하는 헤더와 Core 식별자로 다시 빌드해야 합니다. C ABI는 바뀌지 않습니다.

객체 형식 이름은 `FScriptObject::GetTypeName()/SetTypeName()`으로 접근합니다. 크기가 제한된 이름 캐시에서 불변 이름을 공유하며 VM이나 모듈이 해제된 뒤에도 유지합니다. 빌린 이름 참조는 해당 객체의 형식 이름이 바뀔 때 만료되며, 변경 시 런타임 레이아웃 힌트도 무효화됩니다. 호출 서명 캐시는 모듈, 세대 및 실제 서명을 검증하고 재진입 동안 불변 기록을 유지합니다. 인수, 참조 및 반환값 검사는 기존 규칙을 따릅니다.

## 필드 기본값과 정적 의존성 준비

`RegisterStaticMethodDeclarations`는 일반 필드 배치 전에 호출 시그니처를 게시합니다. 닫힌 제네릭 struct/class/interface는 정적 저장소와 소유자 설명자, 메서드를 게시한 뒤 `CompileOwnFieldDefaults`로 인스턴스 기본값을 바인딩합니다. 네임스페이스, 접근, 형식／값 대입, 필드 메타데이터를 유지하고 테이블을 늘릴 수 있는 콜백 뒤에는 인덱스로 다시 조회합니다. 자기 형식 해석은 어휘 선언 ID를 사용하고 명시적 바깥 인수, 루트 지정, 값 바인딩을 보존합니다.

`FStaticInitializationDeferral`은 모듈 초기화 코드가 인스턴스 필드 버퍼에 들어가지 않게 합니다. `bStaticFieldInitializationStarted`는 초기 배치와 정적 초기화 단계를 나누고 `PendingGenericStaticFieldInits`는 필요할 때까지 소유자를 보류합니다. `FlushPendingGenericStaticFieldInitializers(RequiredType)`는 요청한 소유자만 제거하고 다른 순서를 유지합니다. 초기화 코드는 저장소 전역 변수마다 한 번 출력하며 각 필드 기본값에 삽입하거나 런타임 정적 읽기마다 다시 출력하지 않습니다.

`Private/Compiler/VeroScriptStaticInitializationDependencies.cpp`는 분리된 초기화자 바이트코드에서 의존성을 준비합니다. `PrepareStaticInitializationDependencies`는 선택된 호출／생성자, 필드 기본값, 복사, 지역 소멸을 따라가며 callable 출처, 캡처, 참조 쓰기를 상태 리비전으로 추적합니다. 저장된 클로저, 보존된 할당, nullable 필드와 빈 컨테이너는 선언 형식만으로 실행／소멸 의존성을 추가해서는 안 됩니다. 알 수 없는 콜백 대상은 완전한 런타임 호출 그래프가 아니며 분석은 네이티브 부작용을 실행하지 않습니다.

소비자 식을 임시 버퍼로 컴파일하고 의존성을 준비한 뒤 디버그 위치와 전역 초기화 범위를 맞춰 추가합니다. 필요한 제네릭 초기화는 단락 분기와 필드 버퍼 밖에 둡니다. 분석은 취소를 확인하고 재귀 준비 전 설명자를 복사하며 업무 실행 대신 활성／리비전 가드를 사용합니다. 무관한 초기화의 일반 순서를 보존하고 임의의 순환 해석을 보장하지 않습니다.

나중에 선언된 정적 메서드, 중첩 자기 형식, 별칭, const/ref 접근, 선택 생성자, callable／참조 쓰기, 복사／소멸 수명과 무관한 소유자를 검토합니다. Core VM, 선택 JIT, Core AOT를 따로 검증하며 독립 소스 내보내기는 별도 계약입니다. 시나리오는 `Private/Tests/VeroScriptStaticMemberTests.cpp`, 백엔드 차등 스위트와 C++ Core AOT 스위트에 있습니다. 존재만으로 성공 결과라고 간주하지 않습니다.

## 십진 변환 구현

`Shared/Private/VeroScriptFloatParsing.h`의 `FromCharsDecimal`을 파서와 스칼라 텍스트 서비스가 공유합니다. 부동소수점 `std::from_chars` 지원을 감지하고 대체 경로는 대상 폭으로 직접 변환합니다. 유효 접두부와 sticky 숫자로 입력을 제한하고 C 숫자 로캘과 최근접 반올림을 사용한 뒤 호출자 상태를 복원합니다. 0이 아닌 비정규 수는 유효하지만 0이 아닌 입력이 0으로 반올림되면 출력을 대입하지 않고 범위 오류를 반환합니다. `ParserDetail::ParseFloatLiteral`은 double로 표현 가능한 `f` 리터럴의 언더플로 예외를 별도로 유지합니다. 중간값, 음의 0, 긴 가수, 로캘 및 실패 시 출력 보존을 구분해 검증하며 소스 사례의 존재를 플랫폼 통과로 간주하지 마세요.

Core 바인딩 카탈로그 JSON과 Tooling LSP/DAP JSON도 같은 변환을 사용합니다. `vs_use_shared_private_headers`는 비공개 include 경로를 제공하며 공유 헤더는 공개 SDK API가 아닙니다.

`Shared/Private/VeroScriptJsonString.h`는 Core 런타임 의존성 없이 바인딩 카탈로그, Interop 및 네이티브 언어 도구에 JSON 문자열 이스케이프를 제공합니다. 추가 연산은 바깥따옴표와 UTF-8 검증 없이 문자열 내용을 기록합니다. 호출자는 기존 제어 문자 표기를 유지하고 출력 버퍼와 겹치지 않는 입력을 전달합니다. 이 비공개 빌드 입력은 소스 배포에 포함하고 각 소비자의 구현 지문에 반영해야 합니다.

<a id="floating-formatting-support"></a>

`Shared/Private/VeroScriptFloatFormatting.h`는 패키지 매크로 직렬화, 애플리케이션 i18n, Tooling JSON, Json, GUI 숫자 컨트롤에 `ToCharsDecimal`과 `ToCharsGeneral`을 제공합니다. 부동소수점 `std::to_chars` 오버로드가 있으면 사용하며, 호환 경로는 C 숫자 로캘과 원래 float/double 폭으로 왕복 변환 가능한 표기를 고르고 호출자의 errno와 반올림 상태를 복원합니다. 공개 SDK API가 아닌 내부 구현 지원입니다. Core/대상 AOT, Json 바인딩, GUI 컴파일 지문과 소스 배포에 포함하고 charconv와 별도로 대체 경로를 검증하세요.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/architecture-and-pipeline.md) · [fr-FR](../fr-FR/architecture-and-pipeline.md) · [ja-JP](../ja-JP/architecture-and-pipeline.md) · [ko-KR](architecture-and-pipeline.md) · [zh-CN](../zh-CN/architecture-and-pipeline.md) · [zh-TW](../zh-TW/architecture-and-pipeline.md)
