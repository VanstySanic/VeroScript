# C++ frontend, BindGen, Transpiler

## 프런트엔드 AST

`VeroScriptCppFrontend`는 제한된 C++17 subset을 token, source location, attribute, type, parameter, template, record, enum, variable, function, 일반 `typedef`/`using` alias, namespace, body의 stable model로 parse합니다. 표현 가능한 경우 qualified name, C linkage, storage/semantic qualifier, constructor, destructor, friend, 같은 file 안의 out-of-class definition을 유지합니다.

`FParseOptions`는 include/pragma handling, unknown preprocessor directive, function-body parsing을 제어합니다. `FTypeMapOptions`는 raw host pointer, rvalue reference, known host/enum type, known type parameter, project alias를 제어합니다. 이들은 숨겨진 global policy가 아니라 explicit option입니다.

collector는 input 간 compatible declaration과 forward declaration을 merge합니다. 또한 한 input의 conventional out-of-class member definition을 다른 input의 complete record declaration과 pair합니다. duplicate definition, repeated input path, internal-linkage collision, anonymous namespace, unsupported template, ownership/control-flow meaning을 보존할 수 없는 construct는 거부합니다.

## Binding Catalog 생성

BindGen은 machine-readable Binding Catalog JSON을 emit합니다. input별 API reference와 project manifest도 emit할 수 있습니다. authoritative catalog는 merged catalog이며 API reference는 review artifact이므로 compile해서는 안 됩니다. structured attribute, alias, enum identity, overload information, member flag, documentation은 catalog schema가 표현할 수 있을 때만 보존합니다.

project generation은 모든 declaration을 먼저 resolve하고 충돌하는 input basename에 deterministic name을 부여하며 catalog, reference, manifest를 하나의 transaction으로 replace합니다.

## 소스 변환기

Transpiler는 direct VeroScript meaning이 있는 construct만 lower합니다. VeroScript source와 declaration-level source map을 emit하고 project mode는 input마다 module/map과 manifest를 emit합니다. generated source는 default로 real VeroScript compiler가 compile합니다. 지원하지 않는 ownership, RTTI, pointer access, advanced template, `std` facility, 표현 불가능한 attribute는 명시적으로 실패합니다.

멤버 `operator()`와 `operator[]`는 오버로드와 참조 매개변수를 보존합니다. 소스 변환은 참조 반환을 포함한 참조 시그니처에 `T&` / `const T&`를 사용하며, 연산자 인수는 코어 규칙에 따라 참조에 암시적으로 바인딩됩니다. BindGen은 연산자 및 참조 매개변수 플래그를 보존하지만, 현재 Binding Catalog에서 저장 위치와 수명을 표현할 수 없는 네이티브 참조 반환은 명시적으로 거부합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/frontend-and-tools.md) · [fr-FR](../fr-FR/frontend-and-tools.md) · [ja-JP](../ja-JP/frontend-and-tools.md) · [ko-KR](frontend-and-tools.md) · [zh-CN](../zh-CN/frontend-and-tools.md) · [zh-TW](../zh-TW/frontend-and-tools.md)
