# VeroScriptSdl3

<a id="provider-boundary"></a>

Platform／Graphics／Audio 公共契約須保持獨立於 SDL 和 Core。具體 SDL 實作、執行階段、驅動編譯選項和原始聲明歸 VeroScriptSdl3。修改提供者時核對匹配契約、安裝／移動 SDK、模組存續與擁有者執行緒的延遲回收。原始碼檢查不能代替實際裝置與平台驗證。

## 建置與宿主整合

```sh
cmake -S . -B build/visual -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF -DVEROSCRIPT_BUILD_VISUAL=ON
cmake --build build/visual --config Release
cmake --install build/visual --config Release --component VeroScriptVisual --prefix out/visual-sdk
```

## 維護與驗證

`VEROSCRIPT_SDL3_GRAPHICS` 決定圖形服務註冊。驅動分別由 `VEROSCRIPT_SDL3_GPU`、`VEROSCRIPT_SDL3_D3D11`、`VEROSCRIPT_SDL3_OPENGL` 和 `VEROSCRIPT_SDL3_WEBGPU` 控制，WebGPU 預設 OFF。手柄和音訊使用 `VEROSCRIPT_SDL3_CONTROLLERS` 與 `VEROSCRIPT_SDL3_AUDIO`。原生 WebGPU 透過 `VEROSCRIPT_WEBGPU_ROOT` 使用固定 wgpu-native v29.0.1.1，瀏覽器使用固定 Emdawn port。編譯支援與實際驗收結果須分別記錄。

<a id="sdl-audio-build"></a>

根建置以 `VEROSCRIPT_BUILD_AUDIO=ON` 啟用 Audio；SDL 實作另需 `VEROSCRIPT_BUILD_SDL3=ON` 與 `VEROSCRIPT_SDL3_AUDIO=ON`。基礎模組的獨立建置入口也支援 Audio。Linux 實際驅動須與空裝置測試分別驗證。

`VEROSCRIPT_SDL3_CONTROLLERS` 預設 ON，OFF 僅停用原生手把。每個裝置開啟一次，異執行緒最終釋放仍在所屬執行緒回收。後端保留主機背景輸入設定；實體能力取決於系統／裝置／驅動。不包含陀螺儀、手把觸控板、軌跡球或進階力回饋；音訊可透過 `VEROSCRIPT_SDL3_AUDIO` 單獨啟用；攝影機、SDL_Renderer 及獨立 Haptic 子系統保持關閉。版本不變，請配套重建主機、提供者、繫結和 SDK。

`VeroScriptSdl3ControllerTests` 以 SDL 虛擬裝置驗證列舉、熱插拔、映射、獨立狀態、震動、範圍及生命週期；記憶體後端和腳本測試覆蓋公共介面。實體手把與 Apple／行動端／瀏覽器驅動仍需實機驗收。

`VeroScriptSdl3OpenGl.cpp` 在 `SDL_PLATFORM_ANDROID`、`SDL_PLATFORM_IOS`、`SDL_PLATFORM_TVOS` 使用主程式視窗路徑，要求 OpenGL ES 及一個相容 Platform 視窗，不建立隱藏輔助視窗。Android 分開統計所保留 EGL 繪圖上下文與表面呈現占用，使替換／取消在提交前保留舊裝置；UIKit 保持獨占單繪圖上下文規則。Android 透過主程式 SDLActivity／`SDL_main` 的原生事件執行緒整合初始化，Java UI 執行緒準備完成不等於 SDL 擁有者執行緒已初始化；其他平台仍要求應用程式主執行緒初始化。保持視窗、裝置、事件泵及延遲回收的擁有者執行緒約束，另行驗證目標 SDK／裝置。

[README](../../../../README.md) · [User](../../User/zh-TW/index.md)

合成事件測試不經過 SDL 上游去重路徑。

`VEROSCRIPT_SDL3_GRAPHICS` 由已啟用的圖形驅動推導；應設定具體驅動選項，不直接覆寫此衍生值。

SDL_GPU 算繪器重用已退休的頂點／索引緩衝對，最多 128 對／8 MiB，並受 MaxGeometryBytes 進一步限制。上傳儲存亦會重用；傳輸映射及目標上傳均以 cycling 保留排程中 GPU 工作仍引用的儲存。關閉算繪器會釋放閒置儲存；尚存幾何仍遵守原有擁有權及所屬執行緒退休規則。

<a id="uikit-recovery-capability"></a>

保持 `Graphics/VeroScriptSdl3GraphicsBackend.cpp::GetCapabilities` 與上下文擁有權一致：SDL_PLATFORM_IOS／SDL_PLATFORM_TVOS 下僅 opengles 清除 bDeviceRecovery。不能從提供者 ID 或其餘七個啟用能力推導復原支援。UIKit 無法在舊 EAGL 擁有者旁建立替換；Android 獨立保留上下文是不同契約。執行階段能力回報與公開復原文件須一併維護。

<a id="native-provider-maintenance"></a>

提供者原始碼擁有 SDL 專屬服務描述與具名靜態查詢 `veroscript_sdl3_query`，原生擴充建置匯出 `veroscript_extension_query`。只註冊此次建置包含的服務和圖形驅動，分別核對公開契約及實際能力。著色器輸入、驅動相依及提供者租約歸 SDL3，不進入後端無關契約。應以相符使用端驗證靜態／動態探索、輸入路由、圖形、音訊與擁有者執行緒回收；合成事件或虛擬驅動檢查不能認證真實裝置。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
