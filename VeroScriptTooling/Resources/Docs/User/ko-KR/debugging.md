# 디버깅

VeroScript 디버거는 DAP(디버그 어댑터 프로토콜)를 사용하고 VM을 통해 스크립트를 실행합니다. 디버깅 및 핫 리로드 세션은 JIT 실행 경로를 사용하지 않습니다.

## 표준 입력/출력 모드

```powershell
veroscript --debug script.vs
```

이 모드는 표준 입력 및 출력을 통해 DAP 메시지를 전송하며, 이는 프로세스를 시작하고 관리하는 편집기나 다른 디버그 클라이언트에 적합합니다.

## TCP 모드

```powershell
veroscript --debug --dap-port=4711 script.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 script.vs
```

`--dap-port`은 `1`에서 `65535`까지의 포트에서 TCP DAP 서비스를 시작합니다. `--dap-bind`은 바인딩 주소를 선택하고 기본값은 `127.0.0.1`입니다. 외부 클라이언트가 실제로 요구하는 경우에만 루프백이 아닌 주소에 바인딩합니다.

`veroscript-dap`은 동일한 디버그 호스트에 대한 씬 별칭입니다. IDE F6 시작 및 연결 세션도 DAP를 사용합니다. 세션 연결이 끊어지면 클라이언트는 `terminateDebuggee`을 사용하여 디버기를 종료할지 여부를 선택할 수 있습니다.

## 세션 수명 주기와 시작 인수

클라이언트는 `launch` 또는 `attach`보다 먼저 `initialize`를 보내야 합니다. 어댑터는
`supportsTerminateRequest`를 포함한 capability와 `name`, `version`, `platform`,
`jitCapability` metadata를 반환합니다. wire host는 `initialize` 응답 뒤에 `initialized`
event를 보냅니다.

어댑터가 현재 보고하는 DAP capability는 다음과 같습니다.

| Capability | 값 |
| --- | --- |
| `supportsConfigurationDoneRequest` | `true` |
| `supportsStepInRequest` | `true` |
| `supportsStepOutRequest` | `true` |
| `supportsStepBackRequest` | `false` |
| `supportsConditionalBreakpoints` | `true` |
| `supportsEvaluateRequest` | `true` |
| `supportsSetVariable` | `true` |
| `supportsTerminateRequest` | `true` |

`jitCapability`는 코어 JIT 서비스 계약(현재 `backend-services`)을 나타내며,
네이티브 backend 설치나 선택 여부를 나타내지 않습니다.

`attachMode: "domain"`과 `domainId`는 host가 미리 준비한 영구 실행 도메인에 연결하며,
여러 script 호출 사이에도 디버그 연결을 유지합니다. client는 먼저
`initialize.body.veroscriptSupportsDomainAttach`가 정확히 `true`인지 확인해야 합니다.
그렇지 않으면 이 모드를 거부하고 프로그램 로드로 전환하면 안 됩니다.
`configurationDone`은 연결만 활성화하며 script를 로드하거나 실행하지 않습니다.
지원 기능은 attach 응답을 따릅니다. 기본 연결 해제는 host 정책에 따라 정지된 실행을 재개하고
host를 유지합니다. 이미 JIT를 실행 중인 VM을 중간에 인계받을 수 없습니다.
자세한 내용은 [도메인 계약](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/session-debugging.md)을 참고하세요.

아래 프로그램 로드 흐름은 `launch`와 프로그램 모드의 `attach`
(`attachMode: "program"` 또는 생략)에 적용됩니다. 둘은 동일한 debug loader로 로드합니다. `attach`는 IDE가
live binding을 소유한 embed 또는 TCP host에 연결할 때 사용하는 request이며, 이미 실행 중인
VM에 연결하는 기능은 아닙니다. breakpoint를 설정한 뒤 `configurationDone`을 보내면
실행이 시작됩니다.

`launch`와 `attach`가 지원하는 인수는 다음과 같습니다.

| Field | 의미 |
| --- | --- |
| `program` | 스크립트 경로입니다. CLI debug command가 기본값을 제공할 수 있습니다. |
| `source` | inline source text입니다. 비어 있지 않으면 `program`보다 우선합니다. |
| `fileLabel` | inline `source`의 표시 label 및 source identity입니다. |
| `workspace` | 상대 launch path의 기준 directory입니다. |
| `cwd` | 스크립트의 working directory 및 상대 경로 해석 directory입니다. `workingDirectory` alias도 허용됩니다. 생략하면 가능한 경우 `workspace`를 사용하며 directory가 존재해야 합니다. |
| `args` | `main(string[] args)`로 전달되는 string array이며 스크립트 경로는 포함하지 않습니다. |
| `input` | 스크립트의 초기 standard input이며 DAP protocol stream과 독립적입니다. |
| `env` | debug run의 environment override입니다. 표준 형식은 object이고 `environment`는 alias이며 `{ "name", "value" }` 항목의 array도 허용됩니다. |
| `stdlibSearchPaths` | 순서가 있는 standard-library search root입니다. 단일 root용 호환 alias로 `stdlibPath`도 사용할 수 있습니다. |
| `importPaths`, `implicitImportPaths` | 일반 및 implicit module search path입니다. |
| `defines`, `predefinedMacros` | preprocessor definition 및 host-style predefined macro입니다. |
| `sourceOverlays` | imported module을 해석할 때 사용하는 memory source file입니다. |
| `bindingCatalogPaths` | metadata/stub binding으로 순서대로 로드되는 catalog file입니다. embed host가 `LiveBindings`를 제공하면 live binding이 우선합니다. |
| `allowCircularImports`, `allowProjectWideAccess` | import 및 project visibility policy입니다. |
| `stopOnEntry` | entry point에서 멈춘 뒤 정상 실행을 계속할지 여부입니다. |
| `backend`, `maxCallDepth` | backend preference 및 call-depth limit입니다. debug execution은 계속 VM path를 사용합니다. |

`program`, import, standard library, implicit import, overlay, binding catalog의 상대 경로는
`cwd`가 설정되어 있으면 `cwd`에서, 아니면 `workspace`에서 해석됩니다. `workspace`만
제공하면 그것이 스크립트 working directory가 됩니다. environment override는 debug
session 동안만 적용되고 종료 후 복원됩니다. live binding을 사용하는 `attach` session에는
environment override 또는 working-directory override를 함께 지정할 수 없습니다.

CLI는 스크립트 경로 뒤의 script argument를 받아 DAP request의 기본값으로 전달합니다.

```powershell
veroscript --debug hello.vs red blue
veroscript --debug --dap-port=4711 hello.vs red blue
```

명시적인 DAP `args` array가 이 기본값을 대체합니다. 한 번의 launch는 script argument
최대 4096개, 세 module-path array 각각 최대 2048개, environment entry 최대 1024개를
허용합니다. environment name은 비어 있거나 `=` 또는 NUL을 포함할 수 없고 value도 NUL을
포함할 수 없습니다.

## 비동기 실행과 제어

stdio 및 TCP wire host에서는 `configurationDone`이 VM 종료 전에 acknowledge되고 VM은
execution worker에서 실행됩니다. 따라서 protocol reader는 실행 중에도 control 및
stopped-state request를 처리할 수 있습니다. `stopped` event의 `reason`은 `entry`,
`breakpoint`, `step`, `pause` 중 하나이며, 실행 중인 VM을 cooperative pause로 멈추면
`pause`가 됩니다.

`continue`, `next`, `stepIn`, `stepOut`은 `stopped` 이후에만 유효하고 실행을 재개합니다.
`stackTrace`, `scopes`, `variables`, `evaluate`, `setVariable`도 정지 중에만 유효합니다.
breakpoint는 `configurationDone` 전에 또는 정지 중에 설정해야 하며 실행 중 변경은 거부됩니다.
`pause`는 VM 실행 중에만 가능한 cooperative stop request입니다. `terminate`는 cooperative
cancellation을 요청합니다. 클라이언트는 request response만으로 실행 종료를 가정하지 말고
뒤따르는 `exited` 및 `terminated` event를 기다려야 합니다.

`threads` request는 id `1`, name `main`인 하나의 논리적 VM thread를 노출합니다. 이는
debugger의 실행 관점이며 wire host의 protocol thread와 execution worker thread는 script
thread로 노출되지 않습니다.

`disconnect`의 `terminateDebuggee` 기본값은 `launch`에서 `true`, `attach`에서 `false`입니다.
다중 세션 TCP host에서는 `terminateDebuggee=false`가 현재 client session만 종료하고 listener를
남기며, `true`는 host listener를 중지합니다. `terminate` request는 항상 debug run을
취소하고 host 중지를 요청합니다.

## TCP host와 embedding

CLI TCP host는 기본적으로 한 session을 처리합니다. SDK host는 `VeroScriptDapTcp::Run`과
`FDapTcpRunOptions`를 사용할 수 있습니다. `MaxSessions=0`은 host가 중지될 때까지 client를
계속 받고, `Port=0`은 ephemeral port를 선택하여 `OnListening`으로 알리며,
`BindAddress`는 일반적으로 `127.0.0.1`로 유지해야 합니다. embed host가
`FDapStdioRunOptions::LiveBindings`를 설정하면 `attach` session이 catalog stub 대신 등록된
host function을 실행합니다. live catalog는 DAP 실행 동안 유효해야 합니다. embed host가
자체적으로 readiness를 보고하면 `bAnnounceListen=false`로 설정할 수 있으며, 기본 listener
알림은 host error stream에 기록됩니다. `VeroScriptDapStdio::Run`은 clean session에서 `0`,
protocol 또는 I/O 오류에서 `1`, client가 host terminate를 요청하면 `2`를 반환합니다.
`VeroScriptDapTcp::Run`에서는 client가 host 종료를 요청한 경우 정상 반환인 `0`을 반환하며,
listen, accept 또는 그 밖의 비정상 session 오류는 0이 아닌 값을 반환합니다.

## 중단점 지원

어댑터는 `setBreakpoints`를 통한 source line breakpoint를 지원합니다.
exception breakpoint와 function breakpoint는 지원하지 않습니다. filter가 없는
`setExceptionBreakpoints`는 표준 클라이언트 시작 handshake의 clear 작업으로만 성공하며,
exception filter가 있는 요청과 모든 `setFunctionBreakpoints` 요청은 실패한 DAP 응답을
반환합니다. 클라이언트는 이 두 category를 활성 상태로 표시해서는 안 됩니다.

`setBreakpoints`는 1부터 시작하는 physical source line을 사용합니다. `launch` 또는 `attach`가
debuggee를 load하기 전에는 반환된 breakpoint가 `verified: false`인 pending 상태입니다. code가
load되면 adapter가 이를 bind하고 같은 DAP breakpoint ID 및 `reason: "changed"`가 있는 표준
`breakpoint` event를 emit합니다. verification에는 path가 하나의 compiled source로 해석되고,
요청 line이 preprocessing 뒤에도 active이며 executable location을 찾을 수 있어야 합니다.
basename만으로 file을 match하지 않습니다. bind에 실패하면 unverified 상태와 localized `message`가
반환됩니다.

function 안의 non-executable physical line은 adapter가 그 function에서 다음 executable physical
line으로 bind해 반환할 수 있으므로 client는 반환된 `line`을 사용해야 합니다. module initializer의
breakpoint에는 exact physical line이 필요합니다. preprocessing으로 제거된 line, 범위를 벗어난 line,
없거나 ambiguous한 source는 bind되지 않습니다.

그 밖의 지원되지 않는 DAP command도 현지화된 error message와 함께 실패한 응답을
반환합니다. 표시 언어를 선택하려면 DAP `initialize` arguments의 `locale`에 BCP 47
tag를 지정하십시오. client는 번역된 message text가 아니라 DAP success/error field를
기준으로 분기해야 합니다.

## 디버그 실행 중 출력

`std::console::write`와 `std::console::error`는 줄바꿈을 추가하지 않고 출력합니다.
`std::console::flush()`를 호출하면 대기 중인 표준 출력이 즉시 표시됩니다. DAP 어댑터는
실행 중 표준 출력과 오류 출력을 서로 다른 `output` 이벤트로 전달하며, 최종 결과에서도
두 스트림을 분리해 보존합니다.

## 실행 제약

디버거는 중단점, 스테핑 및 변수 검사를 위해 전체 VM 디버깅 경로를 사용합니다. `--backend=mixed` 또는 `--backend=jit`을 요청해도 현재 디버그 실행이 JIT로 전환되지 않습니다. 일반적인 실행은 디버깅이 끝난 후 요청된 백엔드를 복원할 수 있습니다.

보다 세분화된 디버거 및 호스트 바인딩 통합은 [호스트 통합](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/embedding.md)을 참조하세요.

## 메모리 검사

일시 중지 중 객체나 영역을 확장하면 `[object_id]`, `[byte_length]` 같은 읽기 전용 정보를 확인할 수 있습니다. 주소는 허용된 경우만 표시합니다. ID는 살아 있는 객체를 식별하고 주소는 임시 관찰값입니다. 검사 정보에 대입할 수 없습니다. DAP `variablesReference`는 프로토콜 핸들이며 임의 `readMemory` / `writeMemory`는 지원하지 않습니다.

## 디버그 프레임의 소스 스냅샷

스택 프레임이 양수 `sourceReference`를 제공하면 클라이언트는 DAP `source`로 텍스트를 요청해야 합니다. 반환되는 텍스트는 인라인, 생성 또는 메모리 내 코드의 컴파일된 스냅샷이며, 디스크 파일이 오래되거나 없는 소스 오버레이도 포함합니다. 읽기 전용으로 표시하세요. 같은 이름의 디스크 파일을 수정해도 실행 중인 프로그램은 바뀌지 않습니다. 실행을 재개해도 캡처된 텍스트는 읽을 수 있지만 프레임 및 변수 핸들은 만료됩니다. 프로그램을 다시 로드하거나 지우면 이전 소스 참조도 무효화되므로 현재 프레임에서 다시 가져오세요.

## 네이티브 디버그 호스트 수명주기

소유 스레드에서 수명주기 작업을 직렬화합니다. 콜백을 포함해 Run이 활성 상태이면 LoadSource/LoadFile과 중첩 Run을 거부하고 현재 프로그램을 유지합니다. Clear는 아무 작업도 하지 않습니다. Run이 반환하거나 예외로 끝난 뒤 Clear를 호출하세요. 인수는 Run 시작 시 고정되며 콜백에서 바꾼 내용은 다음 실행에 적용됩니다. SetStopHandler 또는 DAP 이벤트 핸들러 교체·해제는 다음 알림부터 적용되고 실행 중인 콜백과 캡처는 반환할 때까지 유지됩니다. 동시 접근을 허용하지는 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/debugging.md) · [fr-FR](../fr-FR/debugging.md) · [ja-JP](../ja-JP/debugging.md) · [ko-KR](debugging.md) · [zh-CN](../zh-CN/debugging.md) · [zh-TW](../zh-TW/debugging.md)
