# GUI 框架约定

## 范围

已接入原生和脚本控件树、声明作用域、Schema 与注解生成、ML/CSS 编译、类型化工厂、绑定和事件、Context/Application、主题、基础展示动画、每帧 i18n 快照以及离线应用烘焙。已提供基础、集合和导航控件，以及模态对话框。可选编辑器配套功能由独立指南说明。

## 脚本编译与控件

编译前注册 GUI 原生绑定并调用 ConfigureGuiScriptEngine；框架库由配置隐式导入。原生 API 使用 `VeroScript::Gui`，脚本使用 `VS::Gui`。

支持 GUI 的 CLI 会在普通编译或 `--check` 需要 GUI 功能时自动配置脚本框架依赖，现有 GUI 程序无需新增开关。显式 `gui-*` 任务和原生 `ConfigureGuiScriptEngine` 仍隐式导入框架。

普通变量、字段或参数仅仅命名为 `Gui` 不会导入框架。命名空间引用、GUI 注解，以及宏和无法检查的导入仍按原有规则处理。

```cpp
@GuiWidget(Mark="frame")
export class Frame : public VS::Gui::Widget
{
public:
	Frame()
	{
		part = VS::Gui::Widget::Create();
		AddInternalChild(part);
	}
	@GuiMethod(Mark="advance")
	void Advance()
	{
		SetAmount(GetAmount() + 1);
	}
private:
	void OnAmountChanged()
	{
		part.SetGap(GetAmount());
	}

	@GuiProperty(Style, Mark="amount")
	std::attribute<double> Amount = std::attribute<double>(2);
	VS::Gui::Widget part;
};

void main()
{
	Frame frame = Frame::Create().Amount(4)[{
		VS::Gui::Widget::Create().Id("content")
	}];
}
```

GUI 为注解名注册 ASCII 不区分大小写的规则，其编译处理器识别参数名时也忽略 ASCII 大小写。Core 默认仍区分注解名大小写。反射的 `find_arguments(name)` 始终按保存的参数拼写精确查询，GUI 注解也不例外。Mark／Id 的值、ML 标签／属性和 CSS 标识仍区分大小写；声明原名和可选 Mark 都可使用。ML 的 id、class 保持小写。

Builder 保存独立配置并快照复制子数组；隐式转换时创建新控件、安装模板、应用调用者配置，再首次同步。无需最后调用 Build。构造函数可创建内部节点，自动 Getter 和变更钩子延后至配置完成。SetChildren 只替换外部内容。

每个声明控件类型有独立内部名称作用域；elements.Find／Require 在该作用域查询，不穿透嵌套组件。普通页面拥有独立的内部脚本状态，返回标签实际指定的根类型。 `elements.Find("part")` 未找到控件时返回 `nullptr`；`elements.Require("part")` 则断言查询成功。

## 编译式 ML 与 CSS

`page.vswml` 导入上面的 Frame 类型：

```xml
<Widget Layout="Vertical">
    <import src="frame.vs" />
    <style src="page.vswcss" />
    <script><![CDATA[
    public:
        double Counter = 3;
        @GuiMethod(Mark="read-amount")
        double ReadAmount() const { return Counter; }
        @GuiMethod void Increment() { Counter++; }
    ]]></script>
    <frame id="card" class="card"
           bind:amount="read-amount"
           on:pointer-down="Increment">
        <Widget id="extra" Height="20px" />
    </frame>
</Widget>
```

`page.vswcss`：

```css
:root { padding: 12px; gap: 8px; }
.card { amount: 5; }
.card:hover { background-color: #335577; }
```

绑定优先于样式中的 amount 值；标签中的子元素仍是普通外部内容。事件修改页面状态，后续 GUI 更新重新求值。`bind:`、`on:` 为内置前缀，无须声明 xmlns。普通属性只接收固定字面量；Attribute 属性还支持命名 Getter 或 `{ 表达式 }`。

命名处理方法使用 GuiMethod，自定义委托事件使用 GuiEvent。原生路由处理方法接受无参数、Event、兼容的 sender，或 sender 加 Event；自定义事件保持声明的返回和参数类型。内嵌事件块使用 `{ 语句 }`，未知属性及不支持的 CSS 会诊断。

## 属性、更新与生命周期

属性来源为类型默认、样式、局部值／绑定，以及独立的动画展示层。SetX 替换局部绑定；ClearX 移除局部配置并回退到当前样式／默认。optional Getter 无值时回退，异常使更新失败。

Getter 在安全同步轮次按修订缓存；轮次外读取绑定的已提交值。递归读取同一属性、在 Getter 中修改 GUI 会报错。自定义属性钩子成功后才提交通知缓存，失败可重试。

初始化按子到父执行且每个实例成功一次；挂载按父到子，卸载按子到父。挂载钩子的状态修改在布局前同步，布局钩子的修改在下一次 `Update` 同步。布局通知失败时，失败及尚未执行的通知保留到后续更新重试；已成功的通知不会在布局未变化时重复。布局回调可读取完整的当前几何，输入和渲染则需要更新成功。

Context.Update 接收单调递增的累计秒数。Post 可跨线程投递，已接收工作的执行与清理在所有者线程完成；每轮采用稳定批次，工作中新投递的工作留待后续更新。求值／生命周期中的结构编辑须通过 Post。

关闭开始后拒绝新增操作；后端清理失败可重试。最终引用在其他线程释放时延迟回收，应在宿主退出前于创建线程执行 Context／Application／适配器的回收入口。

Application 将提供者的清理异常转换为错误，并保留未完成释放的资源和事件泵租约。显式 Close 失败后可重试；最终引用释放时的清理失败，通过所属线程的 CollectRetired 重试。

## 主题、动画与本地化

Context.SetTheme 安装类型化 CSS，ClearTheme 移除。主题在每个声明作用域内作为最低样式层，包括没有局部样式表的作用域。局部样式和 Setter 保持优先；错误样式候选不发布半轮结果。

原生 Animate／StopAnimation／GetPresentation，以及脚本 AnimateX／StopXAnimation／GetPresentedX，支持 Width、Height、Padding、Gap、Opacity、Background。长度端点必须单位相同且非 Auto。AnimationOptions 提供 Duration、Delay、MaxStep、Easing、HoldEnd。动画从下一次采样开始，脱离上下文时暂停；结束后恢复当前基础值，或按 HoldEnd 保留终值。GetX 仍读取基础属性，不替换其 Getter。

Context.SetLocalization 接入核心 i18n context；GetLocalizationSnapshot 返回本帧开始时固定的一份不可变快照。语言和提供者变更在下帧生效。消息描述与带类型参数由核心保存，GUI 不另行解析 ICU，也不把普通字符串默认为本地化 Key。

## 工厂与资源

先导入资源，再加载工厂：

```cpp
import "page.vswml";
using namespace VS::Gui;

void main()
{
	auto factory = VS::Gui::LoadFactory<VS::Gui::Widget>("page.vswml");
	VS::Gui::Widget page = factory.Create();
}
```

工厂在创建前校验根类型。页面的无上下文、自动外部上下文、必须提供的上下文三种模式互斥；组件以自身为上下文。当前提供的业务对象按准确的静态类型装箱，传入派生对象时先上转型到声明类型。

宿主可通过 FGuiCompilationConfiguration 登记必须提供的业务对象以及值 codec。字面量／样式解析器是可见的普通 VS 函数，接受 string 并返回属性值类型；不可变配置参与编译身份。

文件路径相对所属 ML／CSS／VS 模块。核心资源身份完整保留 provider／module／group／path，通过 ReadResource 读取，不能当文件路径归一化；缺失输入也进入核心依赖账本。

`BakeGuiApplication` 生成保留 GUI 元数据的内部 Core 应用映像；直接消费该映像的原生宿主使用 `ReadPackage`、`RegisterGuiArtifactBindings` 与 Session 的 `LoadPackage`。`gui-compile` 命令进一步通过 Application 服务将映像封装为标准 ZIP `.vsapp` 容器，该容器使用 `FApplication`／Session 的 `LoadApplication` 加载。容器编译和加载要求启用 Application 支持；源码检查和预览不依赖此容器服务。

`CompileGuiSource`／`CompileGuiFile` 复用完整编译器，不执行用户初始化器、不创建窗口或设备。原生宿主直接加载映像时，在 Session 的 `LoadPackage` 前调用 `RegisterGuiArtifactBindings`。资源清单保留去源码／调试信息后的调用者相对查询；烘焙的脚本与样式无需源文件，外部资产仍需提供者。

旧 FMarkupProgram 原生加载 API 继续支持原生类型配置和 IMarkupScriptContext。导出 VS 组件类型、Builder 及自定义属性／事件 Schema 使用编译器路径。

## 后端、输入与坐标

Application 分别接收 Platform、Graphics、Gui 工厂；已有窗口的宿主直接驱动 Context，同一事件源只有一个泵送者。MakeRegisteredServiceFactories 可从匹配的核心原生提供者建立工厂；GUI 提供者返回 IBackendService。

RmlUi 提供者在后端或延迟清理仍存在时保留核心拥有的服务租约。加载宿主拥有唯一 Core；适配器不关闭借用的设备和目标。

Visibility 与 HitTest 分开管理。Hidden 保留布局并隐藏子树，Collapsed 输出空布局。命中使用已提交的绘制几何和顺序；路由固定路径与订阅快照，失效／断开的监听器跳过。事件控制方法只在分发期间有效，保留的事件只保存输入快照。

原生坐标采用 GUI 单位；SetWindowMetrics 按 density／DisplayScale 转换窗口输入，绘制按 DisplayScale。像素尺寸和显示缩放变化后更新度量，不能将像素密度直接当用户界面缩放。

Windows／Linux 验证不等于 macOS、Android、iOS 或浏览器认证；这些目标仍需各自的运行、绘制、字体／输入和部署验证。

## 命令与宿主接入

匹配且启用 GUI 的 Runtime 提供 `gui-check INPUT`、`gui-compile INPUT --output app.vsapp`、`gui-preview INPUT [--frames N] [-- SCRIPT_ARGS]`。检查与编译使用完整编译链但不执行初始化器、不创建窗口／GPU；预览才创建服务。`--frames` 仅用于 ML 自动生成的循环；普通 VS 入口由自身控制退出。

模块参数 `--platform-backend`、`--graphics-backend`、`--gui-backend` 位于命令或脚本名前，对普通 VS 应用同样生效。脚本选项显式设置优先于本次调用／环境配置；空值表示继承，`auto` 交由工厂选择可用提供者，`none` 禁用服务。禁用窗口、GUI 或启用绘制时所需的图形服务会报错。原生宿主未为该服务提供默认选择时，仍以 sdl3／sdl3／rmlui 作为常规回退。执行后端 `--backend` 继续用于 VM／JIT。

GUI 后端选项还必须早于会加载包环境的选项；包环境准备完成后，CLI 会拒绝再选择这些后端。

`CreateCommandLineService` 供宿主复用同一任务；其登记返回的 Owner 必须保留到借用的命令描述释放。宿主通过 SetEngineOptions／SetRuntimeServices 传入本次环境，RegisterBindings／ConfigureEngine 准备普通运行配置，UseArtifact 接入烘焙清单。不要将反向拥有本登记的环境永久保存到其服务状态。

`VeroScriptGuiToolsModule` 只转发到宿主提供的类型化工具服务，不链接另一份 Core；静态入口是 veroscript_gui_tools_query。带内置 GUI 命令的 CLI 已完成登记，无需再次装入此适配器。原生模块可供其他提供两项宿主服务的 CLI 复用。

Schema 明确记录直接委托事件与独立的原生路由定义、阶段和默认动作。Style 不代表可动画；六种基础展示映射登记插值身份，自定义属性默认不具备动画映射。首期不对自定义对象猜测插值方式。

GUI 後端选项还应位于会载入包环境的其他模块选项之前；宿主在发布环境前将其传给包提供者选择。检查／预览也接受已烘焙的 GUI .vsapp。转发适配器遇到 HasHostRegistration 时复用宿主登记。

共享源码示例：[main.vs](../../../../Examples/Framework/main.vs)、[page.vswml](../../../../Examples/Framework/page.vswml)、[page.vswcss](../../../../Examples/Framework/page.vswcss)。

## 文本、字体、图片与绘制

文本使用 FreeType 2.13.3 和 HarfBuzz 10.4.0。内嵌未修改的 Lato Latin 常规字体，名称为 `Vero Sans`；中文等文字需由应用加载对应字体及回退字体。更新控件树前调用 `Context.LoadFont(source, family)`；六参数重载还提供字重、斜体、回退和字体集合索引。字体数据保留至最后一个 RmlUi 上下文关闭。

`Text` 是字面 UTF-8 文本，不作为 RML 解释。使用 `Text="..."` 或字符串 attribute 绑定；暂不支持 ML 正文文本或混合行内标记。字体族、字号、字重、字体样式、文本颜色／对齐、空白、溢出、省略、行高、字距、方向与语言默认从父级继承。`Direction` 指定段落的基础方向，可为 auto/ltr/rtl；auto 按每段的首个强方向字符判断，无强方向字符时使用 LTR。`Language` 只影响语言相关塑形，不覆盖 auto 的方向判断。RmlUi 后端先解析 Unicode 双向运行段，再塑形并逐显示行重排；保存的文本仍为原始逻辑 UTF-8 顺序，left/right 对齐保持物理方向含义。

<a id="image-resources"></a>

`Image` 加载内容图像。`BackgroundImage` 接受 `url(...)` 及线性、径向、圆锥渐变和重复渐变。相对图像从其 VS／ML／CSS 源文件解析，延迟属性 getter 也保留此来源。原生宿主配置 `FResourceReader`，并可选配置 `FImageDecoder`；解码像素使用非预乘 sRGB RGBA8。支持格式取决于所选 [Image 服务](../../../../../../VeroScriptImage/Resources/Docs/User/zh-CN/index.md)，`image/stb` 的格式见 [Stb](../../../../../../Backend/VeroScriptStb/Resources/Docs/User/zh-CN/index.md)。SVG 与动画需要适用的宿主解码器／播放器。

默认内容布局依次放置内置图片、文本和子控件；修改属性或重建控件树都保持这一顺序。

Image、BackgroundImage、MaskImage 的 Getter 对直接值和绑定值统一返回已解析的资源标识／URL，转交给其他模块时不会改变资源来源。

<a id="compositor-properties"></a>

`CornerRadius`、`ClipContent`、`Transform`、`Filter`、`BackdropFilter`、`MaskImage` 和 `BoxShadow` 使用同一属性、绑定及样式路径。支持的合成操作与资源限制由所选后端决定；RmlUi 提供者见 [RmlUi 合成与资源](../../../../../../Backend/VeroScriptRmlUi/Resources/Docs/User/zh-CN/index.md#composition-resources)。

覆写 `OnPaint(PaintContext canvas)`，在自身背景之后、子控件之前，以内容区局部坐标绘制。VS 提供 FillRect、DrawLine、DrawImage、PushClip/PopClip、GetWidth/GetHeight；原生 FPaintContext 还支持索引网格和变换。裁剪使用固定的内容区坐标，画布变换只影响后续几何。画布仅在回调内有效，不可留存，也不能在绘制中修改 GUI 属性或树；裁剪必须成对结束。绘制命令不变时复用 GPU 网格。

便携资源可通过 FGuiAssetOptions 交给 BakeGuiApplication，或重复指定 `gui-compile main.vs --output app.vsapp --asset image.png --asset font.ttf`。清单路径相对命令工作目录解析，代码和 ML／CSS 内的引用仍相对所属模块；未列出的动态资源仍由宿主提供。SDK 安装包含 FreeType、HarfBuzz、SheenBidi／Unicode、stb 和 Lato 的声明。原生 GUI 后端契约为 0.1.1。

```cpp
@GuiWidget
export class CanvasView : public VS::Gui::Widget
{
protected:
	void OnPaint(VS::Gui::PaintContext canvas) override
	{
		canvas.FillRect(8, 8, 80, 24, VS::Gui::Color(0, 0.4, 1, 1));
		canvas.DrawLine(8, 40, 88, 40, 2, VS::Gui::Color(1, 1, 1, 1));
	}
};
// String attribute getters may call std::i18n using the frame's localization snapshot.
VS::Gui::Widget title = VS::Gui::Widget::Create().Text("Hello, VeroScript!").FontSize(24);
```

```xml
<Widget>
    <style>
        .title { font-size: 24px; color: #ffffff; }
        .surface { border-radius: 12px; clip-content: true;
                   background-image: linear-gradient(90deg, #173b70, #438bd0); }
    </style>
    <Widget class="title" Text="Hello, VeroScript!" />
    <Widget class="surface" Width="240px" Height="80px" />
    <Widget Image="assets/logo.png" />
</Widget>
```

<a id="native-resource-services"></a>

VeroScriptImage 与 VeroScriptFont 定义通用资源接口。VeroScriptStb 提供 `image/stb`；VeroScriptFontBackend 提供 `font/font-backend` 及独立的 `text-layout/font-backend` 服务。原生 GUI 宿主通过 `FBackendOptions.Images` / `Fonts` / `TextLayouts` 注入图片与字体。请选择支持所选 Font 和文本布局契约的宿主及配套 SDK，并安装所需服务、后端包和许可文件。

`ApplicationOptions.FontBackend` 和 `TextLayoutBackend` 独立选择服务。空值继承宿主／CLI 的选择（`--font-backend`、`--text-layout-backend`）；`auto` 交由工厂选择提供者，`none` 禁用该可选服务。原生宿主未为该服务提供默认选择时，以 `font-backend` 作为常规回退。原生宿主提供 `FApplicationFactories.Font`／`TextLayout` 或 `Fonts`／`TextLayouts` 实例。使用与 GUI `0.1.1`、Font `0.1.2`、text-layout `0.1.0` 兼容的提供者；上下文存续期间保留字体／布局服务，更换共享配置前关闭全部上下文。`FTextLayout::Create` 的首参传入所选布局服务。

## 基础控件

ProgressBar、Slider 和 ScrollBar 在计算 GetNormalizedValue 前，先将 GetActualValue 夹取到最新配置的边界；范围修改尚未同步时也遵循此规则。非法范围仍在同步时拒绝。

`VS::Gui` 提供 Text、Image、Border、Spacer、Separator、Stack、Overlay、Button、CheckBox、ToggleSwitch、ProgressBar、Slider、ScrollBar、ScrollView、TextBox、TextArea。全部保留子控件，使用相同的 Builder、Attribute、事件和 ML／CSS。Border 提供 BorderWidth／BorderColor；文本编辑使用 Value，继承的 Text 仍是展示内容。

Clicked、CheckedChanged、ValueChanged、Scrolled、TextChanged／Submitted 只报告用户操作。未绑定属性接受修改；已绑定属性保留 Getter，通过事件传递候选值，由业务更新模型。Setter 和普通刷新不发送用户修改事件；由 @GuiProperty 声明的 Attribute 属性提供 IsXBound() 查询。继承事件保留 CheckBox／TextBox 的 sender 类型。范围值按 Minimum／Maximum／Step 归一，Step=0 表示连续，GetActualValue 返回实际值；绑定来源不被覆盖。ProgressBar 只展示。

支持鼠标、触摸、Space／Enter、Tab／Shift+Tab，以及范围控件的方向键、Home／End、PageUp／PageDown。PreventDefault 阻止对应默认行为；样式增加 :checked／:read-only。ScrollView 支持滚轮、触摸拖动和键盘；ScrollX/Y 是请求偏移，GetActualScrollX/Y、GetScrollWidth/Height 是最近布局结果；裁剪、命中、绘制保持一致。ScrollTo 显式替换绑定；ScrollBar 可通过事件连接。

文本编辑支持码点选区、剪贴板、撤销／重做、只读、占位文字和 IME 预编辑／提交。Select 使用逻辑码点位置，结束位置不包含在选区内；一个逻辑选区可以对应多个视觉区段。箭头导航、指针命中和 IME 光标定位共用实际显示行的布局；Backspace／Delete 保持逻辑删除顺序。撤销、焦点切换和重建保留选区的活动端点及光标在双向边界的位置归属。TextChanged 只发送提交文字，TextBox 的 Enter 发送 Submitted。每份文字最多 1 MiB UTF-8；每个撤销／重做历史最多 128 份、8 MiB。Application 接入平台服务，嵌入宿主提供 FTextInputServices；剪贴板失败回滚编辑。真实输入法、可访问性、完整字素导航及其他平台仍需分别验证或实现。

后端需声明并实现滚动／编辑能力。GUI 契约 0.1.1 须一起重建，产品仍为 0.1.1。示例：[ML](../../../../Examples/Controls/main.vs)、[页面](../../../../Examples/Controls/page.vswml)、[样式](../../../../Examples/Controls/page.vswcss)、[代码](../../../../Examples/Controls/code.vs)。

Checked、Value、范围及步长、ReadOnly、滚动偏移属于数据或行为配置，通过代码或 ML 设置。样式配置外观及方向，状态选择器只读取状态。TextBox 会移除未绑定单行值的换行及制表符；GetActualValue 返回归一文字，保留已有绑定来源。

步长不整除区间时，最小值和最大值仍可到达。滑块／滚动条的触摸拖动优先于祖先滚动容器。Select 使用已提交文字的码点索引；未聚焦时保存选区，聚焦后恢复，合法选区会结束预编辑。程序替换文字或切换编辑器类型会清空撤销／重做历史，重建控件树不改变这些规则。完整控件库增加源码编译开销；发行可使用 gui-compile 烘焙，避免启动时进行源码编译。

ScrollWidth/Height 表示不含 Padding 的内容范围，减去内容视口尺寸后与后端实际滚动范围一致。宿主驱动脚本 GUI 时，每帧进入一次脚本会话，在该脚本入口中调用 Context.Update 或 Application.Tick/Run。FScriptServices.Invoke 是逐个宿主回调的回退接口，会增加会话进入开销。纯原生 Context 不经过这个脚本桥接。

```cpp
// Assign gui during application initialization.
VS::Gui::Context gui = nullptr;
void UpdateGui(double elapsedSeconds)
{
	gui.Update(elapsedSeconds);
}
```

```cpp
auto result = session.CallFunction("UpdateGui", {elapsedSeconds});
```

滚动请求超出当前内容范围时，滚轮、键盘和触摸操作从限制后的偏移开始计算；下一次布局前的连续事件仍会累积。移除正在拖选文字的鼠标或触摸设备会结束该拖选，移除其他设备不会取消当前按压。 内层容器无法继续滚动时，滚轮交给可滚动的祖先；零增量不产生修改事件。设备移除时，即使宿主文本输入服务报错或抛出异常，也会释放该设备的捕获并报告错误。

## 布局、单选、数值输入与浮层

`Grid` 按声明顺序使用等宽列，Columns 为 int64，范围 1–4096；Gap 同时控制行列间距。子项宽度由 Grid 决定，行高取该行最高子项，Collapsed 子项不占格。它提供等宽网格，不提供 CSS Grid 的轨道和跨格语法。`Wrap` 默认横向排列，在内容宽度内换行；设置 Layout=Vertical 并限制 Height 可纵向换列。两者复用 Widget 的内容区、Padding 和子控件归属。

`Splitter` 接受最多两个普通子控件；额外浮层不算面板。Value 表示扣除 DividerSize 后第一区域所占比例，默认 0.5；Minimum/Maximum 约束在 [0,1] 内，Step 默认 0.01，DividerSize 默认 6。Vertical=false 左右分割，true 上下分割。分割轴覆盖子项尺寸，另一轴填满内容区。分隔条支持鼠标／触摸捕获拖动和范围控件键盘操作，发出 ValueChanged(Splitter,double)；ReadOnly 禁止用户修改。

`RadioGroup.SelectedKey` 管理选中项。RadioButton 必须位于 RadioGroup 内，Key 非空且在最近的组内唯一；嵌套组互不影响。空或不匹配的 SelectedKey 表示没有选中项。RadioButton 提供 IsChecked()、Select()、Focus()、Selected(RadioButton)；组提供 SelectionChanged(RadioGroup,string)。鼠标、触摸、Space／Enter 选中，方向键在可用选项间循环切换焦点与选择。绑定 SelectedKey 保留 Getter 并报告候选值；程序配置不产生用户事件。

`NumberBox` 的 Value／Minimum／Maximum／Step 为 double，默认 0／0／100／1，另有 ReadOnly、Placeholder、Focus()、Select()、GetActualValue()、ValueChanged(NumberBox,double) 和 Submitted(NumberBox,double)。输入时保留 `-`、`1e` 等未完成文本；回车或转移焦点时提交有限十进制／指数数值，检查范围并按 Step 取值；无效文本恢复上次有效值。Up／Down 按 Step 调整（Step=0 时按 1），Escape 放弃草稿。解析与规范显示使用不受区域影响的小数点；本地化展示文字继续使用核心格式化服务。`PasswordBox` 继承 TextBox，复用选区、IME、Value 及事件签名，仅由后端掩码显示；GetValue／GetActualValue 仍返回真实文本。

`Popup` 是非模态浮层，锚定逻辑父控件下方；保留控件归属、绑定和事件路由，显示元素移至文档浮层，位于普通内容上方并脱离祖先裁剪。OffsetX／OffsetY 调整位置，按视口限制边界，必要时放到锚点上方。尺寸、内边距、边框、背景和子控件沿用普通样式。Open 默认 false；Escape 关闭最上层可用 Popup，DismissOnOutside 默认 true 并消耗关闭它的外部指针按下。OpenChanged(Popup,bool) 报告用户关闭，绑定 Open 保留来源。焦点由调用方显式设置；关闭时，若焦点在该浮层内，恢复此前仍可用的焦点。IME 组合期间先由 Escape 取消组合。Modal=true 时启用模态输入和焦点限制，Dialog 提供该默认配置。

`ToolTip` 复用定位机制，整体不可交互，在鼠标悬停逻辑父控件 Delay 秒后显示，默认 0.5 秒。AutoOpen 默认 true；改为 false 后可显式控制继承的 Open。移出父控件或重置鼠标状态后自动隐藏，其后代也不接收输入和焦点。IsActuallyOpen() 读取最近框架状态；可视性、命中规则和浮层开关共同生效。

这些类型复用现有注解、链式构建和 ML／CSS 编译链路。Columns、DividerSize、Vertical、OffsetX／OffsetY、Delay 支持样式；数值、Key、Open 和绑定策略使用代码或 ML 配置。GUI 原生契约更新为 **0.1.1**，实现这些控件的后端声明 bExtendedControls=true，提供者与使用者须一起重建。

可运行示例：[入口](../../../../Examples/AdditionalControls/main.vs)、[ML](../../../../Examples/AdditionalControls/page.vswml)、[CSS](../../../../Examples/AdditionalControls/page.vswcss)。

自带 Image／Text 内容同样占用等宽格子，位于子控件前。Splitter 默认高度为 120 个界面单位。

当容器小于 DividerSize 时，分隔条限制在可用分割范围内。RadioGroup 回调能够读取完整同步后的选择状态；回调改选其他项时，不再发出原请求的过期 Selected 事件。NumberBox 在窗口失焦或输入状态重置时也会提交草稿。若提交和宿主输入清理同时失败，保留原始提交错误；宿主恢复后由后续 Update 重试清理。数值最多接受一个前导正负号。

更换 FTextInputServices 时，若停止旧服务失败，则保留旧服务和逻辑焦点以供重试。数值提交回调可能关闭上下文或移除目标，因此焦点切换会在回调后重新检查上下文和目标有效性。

## 焦点可见性与回调错误

完成布局后，Tab／Shift+Tab 和 Focus() 会使用最近布局，将目标在祖先 ScrollView 中按最小位移滚动到可见位置；嵌套容器由内向外处理，超大控件不会在两侧反复跳动。浮层不滚动其逻辑祖先。仅在需要改变偏移时发送 Scrolled；绑定的滚动属性保留 Getter，由业务通过事件接受候选偏移。几何在下一次 Update 生效。鼠标／触摸按下带来的焦点不会滚动容器，避免目标在指针下移走。

直接 Activate／Focus 和输入派发使用相同的回调异常处理：标准异常与其他异常均转为 gui.callback_exception 错误，不越过返回错误的接口。回调开始前已提交的数值和业务副作用不会回滚；调用方应处理错误后再重试。

框架保存鼠标最近的窗口坐标，在布局完成后重新命中；悬停样式和自动 ToolTip 在后续 Update 刷新，不合成指针输入事件。取消、设备移除、输入重置、失焦及关闭会清理相应记录，避免鼠标静止时仍悬停在已移走的控件上。

## 鼠标光标与捕获

`Cursor` 是可继承、可用于样式的 `VS::Gui::Cursor` 属性。链式构建和 SetCursor 接受直接值或 `std::attribute<VS::Gui::Cursor>`；ML 可写 `Cursor="pointer"`，CSS 可写 `cursor: pointer`。ClearCursor() 恢复继承／样式来源；显式 Auto 重新采用控件默认行为。

Auto 为按钮、复选框、开关、单选按钮、滑块和滚动条选择手形，为文本／数值／密码输入选择文本光标，仅在 Splitter 分割条上选择对应方向的调整光标。普通区域使用箭头。禁用控件默认使用箭头，可通过 `:disabled { cursor: not-allowed; }` 显式改为禁止图标。内容子控件沿用交互祖先的自动行为；浮层不会取得其锚点按钮的自动光标。

枚举值：Auto、Default、Text、Wait、Crosshair、Progress、ResizeNWSE、ResizeNESW、ResizeEW、ResizeNS、Move、NotAllowed、Pointer、None。CSS 对应 auto、default、text、wait、crosshair、progress、nwse-resize、nesw-resize、ew-resize、ns-resize、move、not-allowed、pointer、none。None 隐藏光标。Showcase 图表演示了十字光标覆盖。

Application 按 Platform 公布的能力接通系统光标和窗口鼠标捕获。嵌入宿主可提供 FContext::SetPointerServices(FPointerServices, Error)；回调在所属线程执行，不允许修改 GUI 状态。没有这些可选服务时，布局及内部指针路由仍可使用。相同光标不重复提交，收到鼠标输入后才开始设置光标；触摸输入不申请鼠标捕获。

鼠标拖动移出窗口后保持拖动光标和窗口捕获。释放、取消、设备移除、失焦／生命周期重置及关闭会清理相应状态；布局和样式变化也会刷新静止鼠标的光标。Platform 的 MouseEntered／MouseLeft 是窗口级通知，GUI 通过根事件 mouse-entered／mouse-left 暴露；移出窗口清理悬停与 ToolTip，不取消拖动或键盘焦点。外部宿主也应转发这些通知。

服务错误和异常通过普通错误接口返回。申请捕获失败会回滚逻辑捕获；光标提交失败可由下一次 Update 重试。服务更换和 Close 保留尚未完成的清理以供重试。解绑恢复可见默认光标，并释放此上下文持有的捕获。GUI 契约为 **0.1.1**，提供者与消费者须一同重建；

捕获同步先于光标求值执行，光标出错不会阻止释放系统鼠标捕获。宿主只提供 SetMouseCapture 时，该服务不会额外求值 Cursor。公开指针操作会保持上下文存活至宿主回调返回，包括宿主在回调中释放其最后一个引用的情况。

原生窗口已经关闭时，Application 将停用文本输入、释放捕获及恢复默认可见光标视为已完成的清理；启用输入或设置其他光标仍会返回错误。

Application.Run 将 WaitMilliseconds 作为空闲帧的最长间隔（默认 16 毫秒），等待前扣除上一帧更新和绘制耗时；已有事件仍会提前唤醒。Tick 不增加等待，宿主负责调度。未配置的原生属性跳过脚本轮询，已配置的 attribute 仍按原有同步规则求值；直接赋值或 Clear 会解除对应绑定。

`Run(beforeUpdate)` 与 `Tick(beforeUpdate)` 在 GUI 同步前调用回调，通常位于输入派发后。根控件尚无布局时，输入派发前的预布局更新也会调用回调，因此首次单步可能调用两次。动画或模型更新可使用 Run 重载，沿用事件等待预算；Tick 仍是不等待的单步更新。异常正常传播，清理时仍会刷新根视图引用。

绑定 Getter（包括返回空 optional 的读取器）仍在每个必要同步轮次求值。直接 Setter 和 Clear 替换或移除局部绑定。变更通知仅在钩子成功后提交，失败的钩子可在后续更新中重试；自定义值类型保留正常的语言复制行为。

GUI 继承宿主执行配置。选择执行后端时，将 `--backend=vm` 或 `--jit-backend="/path/to/Jit" --backend=jit` 放在 `gui-preview` 之前。C++ 宿主设置 FEngineConfig.ExecutionBackend 和 JitBackendFactory（LoadJitBackend 的结果）。没有 JIT 工厂的 mixed、调试、保留状态的初始化和有界清理使用 VM。持久会话的普通函数可按宿主配置使用 JIT。加载源码前应检查 `VeroScriptSession::ApplyConfig` 的返回值并传递拒绝原因。

正式发行包为 `vs-gui`、`vs-rmlui`，产品版本均为 `0.1.1`。Gui 独立依赖 Platform／Graphics／Image／Font，RmlUi 独立依赖 Gui，后端仍按需选择。原生 SDK 使用 `find_package(VeroScriptGui)`；脚本 SDK 添加 `COMPONENTS Bindings`，并提供匹配的 Core／C++ Runtime SDK。`VeroScriptGuiFramework` 兼容入口保留。

## 脚本与宿主契约

属性和事件的已闭合值类型保留核心的类型及模板值参数，包括 bool、定宽整数和枚举。生成源码保留整数的准确类型与范围，转换内置容器、引用及函数签名中的嵌套类型，并保持定长数组的维度顺序。GuiWidget 要求在全局或命名空间中声明非静态的闭合类，不接受局部或嵌套控件类。GuiMethod 和属性变更钩子也必须是非模板实例方法，包括不能声明模板值参数。

CompileGuiSource/File 保留 FCompileOptions.ConstantEvaluationLimits，由核心执行 constexpr 求值。动态 attribute 继续在正常的 GUI 同步轮次采样。

Context.Post 在执行或丢弃前保留已投递的回调及其捕获值；捕获对象可能继续存活至延迟清理完成。Context.SetFocus 同步发送控件通知。

TextArea 保留逻辑 CR、LF、CRLF、NEL（U+0085）、行分隔符（U+2028）及段落分隔符（U+2029）；每个终止序列只显示一次硬换行，Backspace／Delete 将 CRLF 整体删除。码点选区与剪贴板仍统计、保留原始序列。单行控件过滤这些分隔符；Tab 保持原有过滤行为，占位文字的空白仅在显示时归一化，不改动文本值。

光标使用 leading／trailing 归属区分方向边界的视觉位置。水平导航按段落基础方向跨越软折行，从相应的视觉边缘进入下一行，包括整行只有反向运行段的情况；此策略不承诺与所有系统编辑器一致。完整字素导航与连字内部精确光标仍是独立能力。

宿主文本输入服务回调可以查询 GUI 状态；回调返回前，Context 和已挂载控件的修改、服务替换及 Close 会被拒绝。

核心公共 ABI 或字节码内部布局变化后，应使用匹配的 Core／C++ Runtime SDK 重建 GuiScript 和原生提供者，并重新生成相应布局的字节码包。产品版本同为 0.1.1 不能单独说明二进制兼容。

CLI 按需导入会先检查小型本地 `.vslib` 的宏接口。普通库声明及非 GUI 宏不需要额外导入框架；GUI 宏及无法检查的导入保留保守路径，避免重复引入库中已有的标准库声明。

## 通知式绑定与批量更新

`Observable<T>.Get()/Set()` 保存可通知的数据。`Set` 用 `==` 比较，值相等时不通知；修改可变对象内部内容后可调用继承的 `Notify()`。`ChangeSignal.Notify()` 也可代表由其他服务维护的输入。单个数据源使用 `AsBinding()`，组合计算使用 `Binding<T>(getter, inputs)`，依赖以显式的 signal 数组给出；不会自动追踪普通脚本字段。 重复调用 AsBinding 会通过弱缓存复用仍存活的绑定，不形成数据源与绑定之间的持有环。

通知 Getter 必须只读，并完整声明所有会变化的输入。结果按输入修订缓存，包含 optional 无值结果；多个控件共用同一个 Binding 时，计算结果也共用。`Binding<T>::FromAttribute(attribute, inputs)` 明确将已有 attribute 转入通知契约；未知依赖、时间和 weak 对象失效仍需要相应通知。直接传入的 `std::attribute<T>` 保留原有轮询和回退语义。

重复通知由原生脏队列合并，同一批次中每个受影响控件只唤醒一次。`BindingUpdates.Batch(work)` 支持嵌套，将 GUI 工作合并到下一次安全更新；数据自身立即改变，工作抛异常时不回滚已完成的赋值。批量操作中不能执行 Context.Update、Application.Tick 或 Application.Run。控件构建保留原有初始化和父子生命周期顺序。

队列在所有帧服务捕获快照后发布；现有控件同步流程读取受影响属性，计算结果相等时不产生变更钩子，也不额外标脏原生属性。帧快照输入需要在捕获后通知；无法观察的逐帧数据继续使用普通动态 attribute。程序更新不触发用户输入事件；SetX/ClearX 断开原订阅，IsXBound 保持 attribute 的绑定语义。Dispose 或最终释放时解绑，卸载保留配置。Getter 失败保留上次成功提交的值，并沿用现有生命周期的重试流程。FromAttribute 捕获传入的 attribute 配置；之后替换原 attribute 的配置不会替换此 Binding。 FromAttribute 保留回退层的轮询，确保默认 attribute 和原生样式可独立变化；传入 Getter 的结果仍按通知修订缓存。

VS、自动生成的 Builder，以及 .vswml 的 VS 代码区都可调用同一套 setter。现有 `bind:` 表达式保持轮询语义，.vswcss 继续提供样式层。通知和队列处理在 GUI 所属线程执行。

```cpp
auto amount = VS::Gui::Observable<double>(0.25);
VS::Gui::ProgressBar bar = VS::Gui::ProgressBar::Create().Value(amount.AsBinding());

// Setters need a mutable value capture; read-only getters do not.
VS::Gui::BindingUpdates::Batch([amount]() mutable -> void
{
	amount.Set(0.4);
	amount.Set(0.6);
});
// The next Context.Update/Application.Tick commits 0.6 once.

array<VS::Gui::ChangeSignal> inputs = {amount};
VS::Gui::Binding<string> caption = VS::Gui::Binding<string>([amount]() -> string
{
	return to_string(amount.Get());
}, inputs);
VS::Gui::Text label = VS::Gui::Text::Create().Text(caption);
```

## 集合、导航与对话框

宿主跨多次 VeroScriptSession::CallFunction 调用保留 GUI 全局状态时，应在初始化前调用 session.SetHotReloadEnabled(true)；当前 C++ Runtime 通过该持久会话状态共享 globals，否则各次入口使用新的模块全局状态。单次脚本入口内执行 Application.Run／Tick 已共享该入口状态。

`ListView` 接收 `ListItem`，`TreeView` 接收嵌套的 `TreeItem`。两者使用字符串 `SelectedKey` 进行单选，提供 GetActualSelectedKey()、FindItem(key)、Select(key)、Activate(key)，以及对应类型的 SelectionChanged／ItemActivated 事件。Key 省略时依次使用 Id 和顺序编号；可变数据应使用稳定且唯一的显式 Key。空或未匹配的模型不选中项目。Select 请求选中既有项目；Activate 还要求项目可交互。鼠标释放、Enter／Space 激活；方向键与 Home／End 导航。树的 Left／Right 折叠、展开或进入／离开分支。Expanded 支持 attribute，IsActuallyExpanded() 读取当前候选状态，ExpandedChanged(TreeItem,bool) 报告请求。折叠保留子控件及其原有 Visibility。

`TableView` 使用 TableColumn／TableRow。列提供 Key、正像素宽度 ColumnWidth 和 Sortable；每行拥有稳定 Key，以及与列数一致的普通控件单元格。代码可用 SetColumns(array<TableColumn>)、SetRows(array<array<string>>)、GetCellText／SetCellText、SortBy(columnKey,ascending)。排序稳定，按 UTF-8 文本序比较，保留行对象和选中项；不会猜测数值或区域排序规则。AutoSort 默认 true，响应表头点击；另有 ColumnClicked、Sorted、SelectionChanged、ItemActivated 事件。目前提供单选控件表格，数据源可选定高行虚拟化，不包含电子表格编辑。手写控件模式使用批量 SetChildren／SetRows，避免逐条插入时反复重建索引。

`ComboBox` 接收 ListItem，提供 SelectedKey、Open、Placeholder、DropDownHeight、Select、SetItems、GetList、RequestOpen 及选择／打开事件；显示 Text 由所选项的 Text 或 Placeholder 管理。`TabView` 接收带 Key／Title 的 TabItem，未绑定的初始选择采用首个可用页。Select 切换并发出 SelectionChanged(TabView,string)。页面保持挂载；未激活内容单独隐藏，不覆盖用户的 Visibility 或绑定。表头支持 Left／Right／Home／End。

`Menu` 包含 MenuItem，其子项构成子菜单。Command 标识命令，Checkable／Checked 支持勾选命令；项目和菜单的 Invoked 报告命令。子菜单复用下方／右侧浮层、键盘导航和 Escape；一次外部指针按下会关闭整个活动菜单链，并消耗该次按下。Invoke、OpenMenu、CloseMenu 使用相同交互路径。操作系统原生菜单和快捷键注册由宿主另行处理。

`Popup` 默认仍为非模态。Placement 使用 below／right／center；MatchAnchorWidth 可匹配锚点宽度，DismissOnEscape 可禁用 Escape 关闭。GetDismissReason() 返回 escape、outside，或显式请求时的空字符串。`Dialog` 默认 Modal=true、居中、DismissOnOutside=false。通过 Show()、Close(result) 在现有应用循环中使用，Closed(Dialog,string) 报告关闭请求及结果；限制背景指针／键盘焦点，约束 Tab 导航，并恢复仍有效的先前焦点，不启动嵌套循环。Open 绑定保留来源，候选状态在后续同步时服从模型。

所有控件继续使用既有 Builder、attribute、事件和 ML／CSS。Builder 使用 .OnClicked(handler)，实例使用 .AddClicked(handler) 及对应 AddXxx 方法。组合控件的 SetChildren 面向用户内容；GetChildren／父级查询仍返回真实元素树，GetContentChildren 返回用户内容容器的子项。结构变更应在 getter／更新钩子之外执行，或使用 Context.Post。参见[完整示例](../../../../Examples/CompositeControls/main.vs)、[ML](../../../../Examples/CompositeControls/page.vswml) 和 [CSS](../../../../Examples/CompositeControls/page.vswcss)。Grid 按设计保持等宽；需要独立列宽的表格使用 TableColumn.ColumnWidth。

`MenuItem.IsActuallyChecked()` 读取当前勾选候选状态；连续请求基于候选状态切换，下次同步恢复绑定的 Checked 模型。Dialog 的 Closed 使用 OpenChanged 回调之前保存的结果快照，回调中重新打开也不会覆盖该次关闭结果。

数据列表和表格支持自动定高虚拟化：`ListView.SetVirtualItems(labels,rowHeight=28,overscan=2)`、`TableView.SetVirtualRows(rows,rowHeight=28,overscan=2)` 只创建和复用可视区域附近的行。`SetVirtualItemText(index,text)`、`SetCellText(key,column,text)` 可修改离屏数据。自定义行使用 `SetVirtualSource(keys,rowHeight,factory,overscan=2)`；工厂接收 `(string key, CollectionItem recycled)`，返回该行或未挂载的新行，表格要求 TableRow。复用时必须重置旧键对应的状态。`RefreshVirtualItems()` 重新绑定可见行，`SetVirtualOrder(keys)` 用完整键序列调整顺序。内置虚拟表格保留稳定文本排序；自定义数据源自行响应 ColumnClicked 并提供顺序。

`GetItemCount()` 返回逻辑行数；`GetItems()`、`GetRealizedItemCount()`、`FindItem()` 只反映已实例化的行。选择保留逻辑键，`ScrollToItem(key)` 可显示离屏行；拥有焦点、按下状态或指针捕获的行暂不回收。行实例化在 Context.Update 的安全延后阶段执行；布局阶段才发现的变化在下次更新处理。行使用固定边框盒高度并裁剪内容，自定义行的内边距须能容纳在该高度内。键须非空且唯一，overscan 为 0..256，总高度上限为 16,777,216 逻辑像素。不能在工厂或 getter 中替换、排序数据源。工厂错误向上传播，后续更新可以重试；返回的行由视图负责释放。现有手写子控件、SetItems、SetRows 保留原生命周期；暂不包含变高行和树虚拟化。

首次布局前调用 ScrollToItem，会在实际视口可用后校正定位；随后显式设置的滚动位置优先。

## Context 后端默认值

使用标准应用工厂桥接的 `VS::Gui::Context("", width, height)` 时，构造参数中的显式后端优先，其次为本次调用选择，再次为应用／环境工厂默认值；两层均未提供选择时，GUI 后备为 `rmlui`。宿主后端查询回调返回空串表示该层未指定，并不禁用服务；显式 `auto` 或 `none` 不会被 `rmlui` 替换。环境配置中明确保存的空后端值仍按公共规则规范化为 `none`。自定义宿主 Context 工厂自行负责后端名称处理。

<a id="context-callback-reentrancy"></a>

## Context 回调限制

替换主题、帧服务、指针服务或文本输入服务时，不应在其触发的回调中修改 Context 配置；同一 Context 的递归配置会被拒绝。通过 `Post` 将后续工作安排到所有者线程。

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/Framework.md) · [fr-FR](../../fr-FR/Reference/Framework.md) · [ja-JP](../../ja-JP/Reference/Framework.md) · [ko-KR](../../ko-KR/Reference/Framework.md) · [zh-CN](Framework.md) · [zh-TW](../../zh-TW/Reference/Framework.md)
