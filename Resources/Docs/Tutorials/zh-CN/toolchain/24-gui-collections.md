# 24：使用集合、导航与对话框

打开随附 GUI 示例，练习选择项目、排序虚拟表格并接收模态对话框结果。示例的列表和表格各有 10,000 行，只为视口附近的行创建控件。

## 1. 准备

使用匹配且启用 GUI 的完整 Runtime，保留其 `Vm/`，并配置平台／图形 `sdl3`、GUI `rmlui`、图像 `stb`、字体／文本布局 `font-backend` 提供者。在已安装环境包含这些服务的现有 GUI 项目中操作；复制源文件不会安装提供者。运行时须保留完整 Runtime 与提供者资源。

将 [main.vs](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/main.vs)、[page.vswml](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswml)、[page.vswcss](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswcss) 三个文件复制到同一可写项目目录，保持文件名及相对位置，并在此目录打开终端。

## 2. 检查

以下命令将编译处理器的超时设为 20 秒，指令数等其他宿主限制仍然生效；请按项目与宿主情况调整预算。检查成功的退出码为 0，不打开窗口，也不执行 `main`。

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-check main.vs
```

## 3. 运行与交互

使用同样选项预览脚本入口。本例调用 `Application.Run()`，需要关闭窗口退出。`--frames` 仅允许用于 ML 预览入口；对这个 `.vs` 入口指定该参数会返回用法错误。

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-preview main.vs
```

在 ListView／TreeView 中选择行并用方向键导航，切换 ComboBox 和 TabView。点击表头排序；排序按 UTF-8 文本序进行，外观为数字的文本也不会自动采用数值排序。打开 File 菜单及对话框，选择接受／取消或按 Escape，观察状态文本中的结果。模态对话框打开时，背景控件不接收普通指针／键盘输入。

## 4. 观察虚拟化

滚动两个 10,000 行视图。逻辑数据保留，控件按需回收；选中状态属于键，而不是回收的控件实例。本例行高固定为 28 个界面单位，自定义内容也须放入该高度。不提供变高行或树虚拟化。

## 5. 练习

修改显示标签或状态字符串后重新检查。增加数据行数时保持 `count × rowHeight ≤ 16,777,216`，再次观察滚动。自定义行工厂应完整重设回收状态，使用稳定、非空且唯一的键。`SetVirtualSource`、预取范围、所有权与选择接口见 [集合、导航与对话框](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/zh-CN/Reference/Framework.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/toolchain/24-gui-collections.md) · [fr-FR](../../fr-FR/toolchain/24-gui-collections.md) · [ja-JP](../../ja-JP/toolchain/24-gui-collections.md) · [ko-KR](../../ko-KR/toolchain/24-gui-collections.md) · [zh-CN](24-gui-collections.md) · [zh-TW](../../zh-TW/toolchain/24-gui-collections.md)
