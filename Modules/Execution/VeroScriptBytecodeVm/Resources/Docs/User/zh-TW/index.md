# 位元組碼 VM

`VeroScriptBytecodeVm` 是位元組碼直譯器，後端 ID 為 `vs-bytecode`，散發名稱為 `vs-bytecode-vm`。選擇直譯器與 `Vm`／`Mixed`／`Jit` 執行原則獨立；原始 Core／C++ 主機不會隱含選取直譯器。

## 使用已安裝 Runtime

保留相符的完整 Runtime，包括 `Vm/`。自動選擇先使用所選安裝環境中的已啟用候選，沒有可用的自動 VM 候選時才以隨附解譯器作為產品後備。明確選擇或停用優先，不會被該後備覆寫。下列命令選擇隨附 ID 或相符的已解壓縮目錄。選擇 `.vspkg` 中的後端 ID 前，先將套件安裝到所選環境；安裝本身不替換已固定的執行域。自訂 ID 需要套件支援及所選環境。

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

## 嵌入靜態 SDK

在既有 CMake 主機目標 `app` 查找兩個相符 SDK 套件並連結兩個目標，執行前明確設定每個原始 VM；僅連結不會選取。C++ Session／Engine 主機可設定 `FEngineConfig` 或使用對應明確選擇 API。

```cmake
find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeVmAssembly.h"
#include <sstream>

std::ostringstream output;
VeroScript::VeroScriptVm vm(output);
VeroScript::BytecodeVm::Configure(vm);
```

## 選擇與相容性

提交執行時直譯器選擇凍結，Session 的 `Clear()` 與重新載入保留該選擇；設定失敗保留舊選擇，凍結後更換直譯器需建立新 Session。執行編譯指令碼也需要明確 VM，不執行處理器的一般剖析／編譯不需 VM。SDK 標頭、Core、編譯器／執行階段、架構及後端身分須相符，僅版本均為 `0.1.1` 不代表二進位相容。行動／Web 靜態產品明確組裝自己的 VM，不載入桌面動態程式庫。

[C++](../../../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/embedding.md) · [CLI](../../../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md)

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` 設定產品後備欄位；Session 多載使用 `SetFallbackVmBackendFactory`，保留明確選擇與環境候選。直接對原始 `VeroScriptVm` 呼叫 `Configure(vm)` 則透過 `SetVmBackendFactory` 明確組裝靜態工廠，低層 VM 自身不解析已安裝候選。上面的靜態 SDK 範例屬於後一種情況。

<!-- docs-navigation -->

---

[User](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
