# 10: 패키지 빌드 및 애플리케이션 배포

스크립트가 제공 가능한 프로그램이 되면 이를 애플리케이션이나 라이브러리로 패키징합니다. `.vsapp`은 실행하기 위한 것이고, `.vslib`은 다른 스크립트로 가져오기 위한 것입니다.

## 1단계: 라이브러리 모듈 생성

`package_library.vs`을 생성합니다:

```VeroScript
export enum class OperationKind
{
	Add
};

export int calculate(int left, int right)
{
	return left + right;
}
```

내보낸 선언만 라이브러리 API의 일부가 됩니다. 라이브러리를 빌드하고 검사합니다.

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
veroscript package inspect DemoLibrary.vslib
```

## 2단계: 애플리케이션 진입점 생성

`package_app.vs`을 생성합니다:

```VeroScript
#ifdef VERO_USE_BINARY_PACKAGE
import "DemoLibrary.vslib";
#else
import "package_library.vs";
#endif

int main()
{
	print(calculate(20, 22));
	return 0;
}
```

애플리케이션을 빌드하고 실행합니다.

```powershell
veroscript -D VERO_USE_BINARY_PACKAGE --emit-package DemoApp.vsapp package_app.vs
veroscript package inspect DemoApp.vsapp
veroscript DemoApp.vsapp
```

애플리케이션 패키지에는 진입점과 종속성이 포함되어 있으므로 소스 라이브러리 패키지는 런타임 시 패키지 옆에 있을 필요가 없습니다. `package_app.vs`은 기본적으로 소스 모듈을 가져옵니다. `-D VERO_USE_BINARY_PACKAGE`은 이를 생성된 `DemoLibrary.vslib`로 전환하므로 애플리케이션을 컴파일하는 동안 사용할 수 있어야 합니다.

## 3단계: 공개 경계 검토

내보내지 않은 함수는 자체 라이브러리 소스 내에서 사용할 수 있지만 다운스트림 스크립트에서는 이를 호출할 수 없습니다. 패키지를 게시하기 전에 다음을 확인하세요.

1. 원하는 유형과 기능만 내보냅니다.
2. 매개변수 유형, 반환 유형 및 열거형 값이 안정적입니다.
3. 패키지 버전이 대상 런타임 및 바인딩과 호환됩니다.
4. 생성된 파일에서 `package inspect`이 실행되었습니다.

배포된 예제에는 [`examples/package/README.md`](../../../../../VeroScript/Resources/Scripts/examples/package/README.md), [`package_library.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_library.vs) 및 [`package_app.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_app.vs)에 완전한 예가 있습니다.

## 연습

1. 내보낸 `subtract` 함수를 추가합니다.
2. `export`을 제거하고 애플리케이션 컴파일 오류를 관찰합니다.
3. 패키지 버전을 설정하고 릴리스에 사용된 정확한 빌드 명령을 기록합니다.

[패키지 및 배포](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/packages.md)를 참조하세요. 다음: [디버깅 및 AOT 생성](11-debugging-and-aot.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/toolchain/10-packages-and-distribution.md) · [fr-FR](../../fr-FR/toolchain/10-packages-and-distribution.md) · [ja-JP](../../ja-JP/toolchain/10-packages-and-distribution.md) · [ko-KR](10-packages-and-distribution.md) · [zh-CN](../../zh-CN/toolchain/10-packages-and-distribution.md) · [zh-TW](../../zh-TW/toolchain/10-packages-and-distribution.md)
