# 命名空間

命名空間用於組織名稱，避免不同功能之間發生名稱衝突。它可以包含變數、函式、結構體、類別、介面、枚舉和其他類型宣告。

## 宣告和限定名稱

使用 `namespace` 宣告命名空間，使用 `::` 存取其中的成員：

```VeroScript
namespace Util
{
	int Seed = 10;

	int next_seed()
	{
		Seed = Seed + 1;
		return Seed;
	}
}

int current = Util::Seed;
int next = Util::next_seed();
```

限定名稱明確表示成員所屬的命名空間。對於型別成員、枚舉成員和靜態成員，也建議使用 `::`：

```VeroScript
enum class State
{
	Ready,
	Running
};

State state = State::Running;
```

## 全域限定

開頭的 `::` 會從全域/根作用域開始名稱查找：

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

`::GlobalValue` 不會回退到目前命名空間、本地型別別名或 `using namespace`。它也可以限定根層級型別或命名空間，`using namespace ::Name` 同樣有效。

## 匿名命名空間

`namespace { ... }` 用於宣告匿名命名空間。在 VeroScript 中，它是透明的宣告分組：不會建立合成命名空間，也不會提供 C++ 式的翻譯單元內部連結。成員仍然屬於外層命名空間。

在全域作用域中，可以不增加限定地使用匿名命名空間的成員：

```VeroScript
namespace
{
	int transparent_helper()
	{
		return 40;
	}
}

int main()
{
	print(transparent_helper() + 2);
	return 0;
}
```

在具名命名空間內部也遵循相同規則，成員會保留外層命名空間限定：

```VeroScript
namespace Outer
{
	namespace
	{
		int nested_helper()
		{
			return 9;
		}
	}
}

int main()
{
	print(Outer::nested_helper());
	return 0;
}
```

匿名命名空間不會改變匯出規則或模組可見性。需要供其他模組使用的宣告應分別加上 `export`；當透明分組有助於組織原始碼時，可以使用匿名命名空間。

## 標準命名空間

內建容器、通用值類型和公開執行階段函式建議使用保留的 `std` 命名空間：

```VeroScript
import "string.vs";

std::array<int> values = {1, 2, 3};
std::optional<int> selected = std::optional<int>(values[0]);
std::print(std::format("selected: {}", selected.get()));
```

原有的 `array`、`optional`、`print`、`format` 等全域寫入方法繼續作為相容別名。
`std::` 在編譯期解析，不會增加 VM 或 JIT 執行階段分派。 `std::sqrt` 等原始碼標準函式庫
函式仍需正常匯入對應模組，限定名稱不會繞過 `import`/`export`。全域 `std`
命名空間為語言保留，腳本不能向其中添加宣告。

命名空間用於名稱組織，不會自動改變成員的匯出狀態。模組外部是否能夠存取某個宣告，仍由模組的 `export` 規則決定。

完整的型別和函式別名表請見[`std` 標準命名空間](standard-namespace.md)。

## 使用命名空間

`using namespace` 可以把命名空間中的直接成員引入目前包含作用域：

```VeroScript
namespace MathTools
{
	int base = 2;
}

using namespace MathTools;

int value = base + 3;
```

使用指令只影響它所在的包含作用域和後續程式碼，不會重新匯出名稱，也不會影響同級的其他命名空間。名稱可能衝突時，應保留完整限定：

```VeroScript
int value = MathTools::base + 3;
```

命名空間適合表達穩定的功能邊界；當目標是從另一個原始檔案引入宣告時，應使用模組的 `import`。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/namespaces.md) · [fr-FR](../../fr-FR/language-basics/namespaces.md) · [ja-JP](../../ja-JP/language-basics/namespaces.md) · [ko-KR](../../ko-KR/language-basics/namespaces.md) · [zh-CN](../../zh-CN/language-basics/namespaces.md) · [zh-TW](namespaces.md)
