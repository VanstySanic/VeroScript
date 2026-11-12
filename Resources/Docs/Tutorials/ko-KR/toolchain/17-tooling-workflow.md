# Tooling workflow: editor와 debugger 연결하기

이 tutorial에서는 VeroScript project를 language server와 debug adapter에 연결합니다.
CLI를 확인하고 LSP를 설정한 뒤 editor assistance를 시험하고 DAP session을 시작합니다.

## 1단계: CLI 확인

같은 Runtime 릴리스의 CLI와 VM, 버전이 일치하는 언어 서버, LSP와 DAP를 지원하는 클라이언트를 사용합니다. 아래 두 파일을 쓰기 가능한 같은 작업 디렉터리에 저장합니다. 해당 디렉터리에서 명령을 실행하고 편집기에서 작업 공간으로 엽니다.

### hello.vs

```VeroScript
import "lesson-utils.vs";

namespace Lesson
{
	int twice(int value)
	{
		return value * 2;
	}
}

int main(string[] args)
{
	std::chrono::now();
	int value = Lesson::twice(lessonSeed());
	print("value={}, argc={}", value, len(args));
	return 0;
}
```

### lesson-utils.vs

```VeroScript
export int lessonSeed()
{
	return 21;
}
```

```powershell
veroscript --version
veroscript --check hello.vs
veroscript --vm-backend=vs-bytecode --backend=vm hello.vs red blue
```

검사는 스크립트를 실행하지 않고 성공합니다. 실행하면 `value=42, argc=2`가 출력되고 종료 코드는 0입니다.

여기서 실패하면 editor를 설정하기 전에 수정하십시오. CLI와 LSP 결과가 다르면 보통
module path, macro 또는 catalog 설정이 서로 다릅니다.

## 2단계: Language Server 시작

editor에서 일치하는 버전의 `veroscript-lsp`를 표준 입출력으로 시작합니다. editor는
`initialize` 응답을 받은 뒤 `initialized`를 보냅니다. project root를 workspace root로
전달하십시오.

작업 공간 및 클라이언트 기능 정보와 함께 아래 필드를 `initialize` 요청의 `params`에 추가합니다. `.`이 실습 디렉터리를 가리키도록 해당 디렉터리에서 서버를 시작합니다. 편집기가 다른 위치에서 서버를 시작하면 Windows의 `C:/work/project`처럼 절대 경로를 사용합니다. 다른 프로젝트에서 별도의 표준 라이브러리 디렉터리를 지정해야 할 때는 `stdlibPath`를 설정합니다.

```json
{
	"locale": "ko-KR",
	"initializationOptions": {
		"veroscript": {
			"importPaths": [
				"."
			]
		}
	}
}
```

client가 watched file dynamic registration을 지원하면 server는 `initialized` 뒤에
`**/*.vs` 및 `**/*.vslib` watcher를 요청합니다. 지원하지 않으면 client에서 이 pattern을
직접 설정하거나 `workspace/didChangeWatchedFiles` notification을 명시적으로 보내십시오.

`.vs` document를 열면 buffer가 memory overlay로 분석됩니다. 저장 전에도 import를
해결할 수 있습니다. 오래된 version의 incremental `didChange`는 무시됩니다.

## 3단계: editor assistance 시험

1단계에서 저장한 `hello.vs`를 엽니다.

1. syntax 또는 type error를 임시로 만들고 diagnostic을 확인한 다음, 오류를 되돌립니다.
2. `std::chrono::now();`를 `std`로 바꾸고 바로 뒤에 caret을 놓습니다. completion을 실행해
   namespace 항목을 선택하면 `std::`가 삽입됩니다.
   `ch`를 입력하고 `chrono`를 선택하면 `chrono::`가 삽입되어 member를 확인할 수 있습니다.
   마지막으로 줄을 `std::chrono::now();`로 완성해 예제를 유효한 코드로 만듭니다.
   replace range 뒤에 `::`가 없을 때만 completion이 구분자를 추가합니다.
3. `Lesson::twice(lessonSeed())`를 잠시 `Lesson::tw`로 바꾸고 완성을 요청합니다. 클라이언트가 `textDocument.completion.completionItem.snippetSupport`를 알린 경우 `twice`의 매개변수 자리표시자를 확인합니다. 다음 작업 전에 원래 호출로 되돌립니다.
4. namespace, function 또는 type을 hover합니다.
5. `hello.vs`에서 `lessonSeed` 호출의 정의로 이동해 `lesson-utils.vs`가 열리는지 확인합니다. 문서 기호에서 `Lesson`과 `main`을 찾은 뒤 `workspace/symbol`로 `Lesson::twice`를 검색하고 `workspaceSymbol/resolve`로 결과를 확인합니다.
6. 완성 항목을 확인해 Markdown 문서가 있으면 읽습니다. `import "lesson-utils.vs";` 줄의 링크를 엽니다.
7. semantic token을 요청한 뒤 이전 result id로 semantic-token delta를 요청합니다.
8. `Lesson`에서 이름 바꾸기를 준비한 뒤 `Practice`로 바꿉니다. 선언과 한정된 이름을 사용하는 호출이 함께 바뀌는지 확인합니다.
9. 문서 전체를 서식 지정한 뒤 선택 범위의 서식을 바꾸고 해당 줄 전체가 편집되는지 확인합니다. `source.formatDocument`를 사용해 봅니다. `source.organizeImports`를 시험하려면 import 줄을 한 번 복제하고 작업이 중복을 제거하는지 확인합니다. 두 파일을 저장한 뒤 `veroscript --check hello.vs`를 다시 실행합니다.

generic type 및 callable completion은 해당 context에서 LSP snippet을 사용할 수 있습니다.
server는 client가 `textDocument.completion.completionItem.snippetSupport`를 선언한 경우에만 snippet을 보내고,
그 외 삽입은 일반 텍스트로 처리합니다. standard-library namespace는 탐색할 수 있지만
rename할 수 없습니다. code action은 완전한 format과 완전히 중복된
import만 제거하는 보수적인 organize imports를 제공합니다.

## 4단계: host catalog 추가

호스트 바인딩을 사용하는 프로젝트라면 이 단계를 진행합니다. 실습 파일 두 개만 사용하는 경우 5단계로 바로 넘어갈 수 있습니다.

project가 사용하는 catalog를 `initializationOptions.veroscript.bindingCatalogPaths`에 같은 순서로 추가합니다.
function, type, member, annotation, generic template과 documentation이 analysis,
completion, hover와 symbol에 반영됩니다.

server 실행 중 catalog를 편집하면 다음 request에서 reload되고 열린 document가 재분석됩니다.
catalog는 metadata일 뿐 runtime implementation을 등록하지 않습니다.

## 5단계: diagnostic language 변경

document를 열기 전에 `initialize.locale`에 BCP 47 tag를 지정합니다.

```json
{
	"locale": "zh-CN"
}
```

새 `locale`로 언어 서버를 다시 시작합니다. 3단계와 같은 임시 오류를 만들어 진단 코드와 번역된 메시지를 비교합니다. 코드는 그대로이며 번역이 없으면 `en-US`를 사용합니다. 디버깅 전에 오류를 고치고 파일을 저장합니다.

## 6단계: debugger 시작

DAP 클라이언트에서 연결 방식을 선택합니다. 클라이언트가 어댑터를 시작하는 경우 실습 디렉터리에서 다음 명령으로 시작하고 표준 입출력으로 연결합니다.

```powershell
veroscript --debug hello.vs
```

클라이언트를 따로 시작하는 경우 실습 디렉터리에서 TCP 어댑터를 시작한 다음 클라이언트를 `127.0.0.1:4711`에 연결합니다.

```powershell
veroscript --debug --dap-port=4711 hello.vs
```

표준 입출력 방식에서 기본 스크립트 인수를 지정하려면 첫 명령 대신 다음 명령을 사용합니다.

```powershell
veroscript --debug hello.vs red blue
```

클라이언트에서 `cwd`를 실습 디렉터리로 지정하고 `hello.vs`를 시작합니다. `print` 줄에 중단점을 설정하고 시작 요청의 `args`로 `["red", "blue"]`를 전달합니다. CLI에서 미리 인수를 지정했다면 `args`를 생략할 수 있습니다. 중단점에서 멈추면 실행을 재개하고 디버그 출력에 `value=42, argc=2`가 표시되는지 확인합니다. 요청 순서와 옵션은 [디버깅 참조](../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/debugging.md)에 설명되어 있습니다.

DAP `launch` 또는 `attach`에서는 `program` 또는 inline `source`를 사용하고, `args`를
`main(string[] args)`로 전달합니다. 스크립트 standard input에는 `input`, environment
override에는 `env`, working directory에는 `cwd`, standard-library root에는
`stdlibSearchPaths`를 사용하십시오. script input은 DAP protocol stream과 독립적입니다.
상대 경로는 `cwd`에서, 없으면 `workspace`에서 해석됩니다. debugging, 보존된 초기화 구간과 제한된 정리는 VM path를 사용합니다.
영속 Session의 일반 함수는 선택한 Mixed/JIT를 사용할 수 있습니다. 성능은 일반 실행에서 측정하십시오.

DAP `initialize` argument에는 diagnostic 및 error message 표시 언어를 정하는 BCP 47
`locale`을 지정할 수 있습니다. client는 번역된 text가 아니라 DAP success 및 error
field를 기준으로 분기해야 합니다.

source line breakpoint는 `setBreakpoints`로 설정합니다. exception breakpoint와 function
breakpoint는 지원하지 않습니다. 이 request는 명시적으로 실패하므로 client는 해당
category를 active로 표시해서는 안 됩니다.

`configurationDone` 뒤에 wire host는 request를 acknowledge하고 VM을 execution worker에서
실행합니다. 실행 중인 VM에는 `pause`로 cooperative pause를 요청할 수 있으며 뒤따르는
`stopped` event의 reason은 `pause`입니다. `continue`, `next`, `stepIn`, `stepOut`으로
재개하고 `terminate`로 cancellation을 요청합니다. 실행 중에도 protocol은 응답합니다.
`disconnect`의 기본값은 launch에서는 debuggee를 terminate하고 attach에서는 terminate하지
않는 것입니다. 다중 세션 TCP host에서는 `terminateDebuggee=false`로 listener를 유지합니다.

실행 중 `std::console::write`와 `std::console::error`는 줄바꿈 없이 출력합니다.
`std::console::flush()`를 호출하면 진행 상황 일부를 즉시 표시할 수 있습니다. 어댑터는
프로세스 종료 전에 표준 출력과 오류 출력을 서로 다른 DAP output 이벤트로 전달합니다.

LSP session은 `shutdown` 후 `exit`로 종료합니다.

## 연습

1. CLI와 LSP가 같은 import를 해결하도록 합니다.
2. 새 version 뒤에 오래된 `didChange`를 보내 새 buffer가 유지되는지 확인합니다.
3. catalog를 추가하고 host function이 completion과 hover에 나타나는지 확인합니다.
4. language를 바꾸고 diagnostic code가 유지되는지 확인합니다.
5. disk의 imported module을 변경하고 `workspace/didChangeWatchedFiles`를 보낸 뒤 열린
   document의 diagnostic이 갱신되는지 확인합니다.

자세한 내용은 [Language Server reference](../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/lsp.md)와
[Tooling reference](../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/index.md)를 참조하십시오.

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/toolchain/17-tooling-workflow.md) · [fr-FR](../../fr-FR/toolchain/17-tooling-workflow.md) · [ja-JP](../../ja-JP/toolchain/17-tooling-workflow.md) · [ko-KR](17-tooling-workflow.md) · [zh-CN](../../zh-CN/toolchain/17-tooling-workflow.md) · [zh-TW](../../zh-TW/toolchain/17-tooling-workflow.md)
