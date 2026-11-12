# GUI 维护

`#include "module.vs"` 与 `export import "module.vs";` 使用相同的需求判定，别名本身不加载框架；二进制 include 仍检查公开宏。根构建启用 GUI 时，普通 Core 示例和包客户端回归使用仅供测试的 `VeroScriptCliCoreTests` 宿主。GUI 集成用例继续使用实际发行 CLI，单独验证宏和生成器的保守导入策略，不延长 Core 测试期限。

Gui 负责控件/属性/事件、作用域/Schema、ML/CSS 和生命周期。Platform/Graphics/Image/Font 是公开服务，RmlUi/Stb/FontBackend 为独立提供者。保留模块环境、代次根、所属线程清理、类型/值参数及编译身份。分别检查 JIT 派发与明确成本模型回退，仅因不支持而回退不能视为验收通过。

使用独立 CMake 或根 VEROSCRIPT_BUILD_GUI；VEROSCRIPT_GUI_SCRIPTING 开启编译/脚本集成。VEROSCRIPT_GUI_USE_VISUAL_SDK 复用已安装基础库，VEROSCRIPT_GUI_BUILD_RESOURCE_BACKENDS 控制 Stb/FontBackend。ABI/字节码变化后使用匹配 Core/C++ Runtime 重建。分别验证原生生命周期、脚本、Schema、ML、安装后消费及渲染/输入/字体/平台，静态检查不代替运行或 IDE 验收。

脚本宿主通过 `FEngineConfig::VmBackendFactory` 或 `VmBackendPath` 显式选择 VM；标记脚本使用 `FMarkupScriptOptions::Engine`。GUI 将宿主选择传给执行及编译处理器，并保留已有配置。官方字节码 VM 是独立提供者 `vs-bytecode`；链接或安装 Gui 不会替宿主选择后端。原生控件树和纯 ML/CSS 解析无需 VM。

脚本回归按独立场景注册，使用 `ctest --test-dir BUILD -R VeroScriptGuiScriptTests --parallel 4 --output-on-failure` 可覆盖全部场景。直接运行 `VeroScriptGuiScriptTests` 默认执行全部，也可传入一个场景名。场景拆分保留原有断言，测试时限不改变应用的运行期限。

`VeroScriptGuiMarkupTests` 覆盖语法、诊断、样式、状态、作用域、变量、主题及相对文件依赖；`VeroScriptGuiScriptTests` 运行真实 VS，覆盖内外部状态、组件脚本、属性依赖、std::attribute 与模块环境。RmlUi 绘制测试另覆盖 ML／CSS 生成树的布局、离屏像素及 hover 变色。这些是测试职责说明，不是当前候选的通过记录。

核心可能因成本、引用或 finally 语义将 getter、转发方法留在 VM 中。JitSynchronization 分别输出 NativeLeafExecutions／NativeCfgExecutions、派发、Linear／Bridge／VM trampoline 及回退，并在复用 Session 的连续调用中验证受支持的原生入口与逐次执行计数。ScalarNativeFastPathCalls 只统计宿主标量调用，不代表所有脚本原生执行；嵌套计数不能相加当成独立调用。详细诊断默认关闭；仅在独立诊断运行中设置 FJitPolicy.Diagnostics 并读取 GetLastJitDiagnostics()，吞吐测量时保持关闭。

`ApplicationOptions.FontBackend` 与 `TextLayoutBackend` 独立选择服务。字段为空时继承 `FApplicationFactories.DefaultBackend` 或宿主／CLI 选择（`--font-backend`、`--text-layout-backend`）；显式 `auto` 选择候选，`none` 禁用可选服务。仅当原生宿主没有为该服务提供默认选择时，`FApplication::Create` 才用 `font-backend` 作为常规后备。提供 `FApplicationFactories.Font`／`TextLayout` 或原生 `Fonts`／`TextLayouts` 实例。存活的 RmlUi 上下文共享不可变实例，注册工厂使用弱缓存；更换配置前关闭上下文。`FTextLayout::Create` 首参是选定布局服务，段落缓存包含其实例身份。按 GUI `0.1.1`、Font `0.1.2` 和 text-layout `0.1.0` 契约配套重建提供者与消费者；`ContractVersion` 引用 Gui 的 `Version`。静态审查不代表运行验收。

## 回归与生命周期细节

测试覆盖直接激活、数值失焦提交、嵌套滚动、Tab 循环及浮层焦点。

应核对实际 JIT 派发／回退统计，分别测量预热后的更新与绘制，排除编译和显示等待时间。

Context.Post 通过核心的程序代次根值保留脚本回调；执行或丢弃后，其捕获值由核心在安全点清理。Context.SetFocus 保留当前脚本调用器，以便同步发送控件通知。

[User](../../User/zh-CN/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/zh-CN/GENERAL.md)

## 源码导入策略

普通 CLI 配置保留原生绑定，按源码需求加载脚本框架。GUI 名称、注解及生成 ML 请求框架，注释与字符串本身不触发加载；以 .vs 结尾的源码导入使用同一检测。较小的本地 .vslib 导入通过已记录依赖检查公开宏，普通声明及可直接判定的无参字面量常量宏不请求框架。可直接判定为数值、字符或字符串字面量常量的无参宏与普通条件指令本身不请求框架；带参数、替换体包含名称的源码／宿主／导入库公开宏，以及令牌拼接、字符串化、未知指令，宿主宏搭配不透明隐式提供者，无法解析／带转义／逻辑／较大导入及自定义编译处理器仍保守保留完整导入。显式 gui-* 任务及 ConfigureGuiScriptEngine 仍使用隐式导入。维护时同步编译元数据和语义缓存身份；回归须覆盖无 GUI 源码，以及经导入或处理器引入 GUI 的源码。

固定属性同步在回调前记录输入字段版本，仅在回调成功且登记未被替换时提交。求值期间的变化在下一轮仍可观察，失败和重新登记不会提交过期快照。输入缓冲区重复使用，仅在切换来源时清空曾保留的默认配置。动态 getter 仍在每个同步轮次执行。

`BakeGuiApplication` 保持为供嵌入使用的内部 Core 映像接口。CLI 使用 `Application::BuildApplication` 和 `WriteArchive` 封装这些字节，不再直接将映像写为 `.vsapp`。应用准备钩子在包提供者可用后恢复 GUI 资源服务，通用 CLI 和启用 GUI 的 Runtime 共用该流程。验收同时覆盖 ZIP 结构、inspect／检查及移除源码后的迁移运行。未启用 Application 时仍可检查／预览 GUI 源码，编译或加载应用容器会明确报告缺少能力。

源码使用 `import compiler` 生成器时，Core 的编译扩展期限也覆盖所在 GUI 源码图的准备过程。较大的源码图应由宿主配置合适预算，例如 CLI 的 `--compiler-timeout 20000`，或原生宿主的 `FCompileOptions::ScriptCompileExtensions.TimeoutMilliseconds`。

输入身份和版本已验证未变时，复用回调前的字段快照，避免再次扫描。配置未变时可复用绑定源分类，不能跨同步轮次复用 getter 结果。原生回调中的确切数值框架控件可在 getter 之外，使用刚完成同步的属性配置原生状态；手动同步、派生控件和递归查询继续走普通 getter 路径。继承属性仍逐轮求值父级 getter，未变且已验证的值省去复制和重复校验。 原生属性派发按活动掩码分组检查，保持 getter 顺序并覆盖全部已启用的绑定。

原生数值、布尔和字符串属性在同步与查询时使用直接值快照和引用输出，避免临时 optional 对象。空结果保持回退行为，并在下一求值轮次继续读取。在已验证的原生作用域内，确切的框架数值控件可省去生成的空变化钩子；派生控件和已声明钩子继续进行虚调用。

固定的原生 attribute 配置不再要求逐轮轮询脚本。配置替换、原生状态修改和重新挂载仍会唤醒同步；绑定 getter 与可空 getter 持续轮询，空结果也不会停止求值。

各控件独占原生缓存引用对象，内部快照更新保持输入配置跟踪稳定；setter 替换配置时仍会唤醒同步。

确切的基础 Widget 类型也可暂停同步，未带注解的派生类型继续保留保守的自定义同步路径。

attribute 配置的分类只缓存到 Set／Reset 替换该配置为止。分类不求值 getter；绑定和可空 getter 仍在每个必要求值轮次执行。

替换时保留旧负载，直至新配置及其分类完整发布；析构重入可读取或再次替换新状态，随后不会被外层旧状态覆盖。

## 通知式绑定与批量更新

原生数据源图使用弱控件订阅并合并重复通知。控件持有订阅令牌和相应的属性读取器，令牌析构无需调用脚本终结方法。Bridge 的单调计数器为通知分配修订号，固定依赖集合的最大修订号可精确判断变化，无需哈希或逐次创建脚本数组。共用 Binding 只缓存成功读取的结果，包括 optional 无值；原有动态 attribute 的 epoch 行为保持不变。

IFrameBindingService 在所有 BeginFrame 完成后增加发布阶段，不改动原有 IFrameService 虚接口。发布时对控件去重，唤醒现有生命周期观察器，不逐属性调用脚本任务，也不强制布局失效。原同步链路读取脏属性、比较结果、保持生命周期顺序并重试失败 Getter。脚本所属观察器也支持宿主直接更新。原生批量深度防止宿主更新提交中间状态，脚本包装层会更早拒绝这类更新。普通 setter 和 ClearX 断开订阅；内部安装在释放旧捕获前发布替换配置。 FromAttribute 保留回退层的轮询，确保默认 attribute 和原生样式可独立变化；传入 Getter 的结果仍按通知修订缓存。

## 同步输出与绘制提交

原生同步仅复用已经解析的属性输出；Getter、继承及变更钩子仍先执行。数值变化、动画采样、停止或结束均更新该输出。通知发布保留有效配置快照。每个 Widget 持有一个稳定的缓存容器，各原生属性缓存在首次使用时创建；这些输出写入不会使输入配置快照失效。普通动态 attribute 和 FromAttribute 回退层保持原有求值及重试规则。

RmlUi 通过可选 IRenderSubmission2D 将同一渲染器的连续通道集中提交。切换其他渲染器的合成器前，先提交其需要读取的生产通道，再继续记录；失败和异常取消尚未结束的作用域。表面暂无交换链图像时，可取消尚未提交且仅写内部暂存层的工作；持久图层捕获仍须完成。SDL_GPU 及其模块提供者支持此能力，其他渲染器保持普通路径。每次 Render 仍执行 OnPaint。

脚本缓存失效标记先于原生通知推进，避免通知生效后的执行中断让共享结果被误判为有效；原生依赖修订仍用于判断实际变化。这一顺序不代表脚本中断具有事务回滚语义。

Observable.Set 在 finally 中发布通知，覆盖存储值写入后的执行中断。失败或中断的赋值可能保守地使绑定缓存失效；同值设置仍不发出通知。后续恢复仍遵守下述 Core RequiresReset 契约。

求值、初始化和构建的临时标记在保护作用域内修改；嵌套求值和批量更新恢复进入前的层级。取消订阅回调失败时保留重试能力，同时阻止回调递归取消自身。超限、超时或取消后的恢复仍依赖 Core 的有界清理；若 Core 要求重置，宿主应重新创建受影响的 GUI 脚本会话及其绑定上下文；GUI 的 finally 不提供业务状态的事务回滚。

原生属性按原有顺序分为九组同步，保留动态求值和异常恢复。控件弱回调进入脚本后再次检查释放状态；目标已释放时结束分派，避免重复进入宿主桥。

ML 初始化捕获的右侧按外层作用域改写，捕获别名只在闭包体内生效。属性源快照通过 Core 的解包接口读取引用或只读捕获存储中的实际属性对象，再比较身份和修订；普通对象字段保持直接读取，不额外保留捕获负载。回调仍使用 FValueRoot 保留并通过 InvokeCallable 调用，业务接口不变。

原生标量属性同步仅在未取到值时补查配置，避免有效值路径上的重复查询。缓存与 getter 接口保持原样。按顺序执行的同步组复用标量状态变量，属性值作用域保持独立。共享 Binding 先检查递归状态，再复用修订缓存；缓存命中时省去额外的刷新方法调用。

原生元素查找和样式存在性查询显式登记只读成员元数据，确保 Core 可通过 const 宿主句柄和按值捕获调用这些查询。修改操作保留原有元数据，GUI 业务接口不变。

## 原生产物维护

原生选项、公开布局或嵌入库发生变化后，须一并重建 GUI 提供者、宿主、脚本库及 SDK／包候选。相同的产品版本号不能证明实现兼容。

## 后端默认值与内部分派

实现辅助类型和脚本分派入口使用 `VS::Gui::Internal`，例如 `NativeAttributeCache<T>` 和 `NativeValueAttributeCache<T>`；这些属于框架内部实现，不作为业务 API。控件中以 `Vsgui` 开头的成员仍是框架生成和继承钩子的保留名称，用于避免与业务成员冲突。

源码导入检测区分命名空间／注解使用与仅命名为 `Gui` 的普通变量、字段、参数；修改检测时同步处理器配置身份。原生绑定、生成调用点及两个编辑器提取器须与 `VS::Gui::Internal` 限定名一致。`FContext` 在几何变化后记录 `bLayoutNotificationPending`，先发布完整几何再回调，仅在通知成功后清除标记。失败或尚未派发的通知保留待重试，成功通知无新变化时不重复。挂载钩子修改在布局前同步，布局钩子修改在下一次 `Update` 同步；输入与渲染仍要求更新成功。分别验证失败／重试、兄弟通知派发及回调可读几何。

## 同步实现

静态快路径要求运行时类型完全匹配、初始化完成且无自定义脚本同步。未知派生类型保留保守路径。固定标量配置可暂停轮询；配置替换、Setter、样式变化、宿主修改及安装绑定会恢复轮询。绑定与 optional Getter 持续求值，包括空结果；弱观察不能强持有控件。

生成的标量属性保留未装箱的局部、样式及输出快照。变更令牌仅在回调成功后提交，失败和重新登记不得提交旧输入。原生变更令牌减少未变状态的重复查询，但不能跳过动态 Getter 与自定义编解码器。样式保留解析成功的内建字面值，变量展开、资源解析、选择器状态和主题继续动态更新。公共 Builder 方法随类型生成，减少声明操作而不改变语法。

原生同步期间，当前挂载控件的重复检查共享回调入口已验证的作用域。成功与失败后均须恢复；其他控件和回调外查询保留原生检查。维持属性求值、optional 回退、稳定化及生命周期顺序。

## Context 后备与 Runtime 选项路由

`Private/Bindings/VeroScriptGuiRuntimeBindings.cpp` 的默认 `__NativeContext` 构造桥在参数未指定时先查 `FScriptServices.DefaultBackend("gui")`，再查已解析的 `Application.DefaultBackend("gui")`。调用 `Factories.Gui` 前重新检查空结果，仅此时使用 `rmlui`；保留显式 ID／auto／none。`FScriptServices.CreateContext` 绕过此应用工厂分支，仍由宿主负责。`VeroScriptRuntimeMain.cpp` 将 VM／JIT 选择保留在执行配置和包 `SelectedBackends` 中，传入 GUI 命令适配器的值须排除服务键 `vm`、`jit`。分别检查无回调、空回调结果、环境优先级、显式选择，以及 GUI 容器携带 VM／JIT 选项的检查／运行。

## 场景复用与同步边界

样式同步复用未变化的场景结构和选择器匹配结果；普通几何、文字与动画输出不会单独使选择器缓存失效。资源解析、样式值解码和动态 Getter 仍按同步规则执行。只有经生成代码确认无需周期工作的控件才停止轮询；动态绑定或未知同步行为保持原有路径。

`FMarkupRuntime::MatchesScene` 比较主题、控件、父级、名称作用域、所属关系和样式表后复用 `FContext::SceneStyles`。选择器结果跟踪资格、焦点／模态／悬停／按下状态及样式输入；动态资格 getter 禁止复用。提交应用前的输入，使样式输出能使下一稳定化轮次失效；替换根或销毁时清除场景状态。`CanSuspendSynchronization` 对未知方法／构造、其他处理器生成的工作、自定义 codec 及缺少辅助方法的旧包基类保持保守。私有生成名称保留模块的规范链接身份。

## GUI 编译配置

GUI 的 `CompilerOptions` 向脚本编译扩展传递所选 VM；仅在没有显式 VM 且 `bClearVmBackend` 为 false 时使用 `FallbackVmBackendFactory`／`FallbackVmBackendPath`，仅在扩展选项未提供时继承 `ExecutionControl`。显式禁用与调用者独立控制保持优先。

<a id="gui-text-layout-maintenance"></a>

## 文本布局消费方约束

段落缓存身份除逻辑文本与方向外，还须包含所选服务实例及不可变配置；仅后端 ID 不足以区分。样式片段之间保留完整逻辑段落，先确定换行，再查询该行的双向排列；通过解析后的服务保留模块租约。绘制时保留原始 advance 和 offset，只在每个可见簇的最后一个字形后增加一次字间距，使绘制与行宽、光标位置一致。保留有限负字间距，并将最终行宽限制为非负。剪贴板内容与偏移保持逻辑顺序。公开整形、簇及能力限制以 [Font 契约](../../../../../VeroScriptFont/Resources/Docs/User/zh-CN/index.md)为正本。

<!-- docs-navigation -->

---

[开发文档](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
