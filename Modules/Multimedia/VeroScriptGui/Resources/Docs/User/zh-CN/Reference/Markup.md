# VSWML 与 VSWCSS

> 本页介绍 `FMarkupProgram` 原生加载 API。普通 VS 导入、注解 Schema、Builder、自定义属性／事件和类型化工厂见[框架指南](Framework.md)。下文原生适配器的限制只适用于该加载 API。

使用 `VeroScriptGuiMarkup.h` 中的公开 API，将标记加载为带类型化属性及路由事件的 `FWidget` 树，再交给已有上下文。支持文本、字体、图片、自定义绘制和基础控件。原生加载器默认只登记 `Widget`；使用带 Schema 的 VS 编译路径可获得内置控件及其类型化属性／事件，也可显式提供原生类型工厂。标记格式不要求特定的布局或图形后端。

## 加载与依赖

```cpp
#include "VeroScriptGuiMarkup.h"
#include <utility>

bool LoadMarkupUi(const std::string& Entry,
				  std::shared_ptr<VeroScript::Gui::FWidget>& OutRoot,
				  std::string& Error)
{
	using namespace VeroScript;
	Gui::FMarkupSource Source;
	Gui::FMarkupDiagnostic Diagnostic;
	if (!Gui::ReadMarkupFile({}, Entry, Source, Error))
	{
		return false;
	}
	Gui::FMarkupCompileOptions Options;
	Options.Read = Gui::ReadMarkupFile;
	auto Program = Gui::FMarkupProgram::Compile(Source, Options, Diagnostic);
	if (!Program)
	{
		Error = Diagnostic.Format();
		return false;
	}
	auto Root = Program->Create({}, Diagnostic);
	if (!Root)
	{
		Error = Diagnostic.Format();
		return false;
	}
	OutRoot = std::move(Root);
	Error.clear();
	return true;
}
```

用 `SetRoot` 将返回的根节点挂接到已有 `FContext`，并在 `Update` 成功后绘制。加载函数本身不创建窗口、设备或后端。

入口路径由宿主提供；文件内部的 `src` 相对引用以声明文件为基准，不以工作目录为基准。也可提供自己的 `FMarkupReader`，使用模块／资源提供者的完整身份；编译器不解析不透明资源身份。不可变 `Program` 持有源文件及直接读取的依赖快照，`GetDependencies` 可供宿主失效检查。VS 源码的传递导入仍由 Core 的模块环境处理。

文件路径字符串使用 UTF-8；MSVC 源码或生成宏包含非 ASCII 路径时，应以 `/utf-8` 编译这些源码。

编译不创建窗口或 GPU。每次 `Create` 建立独立的控件和脚本状态；已创建实例保留其资源和状态，不因 Program 释放而失效。重新读取／编译资源由宿主显式执行，本阶段没有自动热更新或状态迁移。

## ML 规则

- UTF-8、单根、大小写敏感的 XML 风格语法；支持注释、CDATA、五种 XML 命名实体及数字实体。属性必须引号包裹。`bind:` 和 `on:` 内置，不声明 `xmlns`。
- 根元素可以是已登记的任意控件类型。普通根表示实例入口；`component` 表示可重复实例化的模板。自闭合与成对标签等价，所有 Widget 都可有多个子节点。
- 根下面可有多个 `import`／`style` 和至多一个 `script`。`import src="...vswml"` 必须导入组件；`...vs` 导入脚本。`style` 可用 `src` 或内联内容，不能同时使用。嵌套控件内的这些指令会报错。
- 属性原名及其 Mark 均可用，例如 `Width`／`width`、`HitTest`／`hit-test`；不能同时给同一属性两种来源。`id` 和 `class` 在 ML 中只使用小写，值保持大小写。重复 ID 会报错。
- 普通属性是固定值；`bind:gap="ReadGap"` 或 `bind:gap="{ Count + 1 }"` 安装只读 getter。`on:pointer-down="Increment"` 或 `on:pointer-down="{ Count = Count + 1; }"` 安装事件处理器。花括号内为正常 VS 表达式／语句，不另造求值语言。
- 基础属性提供 getter 入口；具体控件由编译器使用 GuiProperty Schema 区分普通值与可绑定的 Attribute 属性。未接入的属性、控件和事件明确报错，原始文本节点也会报错，不自动生成标签控件。

```xml
<Widget id="root">
    <import src="card.vswml" />
    <style src="page.vswcss" />
    <preview-card id="first"><Widget Height="20px" /></preview-card>
    <PreviewCard id="second" />
</Widget>
```

```xml
<!-- card.vswml -->
<component type="Demo::PreviewCard" mark="preview-card" base="Widget">
	<template><Widget id="content" Height="40px" /></template>
	<style>:root { padding: 8px; } #content { background-color: #4080c0; }</style>
</component>
```

`type` 定义完整 VS 类型名，短名和 `mark` 都可作为标签。`base` 可指定已导入的 ML 组件或已登记的原生工厂；模板和样式按基类到派生类展开。禁止递归模板／继承。当前是文件前端的组件组合与静态模板继承，尚不是任意 VS 类与 ML 类之间的完整导出／继承协议。

模板内部有独立 ID／样式／脚本作用域；页面只能查到组件根。`Find(componentRoot, "content")` 查询其内部。组件使用处追加的子节点仍属于声明页面，组件模板样式不会穿透到它们。原生工厂已有的内部子树也使用私有作用域。程序创建后的任意原生 `AddChild` 不会自动登记为某个 ML 作用域成员。

## 样式

```css
:root { --space: 8px; layout: vertical; padding: 12px; gap: var(--space); }
preview-card { height: 80px; background-color: #203040; }
#first { padding: 10px 12px; }
Widget > .accent { width: 50%; }
preview-card:hover { background-color: #4080c0; }
```

支持类型原名／Mark、`*`、`.class`、`#id`、复合选择器、后代与 `>` 子代、逗号选择器组；状态为 `:root`、`:hover`、`:active`、`:focus`、`:focus-within`、`:disabled`、`:checked`、`:read-only`。状态在宿主分发输入后、下一次 `Update` 中更新。Platform 的输入重置清理 GUI 状态；宿主应转发规范事件，包括取消、失焦与重置。

优先级为默认 → 主题 → 模板样式 → 使用处样式 → 局部固定值／getter；同层按 ID、class／状态、类型的特异度，再按源顺序。作用域优先于跨作用域的选择器特异度。`FMarkupProgram::SetTheme` 设置／移除实例所属 ML 运行时的主题；`SetClasses` 和 `SetId` 在后续更新时影响选择器。只有标为样式属性的基础属性允许放入 CSS。

| 属性原名 | CSS Mark | 允许 CSS |
| --- | --- | --- |
| Width、Height、Padding、Gap | width、height、padding、gap | 是 |
| Layout、Visibility、Opacity、Background | layout、visibility、opacity、background-color | 是 |
| HitTest、Enabled、Focusable | hit-test、enabled、focusable | 否，仅 ML／局部配置 |

长度支持无单位数、`px`／`dp`、百分比和 `auto`，具体合法性按属性检查。Padding 支持一到四个非负像素值。背景支持 `#rgb`／`#rgba`／`#rrggbb`／`#rrggbbaa`、`rgb`／`rgba` 及基础命名色；文件中的 sRGB 转换为 Graphics 的线性色。数值解析不改变宿主 locale。

`--name` 变量继承其父节点已经计算的值，支持 `var(--name, fallback)` 和嵌套 fallback；未定义、循环、类型不符报出源文件位置。变量继承限于同一个 ML 运行时；分别创建后再挂载的树不会隐式共享变量。字体与文本属性按下文规则继承；其他普通属性不自动按 HTML 规则继承。暂不接受 `!important`、兄弟／属性选择器、`@` 规则、CSS 动画或任意原生 RCSS；不支持的语法报错。

普通赋值和 getter 都是局部来源。`Set` 替换已有绑定；`Clear` 清除局部来源，恢复当前样式／默认值，不恢复旧 getter。getter 返回错误使更新失败，不静默退回默认值。

## 可选的真实 VS 脚本适配器

配置 `-DVEROSCRIPT_GUI_SCRIPTING=ON`，链接 `VeroScript::GuiScript`。它使用匹配版本的 `VeroScriptCpp`／Core；原生静态 ML／CSS 不需要这些依赖。独立构建通过 `CMAKE_PREFIX_PATH` 查找 C++ Runtime SDK；嵌入同一构建时复用已有目标。

```cpp
#include "VeroScriptGuiScript.h"
Gui::FMarkupScriptOptions ScriptOptions;
// Configure the application's standard library/module environment as for normal VS execution.
ScriptOptions.Engine.StdlibSearchPaths.push_back("/application/stdlib");
auto Factory = Gui::MakeMarkupScriptFactory(ScriptOptions);
auto Root = Program->Create(Factory, Diagnostic);
```

```xml
<Widget>
    <script><![CDATA[
    public:
        int64 Count = 0;
        @GuiMethod(Mark = "increment")
        void Increment()
        {
            Count = Count + 1;
        }
    ]]></script>
    <Widget id="bar" Height="32px"
            bind:width="{ VS::Gui::Length::Percent(20 + cast&lt;double&gt;(Count % 8) * 10) }"
            on:pointer-down="increment" />
</Widget>
```

内联 script 内容是生成类型的成员声明；页面创建独立状态类，组件创建继承 `VS::Gui::Widget` 的状态类并关联其既有原生根。整树建立后构造脚本状态，然后安装 getter／事件；构造函数可查询已建立的 `elements`，但不得假设已完成挂载或首次布局。每次 Update 重新读取绑定；依赖、缓存和只读限制仍使用 Gui 属性系统。

普通页面还可用 `<script src="logic.vs" type="App::Logic" />`，生成的 `context` 是该类型的实例。未限定的公开成员解析到 context，局部变量保持正常遮蔽；不突破 VS 的访问权限。无 script 时可用不依赖状态的表达式，也可在 C++ Create 的第三个参数传入 `IMarkupScriptContext`；两种上下文不能并存。组件使用自身脚本，不接受外部页面 context。

原生工厂必须返回在当前所有者线程新建的控件，所有子节点同样如此；已初始化或已归属 ML 实例的节点会被拒绝。绑定表达式／代码块保留 `self`、`context`、`elements`、`sender`、`event`、`arg0`、`arg1`；局部变量、lambda 参数、foreach／catch 变量及局部类型别名均不能重新声明这些名称。

内置名称包括 `self`、`context`、`elements`；事件另有 `sender`、`event`，以及 `arg0`／`arg1`。内联代码的 context 指当前状态；组件内 this 具有根控件的基础 GetX／SetX／ClearX 方法。`elements.Find(id)` 可返回空，`Require(id)` 缺失时报错；返回弱持有的基础 Widget 视图。具体控件的属性方法和 Builder 由带 Schema 的 VS 编译链路生成。

命名 getter／事件处理方法必须标 `@GuiMethod`；原名及可选 `Mark` 均可用。GUI 注解名及其选项名忽略 ASCII 大小写；Core 注解名默认仍区分大小写。反射 `find_arguments(name)` 始终按已保存的参数名精确匹配，Mark 值也区分大小写。事件支持无参数、仅 `VS::Gui::Event`、仅 Widget sender、或 sender 加 event；不匹配／歧义会诊断。路由事件返回 void；自定义 GuiEvent 委托通过编译链路保留声明的参数与返回类型。

Event 提供 Target／CurrentTarget、坐标、增量、Pressure、Type／Key／ScanCode／Button／Modifiers／Buttons、PointerId／DeviceId／WindowId／TimestampNanoseconds、Text／SelectionStart／SelectionLength、Repeat／Synthetic；整数编号遵循 Platform 公共契约。`StopPropagation`、`StopImmediatePropagation`、`PreventDefault` 只在当前同步派发期间有效。保存事件只保留输入快照及弱控件引用，不延长原生事件栈帧生命周期。

标准库可正常使用：例如导入 `attribute.vs`，用 `{ Value.get() }` 读取 `std::attribute<double>`；事件中 `Value.set(...)` 后下一次更新反映变化。本地化／格式化使用应用配置的 Core 服务与正常 VS 调用；Text 和可编辑控件均已提供；不把 ML 字符串隐式解释成本地化 Key，应将文本绑定到正常的 Core 本地化／格式化调用。

ML import 由宿主 reader 解析，ReadMarkupFile 只处理相对文件路径。通过 ML 导入标准库时，提供能够解析标准库的 reader，或在外部 VS 文件内使用正常 Core import；Core 的脚本搜索设置不会隐式改变 ML reader。

传入的 Engine 配置遵守普通嵌入接口的资源寿命约定。已有 ModuleEnvironment 的绑定目录会和 Gui 目录合并，保留宿主身份，不改写已发布的环境；命名冲突报错。所有编译、实例化、调用和销毁遵循所属线程；若最后的脚本状态引用在其他线程释放，宿主关闭前须在创建线程调用 `CollectMarkupScripts`。

## 支持范围与限制

GuiWidget／GuiProperty／GuiEvent Schema、Builder、烘焙产物、普通 VS 导入 ML 和 GUI CLI 预览均已提供。已提供基础、集合和导航控件，以及模态对话框。可选编辑器配套功能由独立指南说明。启用 GUI 的 CLI 注册 gui-check／gui-compile／gui-preview；完整命令和类型化工厂见[框架指南](Framework.md)。未知功能不降级解释为 HTML／RML。

可使用[计数器标记](../../../../Examples/Markup/counter.vswml)、[组件标记](../../../../Examples/Markup/card.vswml)和[共享样式](../../../../Examples/Markup/layout.vswcss)作为输入。这些示例随 Resources 集成文档分发，不应假定功能 SDK 归档内含有示例目录。

## 文本、字体、图片与绘制

`Text` 是字面 UTF-8 文本，不作为 RML 解释。使用 `Text="..."` 或字符串 attribute 绑定；暂不支持 ML 正文文本或混合行内标记。字体族、字号、字重、字体样式、文本颜色／对齐、空白、溢出、省略、行高、字距、方向与语言默认从父级继承。`Direction` 指定段落基础方向，可为 auto/ltr/rtl；auto 使用每段的首个强方向字符，无强方向字符时使用 LTR。`Language` 只影响塑形，不覆盖 auto。RmlUi 对混合方向运行段排版，文本值仍保存逻辑 UTF-8 顺序，选区仍使用逻辑码点。ML 的 `Direction="auto" Language="ar"` 与 CSS 的 `direction: auto; language: ar;` 使用相同属性；left/right 对齐保持物理方向含义。

参阅[GUI 框架约定 — 文本、字体、图片与绘制](Framework.md#image-resources)。

默认内容布局依次放置内置图片、文本和子控件；修改属性或重建控件树都保持这一顺序。

Image、BackgroundImage、MaskImage 的 Getter 对直接值和绑定值统一返回已解析的资源标识／URL，转交给其他模块时不会改变资源来源。

参阅[GUI 框架约定 — 文本、字体、图片与绘制](Framework.md#compositor-properties)。

[Framework](Framework.md)

| 属性 | CSS 名称 | 可用于样式 |
| --- | --- | --- |
| Text, Image | text, image | 否 |
| FontFamily, FontSize, FontWeight, FontStyle | font-family, font-size, font-weight, font-style | 是 |
| TextColor, TextAlign, WhiteSpace, TextOverflow | color, text-align, white-space, text-overflow | 是 |
| LineHeight, LetterSpacing, Direction, Language | line-height, letter-spacing, direction, language | 是 |
| CornerRadius, ClipContent | border-radius, clip-content | 是 |
| BorderWidth, BorderColor | border-width, border-color | 是 |
| Cursor | cursor | 是 |
| BackgroundImage, MaskImage, BoxShadow | background-image, mask-image, box-shadow | 是 |
| Transform, Filter, BackdropFilter | transform, filter, backdrop-filter | 是 |

参阅[GUI 框架约定 — 文本、字体、图片与绘制](Framework.md#native-resource-services)。

Cursor：`VS::Gui::Cursor`；ML 使用 `Cursor="pointer"`，CSS 使用 `cursor: pointer`。该属性可继承并支持 Attribute 绑定。`Auto` 采用控件默认行为，`None` 隐藏光标。指针服务与窗口边界通知见 [Framework](Framework.md)。

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/Markup.md) · [fr-FR](../../fr-FR/Reference/Markup.md) · [ja-JP](../../ja-JP/Reference/Markup.md) · [ko-KR](../../ko-KR/Reference/Markup.md) · [zh-CN](Markup.md) · [zh-TW](../../zh-TW/Reference/Markup.md)
