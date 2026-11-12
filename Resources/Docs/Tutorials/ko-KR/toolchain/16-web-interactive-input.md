# 16: WebAssembly SDK의 대화형 입력

전제 조건: 같은 SDK의 TypeScript wrapper와 일반 `veroscript-engine.mjs`/`.wasm`을 ES module 및 top-level await를 지원하는 project에 둡니다. wrapper와 예제를 함께 컴파일하고 JavaScript/engine의 상대 URL을 보존해 HTTP로 제공합니다. import 경로는 생성된 `veroscript-webassembly.js`이며 project에 맞게 조정합니다. [WebAssembly 배포 가이드](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ko-KR/index.md)를 참고하세요.

`read_line`, 형식이 지정된 입력 또는 토큰 입력, `pause`를 호출하는 스크립트에는 `runSourceInteractive`를 사용합니다. 프런트엔드는 VM이 전체 스크립트 상태를 유지하는 동안 하나의 논리적 입력 줄을 비동기적으로 제공합니다.

```typescript
import { createVeroScriptRuntime } from "./veroscript-webassembly.js";

const runtime = await createVeroScriptRuntime();
try {
	const lines = ["Ada", "3"];
	const result = await runtime.runSourceInteractive(
		`import "io.vs";
int main()
{
	print(std::io::read_line());
	return std::io::read_int();
}`,
		{ async readLine() { return lines.shift() ?? null; } },
	);
	console.log(result.output, result.exitCode);
}
finally
{
	runtime.dispose();
}
```

예상 결과는 `result.success`가 `true`, `result.output`이 `Ada\n`, `result.exitCode`가 `3`입니다. 입력은 `Ada`, `3` 두 줄이며 종료 코드 3은 엔진 실패가 아닙니다. 터미널 UI를 연결하기 전에 고정 입력으로 확인합니다.

EOF를 나타내려면 `null`을 반환합니다. 입력이 대기 중일 때 다른 runtime 작업을 호출하거나 `dispose`를 호출하지 마세요. 실행 시간이 제한되지 않은 스크립트를 중지하려면 runtime을 Worker에서 실행하고 해당 Worker를 종료합니다. 연습: 배열을 터미널 UI의 Promise로 바꿔 보세요.

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/toolchain/16-web-interactive-input.md) · [fr-FR](../../fr-FR/toolchain/16-web-interactive-input.md) · [ja-JP](../../ja-JP/toolchain/16-web-interactive-input.md) · [ko-KR](16-web-interactive-input.md) · [zh-CN](../../zh-CN/toolchain/16-web-interactive-input.md) · [zh-TW](../../zh-TW/toolchain/16-web-interactive-input.md)
