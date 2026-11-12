# 模块

模块以源文件为边界组织声明，并控制声明的可见性。模块通常通过 `.vs` 文件表示，使用 `import` 引入，使用 `export` 将声明提供给其他模块。

## 导入模块

使用 `import` 导入源文件或库包：

```VeroScript
import "string.vs";

int main()
{
	print(std::strings::join(std::strings::split("a,b,c", ","), "-"));
}
```

同一个模块被多次导入时，模块身份会抑制重复加载。导入模块不会把它的私有声明全部暴露给当前模块；只有被导出的声明可以被外部使用。

## 导出声明

在声明前使用 `export`，可以让其他模块导入该声明：

```VeroScript
export struct Box<T>
{
	T value;

	Box(T initial)
	{
		value = initial;
	}

	T get() const
	{
		return value;
	}
};

int hidden_value()
{
	return 99;
}
```

上例中的 `Box` 可以被其他模块使用，而 `hidden_value` 没有导出，不能作为库模块的公开 API。导出适用于函数、变量、结构体、类、接口、枚举和泛型声明。

## 重新导出模块

`export import` 导入一个模块，并把目标模块的导出声明继续提供给当前模块：

```VeroScript
export import "string.vs";
```

其他模块只需要导入这个门面模块，就可以使用 `string.vs` 导出的声明：

```VeroScript
import "string_facade.vs";

string text = std::strings::join(std::strings::split("a,b", ","), "-");
```

重新导出不会把目标模块的私有声明变成公开声明，也不会复制源文件文本。

## 模块和命名空间

模块解决源文件之间的依赖和可见性问题；命名空间解决同一模块内的名称组织问题。一个模块可以声明多个命名空间，也可以从其他模块导入命名空间中的公开声明。

## 导入库包

源模块和二进制库包使用相同的 `import` 语法：

```VeroScript
import "DemoLibrary.vslib";
```

库包只能提供已经导出的声明。包的构建、检查和发布方式见[包与分发](../../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/packages.md)。

## 公开宏

参阅[预处理器 — 公开宏](preprocessor.md#exported-macro-contract)。

嵌套类型、局部声明、访问规则和分段泛型实参请参见[类型作用域](scope.md)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/modules.md) · [fr-FR](../../fr-FR/language-basics/modules.md) · [ja-JP](../../ja-JP/language-basics/modules.md) · [ko-KR](../../ko-KR/language-basics/modules.md) · [zh-CN](modules.md) · [zh-TW](../../zh-TW/language-basics/modules.md)
