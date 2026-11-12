# Python — Mobile SDK

请将头文件子目录实体化：`includeDirectory` 内的目录符号链接及 Windows junction／reparse 目录会被拒绝。`includeDirectory` 本身可以解析到 SDK 内的真实目录；普通 `.h` 文件链接可保留，但目标必须位于 SDK 内，其内容会计入 SDK 指纹。

本页面向使用 `Extensions/Language/Python/Resources/Tools/Mobile` 构建与发布 Python 移动包的维护者。必须显式提供目标 CPython SDK；宿主解释器与桌面／macOS 二进制不能替代目标资源。在新目录构建，校验完整文件清单，并收集目标平台验收证据后再发布稳定版本。

Python：宿主 CPython 与目标 major/minor 相同，使用 CMake 3.25+、Android NDK/Ninja 或 macOS/完整 Xcode。目标为带 GIL 的 CPython 3.13/3.14 release，每个 minor 独立打包，所有 slice 的完整版本相同；拒绝 debug/free-threaded。SDK 描述符的相对路径必须留在自身目录，字段为 `schemaVersion`、`platform`、`architecture`、`variant`、`minimumVersion`、`includeDirectory`、`library`、`sysconfigData`。检查器读取目标头文件，通过 AST 读取字面量 `build_time_vars`，不执行目标解释器；检查平台、架构、最低 OS、Python ABI 及 iOS install name。Android 必须有 arm64-v8a/x86_64（API 26），iOS 必须有 arm64 真机与 arm64/x86_64 模拟器（15.0）。

Python 包提供扩展、facade 和资源，应用提供 CPython 及依赖。Android 使用提取后的 JNI 路径，iOS 使用签名的动态 Python 扩展 framework 和 `.fwork`/`.origin`；这与静态 C ABI XCFramework、被禁用的原生 JIT 分别处理。每个 slice 生成新的 build nonce／native identity，`python-mobile.json` 记录全部文件摘要、版本、平台／ABI、SDK 指纹及待验收状态。

发布前审核完整 inventory 与二进制。`device_smoke.py` 拒绝桌面执行，每次重试先使旧证据失效；证据须匹配 build ID、准确 Python／产品版本、ABI、架构和真机／模拟器。所有 slice 都须有证据，Android arm64 还须覆盖 4 KB 和 16 KB 页环境。受治理的 VM smoke 检查导入、回调、诊断、保留值及释放。最终签名应用、依赖／APK ZIP 对齐、iOS runpath／资源／签名、取消和应用测试须另行验收；宿主测试不能替代这些步骤。

[Python](index.md)


## 出版文档

移动包仅收录功能文件、运行资源和许可证，不携带 README、手册或示例；集成 User 文档通过独立 Resources 发行物获取。原生资源路径和设备验收要求保持不变。


## 描述文件与复现

下面是 Android arm64 和 iOS 真机描述示例。sysconfigData 名称仅为示例，必须选择 SDK 中的实际文件，不能把其他 ABI 的文件改名使用。Android x86_64 需修改 `architecture` 和所有对应路径；iOS 模拟器使用 `variant: simulator`、`architecture: arm64` 或 `x86_64` 及单架构二进制。各切片完整版本必须一致，保留 `@rpath/Python.framework/Python`。

从仓库根目录运行。`--host-python` 选择宿主解释器，`--cmake` 选择构建驱动；`--plan` 验证 SDK 并输出命令，不执行构建，Android 规划仍需 `--ndk`。输出目录必须是新目录，中间构建在临时目录进行，每次构建使用新 nonce。除随包声明外，还应保留 CPython 及依赖的许可。

宿主测试 `Extensions/Language/Python/VeroScriptPython/tests/test_mobile.py` 验证 AST、宿主/目标配置、通用模拟器框架与证据。真机烟雾测试使用受控 VM/Mixed（100,000 指令、5 秒、256 层调用、仅允许控制台输出），拒绝桌面运行，`-O` 下仍有效，并核对原生身份、ABI 与版本。CPython、应用测试与最终签名应用审查仍需独立完成。

```json
{
	"schemaVersion": 1,
	"platform": "android",
	"architecture": "arm64-v8a",
	"variant": "device",
	"minimumVersion": "26",
	"includeDirectory": "include/python3.14",
	"library": "lib/libpython3.14.so",
	"sysconfigData": "lib/python3.14/_sysconfigdata__android_aarch64-linux-android.py"
}
```

```json
{
	"schemaVersion": 1,
	"platform": "ios",
	"architecture": "arm64",
	"variant": "device",
	"minimumVersion": "15.0",
	"includeDirectory": "Python.framework/Headers",
	"library": "Python.framework/Python",
	"sysconfigData": "stdlib/_sysconfigdata__ios_arm64-apple-ios.py"
}
```

[Python](../../User/zh-CN/mobile.md)

## 从描述文件构建

工具实现不等于已完成交叉编译或设备验证。必须显式提供目标 CPython SDK，宿主解释器与目标 major/minor 相同，目标使用带 GIL 的 release 构建。Frontend/BindGen/Transpiler/AOT 仅在宿主运行。扩展静态链接 Core、C++ 支持和 Interop；VM/Mixed 不依赖原生 JIT。

Android 需要 NDK/Ninja 和两个 ABI 的 SDK，检查 libpython、扩展的 LOAD/RELRO 16 KB 对齐及兼容的 `libc++_shared.so`。iOS 需要 macOS 与完整 Xcode。通用模拟器 Python.framework 须用 `xcrun lipo -thin` 等方法拆成描述文件所需的 arm64/x86_64 单架构输入，保留 `@rpath/Python.framework/Python`。

在仓库根目录执行，打包命令按目标平台**选择一个**；尖括号路径替换为实际 SDK 或新的输出位置。检查器不会执行目标 Python。

```text
python Extensions/Language/Python/Resources/Tools/Mobile/mobile_sdk.py --sdk <sdk.json> --check-host
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py android --sdk <android-arm64/sdk.json> --sdk <android-x86_64/sdk.json> --ndk <ndk-directory> --out-root <new-output-directory>
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py ios --sdk <ios-device/sdk.json> --sdk <ios-simulator-arm64/sdk.json> --sdk <ios-simulator-x86_64/sdk.json> --out-root <new-output-directory>
```

## Android 部署

将各 `slices/<target>/jniLibs/<abi>/` 复制到应用的 `src/main/jniLibs/<abi>/`，并按 CPython 提供方要求部署目标 libpython 及依赖。整个应用使用一个兼容的 `libc++_shared.so`，不能用 `pickFirst` 掩盖不兼容。把匹配切片的 `python/veroscript/` 放入应用 Python 资源；若原为 APK assets，先提取到实际目录。保留 `stdlib/`、`localization/`、`py.typed`。

加载器需要已提取的实际文件路径。配置应用 Gradle 模块，并在首次 import 前配置解释器：

```kotlin
android {
    defaultConfig { minSdk = 26 }
    packaging { jniLibs { useLegacyPackaging = true } }
}
```

```python
import os
import sys

os.environ["VEROSCRIPT_ANDROID_NATIVE_LIB_DIR"] = native_library_dir
sys.path.insert(0, application_python_directory)
import veroscript
```

`native_library_dir` 来自已安装应用的绝对 `context.applicationInfo.nativeLibraryDir`，须确认库文件确实存在。`application_python_directory` 是含 `veroscript/` 的部署目录。加载器以 `veroscript._veroscript` 名称加载 `libveroscript_python.so`。请通过安装后的 APK 验证，不能使用构建机器路径代替。

## iOS 部署

将外部 **Python.xcframework** 与产出的 **VeroScriptPython.xcframework** 以 **Embed & Sign** 加入应用。Xcode 选择真机或通用模拟器框架；未签名扩展由应用嵌入阶段签名。runpath 包含 `@executable_path/Frameworks`，保留以下布局：

```text
Application.app/
  Frameworks/Python.framework/Python
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript.origin
  python/veroscript/__init__.py
  python/veroscript/_veroscript.fwork
  python/veroscript/stdlib/...
  python/veroscript/localization/...
```

另行配置 CPython 及其标准库，把 `<app-bundle>/python` 加入模块搜索路径，关闭只读 Bundle 的字节码写入。`AppleFrameworkLoader` 读取 `_veroscript.fwork`，两个模拟器架构共用 marker 与 `.origin`。未调整 origin 时不能移到 `app_packages`；不要把 `.so` 放入资源目录，也不要再次转换已准备好的 marker。

## 审核与平台证据

审核将完整清单、facade、标准库及所有语言目录与对应版本源码比较；检查 CPython 初始化符号、依赖、Android ABI/API/16 KB 对齐、iOS 平台/最低系统/framework/Info.plist/marker 及 XCFramework 成员。额外原生载荷即使已加入哈希清单也被拒绝。应在对应 checkout 或 sdist 的 `_native` 根目录运行，不能仅用包内哈希证明资源齐全。

先运行普通审核。将 `device_smoke.py` 复制到应用测试框架，在实际移动解释器的工作线程上向 `run` 传入可写报告路径。导出所有切片及 Android arm64 两种页环境的报告，按实际数量重复 `--evidence`。

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory>
```

```python
from device_smoke import run
run(writable_report_path)
```

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory> --require-device-tests --evidence <report-1.json> --evidence <report-2.json> --evidence <report-3.json>
```

新构建需要新证据。身份使用 `veroscript.HOST_PLATFORM`，产品语义版本使用 `veroscript.__version__`，不能用内核机型名或带字节码信息的 `RUNTIME_VERSION` 替代。`python-mobile.json` 中的 `pending` 不代表已通过验收。Android 发布前检查全部原生依赖，执行 `zipalign -v -c -P 16 4 <application.apk>`，并测试经 AAB 发行路径生成的签名应用。iOS 检查签名、资源、runpath 以及真机和两个模拟器 ABI 的导入。保留 CPython/依赖许可，另行完成 CPython、应用测试及应用商店政策确认。

## 构建身份与 Android 库路径

产品版本来自 `VeroScriptVersion.h`，不能用 CPython 版本或拼接后的运行时字符串替代。Android 应确认原生库已提取到 `ApplicationInfo.nativeLibraryDir`，再于首次 `import veroscript` 前将其绝对路径传给 `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR`。

<a id="android-build-contract"></a>

Android 构建显式启用 `VEROSCRIPT_BUILD_APPLICATION=ON`，保留 `python/veroscript/` 中的 `licenses/application/miniz/LICENSE`、`licenses/application/json/LICENSE.MIT`、`licenses/application/sha256/LICENSE`。工具链验证优先比较已解析的 NDK `ANDROID_ABI`／`ANDROID_PLATFORM_LEVEL`，否则使用 CMake Android／系统字段；旧 NDK 的 `CMAKE_SYSTEM_VERSION=1` 不是目标 API。16 KB 检查验证 LOAD 对齐，以及按页扩展的 RELRO 保护不覆盖 RELRO 外的可写数据；不要求每个 RELRO 末端均 16 KB 对齐，支持的 NDK 共享 STL 可以带未对齐的 RELRO 尾部。

<a id="compiler-nesting-acceptance"></a>

## 原生编译栈

嵌入宿主必须在原生栈至少为 8 MiB 的工作线程执行编译。Android JNI 与 Apple dispatch 线程的栈可能较小，导致解析器尚未返回嵌套上限诊断就发生栈溢出。Session 必须在该线程创建、使用和销毁，不要把已创建的 Session 或回调隐式迁移到另一线程。应在创建应用 Python 工作线程前配置线程栈，或由原生嵌入宿主直接提供。device_smoke.run 也须在此线程执行；原有四组验收现检查 200 层正常执行、512 层上限诊断及后续复用。新证据含 compilerNestingChecked=true，不接受缺少此门禁的旧证据。

`compilerNestingChecked` 是验收元数据，不是公开 Session API。`verify_mobile.py::verify_package` 在 schemaVersion 1 及匹配的构建／运行时／ABI／slice／产物身份之外，要求该字段严格为布尔 true；run 仅在 exercise_runtime 成功后记录它。Python Session／C ABI 编译不经过 Mobile::FRunner 的栈适配。下面示例的 report_file 须为应用拥有的可写目录内的绝对路径；该监督函数会等待结果，应在非 UI 线程调用。模拟回归只验证逻辑，不验收目标二进制。

```python
import threading
from concurrent.futures import ThreadPoolExecutor
import device_smoke

def validate_on_worker(report_file):
	threading.stack_size(8 * 1024 * 1024)
	with ThreadPoolExecutor(max_workers=1) as worker:
		return worker.submit(device_smoke.run, report_file).result()
```

<!-- docs-navigation -->

---

[开发文档](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](mobile.md) · [zh-TW](../zh-TW/mobile.md)
