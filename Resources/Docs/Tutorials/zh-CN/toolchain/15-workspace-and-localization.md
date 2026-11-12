# 15：配置项目选项与诊断本地化

使用 CLI 选项设置导入路径、预处理定义和脚本参数。这些工具链设置不会改变 VeroScript 的语言语义。

## 准备教程文件

使用匹配的 Runtime 及其附带的 VM、本地化资源。创建可写的教程目录，在其中准备 `main.vs` 和 `src`／`shared` 子目录，保存以下三个文件。所有命令从该教程目录运行。搜索路径与宏定义只作用于本次调用；下文每条检查或运行 `main.vs` 的命令都重复提供相同选项。

### main.vs

```VeroScript
import "score.vs";

int main(string[] args)
{
#ifdef FEATURE_SCORE
	print("score={}, argc={}", scoreValue(), len(args));
#else
	print("feature disabled");
#endif
	return 0;
}
```

### src/score.vs

```VeroScript
import "settings.vs";

export int scoreValue()
{
	return baseValue() + 2;
}
```

### shared/settings.vs

```VeroScript
export int baseValue()
{
	return 40;
}
```

启用 `FEATURE_SCORE=1` 并传入 `demo 42` 时，预期打印 `score=42, argc=2`，返回零。去掉宏定义则选择 `feature disabled` 分支。检查成功时返回零，不调用 `main`。

## 第一步：设置编译选项

通过 `-I` 指定导入路径，通过 `-D` 指定预处理定义；多个值可以重复传入。CLI 直接读取这些命令行选项。

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

## 第二步：选择执行后端

本课使用 Runtime 自带的 VM，无需可选 JIT 包。下列命令把 `demo` 和 `42` 传给 `main.vs`，保留脚本原本的输出与退出结果。其他策略和模块环境选择规则见 [CLI 后端参考](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md#cli-backend-policy)。

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

## 第三步：运行前检查

运行前先对入口执行检查：

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

`main(string[] args)` 接收脚本路径之后的参数；脚本路径本身不在 `args` 中。CLI 的进程退出码由 `int main`、`void main` 或 `exit(n)` 的最终结果决定。

## 第四步：选择诊断语言

CLI 可以使用 `--lang` 临时选择语言：

```powershell
veroscript --lang zh-CN --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript --lang en-US --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

也可以在 PowerShell 会话中使用 `VERO_LANG`：

环境变量示例在检查后恢复原有 `VERO_LANG`。

```powershell
$tutorialPreviousLanguage = $env:VERO_LANG
try
{
	$env:VERO_LANG = "zh-CN"
	veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
	$env:VERO_LANG = $tutorialPreviousLanguage
}
```

语言标签从已部署的目录中发现。缺失的翻译回退到英文；显式传入不受支持的 `--lang` 值会报告参数错误。
解析器的声明诊断（包括无效的 `static class` 和 `static struct` 声明）在 CLI 与 LSP 中使用相同的稳定 ID 和目录。工具逻辑应使用诊断 ID；翻译后的消息只用于显示。

## 第五步：为项目提供本地化目录

将发行包的完整 `localization/` 目录树复制到项目中，保留完整的 `en-US` 英文基准。下图展示语言目录文件；复制得到的其他目录文件也应保留。

```text
localization/
  en-US/
    language.json
    ...
  zh-CN/
    language.json
    ...
```

通过 `VERO_LOCALIZATION_DIR` 显式选择项目目录树：可执行文件旁的目录优先于工作目录的自动发现。下面的示例在命令结束后恢复原有设置。

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang zh-CN --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

`en-US` 定义完整基准。其他带子标签的规范 BCP 47 目录只能包含英文目录树中已有的文件和消息 ID；缺失的翻译文件或键回退到英文。文件必须是字符串值组成的扁平 UTF-8 JSON 对象，不能有重复键或尾随内容。添加语言时，将其规范目录和 JSON 文件加入已选中的目录树。

## 练习

1. 在一次运行命令中去掉 `-D FEATURE_SCORE=1`，观察 `feature disabled`；恢复宏定义并传入 `demo 42`，确认 `score=42, argc=2`。
2. 把入口参数改为三个值，在 `main(string[] args)` 中逐个打印。
3. 选中项目目录树后，删除 `zh-CN/language.json` 中的 `diag.expected_expression`，执行下方命令，确认诊断以英文显示“expected expression”。

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang zh-CN -e 'int main() { return +; }'
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

[CLI 参考](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md) · [Localization](../../../../../VeroScript/Resources/Docs/User/zh-CN/localization.md)

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/toolchain/15-workspace-and-localization.md) · [fr-FR](../../fr-FR/toolchain/15-workspace-and-localization.md) · [ja-JP](../../ja-JP/toolchain/15-workspace-and-localization.md) · [ko-KR](../../ko-KR/toolchain/15-workspace-and-localization.md) · [zh-CN](15-workspace-and-localization.md) · [zh-TW](../../zh-TW/toolchain/15-workspace-and-localization.md)
