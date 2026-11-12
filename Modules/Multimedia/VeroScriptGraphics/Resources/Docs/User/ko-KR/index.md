# VeroScriptGraphics — 그래픽 리소스와 2D 그리기

<a id="graphics-service-contract"></a>

Graphics는 VS::Graphics로 장치, 텍스처, 샘플러, 인덱스 기하와 Renderer2D를 제공합니다. 공개 계약에는 SDL/Core 형식이 없습니다. 모듈과 네이티브 서비스 계약은 같은 버전 0.1.1을 사용하며 갱신 시 호스트와 공급자를 함께 빌드합니다.

스크립트의 리소스, 기능, 크기 조회 및 완료된 요청의 결과 조회(`GetDevice`, `GetPixels`) 메서드는 const이므로 읽기 전용 값 캡처에서도 호출할 수 있습니다. 그리기, 갱신, 요청 폴링·취소 및 리소스 닫기에는 쓰기 가능한 수신 객체가 필요합니다. `GetError`도 요청을 폴링하므로 이러한 작업에는 `mutable` 또는 쓰기 가능한 참조 캡처를 사용하세요.

기하는 한 번 업로드한 뒤 재사용합니다. 정점은 X,Y,U,V,R,G,B,A 순서의 double 여덟 개, 인덱스는 범위를 확인한 int64입니다. UpdateTexture는 빈틈없이 배치한 array<uint8> RGBA를 업로드합니다. CreateSampler는 Filter와 U/V AddressMode를 선택합니다. DrawList2D는 AddGeometry 시 SetTexture, SetTint, SetTransform, SetBlendMode를 저장합니다. PushClip은 사각형의 교집합, PopClip은 이전 클립 복원을 처리합니다.

위치는 뷰포트 내부 그리기 픽셀이며 Y는 아래쪽, 클립은 대상 절대 픽셀입니다. SetTransform은 열 우선 double 열여섯 개를 받고 GUI는 Platform DisplayScale로 픽셀을 계산합니다. 정점 색은 선형 RGBA이며 기본값은 비사전곱입니다. Tint는 항상 비사전곱입니다. StraightAlpha는 비사전곱, PremultipliedAlpha는 사전곱 텍스처를 사용합니다. 투명 검정 위에 그린 반투명 레이어는 사전곱 픽셀을 저장하므로 합성에 PremultipliedAlpha를 사용합니다. sRGB 변환은 형식을 따릅니다.

RenderTexture와 RenderSurface는 target,list,clear,R,G,B,A를 받고 Viewport 메서드는 x,y,width,height를 추가합니다. 지우기는 대상 전체에 적용됩니다. 이전 내용을 유지하려면 텍스처를 초기화해야 하고 창 표면은 지우기가 필수입니다. 교환 이미지가 없으면 성공해도 표시하지 않습니다. 소유 스레드에서 각 리소스를 장치보다 먼저 닫고 Clear로 목록 참조를 해제합니다. 닫힌 리소스, 다른 장치 및 현재 대상 읽기를 거부합니다. 기본 한도는 텍스처/정렬 전송 256 MiB, 기하 64 MiB, 명령과 클립 깊이 65,536입니다. 동기 읽기는 C++ API를 사용합니다. 스크립트는 `Device.ReadTextureAsync` 요청을 폴링하고 성공한 뒤에만 픽셀을 가져옵니다.

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

SDL 창 표면은 일관된 선형 혼합을 위해 SDR_LINEAR 합성을 요구하며 지원하지 않으면 생성이 실패합니다.

## 설치한 네이티브 SDK 사용

```cmake
find_package(VeroScriptGraphics 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Graphics)
```

<a id="vertex-color-mode"></a>

SetVertexColorMode는 Blend와 독립적으로 VertexColorMode::Straight(기본값) 또는 Premultiplied를 선택합니다. 미리 곱한 정점 RGB는 보간 중에도 그 형식을 유지합니다. Tint는 항상 비사전곱이며 PremultipliedAlpha 출력에서 alpha를 RGB에 한 번만 적용합니다. AddGeometry는 이 상태를 저장하고 Clear는 Straight를 복원합니다. 정점 색은 계속 선형 값이어야 하며 이 설정은 sRGB를 디코딩하지 않습니다.

## 합성과 수동 장치 복구

Composition2D는 AddDraw, BeginLayer/EndLayer, PushMask/PopMask, AddBlur, AddColorMatrix, AddShadow, AddBackdropBlur를 기록합니다. Device.CreateCompositor2D()는 임시 대상을 재사용하며 CreateCompositor2DWithLimits(bytes,depth)로 예산을 지정합니다. 기본값은 256 MiB와 32단계이고 기록 한도는 64단계 및 65,536개 명령/그리기입니다. 그룹 불투명도는 합성 후 한 번만 적용하고 중첩 및 역 alpha 마스크는 교차합니다. 흐림은 반경 0..64픽셀, 13회 샘플링의 분리 가능한 두 패스입니다. 그림자는 원본을 보존하고 배경 흐림은 마스크 안만 대체합니다. 색상 행렬은 행 우선 스무 값입니다. 합성 픽셀은 선형 사전 곱 RGBA이며 합성기가 비사전 곱 ClearColor를 변환합니다. 기존 내용 로드 및 Opaque 그리기는 호환되는 사전 곱 픽셀이 필요합니다. sRGB 변환은 텍스처 형식에 따릅니다. 범위 오류나 예산 초과는 최종 대상을 보존합니다. Trim으로 임시 대상을 해제하고 장치보다 먼저 합성기를 닫습니다. DrawList2D.SetAlphaMask, SetBlur, SetColorMatrix, ResetEffect는 단일 패스 효과이고 Additive는 사전 곱 채널을 더합니다. 표면 메서드는 composition 앞에 현재 픽셀 width/height를 받고 Viewport 버전은 끝에 x,y,width,height를 추가합니다. 표시 실패는 false이며 GetPooledBytes는 int64입니다. Device.GetGeneration과 SupportsDeviceRecovery는 수동 복구를 나타냅니다. Service.RecoverDevice(previous,driver)는 교체 장치를 먼저 생성하고 성공한 뒤에만 이전 세대를 무효화합니다. 실패하면 이전 세대를 변경하지 않습니다. 성공 후 자원을 다시 생성/업로드하고 이전 표면을 닫은 뒤 창을 다시 사용하며 이전 자원과 장치를 닫습니다. Invalidate 이후에도 자원의 Close는 유효합니다. 선택한 드라이버는 Device.Poll로 장치 손실을 보고할 수 있습니다. 복구와 자원 복원은 호스트가 명시적으로 수행합니다. 이 API에는 Renderer3D, 임의 사용자 셰이더, 호스트 명령 기록이 포함되지 않습니다.

합성기의 임시 텍스처는 기본적으로 RGBA8 sRGB로 저장하고 샘플링과 혼합은 선형 공간에서 수행합니다. 선형 8비트 중간 이미지에서 sRGB 암부 색상이 사라지는 것을 방지합니다. C++에서는 선형 양자화를 수반하는 RGBA8 UNORM을 명시적으로 선택할 수 있으며 두 스크립트 생성 메서드는 sRGB 기본값을 사용합니다. ColorMatrix와 AlphaMask는 음수 및 1보다 큰 UV에서도 Clamp/Repeat/Mirror를 따릅니다. Blur와 Shadow는 소스 사각형 바깥을 투명하게 처리하며 반경은 소스 픽셀 단위입니다.

## 그래픽 API와 비동기 요청

[VeroScriptSdl3 — 기반 구현 — 그래픽 API와 비동기 요청](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/ko-KR/index.md#sdl-graphics-drivers)을 참조하세요.

<a id="asynchronous-requests"></a>

CreateDeviceAsync/RecoverDeviceAsync는 DeviceRequest, Device.ReadTextureAsync는 ReadbackRequest를 반환합니다. 소유 스레드에서 Poll 결과 Pending / Succeeded / Failed / Cancelled를 확인하고 성공 후 GetDevice/GetPixels를 호출합니다. 스크립트 GetError로 진단을 읽습니다. 기본 구현은 동기 완료될 수 있습니다. 브라우저 WebGPU의 동기 생성·읽기·WaitIdle은 graphics.async_required를 반환하므로 폴링 사이에 이벤트 루프로 제어를 돌려주어야 합니다.

<a id="resource-retirement"></a>

복구 성공 후 이전 세대를 무효화하고 리소스를 다시 만듭니다. Cancel은 결과를 버리지만 네이티브 작업은 계속될 수 있습니다. 리소스·장치·요청을 닫고 참조를 해제한 뒤 소유 스레드에서 Service.HasPendingRetirements()가 false가 될 때까지 정리합니다. 브라우저에서는 프레임 사이에 기다립니다. Platform 소유 서비스를 정리 완료까지 유지합니다.

<a id="graphics-capabilities"></a>

기능은 선택한 장치/드라이버에 속합니다. C++는 `IDevice::GetCapabilities()`를, 스크립트는 `SupportsGuiBasic`, `SupportsTransforms`, `SupportsClipMasks`, `SupportsFilters`, `SupportsDeviceRecovery`를 조회합니다. 교체 요청 전에 복구 지원을 확인하고 실패를 처리하세요. 공급자 이름만으로는 충분하지 않습니다. SDL3 제한은 [UIKit 장치 복구](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/ko-KR/index.md#uikit-device-recovery)를 참조하세요.

스크립트 정점의 여덟 double은 유한 float 범위에 들어야 하며 int64 인덱스는 네이티브 uint32 변환 전에 확인합니다. `UpdateTexture`는 요청 영역의 너비 × 높이 × 4 패킹된 RGBA 바이트를 소비하고 인코딩을 보존합니다. `ResetTexture`는 단색 그리기로 돌아갑니다. 그리기 목록 변경과 렌더링은 소유 스레드에서 수행하며 동시에 실행하면 안 됩니다. 네이티브 할당 예외는 C++로 전파될 수 있고 스크립트 어댑터는 콜백 실패를 진단으로 바꿉니다. 읽기는 저장된 RGBA 바이트를 반환하고 실패 시 제공한 출력을 보존합니다. 블로킹 네이티브 읽기/WaitIdle은 일반 프레임 루프 밖에서 사용하세요.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
