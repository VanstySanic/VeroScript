# 스크립트 컴파일 확장

확장은 std::compiler로 어노테이션 설명과 처리기를 등록하는 일반 VeroScript 모듈입니다. Core는 업무 소스 처리 전에 로드하며 런타임은 생성된 업무 바이트코드를 사용합니다.

## 로드

진입 파일 앞부분에서 주석, 공백, BOM 또는 앞선 compiler import 다음에 작성합니다.

```vs
import compiler "builder.vs";
```

경로는 리터럴입니다. 일반 import는 보조 코드를, import compiler는 등록 실행과 등록 의존성을 제공합니다. 순환은 거부합니다. 후속 업무 모듈은 진입 파일이나 호스트가 이미 로드한 확장만 사용할 수 있습니다. 확장은 자신의 미등록 문법에 의존할 수 없습니다.

호스트는 `FCompileOptions::ScriptCompileExtensions.Roots`에 Path와 선택적 RegistrationEntry를 제공합니다. Loader, C++ Session과 Codegen은 같은 설정을 사용합니다. bAllowSourceImports=false도 명시적 루트는 유지합니다. 오버레이, import와 stdlib는 기존 로더 규칙을 따릅니다.

## 등록과 서비스

확장은 compiler.vs를 import하고 루트 자신의 등록 함수를 export합니다.

```vs
import "compiler.vs";
export void register_extension(std::compiler::registration reg) {}
```

인자 없는 팩터리는 fn(std::compiler::context,std::compiler::output)->void를 반환합니다. 작업마다 새 VM 상태를 만들고 등록과 팩터리 생성은 한 번 수행합니다. 콜백 상태는 단계 간 유지됩니다. 네이티브와 스크립트 처리기는 순서와 충돌 검사를 공유합니다. 설명만 등록하면 실행 기능이 생기지 않습니다.

Context는 단계, 이벤트, 모듈, 선언, 매개변수, 기본값, 한정자, 제네릭, 어노테이션, 주석과 위치를 제공합니다. Output은 add_declaration, edit_source, replace_declaration, complete_body, replace_body, set_metadata, report_diagnostic, submit_operation을 지원합니다. 전체 작업은 하위 메타데이터와 출처를 포함하며 Core의 권한, Snapshot, 충돌과 의미 검사를 거칩니다.

registration, context 서비스와 output은 호출 동안만 유효합니다. 소유한 읽기 전용 값은 보관할 수 있으나 오래된 scope는 다음 배치를 수정할 수 없습니다. UTF-8 범위는 문자를 나눌 수 없습니다. 제네릭은 선언 시 한 번 처리하고 일반 치환을 수행합니다. 사용자 본문, 가상 override와 기본 본문은 공통 파이프라인을 따릅니다.

초기화, 등록, 팩터리와 콜백은 총 예산과 취소 제어를 공유합니다. 컴파일 VM은 콘솔, 직접 파일·환경 접근, 작업 생성, 디렉터리 변경, 주소와 시간 조회를 금지합니다. 추가 네이티브 바인딩은 호스트가 명시합니다. 선언되지 않은 비결정성은 준비 결과 재사용을 막습니다. 신뢰할 수 없는 네이티브 코드의 완전한 샌드박스는 아닙니다.

configuration과 read_dependency는 소유한 읽기 전용 값을 반환합니다. 리소스 경로는 활성 확장 루트 기준입니다. 소스, 패키지, 설정, 서비스 계약과 존재·부재 의존성을 캐시 검증에 포함합니다.

## 패키지와 연결

CompileScriptCompileExtensionSource는 등록을 실행하지 않고 컴파일합니다. BuildPackage로 Library를 만들고 CompileExtensionManifest를 옵션에 복사합니다. 로드는 초기화 전에 진입점, 버전, 서비스 지문, 바인딩과 의존성을 확인합니다. 업무 패키지는 생성 작업을 재생하며 생산자 확장을 실행하지 않습니다.

C++는 ScriptCompileExtensions, C는 vs_interop_session_set_script_compile_options, C#은 SetScriptCompileExtensions, Python은 set_script_compile_extensions를 사용합니다. 세션 설정은 생성 스레드에서 유휴 상태일 때 변경합니다. CLI는 --compiler-extension, --compiler-config, --no-compiler-imports, --compiler-instructions, --compiler-timeout 및 compiler-package source.vs output.vslib를 제공합니다.

도구와 소스 AOT는 같은 준비 프로그램을 사용합니다. 독립 산출물은 처리기 VM을 보존하지 않습니다. JIT는 업무 코드, 컴파일 확장은 항상 VM에서 실행합니다. 캐시는 호스트의 소스/import 키와 함께 설정 및 모든 의존성을 재검증합니다.

예제: [builder.vs](../../../Examples/ScriptCompileExtensions/builder.vs), [main.vs](../../../Examples/ScriptCompileExtensions/main.vs).

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

Language: [en-US](../en-US/script-compile-extensions.md) · [fr-FR](../fr-FR/script-compile-extensions.md) · [ja-JP](../ja-JP/script-compile-extensions.md) · [ko-KR](script-compile-extensions.md) · [zh-CN](../zh-CN/script-compile-extensions.md) · [zh-TW](../zh-TW/script-compile-extensions.md)
