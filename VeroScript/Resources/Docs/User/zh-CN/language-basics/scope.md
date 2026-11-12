# 作用域

作用域决定一个名称在哪些源代码位置有效。VeroScript 的常见作用域包括模块作用域、命名空间作用域、函数作用域、代码块作用域和成员作用域。

## 代码块作用域

在函数或控制流语句中使用花括号可以形成代码块。局部变量从声明点开始有效，到所在代码块结束：

```VeroScript
int main()
{
	int outer = 1;

	if (outer > 0)
	{
		int inner = 2;
		print(outer);
		print(inner);
	}

	// inner is out of scope here.
	print(outer);
}
```

内部代码块可以访问外层代码块中已经声明的名称。外层代码块不能访问内部代码块的局部变量。

## 函数作用域

函数参数和函数体中的局部变量属于函数作用域。不同函数可以使用相同的局部变量名，它们表示不同的声明：

```VeroScript
void first()
{
	int value = 1;
	print(value);
}

void second()
{
	int value = 2;
	print(value);
}
```

## 模块作用域

在源文件顶层声明的变量、函数和类型属于当前模块。未导出的声明通常只供模块内部使用；使用 `export` 导出的声明可以按照模块导入规则供其他模块使用。

## 命名空间作用域

命名空间可以把一组声明放入独立的名称范围：

```VeroScript
namespace math
{
	int add(int left, int right)
	{
		return left + right;
	}
}

int main()
{
	print(math::add(1, 2));
}
```

命名空间成员可以使用 `.` 或 `::` 访问。命名空间不能替代模块；模块控制文件和包的导入关系，命名空间控制声明名称的组织方式。

### 全局作用域限定

前缀 `::` 会强制从全局作用域查找，不使用当前命名空间、本地别名或 `using namespace` 的回退结果。需要明确引用根级声明时使用 `::Name`。

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

它还可以限定根级类型或命名空间，`using namespace ::Name` 同样有效。

## 成员作用域

结构体、类和接口中的字段及方法属于类型的成员作用域。成员访问受到 `public`、`protected` 和 `private` 等访问说明符限制：

```VeroScript
class Counter
{
private:
	int value = 0;

public:
	int get()
	{
		return value;
	}
};
```

成员函数可以通过 `this` 访问当前对象。静态成员不依赖某个对象实例，访问方式和普通成员不同。

## 名称隐藏

内层作用域可以声明与外层相同的名称。此时内层名称会隐藏外层名称；为了保持代码清晰，应避免不必要的同名声明。


## 嵌套类型与局部类型

类型体内可以声明嵌套 class、struct、interface、enum 和类型别名。通过 `Outer::Inner` 使用；每段泛型分别提供自己的实参，例如 `Outer<int>::Inner<string>`。公开子类型可以通过已导出的外层类型使用；私有／受保护路径和公开别名继续执行普通访问检查。

可执行块内可以声明局部类型，名称从声明处至块结束可见，内层块可以遮蔽。局部类型可以使用可见类型、别名、枚举常量和泛型参数，不捕获外层变量或外层 `this`；访问实例成员时显式传入对象。静态成员按具体类型和执行实例保存，不在每次经过声明时重新初始化。

```VeroScript
class Outer<T>
{
public:
	struct Inner<U>
	{
		T key;
		U value;
	};
};

void main()
{
	Outer<int>::Inner<string> item = Outer<int>::Inner<string>();
	item.key = 7;
	item.value = "saved";
	struct Result
	{
		int count = 1;
	};
	Result result = Result();
	print("{} {} {}", item.key, item.value, result.count);
}
```

非局部嵌套方法支持 `T Outer<T>::Inner<U>::read() { ... }`，可以在导入声明的另一源码文件中提供匹配定义。局部类型的方法在类型内或处理器生成的正文中定义。不支持独立的不完整类型前置声明、重新打开类型本体或从别的文件定义局部类型方法。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/scope.md) · [fr-FR](../../fr-FR/language-basics/scope.md) · [ja-JP](../../ja-JP/language-basics/scope.md) · [ko-KR](../../ko-KR/language-basics/scope.md) · [zh-CN](scope.md) · [zh-TW](../../zh-TW/language-basics/scope.md)
