# 19: 모바일 앱에서 스크립트 실행

Android/iOS 앱에서 대상에 맞는 모바일 SDK를 사용합니다. 앱에는 대상 CPython 호스팅 환경 또는 .NET 10 MAUI가 필요합니다. 데스크톱 패키지는 모바일 네이티브 바이너리를 제공하지 않습니다. 이 통합은 실험 단계입니다.

[Python](../../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/mobile.md) · [CSharp](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/mobile.md)

1. 선택한 바인딩 가이드에 따라 네이티브 라이브러리를 배포하고 표준 라이브러리 및 현지화 리소스를 설치합니다. Session을 만들기 전에 설치를 완료하세요.

2. 같은 작업 스레드에서 카탈로그 로드, 언어 선택, VM Session 생성과 설정을 수행합니다. `import "core.vs"; void main() { print(42); }`를 로드하고 로드와 실행 결과를 모두 확인한 뒤 print 콜백을 수집하고 Session을 닫습니다. 수집한 `42`를 UI 스레드로 반환합니다.

3. 영어 이외의 언어로 `void main() { int x = ; }`를 실행해 진단을 표시하고 UI 응답성을 확인합니다. 올바른 스크립트로 복구하고 실행 제한을 적용한 뒤 지원 대상마다 최종 서명된 앱을 검증하세요.

[en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)
