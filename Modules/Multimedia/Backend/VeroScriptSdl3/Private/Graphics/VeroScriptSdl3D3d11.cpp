// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3GraphicsBackend.h"

#if defined(_WIN32) && VEROSCRIPT_SDL3_D3D11
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <chrono>

namespace VeroScript::Sdl3
{
	using namespace Graphics;
	using Microsoft::WRL::ComPtr;
	namespace
	{
		#include "../VeroScriptSdl3Shaders.inl"
		bool Result(HRESULT Code, const char* Operation, std::string& Error)
		{
			if (FAILED(Code))
			{
				Error = std::string("graphics.d3d11_failure: ") + Operation + " HRESULT=" + std::to_string(Code);
				return false;
			}
			Error.clear();
			return true;
		}
		struct FTexture final : ITextureData
		{
			ComPtr<ID3D11Texture2D> Texture;
			ComPtr<ID3D11RenderTargetView> Target;
			ComPtr<ID3D11ShaderResourceView> View;
			FTextureOptions Options;
		};
		struct FSampler final : ISamplerData
		{
			ComPtr<ID3D11SamplerState> Sampler;
		};
		struct FGeometry final : IGeometryData
		{
			ComPtr<ID3D11Buffer> Vertices, Indices;
			UINT Count = 0;
		};
		struct FSurface final : ISurfaceData
		{
			SDL_Window* Window = nullptr;
			ComPtr<IDXGISwapChain1> Chain;
			ComPtr<ID3D11RenderTargetView> Target;
			int Width = 0, Height = 0;
		};
		class FD3d11 final : public IGraphicsBackend
		{
		public:
			const char* GetDriver() const noexcept override
			{
				return bWarp ? "direct3d11-warp" : "direct3d11";
			}
			bool Poll(std::string& E) override
			{
				const auto Reason = Device->GetDeviceRemovedReason();
				if (FAILED(Reason))
				{
					bLost = true;
					E = "graphics.device_lost: D3D11 HRESULT=" + std::to_string(Reason);
					return false;
				}
				E.clear();
				return true;
			}
			bool IsLost() const noexcept override
			{
				return bLost;
			}
			std::unique_ptr<ITextureData> CreateTexture(const FTextureOptions& O, std::string& E) override
			{
				auto T = std::make_unique<FTexture>();
				T->Options = O;
				D3D11_TEXTURE2D_DESC D{};
				D.Width = static_cast<UINT>(O.Width);
				D.Height = static_cast<UINT>(O.Height);
				D.MipLevels = D.ArraySize = D.SampleDesc.Count = 1;
				D.Format = O.Format == ETextureFormat::Rgba8Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
																 : DXGI_FORMAT_R8G8B8A8_UNORM;
				D.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				if (!Result(Device->CreateTexture2D(&D, nullptr, &T->Texture), "create texture", E) ||
					!Result(Device->CreateRenderTargetView(T->Texture.Get(), nullptr, &T->Target), "create target",
							E) ||
					!Result(Device->CreateShaderResourceView(T->Texture.Get(), nullptr, &T->View), "create view", E))
				{
					return {};
				}
				return T;
			}
			std::unique_ptr<ISamplerData> CreateSampler(const FSamplerOptions& O, std::string& E) override
			{
				auto S = std::make_unique<FSampler>();
				D3D11_SAMPLER_DESC D{};
				D.Filter = O.Filter == EFilter::Nearest ? D3D11_FILTER_MIN_MAG_MIP_POINT
														: D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
				const auto Address = [](EAddressMode V)
				{
					return V == EAddressMode::Repeat   ? D3D11_TEXTURE_ADDRESS_WRAP
						   : V == EAddressMode::Mirror ? D3D11_TEXTURE_ADDRESS_MIRROR
													   : D3D11_TEXTURE_ADDRESS_CLAMP;
				};
				D.AddressU = Address(O.AddressU);
				D.AddressV = Address(O.AddressV);
				D.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
				D.MaxAnisotropy = 1;
				D.ComparisonFunc = D3D11_COMPARISON_NEVER;
				D.MaxLOD = D3D11_FLOAT32_MAX;
				if (!Result(Device->CreateSamplerState(&D, &S->Sampler), "create sampler", E))
				{
					return {};
				}
				return S;
			}
			std::unique_ptr<IGeometryData> CreateGeometry(const std::vector<FVertex2D>& V,
														  const std::vector<std::uint32_t>& I, std::string& E) override
			{
				auto G = std::make_unique<FGeometry>();
				G->Count = static_cast<UINT>(I.size());
				D3D11_BUFFER_DESC D{};
				D.Usage = D3D11_USAGE_IMMUTABLE;
				D.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				D.ByteWidth = static_cast<UINT>(V.size() * sizeof(FVertex2D));
				D3D11_SUBRESOURCE_DATA S{};
				S.pSysMem = V.data();
				if (!Result(Device->CreateBuffer(&D, &S, &G->Vertices), "create vertices", E))
				{
					return {};
				}
				D.BindFlags = D3D11_BIND_INDEX_BUFFER;
				D.ByteWidth = static_cast<UINT>(I.size() * sizeof(std::uint32_t));
				S.pSysMem = I.data();
				if (!Result(Device->CreateBuffer(&D, &S, &G->Indices), "create indices", E))
				{
					return {};
				}
				return G;
			}
			std::unique_ptr<ISurfaceData> CreateSurface(SDL_Window* W, std::string& E) override
			{
				const auto Hwnd = static_cast<HWND>(
					SDL_GetPointerProperty(SDL_GetWindowProperties(W), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
				if (!Hwnd)
				{
					E = "graphics.incompatible_window: D3D11 requires a Win32 window";
					return {};
				}
				ComPtr<IDXGIDevice> Dxgi;
				ComPtr<IDXGIAdapter> Adapter;
				ComPtr<IDXGIFactory2> Factory;
				if (!Result(Device.As(&Dxgi), "query DXGI device", E) ||
					!Result(Dxgi->GetAdapter(&Adapter), "query adapter", E) ||
					!Result(Adapter->GetParent(IID_PPV_ARGS(&Factory)), "query factory", E))
				{
					return {};
				}
				auto S = std::make_unique<FSurface>();
				S->Window = W;
				DXGI_SWAP_CHAIN_DESC1 D{};
				D.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				D.SampleDesc.Count = 1;
				D.BufferCount = 2;
				D.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				D.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				D.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
				if (!Result(Factory->CreateSwapChainForHwnd(Device.Get(), Hwnd, &D, nullptr, nullptr, &S->Chain),
							"create swapchain", E))
				{
					return {};
				}
				Factory->MakeWindowAssociation(Hwnd, DXGI_MWA_NO_ALT_ENTER);
				return S;
			}
			bool UpdateTexture(ITextureData& T, const FRect& R, const std::vector<std::uint8_t>& P,
							   std::string& E) override
			{
				D3D11_BOX Box{static_cast<UINT>(R.X),
							  static_cast<UINT>(R.Y),
							  0,
							  static_cast<UINT>(R.X + R.Width),
							  static_cast<UINT>(R.Y + R.Height),
							  1};
				Context->UpdateSubresource(static_cast<FTexture&>(T).Texture.Get(), 0, &Box, P.data(),
										   static_cast<UINT>(R.Width * 4), 0);
				return Result(Device->GetDeviceRemovedReason(), "update texture", E);
			}
			bool ReadTexture(ITextureData& T, std::vector<std::uint8_t>& R, std::string& E) override
			{
				auto& Source = static_cast<FTexture&>(T);
				D3D11_TEXTURE2D_DESC D{};
				Source.Texture->GetDesc(&D);
				D.BindFlags = 0;
				D.Usage = D3D11_USAGE_STAGING;
				D.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				ComPtr<ID3D11Texture2D> Staging;
				if (!Result(Device->CreateTexture2D(&D, nullptr, &Staging), "create readback", E))
				{
					return false;
				}
				Context->CopyResource(Staging.Get(), Source.Texture.Get());
				std::vector<std::uint8_t> Copy(static_cast<std::size_t>(D.Width) * D.Height * 4);
				D3D11_MAPPED_SUBRESOURCE M{};
				if (!Result(Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &M), "map readback", E))
				{
					return false;
				}
				for (UINT Y = 0; Y < D.Height; ++Y)
				{
					std::memcpy(Copy.data() + static_cast<std::size_t>(Y) * D.Width * 4,
								static_cast<const std::uint8_t*>(M.pData) + static_cast<std::size_t>(Y) * M.RowPitch,
								static_cast<std::size_t>(D.Width) * 4);
				}
				Context->Unmap(Staging.Get(), 0);
				R.swap(Copy);
				E.clear();
				return true;
			}
			bool Render(ITextureData* T, ISurfaceData* S, const FRenderOptions2D& O, const FRect& V,
						const std::vector<FBackendDraw>& Draws, bool& Presented, std::string& E) override
			{
				Presented = false;
				FSurface* Surface = S ? &static_cast<FSurface&>(*S) : nullptr;
				if (Surface)
				{
					int W = 0, H = 0;
					if (!SDL_GetWindowSizeInPixels(Surface->Window, &W, &H))
					{
						return SdlError("query D3D11 surface", E);
					}
					if (W <= 0 || H <= 0 || (SDL_GetWindowFlags(Surface->Window) & SDL_WINDOW_MINIMIZED))
					{
						E.clear();
						return true;
					}
					if (!Surface->Target || W != Surface->Width || H != Surface->Height)
					{
						Context->OMSetRenderTargets(0, nullptr, nullptr);
						Surface->Target.Reset();
						if (!Result(Surface->Chain->ResizeBuffers(0, W, H, DXGI_FORMAT_UNKNOWN, 0), "resize swapchain",
									E))
						{
							return false;
						}
						ComPtr<ID3D11Texture2D> Back;
						D3D11_RENDER_TARGET_VIEW_DESC D{};
						D.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
						D.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
						if (!Result(Surface->Chain->GetBuffer(0, IID_PPV_ARGS(&Back)), "get swapchain image", E) ||
							!Result(Device->CreateRenderTargetView(Back.Get(), &D, &Surface->Target),
									"create swapchain target", E))
						{
							return false;
						}
						Surface->Width = W;
						Surface->Height = H;
					}
				}
				auto* Target = Surface ? Surface->Target.Get() : static_cast<FTexture*>(T)->Target.Get();
				Context->OMSetRenderTargets(1, &Target, nullptr);
				if (O.bClear)
				{
					const float C[]{O.ClearColor.R, O.ClearColor.G, O.ClearColor.B, O.ClearColor.A};
					Context->ClearRenderTargetView(Target, C);
				}
				D3D11_VIEWPORT View{static_cast<float>(V.X),
									static_cast<float>(V.Y),
									static_cast<float>(V.Width),
									static_cast<float>(V.Height),
									0,
									1};
				Context->RSSetViewports(1, &View);
				Context->RSSetState(Raster.Get());
				Context->OMSetDepthStencilState(Depth.Get(), 0);
				Context->IASetInputLayout(Layout.Get());
				Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				Context->VSSetShader(Vertex.Get(), nullptr, 0);
				Context->PSSetShader(Fragment.Get(), nullptr, 0);
				ID3D11Buffer* VB = VertexUniform.Get();
				ID3D11Buffer* FB = FragmentUniform.Get();
				Context->VSSetConstantBuffers(0, 1, &VB);
				Context->PSSetConstantBuffers(0, 1, &FB);
				for (const auto& Draw : Draws)
				{
					const auto U = MakeVertexUniform(Draw, V);
					const auto F = MakeFragmentUniform(Draw);
					Context->UpdateSubresource(VB, 0, nullptr, &U, 0, 0);
					Context->UpdateSubresource(FB, 0, nullptr, &F, 0, 0);
					D3D11_RECT Clip{static_cast<LONG>(Draw.Clip.X), static_cast<LONG>(Draw.Clip.Y),
									static_cast<LONG>(Draw.Clip.X + Draw.Clip.Width),
									static_cast<LONG>(Draw.Clip.Y + Draw.Clip.Height)};
					Context->RSSetScissorRects(1, &Clip);
					Context->OMSetBlendState(Blends[static_cast<unsigned>(Draw.Command->Blend)].Get(), nullptr,
											 0xffffffff);
					const auto& G = static_cast<FGeometry&>(*Draw.Geometry);
					auto* Vert = G.Vertices.Get();
					const UINT Stride = sizeof(FVertex2D), Offset = 0;
					Context->IASetVertexBuffers(0, 1, &Vert, &Stride, &Offset);
					Context->IASetIndexBuffer(G.Indices.Get(), DXGI_FORMAT_R32_UINT, 0);
					ID3D11ShaderResourceView* Images[]{static_cast<FTexture*>(Draw.Texture)->View.Get(),
													   static_cast<FTexture*>(Draw.Mask)->View.Get()};
					ID3D11SamplerState* Samplers[]{static_cast<FSampler*>(Draw.Sampler)->Sampler.Get(),
												   static_cast<FSampler*>(Draw.Sampler)->Sampler.Get()};
					Context->PSSetShaderResources(0, 2, Images);
					Context->PSSetSamplers(0, 2, Samplers);
					Context->DrawIndexed(G.Count, 0, 0);
				}
				ID3D11ShaderResourceView* Empty[2]{};
				Context->PSSetShaderResources(0, 2, Empty);
				Context->OMSetRenderTargets(0, nullptr, nullptr);
				if (!Result(Device->GetDeviceRemovedReason(), "render", E))
				{
					return false;
				}
				if (Surface)
				{
					const auto Code = Surface->Chain->Present(1, DXGI_PRESENT_DO_NOT_WAIT);
					if (Code == DXGI_ERROR_WAS_STILL_DRAWING || Code == DXGI_STATUS_OCCLUDED)
					{
						E.clear();
						return true;
					}
					if (!Result(Code, "present", E))
					{
						return false;
					}
					Presented = true;
				}
				E.clear();
				return true;
			}
			bool WaitIdle(std::string& E) override
			{
				D3D11_QUERY_DESC D{D3D11_QUERY_EVENT, 0};
				ComPtr<ID3D11Query> Q;
				if (!Result(Device->CreateQuery(&D, &Q), "create idle query", E))
				{
					return false;
				}
				Context->End(Q.Get());
				Context->Flush();
				const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
				HRESULT Code = S_FALSE;
				while ((Code = Context->GetData(Q.Get(), nullptr, 0, 0)) == S_FALSE)
				{
					if (std::chrono::steady_clock::now() >= Deadline)
					{
						E = "graphics.timeout: D3D11 idle deadline exceeded";
						return false;
					}
					std::this_thread::yield();
				}
				return Result(Code, "wait idle", E);
			}
			bool Initialize(const FDeviceOptions& O, std::string& E)
			{
				bWarp = O.Driver == "direct3d11-warp";
				const D3D_FEATURE_LEVEL Level = D3D_FEATURE_LEVEL_11_0;
				if (!Result(D3D11CreateDevice(nullptr, bWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
											  O.bDebug ? D3D11_CREATE_DEVICE_DEBUG : 0, &Level, 1, D3D11_SDK_VERSION,
											  &Device, nullptr, &Context),
							"create device", E))
				{
					return false;
				}
				if (!Result(Device->CreateVertexShader(VertD3d11, sizeof(VertD3d11), nullptr, &Vertex),
							"create vertex shader", E) ||
					!Result(Device->CreatePixelShader(FragD3d11, sizeof(FragD3d11), nullptr, &Fragment),
							"create fragment shader", E))
				{
					return false;
				}
				const D3D11_INPUT_ELEMENT_DESC Inputs[]{
					{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
					{"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
					{"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}};
				if (!Result(Device->CreateInputLayout(Inputs, 3, VertD3d11, sizeof(VertD3d11), &Layout),
							"create input layout", E))
				{
					return false;
				}
				D3D11_BUFFER_DESC B{};
				B.Usage = D3D11_USAGE_DEFAULT;
				B.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				B.ByteWidth = sizeof(FVertexUniform2D);
				if (!Result(Device->CreateBuffer(&B, nullptr, &VertexUniform), "create vertex uniforms", E))
				{
					return false;
				}
				B.ByteWidth = sizeof(FFragmentUniform2D);
				if (!Result(Device->CreateBuffer(&B, nullptr, &FragmentUniform), "create fragment uniforms", E))
				{
					return false;
				}
				D3D11_RASTERIZER_DESC R{};
				R.FillMode = D3D11_FILL_SOLID;
				R.CullMode = D3D11_CULL_NONE;
				R.ScissorEnable = TRUE;
				R.DepthClipEnable = TRUE;
				if (!Result(Device->CreateRasterizerState(&R, &Raster), "create raster state", E))
				{
					return false;
				}
				D3D11_DEPTH_STENCIL_DESC Z{};
				Z.DepthFunc = D3D11_COMPARISON_ALWAYS;
				if (!Result(Device->CreateDepthStencilState(&Z, &Depth), "create depth state", E))
				{
					return false;
				}
				for (unsigned I = 0; I < 4; ++I)
				{
					D3D11_BLEND_DESC D{};
					auto& A = D.RenderTarget[0];
					A.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
					A.BlendEnable = I != 0;
					A.SrcBlend = I == 1 ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_ONE;
					A.DestBlend = I == 3 ? D3D11_BLEND_ONE : D3D11_BLEND_INV_SRC_ALPHA;
					A.BlendOp = D3D11_BLEND_OP_ADD;
					A.SrcBlendAlpha = D3D11_BLEND_ONE;
					A.DestBlendAlpha = A.DestBlend;
					A.BlendOpAlpha = D3D11_BLEND_OP_ADD;
					if (!Result(Device->CreateBlendState(&D, &Blends[I]), "create blend state", E))
					{
						return false;
					}
				}
				return true;
			}

		private:
			ComPtr<ID3D11Device> Device;
			ComPtr<ID3D11DeviceContext> Context;
			ComPtr<ID3D11VertexShader> Vertex;
			ComPtr<ID3D11PixelShader> Fragment;
			ComPtr<ID3D11InputLayout> Layout;
			ComPtr<ID3D11Buffer> VertexUniform, FragmentUniform;
			ComPtr<ID3D11RasterizerState> Raster;
			ComPtr<ID3D11DepthStencilState> Depth;
			std::array<ComPtr<ID3D11BlendState>, 4> Blends;
			bool bWarp = false, bLost = false;
		};
	}
	std::unique_ptr<IGraphicsBackend> CreateD3d11Backend(const FDeviceOptions& O, std::string& E)
	{
		auto Driver = std::make_unique<FD3d11>();
		if (!Driver->Initialize(O, E))
		{
			return {};
		}
		return Driver;
	}
}
#else
namespace VeroScript::Sdl3
{
	std::unique_ptr<IGraphicsBackend> CreateD3d11Backend(const Graphics::FDeviceOptions&, std::string& E)
	{
		E = "graphics.unsupported_driver: D3D11 is not enabled on this platform";
		return {};
	}
}
#endif
