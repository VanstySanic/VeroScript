# VeroScript CLI 기술 문서

`VeroScriptCli`는 얇은 process layer입니다. compiler, VM, C++ embedding API, language-service semantics를 재구현하지 않고 `veroscript`, `veroscript-lsp`, `veroscript-dap` entry point를 제공합니다.

## process 책임

CLI는 argument parsing, command dispatch, process-level stream, REPL/watch orchestration, source/file selection, environment/SDK asset lookup, exit-code policy, LSP/DAP process main을 소유합니다. core, C++ session, AOT, Tooling module이 language behavior의 source of truth입니다.

command option, subcommand name, environment variable, exit code, diagnostic ID, output format은 compatibility surface입니다. prose를 개선한다는 이유만으로 번역하거나 변경해서는 안 됩니다.

## 런타임 리소스

standalone process는 version-matched standard library와 localization catalog를 찾아야 합니다. asset resolution은 relocatable해야 하며 stale build-directory copy를 조용히 사용해서는 안 됩니다. asset의 누락 또는 비호환은 startup/configuration failure이며 empty-language fallback이 아닙니다.

## 프로토콜 진입점

LSP와 DAP main은 protocol behavior를 `VeroScriptTooling`에 delegate합니다. CLI는 해당 service가 기대하는 stream framing, request ordering, cancellation, stderr/stdout separation, process exit behavior를 보존해야 합니다.

## command와 asset 세부 사항

ordinary main은 compile/check/run과 package-oriented operation을 dispatch합니다. REPL/watch process는 interactive source/file-change orchestration을 더합니다. `VeroScriptLspMain`과 `VeroScriptDapMain`은 protocol-only process entry point이며 language behavior를 `VeroScriptTooling`에 delegate합니다. CLI는 Session, debugger, workspace state의 두 번째 implementation을 만들면 안 됩니다.

CLI path resolver는 source root, SDK root, `bin` directory, standard-library directory, localization directory의 구분을 유지합니다. 발견한 executable과 asset은 compatible installation에서 와야 합니다. environment-variable/`PATH` fallback은 discovery behavior이며 version을 섞을 수 있다는 허가가 아닙니다.

## 상세 페이지

- [CLI process와 asset 계약](process-and-assets.md)

## 명세의 기준 소스

`Private/`, CLI CMake target, process test, Tooling public API, packaging/SDK smoke test가 근거입니다. command dispatch, asset lookup, stream behavior, environment variable, exit code, LSP/DAP process boundary 변경과 이 문서를 함께 갱신합니다.

## 프로젝트 초기화

`Private/VeroScriptCliProject.cpp`는 로컬 C++17 파일 생성으로 `project init`을 구현하며 패키지 환경을
로드하거나 컴파일하지 않습니다. 배타적 생성으로 동시 호출에서도 기존 파일을 보존하며 네이티브 CLI
레지스트리는 `project` 명령을 예약합니다. `VeroScriptProjectInitCliTests` 프로세스 테스트는 경로,
재실행, 충돌, 동시 생성 및 쓰기 실패 복구를 검증하며 네이티브 빌드에서만 등록합니다.
빌드 서비스가 있으면 생성된 애플리케이션과 문서의 예제도
빌드하고 실행합니다. 템플릿은 기존 `0.1.1` 스키마에 맞춰 유지하며 초기화에서 의존성 잠금 파일이나
설치 상태를 만들지 않습니다.
UTF-8 경로 변환은 C++17과 C++20을 지원하며 패키징 CMake 회귀 테스트에서 두 모드로
초기화 코드를 독립 컴파일하고 경고를 오류로 처리합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
