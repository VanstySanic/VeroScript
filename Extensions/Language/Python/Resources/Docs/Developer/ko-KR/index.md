# VeroScript Python 기술 문서


이 directory는 maintainer와 binding author를 위한 source/binary contract 문서이며 Python user tutorial이 아닙니다. Python/VeroScript syntax, API·module name, command option, JSON key, diagnostic은 source 표기를 그대로 유지합니다.

## 모듈 소유권

그룹은 다섯 모듈로 구성됩니다.

- `VeroScriptPython`은 stable C Interop ABI 위의 CPython adapter이며 session, typed value/container, package, callback, reflection, retained callable, serialization, Python-backed host class를 소유합니다.
- `VeroScriptPythonAot`는 standalone Python source 또는 multi-file source package를 emit합니다. 생성 결과는 VM, Interop shared library, CPython extension을 load하지 않습니다.
- `VeroScriptPythonFrontend`는 입력 module을 import·execute하지 않고 지원 typed Python 3.11 declaration subset을 static parse합니다.
- `VeroScriptPythonBindGen`은 frontend declaration을 Binding Catalog metadata와 input별 API reference로 변환합니다.
- `VeroScriptPythonTranspiler`는 지원 typed subset을 VeroScript로 lower하고 real compiler로 생성 source를 검증합니다.

## static tool 경계

Frontend, BindGen, Transpiler는 static tool입니다. input module을 import·execute하거나 runtime side effect를 사용해서는 안 됩니다. 지원하지 않는 dynamic feature, untyped signature, 보존할 수 없는 arbitrary-precision semantics, async behavior, reflection, inheritance는 근사하지 말고 명시적으로 실패해야 합니다.

project mode는 output 전에 제공된 module graph 전체를 resolve합니다. relative package layout, module name, import, source map, API reference, manifest entry는 서로 일관되어야 합니다. parse/lowering/import resolution/final compiler validation 중 하나라도 실패하면 기존 output을 교체해서는 안 됩니다.

## runtime과 generated code 경계

CPython adapter는 implementation이 허용하는 native work 동안 GIL을 release하고 Python callback에서 reacquire합니다. creator-thread restriction, retained-callable ownership, callback reentrancy, shared Python state synchronization은 public contract입니다. worker callback은 creator-thread-confined session operation을 호출하면 안 됩니다.

standalone AOT runtime은 documented executable subset에 한해 VeroScript integer, container, exception, module, concurrency semantics를 보존합니다. 생성 worker는 OS thread를 사용하고 script global/static field를 분리하며 channel identity를 명시적 synchronized sharing capability로 보존합니다.

## Python object와 artifact 세부 사항

주요 runtime object는 `Session`, `Value`, `Package`, retained callable handle, `HostParameter`, host class/function registration object입니다. session은 creator thread에서 explicit close해야 합니다. 빈/narrow container가 Python type guessing에 의존하지 않도록 `Value` container는 explicit element type을 쓰며 iteration과 `keys()`/`values()`/`items()`는 owned snapshot을 반환합니다. host callback과 mutation callback은 exception/threading 규칙이 다르고 callback-owned state는 자체 동기화가 필요합니다.

AOT bundle은 manifest와 compatibility runtime file을 포함합니다. 파일을 임시 디렉터리에 준비한 뒤 교체하며 empty regular directory 또는 foreign content가 없는 structurally valid VeroScript Python AOT manifest directory에만 허용됩니다. recognized interpreter `__pycache__`만 허용되는 generated extra입니다. 최종 경로의 링크와 Windows junction은 거부합니다. 교체 중 출력 경로가 잠시 없을 수 있고 실패 시 복원을 시도합니다. generated program은 사용자의 Python interpreter를 선택하며 Python SDK를 discover하거나 bundle하지 않습니다.

## packaging과 versioning

wheel과 source distribution은 일치하는 native extension, Interop library, Python source, type information, standard library, localization asset을 포함해야 합니다. `VeroScriptVersion.h`가 authoritative semantic version source이고 package metadata와 imported runtime version은 일치해야 합니다.

Frontend project mode는 input별 AST artifact와 project manifest를 emit합니다. BindGen project mode는 merged Binding Catalog 하나, input별 API reference 하나, `bindgen.manifest.json`을 emit합니다. Transpiler project mode는 relative package layout을 보존하고 제공된 project-relative import를 resolve하며 source map을 emit하고 complete-graph validation 뒤에만 `transpile.manifest.json`을 작성합니다. import failure, duplicate module, ambiguous declaration, final compiler check failure는 기존 output을 보존해야 합니다.

## 상세 페이지

- [Python runtime과 static tool](runtime-and-tools.md)
- [Python AOT와 packaging](aot-and-packaging.md)
- [Python toolchain과 package 계약](toolchain-and-packaging.md)

## 명세의 기준 소스

Python source package, native adapter code, frontend AST, generator/transpiler test, CMake file, package metadata, distribution validation tool이 source of truth입니다. supported subset, threading rule, generated artifact, package layout, ABI mapping 변경과 이 문서를 함께 갱신합니다.


[Android/iOS의 Python](mobile.md)

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
