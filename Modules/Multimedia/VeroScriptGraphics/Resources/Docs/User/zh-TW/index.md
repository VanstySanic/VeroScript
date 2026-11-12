# VeroScriptGraphics — 圖形資源與二維繪製

<a id="graphics-service-contract"></a>

Graphics 透過 VS::Graphics 提供裝置、紋理、取樣器、索引幾何與 Renderer2D，公共原生契約不含 SDL/Core 型別。模組與原生服務契約統一使用版本 0.1.1；更新時一併重建宿主及提供者。

腳本中的資源、能力、尺寸查詢與已完成請求的結果讀取（`GetDevice`、`GetPixels`）標記為 const 方法，可從唯讀的傳值擷取呼叫。繪製、更新、請求輪詢或取消、關閉資源仍需要可寫接收物件。`GetError` 也會輪詢請求；這些主動操作應使用 `mutable` 或可寫參考擷取。

幾何上傳一次後即可重用。指令碼頂點依序包含八個 double：X,Y,U,V,R,G,B,A；索引為經範圍檢查的 int64。UpdateTexture 接受緊密排列的 array<uint8> RGBA 位元組。CreateSampler 選擇 Filter 與 U/V AddressMode。DrawList2D 在 AddGeometry 時保存 SetTexture、SetTint、SetTransform、SetBlendMode 的快照；PushClip 交集矩形，PopClip 還原先前裁剪。

位置使用視口內的繪製像素，Y 向下；裁剪使用目標絕對像素。SetTransform 接受十六個依欄優先排列的 double；GUI 透過 Platform DisplayScale 將版面單位換算為像素。頂點色彩為線性 RGBA，預設非預乘；Tint 始終非預乘。StraightAlpha 取樣非預乘紋理，PremultipliedAlpha 取樣預乘紋理。透明黑背景上的半透明圖層會保存預乘像素，合成時應使用 PremultipliedAlpha。sRGB 編解碼依紋理及目標格式處理。

RenderTexture 與 RenderSurface 接受 target,list,clear,R,G,B,A；帶 Viewport 的方法追加 x,y,width,height。清除作用於整個目標。保留舊內容前須初始化紋理；視窗表面必須清除，交換鏈暫不可用時可成功但不呈現。在擁有者執行緒先關閉幾何、取樣器、紋理、表面及渲染器，再關閉裝置；Clear 釋放列表參照。拒絕已關閉、跨裝置及讀取目前目標的資源。預設紋理及對齊傳輸上限 256 MiB、幾何 64 MiB、命令與裁剪層數 65,536。同步回讀使用 C++ API；指令碼使用 `Device.ReadTextureAsync`，輪詢要求並僅在成功後取得像素。

```veroscript
VS::Graphics::Service graphics = VS::Graphics::Service::Create("sdl3");
VS::Graphics::Device device = graphics.CreateDevice("");
VS::Graphics::Renderer2D renderer = device.CreateRenderer2D();
array<double> vertices = {0.0,0.0,0.0,0.0,1.0,0.0,0.0,1.0,
						 32.0,0.0,1.0,0.0,1.0,0.0,0.0,1.0,
						 0.0,32.0,0.0,1.0,1.0,0.0,0.0,1.0};
array<int64> indices = {0,1,2};
VS::Graphics::Geometry2D geometry = renderer.CreateGeometry(vertices,indices);
VS::Graphics::DrawList2D list = VS::Graphics::DrawList2D::Create();
list.PushClip(0,0,64,64);
list.AddGeometry(geometry);
list.PopClip();
VS::Graphics::Texture target = device.CreateTextureWithFormat(64,64,VS::Graphics::TextureFormat::Rgba8Unorm);
renderer.RenderTexture(target,list,true,0.0,0.0,0.0,0.0);
list.Clear();
geometry.Close();
target.Close();
renderer.Close();
device.Close();
```

SDL 視窗表面要求 SDR_LINEAR 合成，使混合與清除維持線性色彩語義；不支援時建立表面會失敗。

## 使用已安裝的原生 SDK

```cmake
find_package(VeroScriptGraphics 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Graphics)
```

<a id="vertex-color-mode"></a>

SetVertexColorMode 獨立於 Blend，接受 VertexColorMode::Straight（預設）或 Premultiplied。已預乘的頂點 RGB 在插值期間保持預乘；Tint 始終非預乘；使用 PremultipliedAlpha 輸出時，其 alpha 僅對 RGB 縮放一次。AddGeometry 保存此狀態，Clear 恢復 Straight。頂點色彩仍須為線性值，此選項不執行 sRGB 解碼。

## 合成與手動裝置復原

Composition2D 記錄 AddDraw 快照、BeginLayer/EndLayer、PushMask/PopMask、AddBlur、AddColorMatrix、AddShadow 及 AddBackdropBlur。Device.CreateCompositor2D() 重用暫存目標；CreateCompositor2DWithLimits(bytes,depth) 設定預算，預設 256 MiB、32 層，建構器上限 64 層及 65,536 條命令／繪製。群組透明度在圖層完成後只套用一次；巢狀及反向 alpha 遮罩取交集。模糊使用兩個可分離的 13 次取樣通道，半徑 0..64 像素；陰影保留原內容，背景模糊只替換遮罩範圍。色彩矩陣含二十個列優先排列的數值（每列五個值）。合成中間與最終像素皆為線性預乘 RGBA；合成器會將非預乘 ClearColor 轉為預乘，載入模式要求舊內容已預乘，Opaque 繪製須寫入相容的預乘像素。sRGB 轉換由紋理格式處理。無效範圍或預算不足不會改寫最終目標。Trim 釋放快取目標；裝置前先關閉合成器。DrawList2D.SetAlphaMask、SetBlur、SetColorMatrix 與 ResetEffect 提供單通道效果；Additive 將預乘來源通道相加。合成器表面方法在 composition 前接收目前繪製像素 width/height，Viewport 方法最後附加 x,y,width,height；呈現失敗輸出 false，GetPooledBytes 回傳 int64。Device.GetGeneration 與 SupportsDeviceRecovery 標示手動替換支援。Service.RecoverDevice(previous,driver) 先建立替代裝置，僅在成功後使舊代失效；建立失敗不改變舊代狀態。成功後須重新建立／上傳全部資源，先關閉舊表面再重新使用視窗，然後關閉舊資源和舊裝置。Invalidate 後仍可呼叫資源 Close。所選驅動可透過 Device.Poll 回報裝置遺失；復原裝置及重建資源仍由主程式明確執行。此 API 不提供 Renderer3D、任意使用者著色器或主程式命令錄製。

合成器暫存紋理預設採用 RGBA8 sRGB 儲存，取樣與附件混合仍在線性空間進行，避免透過線性 8 位中間紋理時丟失 sRGB 暗部色彩。C++ 可明確選擇 RGBA8 UNORM 並接受其線性量化；兩種腳本建構入口皆使用 sRGB 預設值。ColorMatrix 與 AlphaMask 對負 UV 及大於一的 UV 遵循 Clamp/Repeat/Mirror 取樣規則；Blur 與 Shadow 將來源矩形外視為透明，半徑以來源像素計量。

## 圖形 API 與非同步請求

請參閱[VeroScriptSdl3 — 基礎實作 — 圖形 API 與非同步請求](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/zh-TW/index.md#sdl-graphics-drivers)。

<a id="asynchronous-requests"></a>

CreateDeviceAsync/RecoverDeviceAsync 回傳 DeviceRequest；Device.ReadTextureAsync 回傳 ReadbackRequest。所屬執行緒 Poll 回傳 Pending / Succeeded / Failed / Cancelled，成功後才可 GetDevice/GetPixels；腳本 GetError 讀取診斷。預設實作可能同步完成。瀏覽器 WebGPU 的同步建立、讀回與 WaitIdle 回報 graphics.async_required；輪詢之間須讓出瀏覽器事件迴圈。

<a id="resource-retirement"></a>

復原成功才使舊裝置世代失效，之後重建資源。Cancel 捨棄結果，底層作業可能繼續。先關閉並釋放資源、裝置與請求，再於所屬執行緒收集至 Service.HasPendingRetirements() 為 false；瀏覽器須逐幀等待。保留 Platform 所屬服務至清理完成。

<a id="graphics-capabilities"></a>

能力屬於實際選擇的裝置／驅動程式。C++ 使用 `IDevice::GetCapabilities()`；腳本查詢 `SupportsGuiBasic`、`SupportsTransforms`、`SupportsClipMasks`、`SupportsFilters` 和 `SupportsDeviceRecovery`。要求替換前檢查復原支援並處理失敗，不能僅憑提供者名稱判斷。SDL3 限制由 [UIKit 裝置復原](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/zh-TW/index.md#uikit-device-recovery)維護。

腳本頂點的八個 double 必須可表示為有限 float，int64 索引轉為原生 uint32 前會檢查。`UpdateTexture` 對指定區域讀取恰好寬 × 高 × 4 個緊密排列的 RGBA 位元組，保留其編碼。`ResetTexture` 恢復純色繪製。繪製清單修改與算繪使用擁有者執行緒，不能並行進行。原生配置例外可能經 C++ 傳遞，腳本適配器將回呼失敗轉換為診斷。回讀傳回實際儲存的 RGBA 位元組，失敗時保留所提供的輸出；阻塞原生回讀／WaitIdle 應放在一般畫格迴圈之外。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
