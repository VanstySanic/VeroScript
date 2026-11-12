# Python toolchain과 package 계약

## strict static-tool 계약

frontend는 class에서 field, enum value, property 또는 method declaration이 중복되면 `PYF0003`을 보고합니다. 해당 property의 단일 setter만 허용됩니다. Transpiler에서 `None`은 callable return으로만 가능하고 모든 constructor는 `-> None`을 선언해야 하며 instance receiver는 default가 없는 positional `self`여야 하고 method decorator는 `@staticmethod`만 허용됩니다.

runtime binding 전에 BindGen은 complete Binding Catalog와 실제 Python callable도 검증합니다. duplicate constructor 또는 visible member name, 맞지 않는 field/getter/setter signature, catalog와 required positional shape가 정확히 일치하지 않는 constructor, function, method, property accessor를 거부합니다. 이 실패는 partial binding을 등록하거나 generated artifact를 교체하지 않습니다.

## 네이티브 확장

`VeroScriptPython`은 embedded distribution이 아닌 CPython extension입니다. CMake는 Python 3.11~3.15 CPython interpreter와 `Development.Module`을 요구하며 다른 implementation을 거부합니다. extension은 `VeroScriptInterop`를 link하고 CPython ABI suffix를 사용하며 Interop library를 importable package 옆에 둡니다. matching `localization/`/`stdlib/` tree도 같이 deploy하므로 test/local import에 global PATH/library path 변경이 필요 없습니다.

## tool과 component

frontend, BindGen, Transpiler, Python AOT package는 Python tool component로 install합니다. CMake target은 selected interpreter로 test하고 relative site-package path에 설치하여 staged prefix를 relocatable하게 만듭니다. runtime wheel은 native extension, Interop library, Python source, type information, standard library, Catalog asset을 포함합니다.

## 릴리스 검증

distribution validation은 importability, native-library placement, version agreement, package content, wheel/source-distribution behavior, interpreter/platform matrix를 검사합니다. repository source tree에서만 동작하는 package는 release artifact가 아닙니다.

## Wheel 매트릭스 게시

Python 제품 전체 게시의 인터프리터 매트릭스는 `Extensions/Language/Python/pyproject.toml`에서 결정됩니다. `requires-python`과 `cibuildwheel` 선택이 일치해야 하며 현재 CPython 3.11–3.15가 대상입니다. 대상 운영 체제와 아키텍처에 맞는 다섯 가지 네이티브 인터프리터를 준비하세요. 버전별 명령과 Windows `py` 런처로 검색합니다. 전체 게시에서 경로를 지정하려면 Windows의 `-PythonExecutables` 또는 Unix의 반복된 `--python-executable` 인수를 사용합니다. `VEROSCRIPT_PYTHON_EXECUTABLES` 목록도 사용할 수 있으며 구분자는 Windows에서 `;`, Unix에서 `:`입니다. 버전 누락, 디버그 빌드, 자유 스레딩 인터프리터 및 아키텍처 불일치는 사전 검사에서 거부되며 인터프리터를 자동 설치하지 않습니다.

다음은 저장소 루트에서 빌드 없이 Windows x64 인터프리터 구성을 확인하는 예입니다. 네이티브 호스트와 일치하는 대상을 선택하세요. 독립 진입점은 경로를 명시하는 반복된 `--python` 인수를 지원합니다.

```text
python Resources/Tools/Packaging/build-veroscript-python-wheels.py --target win-x64 --check-interpreters
```

파이프라인은 소스 배포본 하나를 만들고 각 Wheel을 그 배포본에서 별도의 소스·빌드 디렉터리로 빌드합니다. 각 Wheel을 일치하는 인터프리터의 새 환경에 설치한 뒤 `Extensions/Language/Python/Resources/Tools/Packaging/test-wheel.py`에서 선택한 설치된 패키지용 모든 테스트 모음과 `pip check`로 검사합니다. 전체 게시에는 모든 Wheel ABI, 소스 배포본, 일관된 태그와 네이티브 식별 정보 및 포함된 라이선스가 필요합니다. 제품 매니페스트의 `pythonWheelVersions`에 매트릭스를 기록하므로 저장소 없이도 감사할 수 있습니다. 부분 컴포넌트 게시는 부분 상태를 유지합니다. Python을 선택하면 `-SkipBuild` 또는 `--skip-build`에서도 이 매트릭스를 빌드하고 검증합니다.

Python sdist는 `Release/Extensions/Language/Python/any/<version>/packages/`에 한 번만 저장합니다. Wheel은 실제 플랫폼/ABI 태그로 선택하며 태그가 허용할 때만 매니페스트를 공유합니다. 대상 인덱스는 sdist와 Wheel 매니페스트를 함께 참조합니다. 체크아웃이 깨끗해도 실제 sdist 구성원에 `.cxx`, `.gradle`, `.externalNativeBuild` 캐시가 없는지 확인하세요. `pyproject.toml`에서 명시적으로 제외하며 소스 배포 검증에서도 거부합니다. 격리 압축 해제 후 빌드에 필요한 소스, 헤더, 빌드 스크립트, 메타데이터와 법적 고지는 유지합니다.

SDK와 설치 관리자의 네이티브 모듈은 주 빌드의 단일 CPython ABI를 유지합니다. Linux Wheel은 실제 네이티브 태그를 유지하며 매트릭스가 `manylinux` 또는 `musllinux` 호환성을 보장하지는 않습니다.

## 현지화 유지 관리

각 Python 도구는 자체 `Resources/Localization/<culture>/` 카탈로그를 관리하며, 여러 도구에서 사용하는 메시지만 Python 영역에서 공유합니다. 배포 시 `python_tools.*` ID가 중복되지 않도록 통합합니다. 파일은 고유 ID와 공백뿐이지 않은 문자열 값을 갖는 UTF-8 JSON 객체여야 합니다. 손상된 문화권은 제외하고 다음 일치 후보를 시도합니다.

영어 카탈로그가 허용된 메시지 ID와 위치 인수 계약을 정의합니다. 번역은 `{0}`, `{1}` 등의 번호 인수를 재배열할 수 있지만 인덱스 집합을 유지해야 합니다. 이름이 있는 필드, 속성 접근, 인덱스 접근은 지원하지 않습니다. 리터럴 중괄호는 `{{`와 `}}`로 이스케이프합니다. 템플릿이 없거나 잘못되면 실제 영어 카탈로그를 사용하고, 서식 지정이 실패하면 내장 영어로 폴백합니다. 출판 전에 Frontend 현지화 회귀 테스트와 여섯 언어 배포 검증을 통과시켜야 합니다.

잘못된 JSON, 중복 메시지 ID, 문자열이 아니거나 공백뿐인 값, 잘못된 UTF-8은 해당 문화권을 거부하게 합니다. 번역에만 있는 ID는 무시합니다.

<a id="python-package"></a>

Python 그룹은 `Extensions/Language/Python/pyproject.toml`의 PEP 517 프로젝트를 관리합니다. 저장소 루트에서 아래 명령으로 로컬 wheel／소스 배포 후보를 만들고 검사합니다. 체크아웃 설치에는 `python -m pip install ./Extensions/Language/Python`을 사용합니다. 소스 배포는 네이티브 입력을 포함하지만 선택한 CPython 개발 파일과 네이티브 도구 체인은 여전히 필요합니다. `VEROSCRIPT_PYTHON_INSTALL_DIR`은 CMake 스테이징의 이동성을 위한 상대 설치 경로입니다. 설정된 wheel 행렬의 `cibuildwheel`은 저장소 루트에서 실행하며 빌드나 검증은 릴리스를 업로드하지 않습니다. 버전 메타데이터는 `VeroScriptVersion.h`에서 가져오며 로드한 런타임과 일치해야 합니다.

```sh
python -m pip install build
python -m build Extensions/Language/Python --outdir .tmp/python-dist
python Extensions/Language/Python/VeroScriptPython/Resources/Tools/Packaging/validate_distribution.py .tmp/python-dist --expected-version 0.1.1
```

## 배포 메타데이터 검증

wheel과 소스 배포본의 `Requires-Python` 범위는 선언된 전체 매트릭스와 일치해야 하며 현재는 `>=3.11,<3.16`입니다. 부분 게시에서도 제공된 모든 산출물을 검사합니다. Windows 확장 접미사는 `win32`, `win_amd64`, `win_arm64` 및 선택한 CPython ABI와 일치해야 합니다. macOS wheel의 최소 버전 태그는 확장과 포함된 Interop 라이브러리 양쪽의 최소 OS 요구 사항을 만족해야 합니다. 교차 조립은 컴파일러, 도구 체인, 생성기 플랫폼 및 배포 설정을 유지하고 확장을 컴파일하기 전에 대상 CPython 헤더를 확인합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
