# 可替換 VM 後端

<a id="vm-provider-assembly"></a>

Core 負責程式、值、邏輯框架、語言操作、執行控制與排程；完整位元組碼解譯迴圈屬於 `Modules/Execution/VeroScriptBytecodeVm` 的 `VeroScriptBytecodeVm`。後端 ID 是 `vs-bytecode`，套件名稱是 `vs-bytecode-vm`。純 Core 不隱式選擇 VM 或 JIT。Runtime 明確裝配 `Vm/` 下的官方動態庫；Interop、Web 與行動靜態產品明確注入 VM。

## 靜態裝配

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptBytecodeVm.h"
#include "VeroScriptVm.h"

auto factory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
VeroScript::VeroScriptVm vm(output);
std::string error;
if (!vm.SetVmBackendFactory(factory, error))
{
	throw std::runtime_error(error);
}
```

## 動態裝配

```cpp
if (!vm.SetVmBackendPath("./Vm", error))
{
	throw std::runtime_error(error);
}
```

```text
veroscript --vm-backend vs-bytecode app.vs
veroscript --vm-backend ./custom-vm app.vs
veroscript --vm-backend ./Vm/VeroScriptVmBytecode.dll app.vs
```

選擇屬於執行域，首次實際執行腳本後固定。Session.Clear() 不解除固定。每個活動上下文有獨立實例；同上下文同步重入使用活動實例。官方後端可在同域同執行緒重用一個完全閒置的實例。設定失敗保留原選擇；未進入執行時可修正。

腳本編譯處理器也需要明確的執行設定：FCompileOptions::ScriptCompileExtensions.VmBackendFactory 或 .VmBackendPath。純解析、普通編譯與不執行處理器的分析不需要 VM。worker 及獨立清理上下文繼承選擇和既有治理規則。

## 獨立建置與 SDK

執行契約只有標頭檔，不連結 Core 二進位檔。後端透過實際所屬 Core 的服務表呼叫語言操作。宿主連結 Core 與靜態 VM，或連結 Core 並載入動態 VM。組合 SDK 可包含兩者；連結本身不會自動註冊。

```text
cmake --install <build> --component VeroScriptExecutionContract --prefix <sdk>
cmake --install <build> --component VeroScriptBytecodeVmSdk --prefix <sdk>
cmake -S Modules/Execution/VeroScriptBytecodeVm -B <vm-build> -DCMAKE_PREFIX_PATH=<sdk>
```

## 執行與發行約束

EExecutionBackend::Vm/Mixed/Jit 仍是獨立策略，JIT 沒有預設後端。缺少 VM 或能力不足會明確報錯，不重複準備參數或隱藏回退。限額、取消、權限與共享的有界清理保留 Core 語意；清理未完成或後端狀態無效時必須重設 Session。版本為 0.1.1；二進位檔也須符合公開標頭指紋、編譯器、STL、CRT、平台與佈局。Windows 使用匹配的 /MD；iOS/Web 使用靜態裝配。桌面測試不能替代行動工具鏈及裝置驗證。

獨立發行同時提供可攜 ZIP/tar 和可安裝的 `.vspkg`；後者的套件清單位於壓縮檔根目錄。使用 `veroscript install ./vs-bytecode-vm-0.1.1-<target>.vspkg --path=<專案目錄>` 安裝。安裝記錄 `vm` 服務及 `vs-bytecode` 代號，不修改既有執行域的選擇；可攜套件解壓後也可直接以檔案或目錄指定。

## 僅編譯，不裝配 VM

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript)
```

```cpp
#include "VeroScriptCompile.h"

auto compiled = VeroScript::VeroScriptCompile::CompileSource(
	"int answer(){return 42;}", "answer.vs");
// compiled.bSuccess / compiled.Error / compiled.Module
```

GUI 等 C++ SDK 宿主透過 `FEngineConfig` 傳入所選 VM；標記指令碼使用 `Gui::FMarkupScriptOptions::Engine`。配接器保留宿主的選擇，原始 C++ SDK 不隱式選擇後端。儲存庫獨立模組測試可用 `-DVEROSCRIPT_TEST_VM_LIBRARY=<相符的動態後端檔案>` 明確裝配測試 VM。
