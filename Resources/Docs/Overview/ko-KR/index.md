# VeroScript 통합 개요

[GitHub 소스 코드](https://github.com/VanstySanic/VeroScript)

이 개요는 작업 목적에 따라 언어, 도구, 호스트 SDK 및 유지보수 문서를 연결합니다. 자세한 의미, API 및 제한은 모듈 참조 문서에서 확인하고 모듈 간 관계와 읽기 순서는 여기에서 관리합니다.

## 크로스 플랫폼 배포

| 배포 환경 | 대상 구성 | 안내 |
| --- | --- | --- |
| 데스크톱 Runtime 및 SDK | Windows (x86, x64, ARM64); Linux (x64, ARM64); macOS (x64, ARM64) | [설치 및 배포](../../../../VeroScriptInstaller/Resources/Docs/User/ko-KR/index.md) |
| 모바일 임베딩 | Android, iOS (실험 단계의 어댑터) | [모바일 SDK](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/ko-KR/index.md) |
| 브라우저 및 JavaScript 호스트 | WebAssembly | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ko-KR/index.md) |

운영체제와 CPU 아키텍처에 맞는 배포 패키지를 선택하세요. 패키지 제공 범위와 검증 상태는 제품 및 버전에 따라 다릅니다. 의존성, 사용 가능한 백엔드와 배포 요구 사항은 해당 제품의 플랫폼 안내에서 확인하세요.

## 읽기 순서 선택

| 목표 | 시작 문서 | 이어서 볼 문서 |
| --- | --- | --- |
| 스크립트 작성과 실행 | [소개](../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md) → [언어 기본](../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/index.md) | [CLI](../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md), [표준 라이브러리](../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/index.md), [예제 목록](../../../../VeroScript/Resources/Docs/User/ko-KR/examples.md) |
| 설치, 업데이트 또는 배포 | [설치와 배포](../../../../VeroScriptInstaller/Resources/Docs/User/ko-KR/index.md) | [패키지](../../../../VeroScriptCli/Resources/Docs/User/ko-KR/packages.md), [CLI 참조](../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md) |
| 호스트에 스크립트 내장 | 아래에서 SDK 선택 | [Interop 경계](../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md), [도구와 통합](../../User/ko-KR/toolchain/index.md) |
| 편집, 진단 및 디버깅 | [CLI 참조](../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md) | [LSP](../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/lsp.md), [디버깅](../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/debugging.md), [진단 현지화](../../../../VeroScript/Resources/Docs/User/ko-KR/localization.md) |
| 구현 개발과 유지보수 | [개발자 읽기 순서](#개발자-읽기-순서) | [모든 개발자 문서](../../Developer/ko-KR/index.md) |

실행 백엔드, 호스트 바인딩 또는 패키지 설정을 선택하기 전에 설치한 구성 요소와 버전을 확인하세요. 명령과 SDK 예제에는 해당 구성 요소가 필요합니다. 참조 문서는 실제 빌드 및 실행 검증을 대신하지 않습니다.

## 호스트 SDK 선택

| 호스트 | 사용자 참조 | 유지보수 시작 문서 |
| --- | --- | --- |
| 네이티브 C++ | [C++ SDK](../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/index.md) | [C++ 모듈](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ko-KR/index.md) |
| C ABI 또는 다른 언어 | [Interop SDK](../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md) | [ABI와 수명](../../../../VeroScriptInterop/Resources/Docs/Developer/ko-KR/abi-and-lifetime.md) |
| CPython | [Python SDK](../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/index.md) | [Python 모듈](../../../../Extensions/Language/Python/Resources/Docs/Developer/ko-KR/index.md) |
| .NET / C# | [C# SDK](../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/index.md) | [C# 모듈](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/ko-KR/index.md) |
| 브라우저 또는 JavaScript | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ko-KR/index.md) | [WebAssembly 모듈](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/ko-KR/index.md) |

값, 핸들, 콜백, 스레드 및 해제 규칙은 선택한 SDK의 계약을 확인하세요. 지원하는 소스 부분집합, AOT 제한 및 대상 환경도 각 모듈 문서가 정의합니다. 언어 이름만으로 호환 범위를 판단하지 마세요.

## 개발자 읽기 순서

1. [코어 구조와 파이프라인](../../../../VeroScript/Resources/Docs/Developer/ko-KR/architecture-and-pipeline.md) 및 [실행과 상태](../../../../VeroScript/Resources/Docs/Developer/ko-KR/execution-and-state.md)에서 컴파일, 실행 및 상태 경계를 파악하고, 백엔드나 동시성을 다룰 때 [JIT와 동시성](../../../../VeroScript/Resources/Docs/Developer/ko-KR/jit-and-concurrency.md)을 읽습니다.
2. 호스트 경계를 따라 [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/ko-KR/index.md), [C++ 세션](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ko-KR/session.md) 및 해당 언어 어댑터 문서를 읽습니다.
3. 도구 경계를 따라 [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/ko-KR/index.md), [도구 프로토콜](../../../../VeroScriptTooling/Resources/Docs/Developer/ko-KR/architecture-and-protocols.md)를 읽습니다.
4. 릴리스 전에 [빌드와 리소스](../../../../VeroScript/Resources/Docs/Developer/ko-KR/build-and-assets.md), [설치 관리자 빌드와 패키징](../../../../VeroScriptInstaller/Resources/Docs/Developer/ko-KR/build-and-bundle.md), [게시 검증 절차](../../../../VeroScriptInstaller/Resources/Docs/Developer/ko-KR/release-validation.md)을 확인합니다.

이전 검증 결과는 기록된 소스·산출물·환경에만 적용됩니다. 제품 빌드, 무결성 검사 및 런타임 인수 결과 기록은 [배포 검증 절차](../../../../VeroScriptInstaller/Resources/Docs/Developer/ko-KR/release-validation.md)를 따르며 증거는 무시 대상 출력 디렉터리에 보존합니다.

## 문서와 출판

전체 모듈 목록은 [사용자 진입 페이지](../../User/ko-KR/index.md)를 참조하세요. 문서 소유권, 여섯 언어 유지보수, 검증 명령과 All/User 패키징은 [문서 유지보수 가이드](../../Developer/ko-KR/index.md)에서 관리합니다.

출판된 문서의 링크와 앵커는 오프라인에서 사용할 수 있습니다. 소스 코드 참조와 외부 사이트는 네트워크가 필요합니다. 사용법과 튜토리얼에는 User를, 유지보수 가이드와 규약에는 All을 사용하세요. 두 컬렉션 모두 History를 포함하지 않습니다.

[사용자 문서](../../User/ko-KR/index.md) · [개발자 문서](../../Developer/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
