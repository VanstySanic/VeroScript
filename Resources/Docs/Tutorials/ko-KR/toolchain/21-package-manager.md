# 21: 로컬 패키지 설치

패키지 관리가 활성화된 데스크톱 Runtime과 빈 학습 폴더를 사용합니다. 레지스트리나 네트워크가 필요 없는 오프라인 실습입니다.

## 1. 파일 만들기

library/veroscript-package.json에 패키지, library/scripts/math.vs에 모듈, app/veroscript-project.json에 프로젝트, app/main.vs에 진입 파일을 저장합니다. archive 의존성이 가리키는 폴더 관계를 유지하세요.

### library/veroscript-package.json

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

### library/scripts/math.vs

```VeroScript
export int answer()
{
	return 42;
}
```

### app/veroscript-project.json

```json
{
	"format": "veroscript-project",
	"formatVersion": "0.1.1",
	"name": "demo-app",
	"version": "0.1.1",
	"dependencies": {
		"demo-math": {
			"version": "0.1.1",
			"archive": "../demo-math-0.1.1.vspkg"
		}
	}
}
```

### app/main.vs

```VeroScript
import "demo/math.vs";
int main()
{
	print(answer());
	return 0;
}
```

## 2. 패키징, 설치와 실행

학습 폴더에서 실행하세요. 마지막 스크립트는 42를 출력하고 install/list/verify는 앞서 자체 결과를 출력합니다. 이 아카이브는 ZIP 설치 배포물이며 .vslib가 아닙니다.

```sh
veroscript pack ./library --output ./demo-math-0.1.1.vspkg
veroscript install --path ./app --offline
veroscript list --path ./app --json
veroscript verify --path ./app
veroscript --backend=vm --path ./app ./app/main.vs
```

## 3. 확인하고 변경하기

pack은 기존 출력 파일을 덮어쓰지 않습니다. 학습용 이전 아카이브를 다른 이름으로 옮긴 뒤 원래 경로에 다시 패키징하세요.

answer()를 43으로 바꾸고 다시 패키징하세요. 재설치 전까지 설치된 불변 스냅샷은 42를 반환합니다. --locked는 바뀐 아카이브를 감지하고 새 바이트를 자동 채택하지 않습니다. 소스를 바꿀 때 의도한 잠금을 복원하거나 다시 생성하세요.

## 4. 다음 단계

설치가 자동 import를 하지 않으며 main.vs가 demo/math.vs를 명시적으로 import합니다. 컴파일 전용 의존성은 runtime 배포에 포함하지 않습니다. 업데이트를 위해 영수증/잠금을 삭제하거나 설치 내용을 편집하지 마세요.

[참조](../../../../../VeroScriptPackageManager/Resources/Docs/User/ko-KR/index.md)

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/toolchain/21-package-manager.md) · [fr-FR](../../fr-FR/toolchain/21-package-manager.md) · [ja-JP](../../ja-JP/toolchain/21-package-manager.md) · [ko-KR](21-package-manager.md) · [zh-CN](../../zh-CN/toolchain/21-package-manager.md) · [zh-TW](../../zh-TW/toolchain/21-package-manager.md)
