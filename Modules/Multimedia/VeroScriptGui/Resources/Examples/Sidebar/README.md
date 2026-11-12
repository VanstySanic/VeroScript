# 侧栏动画

在已配置 GUI 后端的项目环境中运行：`veroscript gui-preview main.vs`。

点击“收起侧栏／展开侧栏”，侧栏宽度与内容区左内边距会一起变化；动画中可再次点击反向切换。导航标题带淡入效果，计数按钮可直接交互。

动画复用 `AnimateWidth`、`AnimatePadding` 和 `AnimateOpacity`，无需每帧更新脚本属性。`HoldEnd` 保留动画终值，`GetPresentedX` 读取显示值，`GetX` 仍读取基础属性。

此示例使用 Windows 的 `C:/Windows/Fonts/msyh.ttc` 显示中文；其他平台需将字体路径改为当地可用的字体。字体文件不随示例分发。
