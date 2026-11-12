# C++ AOT 发射器

`VeroScriptCppAot` 从通过编译检查的 VeroScript 程序发射 C++。公共 API 支持源字符串、文件、程序、单个生成源字符串，以及包含相对文件和 JSON 清单的 `FCppAotSourceBundle`。

## Bundle 不变量

生成 Bundle 有意采用一个 C++ 翻译单元和生成的 `.inl` 片段。把文本输出拆成独立链接的模块会改变模板实例化、内部链接和全局初始化顺序。因此 Bundle 布局是输出契约，不是可选格式。

## 资格与运行时支持

发射器验证入口模块，只接受支持的 `main` 形状：`void main()`、`int main()` 或相应的 `string[] args` 形式。它仅为程序实际使用的值、容器、channel 和 worker 任务发射兼容支持。AOT worker 全局变量和静态字段按原生线程隔离；channel 是显式同步共享能力。

支持以字符串指定任务入口、协作式 `std::concurrent::cancel_task()` / `std::concurrent::cancel_requested()`，以及文件系统操作 `std::filesystem::is_file()`、`std::filesystem::is_directory()` 和 `std::filesystem::remove_dir()`。以可调用对象指定任务入口及可执行的 `field_info<T>` / `method_info<T>` 反射会被 AOT 明确拒绝并给出诊断。

当降级会改变 VeroScript 的数值、所有权、异常、析构、模块或并发语义时，发射器必须拒绝程序。编译、资格检查、降级或输出写入失败后都不得返回部分源码。

生成的 `array<bool>` 为每个元素使用独立、可寻址的布尔存储槽，以便索引和引用参数提供真实的 `bool&`。元素存储不采用位压缩，因此相同容量下比 `std::vector<bool>` 占用更多内存。

## 维护

公共发射器头文件、实现、生成源码测试、Bundle 测试和生成文件验证路径是权威依据。新的运行时辅助代码只能在测试证明对应 VeroScript 行为且辅助代码保持 Bundle 自包含时加入。

参阅[C++ AOT — 用户 pair 模板](../../User/zh-CN/aot.md#cleanup-container-operations)。

独立源码的十进制转换应与 `Shared/Private/VeroScriptFloatParsing.h` 保持一致。发射器的 `VeroScriptFloatParsingSource.h`、`MakeRuntimeSupport` 和生成的 `VsFloatParsingDetail` 构成自包含输出路径。共享输入迁移时应同步修改 CMake 读取路径与 configure 依赖，生成代码的使用者不应依赖源码树私有头文件；目标宽度舍入及区域／范围行为须与 Core AOT 执行分别验证。

<a id="standalone-file-paths"></a>

生成的 `VsPathFromUtf8` 保持自包含。Windows 的 char8_t 与 C++17 u8path 两个分支均先构造路径并转为原生分隔符，只对至少 248 个原生 UTF-16 单位的绝对路径增加扩展前缀。完整 UNC 使用 UNC 扩展形式，既有扩展前缀保留，相对路径不转为绝对。转换前保留嵌入 NUL 拒绝。同步维护 Unicode、长绝对／UNC、相对路径及两个语言标准分支的文件 I/O 生成与回归；生成物使用者不能依赖私有源码头。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](aot.md) · [zh-TW](../zh-TW/aot.md)
