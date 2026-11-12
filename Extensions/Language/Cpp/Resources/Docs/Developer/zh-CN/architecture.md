# C++ 组架构

## 依赖方向

C++ 组是 `VeroScript/` 之上的适配器和工具链层。`VeroScriptCpp` 提供嵌入与会话操作，`VeroScriptCppAot` 直接依赖核心。`VeroScriptCppFrontend` 拥有共享 C++17 声明模型；`VeroScriptCppBindGen` 和 `VeroScriptCppTranspiler` 独立消费它。任何消费者都不得为了自己的降级策略改动前端 AST。

核心不包含 C++ 工具头。`VeroScriptInterop` 是独立稳定 C ABI，不是 C++ 前端、BindGen 或 Transpiler 的实现依赖；嵌入层可以使用核心公开的运行时和绑定接口。

## 原生调试边界

`VeroScriptSessionDebug.h` 定义供宿主或 Tooling 运行域实现使用的 C++ Session 桥接。
Session 负责桥接的安装与注销，并在真实执行 VM 周围管理共享调试器的寿命；
程序替换、回滚和销毁会通知桥接，清理 VM 不会继承调试器。
边界提供借用的程序、绑定、源码和执行上下文数据。
宿主负责复制同步元数据，并在执行线程停止的安全点处理调试请求队列。
Cpp 不依赖 Tooling；运行域注册、DAP 和连接生命周期归 Tooling，
编辑器／PIE、对象生命周期接入及游戏线程暂停策略归独立 UE 集成。

`VeroScriptCppSessionDebugTests` 覆盖真实参数修改、对象调用、保留回调、
解释器选择、重载回滚、异常清理、桥接替换／注销和协议请求的线程调度。
此公共边界改变 Session 的 C++ 内存布局，原生宿主需随 SDK 重新编译。
`VeroScriptCppJitContractTests` 使用独立的公开工厂实现验证配置和流式入口的
显式后端传递，以及缓存预热、真实 VM 调试和断开后恢复后端的转换；
测试不依赖具体 JIT 实现。
独立 SDK 消费示例仅使用导出头文件与库，验证调试器挂接、真实局部变量修改和注销后的继续执行。
TCP 示例测试使用独立子进程，限制网络等待并回收进程，不会分离借用局部 Session 的服务线程。

## 源码布局

每个原生模块从 `Public/` 导出受支持头文件，并将实现与测试放在 `Private/`。CMake 递归发现私有源码、从库目标排除专门测试源、另建测试可执行程序。公共头文件必须能在公共头测试中编译，不能依赖私有 include 路径。

## 共享输出模型

前端诊断含代码、消息、源位置和 token/声明数据；BindGen 和 Transpiler 在结果对象中保留这些诊断。项目操作必须收集所有输入后才生成目录、引用、源码映射或清单。输出辅助程序拒绝同一文件路径，并且仅在完整操作成功后替换文件。

## 变更规则

公共 struct、enum、源位置、生成文件格式、CMake 目标或支持子集规则改变时，必须更新专门测试和相关主题页。README 可说明命令，但公共头文件和测试才是行为依据。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/architecture.md) · [fr-FR](../fr-FR/architecture.md) · [ja-JP](../ja-JP/architecture.md) · [ko-KR](../ko-KR/architecture.md) · [zh-CN](architecture.md) · [zh-TW](../zh-TW/architecture.md)
