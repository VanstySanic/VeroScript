# 컴파일 처리와 소스 메타데이터

Core는 VeroScriptCompileProcessing.h를 통해 네이티브 처리기를 제공합니다. 소스 변환, 타입과 멤버 생성, 선언과 본문 교체, 주석과 어노테이션 추가를 지원합니다. 생성 코드는 일반 가시성, 타입, 제네릭 검사를 거칩니다. 업무 규칙은 확장에서 정의합니다.

## 등록과 수명

FCompileProcessorRegistry에 FCompileProcessorDesc를 등록하고 Freeze()한 뒤 FCompileOptions::CompileProcessing.Registry에 전달합니다. 설명자는 식별자, 버전, 설정 키, 순서, 단계, 구독, 허용 작업과 팩터리를 선언합니다. 컴파일 작업마다 별도 인스턴스를 생성하고 단계 사이에서 유지합니다. 콜백의 컨텍스트와 AST 뷰는 호출 동안만 유효합니다. 필요한 네이티브 소유자는 팩터리에서 유지합니다.

구독이 여러 개 일치해도 이벤트당 처리기 호출은 한 번입니다. 순서는 Order, ProcessorId이며 충돌하는 작업을 허용하지 않습니다. 등록되지 않은 어노테이션은 메타데이터로 유지합니다. FAnnotationProcessorRegistry도 같은 트랜잭션을 사용합니다. Members에서 메서드를 추가하고 사용자 구현 연결 후 Bodies에서 기본 본문을 완성합니다.

## 단계

| 단계 | 입력과 작업 |
| --- | --- |
| RawSource | 원본 텍스트; EditSource로 사용자 문법과 import 변환 |
| ActiveSource | 전처리된 텍스트; import와 전처리 경계 유지 |
| Types | 선언 개요; 타입, 인터페이스, enum, 별칭 추가와 교체 |
| Members | 등록된 타입; 멤버, 함수, 전역 변수 추가와 교체 |
| Bodies | 연결된 구현; CompleteBody, ReplaceBody, SetMetadata |

Types 이후 별칭과 상속을 연결합니다. Members 이후 파일 간 구현 연결과 지연된 본문 분석을 수행합니다. Bodies는 레이아웃이나 시그니처를 변경하지 않습니다. 생성 선언은 처리기를 재귀 호출하지 않으며 다음 단계에서 조회할 수 있습니다.

AddDeclaration은 모듈, namespace, 선택적 OwnerId, 현재 Snapshot의 FCompileScope를 사용하며 TargetId를 갖지 않습니다. 교체는 선언 종류, 이름, 제네릭 인자 수를 유지합니다. CompleteBody는 없는 본문만 채웁니다. 순수 가상 메서드와 deleted 선언은 본문을 받을 수 없으나 기존 순수 가상 소멸자 규칙은 유지됩니다. 오래된 대상과 충돌 작업은 적용 전에 거부합니다.

FCompileMetadata는 생략과 명시적 빈 컬렉션을 구분합니다. 교체 시 영향받지 않은 설명은 유지하고 오래된 하위 메타데이터는 제거합니다. EditSource는 UTF-8 바이트 범위와 출처 매핑을 사용합니다. ActiveSource는 전처리 지시문이나 해결된 import 순서를 변경할 수 없습니다.

## 메타데이터, 캐시, 패키지

소스 메타데이터 인코딩은 스냅샷 내부의 문자열 참조로 반복되는 경로와 식별자를 공유합니다. 값, 문서와 노드의 식별자, 위치 및 소스 매핑은 유지하며 인코딩 크기 제한은 64 MiB로 유지하며 펼친 문자열 데이터에는 별도로 512 MiB 제한을 적용합니다. 디코더는 문자열을 직접 저장하던 이전 형식도 읽습니다. 준비 결과 지문, 바이트코드 및 패키지는 같은 코덱을 사용하므로 구현 식별자가 바뀌면 일치하는 Core와 네이티브 백엔드를 다시 빌드해야 합니다.

VeroScriptSourceMetadata.h는 불변 문서, 선언, 주석과 어노테이션 인자 트리를 소유합니다. 플래그, 값 종류, 중첩, 중복, 원문과 범위를 유지합니다. RegisterAnnotation은 동작 없이 설명만 등록할 수 있습니다. 리플렉션 정보 조회는 실행 권한을 추가하지 않습니다.

CommentRetention은 Full, Documentation, NoComments를 선택합니다. NoComments도 어노테이션과 등록된 설명을 유지합니다. Unavailable은 소스 정보가 없음을 뜻합니다. 핫 리로드 후에도 이전 불변 스냅샷을 읽을 수 있습니다.

PreparedProgram은 선언, 작업, 출처와 의존성 지문을 보존합니다. MatchesHost, MatchesProcessing, MatchesDependencies로 재검증하고 소스, 오버레이, 매크로, import와 일반 옵션도 호스트 캐시 키에 포함합니다. 캐시 불가 처리기의 결과는 작업 간 재사용하지 않습니다. AST만으로 이전 소스 변환이나 처리기 상태를 재구성할 수 없습니다.

열린 제네릭 패키지는 변환 템플릿, 작업과 생산자 메타데이터를 보존합니다. 소비자는 생산자 처리기 없이 Types, Members, Bodies를 재생합니다. 닫힌 코드는 바이트코드를, 타입 치환은 일반 언어 규칙을 사용합니다. 0.1.1 계약 변경 시 이전 개발 산출물을 재생성합니다.

취소는 협력 방식입니다. 긴 계산에는 CheckCancelled, 의존성 읽기에는 ReadDependency를 사용하여 없는 리소스도 기록합니다. 이벤트와 생성량에는 제한이 있습니다. 차단된 네이티브 콜백은 호스트의 협력이 필요합니다. Interop, 도구와 AOT는 같은 준비 결과를 사용하고 JIT는 생성된 업무 코드를 실행합니다.

예제: [네이티브 처리기](../../../Examples/CompileProcessing/main.cpp), [스크립트 확장](script-compile-extensions.md).

## 애너테이션 이름 비교

기본적으로 대소문자를 구분합니다. `annotation_definition.case_sensitive = false`를 설정하면 `@GuiWidget`, `@guiWidget`, `@GUIWIDGET`가 하나의 정의를 사용합니다. 처리기 구독, `has_annotation`, `find_annotations`, `annotation_info.matches_name(name)`가 같은 규칙을 사용합니다. `name()`은 원래 표기, `definition().name()`은 등록 이름, `definition().case_sensitive()`는 규칙을 반환합니다. 인수와 메타데이터 키는 계속 대소문자를 구분합니다. `find_annotations`와 `matches_name`에는 `@`가 필요하며 `has_annotation`에서는 생략할 수 있습니다. 등록되지 않은 이름은 대소문자를 구분합니다.

`FAnnotationDefinition::CaseSensitive`, `FHostBindingRegistry::RegisterAnnotation(..., false)`, JSON `caseSensitive`, C Interop `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE` (`name_flags`), C# `CaseSensitive`, Python `case_sensitive`. ASCII; 로캘과 무관합니다. 겹치는 활성 등록을 거부하며 지문, 바이트코드, 라이브러리, 제네릭에 규칙을 유지합니다。

## 통합 책임

`FCompileResult::PreparedProgram`은 공유 준비 결과입니다. C++ Session은 `SetCompileProcessing`을 공개하고 코드 생성 호스트는 `FCodegenOptions::CompileProcessing`을 전달하며 Tooling 분석 및 프로토콜 옵션도 같은 설정을 보유합니다. 네이티브 Interop 호스트는 `VeroScriptInteropCompileProcessing.h`의 `CreateCompileProcessingService`로 불투명 서비스를 만들 수 있습니다. 이 통합 지점은 이미 구현되어 있습니다.

레지스트리와 네이티브 소유자를 명시적으로 제공합니다. 일반 CLI는 임의의 네이티브 프로세서 팩터리를 자동 발견하지 않습니다. 스크립트 확장과 명시적으로 선택한 네이티브 모듈은 설정된 로더 경로를 사용합니다. 준비된 프로그램의 프로세서를 다시 실행하거나 주석을 분석해 별도 선언을 만들면 안 됩니다. JIT는 처리 후의 업무 코드를 실행합니다. 플랫폼과 애플리케이션 검증은 별도로 수행해야 합니다.


## 범위 선언과 블록 생성

선언 뷰는 `ScopeId`, `ParentScopeId`, `bLocal`, `bNested`와 의미상 소유자를 제공합니다. `FCompileProcessingContext::Blocks`는 콜백 동안만 유효한 읽기 전용 빌린 뷰입니다. Types 단계에서 `FCompileScope::ScopeId`와 `InsertionBeforeId`로 지역 타입 또는 별칭을 추가합니다. 빈 블록이나 끝에 추가할 때는 종료 앵커를 사용하며 module/owner 필드의 의미는 유지됩니다.

스크립트 처리기는 `context.blocks()`, `context.block_scope(scope_id, before_id)`를 사용합니다. 블록과 앵커는 `scope_id()`, `statements()`, `id()`, `range()` 등의 getter 메서드를 제공하며 소스 범위는 일반 값 필드를 유지합니다. scope나 임의 오프셋을 만들어 스냅샷 검사를 우회할 수 없습니다.

같은 앵커의 작업 순서는 결정적이며 선언은 삽입 위치부터 보입니다. 부모 교체와 이전 자식 앵커 조작은 충돌합니다. 본문 완성/교체 시 지역 선언 목록을 다시 만듭니다. 생성된 자식은 일반 검사만 수행하고 처리기를 다시 실행하지 않습니다. 제네릭 구체화도 준비된 선언을 대입합니다. VSBC와 라이브러리는 식별자, 생성 기록, 자식 목록, 소속을 유지합니다. 0.1.1 레이아웃 변경 후 개발 산출물을 다시 생성해야 합니다.

## 선언 추가 배치와 소유자 참조

`Private/Processing/VeroScriptDeclarationProcessing.cpp`의 Members 단계 `DeclarationAppendsOnly` 경로는 모든 연산이 `AddDeclaration`이고 `TargetId`와 `ScopeId`가 비어 있어야 합니다. `OwnerId`가 없는 네임스페이스 도우미와 검증된 선언 스냅샷에 이미 있는 class/struct/interface 소유자의 멤버를 섞어 추가할 수 있습니다. 단계, 스냅샷, 쓰기 가능 모듈 및 충돌 검사는 유지되며 새로 생성한 소유자를 이전 스냅샷의 대상으로 만들지 않습니다.

`ReuseOwners`는 `P.Classes`, `P.Structs`, `P.Interfaces`의 저장소 주소가 바뀌지 않는 동안만 해당 스냅샷을 빌립니다. 생성한 도우미 본문은 지역 타입을 추가하여 컨테이너를 확장할 수 있으므로 저장소가 바뀌면 `Collect(P)`로 참조를 다시 수집합니다. 이 확장을 넘어 선언/AST 포인터를 보관하지 마세요. 도우미와 멤버의 혼합, 제네릭 소유자, 가상/인터페이스 호출과 지역 선언을 확인하세요. `Private/Tests/VeroScriptCompileProcessingTests.cpp`의 `FInterleavedAppendGenerator`는 소스 시나리오이며 테스트 통과 결과는 아닙니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

Language: [en-US](../en-US/compile-processing.md) · [fr-FR](../fr-FR/compile-processing.md) · [ja-JP](../ja-JP/compile-processing.md) · [ko-KR](compile-processing.md) · [zh-CN](../zh-CN/compile-processing.md) · [zh-TW](../zh-TW/compile-processing.md)
