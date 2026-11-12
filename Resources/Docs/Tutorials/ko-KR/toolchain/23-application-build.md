# 라이브러리와 애플리케이션 만들기

Build/Application을 지원하는 일치 CLI, 쓰기 가능한 저장소와 배포된 hello 예제가 필요합니다. 스크립트 콘텐츠만 사용하므로 네이티브 공급자 설치나 네트워크 서비스는 필요 없습니다.

1. 연결된 hello 폴더 전체를 /absolute/hello 같은 쓰기 가능한 절대 경로로 복사하고 src/package를 유지합니다. 먼저 계획을 보고 application 대상만 빌드합니다. 의존 라이브러리는 자동 빌드되며 release 프로필을 명시합니다.

   [hello](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)

```sh
veroscript build --path /absolute/hello --profile release --target application --plan --json
veroscript build --path /absolute/hello --profile release --target application
veroscript /absolute/hello/dist/release/portable/application/sample.vsapp
```

2. 계획은 사용자 단계를 실행하지 않고 의존 그래프를 보여줍니다. 성공하면 dist/release/portable/application/sample.vsapp와 라이브러리를 만들고 실행 결과는 result: 42, 종료값 0입니다. 계획/빌드 오류를 먼저 고치며 생성만으로 실행 성공을 판단하지 않습니다.

3. 라이브러리 답을 43으로 바꾸고 새 계획과 빌드를 진행합니다. 예상 출력은 result: 43입니다. 생성 컨테이너를 수정하지 말고 소스와 설정을 입력으로 유지합니다.

[Build](../../../../../VeroScriptBuild/Resources/Docs/User/ko-KR/index.md) · [Application](../../../../../VeroScriptApplication/Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)

/absolute/hello를 실제 절대 경로로 바꿉니다. Windows에서는 C:/work/hello 등을 사용합니다.

## 소스 입력

- [veroscript-build.json](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)
- [src/main.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/main.vs)
- [src/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/library.vs)
- [package/veroscript-package.json](../../../../../VeroScriptBuild/Resources/Examples/hello/package/veroscript-package.json)
- [package/scripts/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/package/scripts/library.vs)

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](../../fr-FR/toolchain/23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)
