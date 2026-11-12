# 指令碼編譯擴充

<a id="source-extension-activation"></a>

使用宿主或應用提供的擴充，可在一般檢查前處理註記或產生宣告。在入口檔案開頭、選用註解及已有編譯器匯入之後，以 import compiler 啟用原始碼擴充；一般 import 不執行擴充註冊。

## 僅註冊描述的擴充

將第一份檔案儲存為 notes.vs，第二份儲存為 main.vs。此擴充僅描述 @Note，不產生業務行為。

```VeroScript
import "compiler.vs";

export void register_extension(std::compiler::registration reg)
{
	std::compiler::annotation_definition note = std::compiler::annotation_definition();
	note.name = "@Note";
	std::compiler::configuration_value label = std::compiler::configuration("note.text");
	note.documentation = label.exists() ? label.value() : "A descriptive annotation.";
	note.targets = array<std::compiler::target>{std::compiler::target::class_type};
	reg.register_annotation(note);
}
```

```VeroScript
import compiler "notes.vs";

@Note class Message {};

int main()
{
	type<Message> info = type<Message>();
	print(info.annotation(0).definition().documentation());
	return 0;
}
```

```sh
veroscript --backend=vm --check main.vs
veroscript --backend=vm main.vs
```

程式輸出 A descriptive annotation.。Runtime 標準程式庫須包含 compiler.vs；core.vs 不重新匯出此編譯專用入口。

## 擴充套件與限制

compiler-package 產生 .vslib 擴充，不執行註冊。將相同業務程式移除 import compiler 後存為 consumer.vs，明確選擇 notes.vslib；不要同時啟用原始碼與套件中的副本。

```sh
veroscript compiler-package notes.vs notes.vslib
veroscript --backend=vm --compiler-extension notes.vslib consumer.vs
```

請參閱[命令列工具 — 指令碼編譯擴充](../../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md#cli-compiler-options)。

```sh
veroscript --backend=vm --compiler-extension notes.vslib --compiler-config "note.text=Configured annotation." consumer.vs
```

```text
Configured annotation.
```

[註記](../language-basics/annotations.md)

擴充整體預設預算為 1,000,000 條 VM 指令及 2,000 毫秒。0 表示關閉對應預算；這些限制不能強制中斷阻塞的原生回呼。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md) · [en-US](../../en-US/standard-library/compiler.md) · [fr-FR](../../fr-FR/standard-library/compiler.md) · [ja-JP](../../ja-JP/standard-library/compiler.md) · [ko-KR](../../ko-KR/standard-library/compiler.md) · [zh-CN](../../zh-CN/standard-library/compiler.md) · [zh-TW](compiler.md)
