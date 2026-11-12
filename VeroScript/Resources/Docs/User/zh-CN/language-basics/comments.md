# 注释

注释用于向源文件添加说明。VeroScript 支持单行注释和块注释。

## 单行注释

单行注释以 `//` 开始，直到当前行结束：

```VeroScript
// Explain the purpose of the variable below.
int retry_count = 3;

int value = 1; // A comment may follow a statement.
```

## 块注释

块注释以 `/*` 开始，以 `*/` 结束，可以跨越多行：

```VeroScript
/*
	This note applies to the following code block.
*/
int result = 0;
```

注释本身不是执行语句，未闭合的块注释会产生词法错误。宿主可选择保留全部注释、仅文档注释或不保留注释，保留的注释和文档可通过源码元数据查询。已激活的编译扩展可以利用这些信息生成代码。注解元数据的保留独立于注释保留策略。

## 编辑器工具使用的源码文档

Tooling 悬停会读取紧邻源码声明之前的连续注释。源码定义的预处理宏补全项也可以携带其前置注释；宿主补全文档来自绑定元数据。可使用 `//`（包括 `///`）或 `/* ... */` 块注释；注释与声明之间不能有空行。这是 Tooling 约定，编译器仍将其视为普通词法注释。

`@param 名称 说明` 用于说明参数；`@return 说明` 和 `@returns 说明` 用于说明返回值。悬停会使用当前语言目录中的标签显示这些标记。未知标记仍作为普通注释文本保留；宏补全文档保留原始注释内容。

```VeroScript
// Adds two integers.
// @param left First addend.
// @param right Second addend.
// @return The sum.
int Add(int left, int right)
{
	return left + right;
}
```

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/comments.md) · [fr-FR](../../fr-FR/language-basics/comments.md) · [ja-JP](../../ja-JP/language-basics/comments.md) · [ko-KR](../../ko-KR/language-basics/comments.md) · [zh-CN](comments.md) · [zh-TW](../../zh-TW/language-basics/comments.md)
