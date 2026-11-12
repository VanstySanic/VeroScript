# 07：模块、导入和预处理器

当程序超过一个文件时，模块负责组织依赖和公开 API；预处理器则适合在编译前根据条件选择代码。本篇先做一个私有实现，再通过 `export` 公开需要的函数。

## 第一步：创建模块

创建 `math_utils.vs`：

```VeroScript
export int square(int value)
{
	return value * value;
}

int hidden_value()
{
	return 99;
}
```

`square` 前面的 `export` 表示它是模块的公开声明；`hidden_value` 没有导出，其他模块不能把它当作公开 API 使用。

再创建 `main.vs`：

```VeroScript
import "math_utils.vs";

int main()
{
	print(square(7));
	return 0;
}
```

在两个文件所在目录运行：

```powershell
veroscript --check main.vs
veroscript main.vs
```

## 第二步：使用重新导出

如需提供统一的导入入口，可以创建 `facade.vs`：

```VeroScript
export import "math_utils.vs";
```

然后让入口文件只导入门面：

```VeroScript
import "facade.vs";

int main()
{
	print(square(8));
	return 0;
}
```

重新导出只会继续公开原模块已经导出的声明，不会把 `hidden_value` 变成公共函数。

## 第三步：加入编译条件

预处理器在编译时选择参与的源代码；运行时判断应使用普通的 `if`。例如：

```VeroScript
#define MESSAGE "development"

#ifdef RELEASE
#define MESSAGE "release"
#endif

int main()
{
	print(MESSAGE);
	return 0;
}
```

运行时传入 `-D RELEASE`：

```powershell
veroscript --check main.vs
veroscript -D RELEASE main.vs
```

宏在编译前展开。宏名称和函数名称不要随意重名，复杂逻辑优先写成真正的函数。

## 第四步：使用匿名命名空间组织声明

匿名命名空间写作 `namespace { ... }`，可以在不增加额外限定的情况下组织声明。在 VeroScript 中它是透明的，不会创建合成命名空间，也不提供 C++ 式的内部链接。

```VeroScript
namespace
{
	int local_helper()
	{
		return 6;
	}
}

int main()
{
	print(local_helper());
	return 0;
}
```

在命名命名空间内部，成员会保留外层命名空间限定：

```VeroScript
namespace Diagnostics
{
	namespace
	{
		int code()
		{
			return 200;
		}
	}
}

int main()
{
	print(Diagnostics::code());
	return 0;
}
```

如果其他模块需要使用某个声明，应在该声明上单独使用 `export`。匿名命名空间本身不会被导出，也不会仅凭自身将声明变为私有。

## 练习

1. 导出一个 `cube` 函数，并从主模块调用。
2. 创建一个门面模块，同时重新导出字符串模块和自己的数学模块。
3. 增加 `#if` 分支，让调试版本打印额外信息。
4. 在命名命名空间内部的匿名命名空间中放置一个辅助函数，并通过外层命名空间限定调用它。

模块细节见[模块](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/modules.md)、[预处理器](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/preprocessor.md)和[命名空间](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/namespaces.md)。可对照 [`examples/reexport_main.vs`](../../../../../VeroScript/Resources/Scripts/examples/reexport_main.vs) 和 [`examples/preprocessor.vs`](../../../../../VeroScript/Resources/Scripts/examples/preprocessor.vs)。

下一篇进入标准库，用脚本处理真实文本和文件：[字符串、文件和路径](../standard-library/08-strings-files-and-paths.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/language/07-modules-and-preprocessor.md) · [fr-FR](../../fr-FR/language/07-modules-and-preprocessor.md) · [ja-JP](../../ja-JP/language/07-modules-and-preprocessor.md) · [ko-KR](../../ko-KR/language/07-modules-and-preprocessor.md) · [zh-CN](07-modules-and-preprocessor.md) · [zh-TW](../../zh-TW/language/07-modules-and-preprocessor.md)
