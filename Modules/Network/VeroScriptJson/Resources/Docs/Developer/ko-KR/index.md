# JSON 유지보수

데이터 계층은 DOM/텍스트/스트림을 소유합니다. Schema는 기록된 패치의 jsoncons 1.10.0과 독립 QuickJS-ng 0.17.0 regex/Unicode를 쓰며 JavaScript 런타임은 포함하지 않습니다. 정확한 수치 어댑터를 유지합니다. C++ 매핑은 Core 독립, 스크립트 매핑은 모듈 프로세서 소유입니다. 단순 구조체 모듈은 축소 지원을 쓰고 import/제네릭/상속/주석/실행 멤버/복잡한 기본값은 전체 지원을 사용합니다. 선언 순서, 범위와 닫힌 형식 식별자를 지킵니다.

모듈 CMake를 사용합니다. 루트는 VEROSCRIPT_BUILD_JSON, 정적/동적 바인딩은 VEROSCRIPT_JSON_BUILD_BINDINGS로 제어합니다. 설치한 일치 Core SDK로 Core 재빌드 없이 바인딩을 만들 수 있습니다. 네이티브 대상은 VeroScript::Json, COMPONENTS Bindings는 VeroScript::JsonBindings입니다. Bindings 설치는 데이터와 라이선스에 추가되며 동적 모듈은 lib/veroscript/modules에 설치됩니다. 등록에는 제품 버전뿐 아니라 구현 식별자가 필요합니다.

[네트워크: 소스 구성 — 빌드와 네이티브 모듈 등록](../../../../../VeroScriptNetwork/Resources/Docs/Developer/ko-KR/index.md#network-execution-assembly)을 참조하세요.

실행이 허용되면 네이티브 데이터, 정적/동적 등록, 소스 모듈, 바이너리 라이브러리를 따로 검증합니다. VEROSCRIPT_JSON_SCHEMA_CORPUS는 고정 suite와 format, 미지 format은 주석 모드입니다. VEROSCRIPT_JSON_NORMALIZATION_CORPUS는 Unicode 17 NormalizationTest.txt, VEROSCRIPT_JSON_CORPUS는 JSONTestSuite입니다. 자료는 Resources/Tools/Tests의 명시적 도구로만 가져오며 보통 빌드는 다운로드하지 않습니다. 실제 입력/소스/도구 식별자와 결과를 기록하고 과거 통과 수로 현재 호스트, JIT/AOT, 모바일, 브라우저를 인증하지 않습니다.

여섯 json.json, 안정 ID/인수와 share/VeroScriptJson/Resources/Localization 배치를 유지합니다. json profile은 누적 중첩 대신 기존 항목을 대체합니다. 고정 입력으로 Resources/Tools/Unicode/generate_schema_unicode.py를 써서 표를 갱신하고 Python을 제품 런타임 종속성으로 만들지 않습니다. 원문 고지와 소스 목록을 보존합니다.

```sh
cmake -S Modules/Network/VeroScriptJson -B build/json -DCMAKE_BUILD_TYPE=Release
cmake --build build/json --config Release
ctest --test-dir build/json -C Release --output-on-failure
cmake --install build/json --config Release --prefix /absolute/sdk
```

[User](../../User/ko-KR/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/ko-KR/GENERAL.md)
