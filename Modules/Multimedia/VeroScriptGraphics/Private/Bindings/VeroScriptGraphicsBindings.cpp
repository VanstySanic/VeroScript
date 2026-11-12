// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGraphicsBindings.h"
#include "VeroScriptGraphicsBindingState.h"
#include "VeroScriptGraphics2D.h"
#include <limits>
#include <stdexcept>
#include "VeroScriptPlatformBindings.h"
#include <array>
#include <cmath>
#include <exception>

namespace VeroScript::Graphics
{
	using namespace BindingDetail;

	bool RegisterScriptBindings(FHostBindingRegistry& Registry, FServiceFactory Factory, std::string& Error)
	{
		if (!Factory)
		{
			Error = "graphics.missing_factory: host must provide a backend resolver";
			return false;
		}
		bool bPlatformAvailable = false;
		for (const auto& Type : Registry.GetTypes())
		{
			if (Type.Name == "VS.Platform.Window")
			{
				bPlatformAvailable = true;
				break;
			}
		}
		if (!bPlatformAvailable)
		{
			Error = "graphics.missing_platform_bindings: register Platform bindings first";
			return false;
		}
		FHostBindingRegistry Candidate = Registry;
		auto State = std::make_shared<FState>();
		State->Factory = std::move(Factory);
		for (unsigned Index = 0; Index < Names.size(); ++Index)
		{
			if (!Candidate.RegisterType(Names[Index], EHostTypeKind::Class, State->Ids[Index], Error))
			{
				return false;
			}
		}
		const FRegistrar Add(Candidate, State, Error);
		const FRegistrar AddConst(Candidate, State, Error, true);
		const auto Static = EHostMemberKind::StaticMethod, Method = EHostMemberKind::Method;
		if (!Add(
				0, "Create", Static, ENativeType::HostObject, {ENativeType::String},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					auto Service = State->Factory(std::get<std::string>(C.Args[0]), E);
					if (!Service)
					{
						return false;
					}
					Out = Make(State, 0, std::move(Service));
					return true;
				},
				Names[0]))
		{
			return false;
		}
		if (!Add(
				0, "CreateDevice", Method, ENativeType::HostObject, {ENativeType::String},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					FDeviceOptions Options;
					Options.Driver = std::get<std::string>(C.Args[0]);
					auto Device = This<std::shared_ptr<IService>>(C)->CreateDevice(Options, E);
					if (!Device)
					{
						return false;
					}
					Out = Make(State, 1, std::move(Device));
					return true;
				},
				Names[1]))
		{
			return false;
		}
		if (!Add(
				1, "CreateTexture", Method, ENativeType::HostObject, {ENativeType::Int64, ENativeType::Int64},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					FTextureOptions Options;
					Options.Width = std::get<std::int64_t>(C.Args[0]);
					Options.Height = std::get<std::int64_t>(C.Args[1]);
					auto Texture = This<std::shared_ptr<IDevice>>(C)->CreateTexture(Options, E);
					if (!Texture)
					{
						return false;
					}
					Out = Make(State, 2, std::move(Texture));
					return true;
				},
				Names[2]))
		{
			return false;
		}
		if (!Add(1, "CreateSurface", Method, ENativeType::HostObject, {ENativeType::HostObject},
				 [State](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 auto Window = Platform::GetScriptWindow(C.Args[0], E);
					 if (!Window)
					 {
						 return false;
					 }
					 auto Surface = This<std::shared_ptr<IDevice>>(C)->CreateSurface(std::move(Window), E);
					 if (!Surface)
					 {
						 return false;
					 }
					 Out = Make(State, 3, std::move(Surface));
					 return true;
				 },
				 Names[3], {"VS.Platform.Window"}))
		{
			return false;
		}
		if (!Add(1, "ClearTexture", Method, ENativeType::Void,
				 {ENativeType::HostObject, ENativeType::Double, ENativeType::Double, ENativeType::Double,
				  ENativeType::Double},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto* Texture = Get<std::shared_ptr<ITexture>>(C.Args[0], E);
					 FColor Color;
					 return Texture && ReadColor(C, Color, E) &&
							This<std::shared_ptr<IDevice>>(C)->ClearTexture(*Texture, Color, E);
				 },
				 "", {Names[2], "", "", "", ""}))
		{
			return false;
		}
		if (!Add(1, "Present", Method, ENativeType::Bool,
				 {ENativeType::HostObject, ENativeType::Double, ENativeType::Double, ENativeType::Double,
				  ENativeType::Double},
				 [](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 const auto* Surface = Get<std::shared_ptr<ISurface>>(C.Args[0], E);
					 FColor Color;
					 bool bPresented = false;
					 if (!Surface || !ReadColor(C, Color, E) ||
						 !This<std::shared_ptr<IDevice>>(C)->Present(*Surface, Color, bPresented, E))
					 {
						 return false;
					 }
					 Out = bPresented;
					 return true;
				 },
				 "", {Names[3], "", "", "", ""}))
		{
			return false;
		}
		if (!Add(1, "WaitIdle", Method, ENativeType::Void, {},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IDevice>>(C)->WaitIdle(E);
				 }))
		{
			return false;
		}
		if (!AddConst(1, "GetDriver", Method, ENativeType::String, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<std::shared_ptr<IDevice>>(C)->GetDriver();
					 return true;
				 }))
		{
			return false;
		}
		for (unsigned Type = 1; Type < 4; ++Type)
		{
			if (!AddConst(Type, "IsOpen", Method, ENativeType::Bool, {},
					 [Type](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 if (Type == 1)
						 {
							 Out = This<std::shared_ptr<IDevice>>(C)->IsOpen();
						 }
						 else if (Type == 2)
						 {
							 Out = This<std::shared_ptr<ITexture>>(C)->IsOpen();
						 }
						 else
						 {
							 Out = This<std::shared_ptr<ISurface>>(C)->IsOpen();
						 }
						 return true;
					 }))
			{
				return false;
			}
			if (!Add(Type, "Close", Method, ENativeType::Void, {},
					 [Type](const FNativeCallContext& C, FValue&, std::string& E)
					 {
						 if (Type == 1)
						 {
							 return This<std::shared_ptr<IDevice>>(C)->Close(E);
						 }
						 if (Type == 2)
						 {
							 return This<std::shared_ptr<ITexture>>(C)->Close(E);
						 }
						 return This<std::shared_ptr<ISurface>>(C)->Close(E);
					 }))
			{
				return false;
			}
		}
		for (const bool bWidth : {true, false})
		{
			if (!AddConst(2, bWidth ? "GetWidth" : "GetHeight", Method, ENativeType::Int64, {},
					 [bWidth](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto Options = This<std::shared_ptr<ITexture>>(C)->GetOptions();
						 Out = bWidth ? Options.Width : Options.Height;
						 return true;
					 }))
			{
				return false;
			}
		}

		const char* CapabilityNames[] = {"SupportsGuiBasic", "SupportsTransforms", "SupportsClipMasks",
										 "SupportsFilters"};
		for (unsigned I = 0; I < 4; ++I)
		{
			if (!AddConst(1, CapabilityNames[I], Method, ENativeType::Bool, {},
					 [I](const FNativeCallContext& C, FValue& O, std::string&)
					 {
						 const auto Caps = This<std::shared_ptr<IDevice>>(C)->GetCapabilities();
						 O = I == 0	  ? Caps.bGuiBasic
							 : I == 1 ? Caps.bGuiTransform
							 : I == 2 ? Caps.bClipMasks
									  : Caps.bFilters;
						 return true;
					 }))
			{
				return false;
			}
		}
		const auto RegisterEnum = [&](const char* Name, const std::vector<const char*>& Values)
		{
			FHostTypeId Type;
			if (!Candidate.RegisterType(Name, EHostTypeKind::Enum, Type, Error) ||
				!Candidate.SetEnumScoped(Type, true, Error))
			{
				return false;
			}
			for (std::size_t I = 0; I < Values.size(); ++I)
			{
				FHostMemberDesc Member;
				Member.Name = Values[I];
				Member.Kind = EHostMemberKind::EnumMember;
				Member.EnumValue = static_cast<std::int64_t>(I);
				if (!Candidate.RegisterMember(Type, std::move(Member), Error))
				{
					return false;
				}
			}
			return true;
		};
		if (!RegisterEnum("VS.Graphics.Filter", {"Nearest", "Linear"}) ||
			!RegisterEnum("VS.Graphics.AddressMode", {"Clamp", "Repeat", "Mirror"}) ||
			!RegisterEnum("VS.Graphics.BlendMode", {"Opaque", "StraightAlpha", "PremultipliedAlpha", "Additive"}) ||
			!RegisterEnum("VS.Graphics.VertexColorMode", {"Straight", "Premultiplied"}))
		{
			return false;
		}
		if (!RegisterEnum("VS.Graphics.TextureFormat", {"Rgba8Unorm", "Rgba8Srgb"}))
		{
			return false;
		}
		if (!Add(1, "CreateTextureWithFormat", Method, ENativeType::HostObject,
				 {ENativeType::Int64, ENativeType::Int64, ENativeType::Enum},
				 [State](const FNativeCallContext& C, FValue& O, std::string& E)
				 {
					 const auto Format = std::get<FEnumValue>(C.Args[2]).Value;
					 if (Format < 0 || Format > 1)
					 {
						 E = "graphics.invalid_texture: unknown format";
						 return false;
					 }
					 FTextureOptions Options;
					 Options.Width = std::get<std::int64_t>(C.Args[0]);
					 Options.Height = std::get<std::int64_t>(C.Args[1]);
					 Options.Format = static_cast<ETextureFormat>(Format);
					 auto Texture = This<std::shared_ptr<IDevice>>(C)->CreateTexture(Options, E);
					 if (!Texture)
					 {
						 return false;
					 }
					 O = Make(State, 2, std::move(Texture));
					 return true;
				 },
				 Names[2], {"", "", "VS.Graphics.TextureFormat"}))
		{
			return false;
		}
		if (!AddConst(
				2, "GetFormat", Method, ENativeType::Enum, {},
				[](const FNativeCallContext& C, FValue& O, std::string&)
				{
					O = FEnumValue{"VS.Graphics.TextureFormat",
								   static_cast<std::int64_t>(This<std::shared_ptr<ITexture>>(C)->GetOptions().Format)};
					return true;
				},
				"VS.Graphics.TextureFormat"))
		{
			return false;
		}
		if (!Add(
				1, "CreateRenderer2D", Method, ENativeType::HostObject, {},
				[State](const FNativeCallContext& C, FValue& O, std::string& E)
				{
					auto R = This<std::shared_ptr<IDevice>>(C)->CreateRenderer2D(E);
					if (!R)
					{
						return false;
					}
					O = Make(State, 5, std::move(R));
					return true;
				},
				Names[5]))
		{
			return false;
		}
		if (!Add(1, "CreateSampler", Method, ENativeType::HostObject,
				 {ENativeType::Enum, ENativeType::Enum, ENativeType::Enum},
				 [State](const FNativeCallContext& C, FValue& O, std::string& E)
				 {
					 const auto F = std::get<FEnumValue>(C.Args[0]).Value, U = std::get<FEnumValue>(C.Args[1]).Value,
								V = std::get<FEnumValue>(C.Args[2]).Value;
					 if (F < 0 || F > 1 || U < 0 || U > 2 || V < 0 || V > 2)
					 {
						 E = "graphics.invalid_sampler: unknown option";
						 return false;
					 }
					 auto R = This<std::shared_ptr<IDevice>>(C)->CreateSampler(
						 {static_cast<EFilter>(F), static_cast<EAddressMode>(U), static_cast<EAddressMode>(V)}, E);
					 if (!R)
					 {
						 return false;
					 }
					 O = Make(State, 4, std::move(R));
					 return true;
				 },
				 Names[4], {"VS.Graphics.Filter", "VS.Graphics.AddressMode", "VS.Graphics.AddressMode"}))
		{
			return false;
		}
		if (!Add(1, "UpdateTexture", Method, ENativeType::Void,
				 {ENativeType::HostObject, ENativeType::Int64, ENativeType::Int64, ENativeType::Int64,
				  ENativeType::Int64, ENativeType::Array},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto* Texture = Get<std::shared_ptr<ITexture>>(C.Args[0], E);
					 if (!Texture)
					 {
						 return false;
					 }
					 const auto& Bytes = Array(C.Args[5]);
					 if (Bytes.ElementCount() > 256 * 1024 * 1024)
					 {
						 E = "graphics.resource_limit: upload exceeds script byte limit";
						 return false;
					 }
					 if (const auto* Compact = std::get_if<std::vector<std::uint8_t>>(&Bytes.ScalarElements))
					 {
						 return This<std::shared_ptr<IDevice>>(C)->UpdateTexture(
							 *Texture, std::get<std::int64_t>(C.Args[1]), std::get<std::int64_t>(C.Args[2]),
							 std::get<std::int64_t>(C.Args[3]), std::get<std::int64_t>(C.Args[4]), *Compact, E);
					 }
					 std::vector<std::uint8_t> Copy;
					 Copy.reserve(Bytes.ElementCount());
					 for (std::size_t I = 0; I < Bytes.ElementCount(); ++I)
					 {
						 Copy.push_back(Element<std::uint8_t>(Bytes, I));
					 }
					 return This<std::shared_ptr<IDevice>>(C)->UpdateTexture(
						 *Texture, std::get<std::int64_t>(C.Args[1]), std::get<std::int64_t>(C.Args[2]),
						 std::get<std::int64_t>(C.Args[3]), std::get<std::int64_t>(C.Args[4]), Copy, E);
				 },
				 "", {Names[2], "", "", "", "", ""}, {"", "", "", "", "", "uint8"}))
		{
			return false;
		}
		if (!Add(5, "CreateGeometry", Method, ENativeType::HostObject, {ENativeType::Array, ENativeType::Array},
				 [State](const FNativeCallContext& C, FValue& O, std::string& E)
				 {
					 const auto& V = Array(C.Args[0]);
					 const auto& I = Array(C.Args[1]);
					 if (!V.ElementCount() || V.ElementCount() % 8 || V.ElementCount() > 64 * 1024 * 1024 / 4 ||
						 I.ElementCount() > 64 * 1024 * 1024 / 4)
					 {
						 E = "graphics.invalid_geometry: expected eight scalars per vertex within script limits";
						 return false;
					 }
					 std::vector<FVertex2D> Vertices(V.ElementCount() / 8);
					 std::vector<std::uint32_t> Indices;
					 Indices.reserve(I.ElementCount());
					 for (std::size_t N = 0; N < Vertices.size(); ++N)
					 {
						 float Values[8];
						 for (std::size_t K = 0; K < 8; ++K)
						 {
							 const double Value = Element<double>(V, N * 8 + K);
							 if (!std::isfinite(Value) || std::abs(Value) > std::numeric_limits<float>::max())
							 {
								 E = "graphics.invalid_geometry: scalar does not fit finite float";
								 return false;
							 }
							 Values[K] = static_cast<float>(Value);
						 }
						 Vertices[N] = {
							 Values[0], Values[1], Values[2], Values[3], {Values[4], Values[5], Values[6], Values[7]}};
					 }
					 for (std::size_t N = 0; N < I.ElementCount(); ++N)
					 {
						 const auto Value = Element<std::int64_t>(I, N);
						 if (Value < 0 || static_cast<std::uint64_t>(Value) > std::numeric_limits<std::uint32_t>::max())
						 {
							 E = "graphics.invalid_geometry: index does not fit uint32";
							 return false;
						 }
						 Indices.push_back(static_cast<std::uint32_t>(Value));
					 }
					 auto Geometry = This<std::shared_ptr<IRenderer2D>>(C)->CreateGeometry(Vertices, Indices, E);
					 if (!Geometry)
					 {
						 return false;
					 }
					 O = Make(State, 6, std::move(Geometry));
					 return true;
				 },
				 Names[6], {}, {"double", "int64"}))
		{
			return false;
		}
		if (!Add(
				7, "Create", Static, ENativeType::HostObject, {},
				[State](const FNativeCallContext&, FValue& O, std::string&)
				{
					O = Make(State, 7, std::make_shared<FDrawList2D>());
					return true;
				},
				Names[7]))
		{
			return false;
		}
		const auto RectParams = std::vector<ENativeType>(4, ENativeType::Int64);
		if (!Add(7, "PushClip", Method, ENativeType::Void, RectParams,
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<FDrawList2D>>(C)->PushClip(
						 {std::get<std::int64_t>(C.Args[0]), std::get<std::int64_t>(C.Args[1]),
						  std::get<std::int64_t>(C.Args[2]), std::get<std::int64_t>(C.Args[3])},
						 E);
				 }))
		{
			return false;
		}
		if (!Add(7, "PopClip", Method, ENativeType::Void, {},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<FDrawList2D>>(C)->PopClip(E);
				 }))
		{
			return false;
		}
		if (!Add(7, "Clear", Method, ENativeType::Void, {},
				 [](const FNativeCallContext& C, FValue&, std::string&)
				 {
					 This<std::shared_ptr<FDrawList2D>>(C)->Clear();
					 return true;
				 }))
		{
			return false;
		}
		if (!Add(7, "SetTransform", Method, ENativeType::Void, {ENativeType::Array},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto& Values = Array(C.Args[0]);
					 if (Values.ElementCount() != 16)
					 {
						 E = "graphics.invalid_transform: expected sixteen column-major values";
						 return false;
					 }
					 FTransform2D T;
					 for (std::size_t I = 0; I < 16; ++I)
					 {
						 const double V = Element<double>(Values, I);
						 if (!std::isfinite(V) || std::abs(V) > std::numeric_limits<float>::max())
						 {
							 E = "graphics.invalid_transform: value does not fit finite float";
							 return false;
						 }
						 T.Values[I] = static_cast<float>(V);
					 }
					 return This<std::shared_ptr<FDrawList2D>>(C)->SetTransform(T, E);
				 },
				 "", {}, {"double"}))
		{
			return false;
		}
		if (!Add(7, "SetTint", Method, ENativeType::Void, std::vector<ENativeType>(4, ENativeType::Double),
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 FColor Color;
					 float* Slots[] = {&Color.R, &Color.G, &Color.B, &Color.A};
					 for (unsigned I = 0; I < 4; ++I)
					 {
						 const double V = std::get<double>(C.Args[I]);
						 if (!std::isfinite(V) || V < 0 || V > 1)
						 {
							 E = "graphics.invalid_color: expected normalized components";
							 return false;
						 }
						 *Slots[I] = static_cast<float>(V);
					 }
					 return This<std::shared_ptr<FDrawList2D>>(C)->SetTint(Color, E);
				 }))
		{
			return false;
		}
		if (!Add(7, "SetBlendMode", Method, ENativeType::Void, {ENativeType::Enum},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto V = std::get<FEnumValue>(C.Args[0]).Value;
					 if (V < 0 || V > static_cast<std::int64_t>(EBlendMode::Additive))
					 {
						 E = "graphics.invalid_blend: unknown mode";
						 return false;
					 }
					 return This<std::shared_ptr<FDrawList2D>>(C)->SetBlendMode(static_cast<EBlendMode>(V), E);
				 },
				 "", {"VS.Graphics.BlendMode"}))
		{
			return false;
		}
		if (!Add(7, "SetVertexColorMode", Method, ENativeType::Void, {ENativeType::Enum},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto V = std::get<FEnumValue>(C.Args[0]).Value;
					 if (V < 0 || V > 1)
					 {
						 E = "graphics.invalid_vertex_color_mode: unknown input encoding";
						 return false;
					 }
					 return This<std::shared_ptr<FDrawList2D>>(C)->SetVertexColorMode(static_cast<EVertexColorMode>(V),
																					  E);
				 },
				 "", {"VS.Graphics.VertexColorMode"}))
		{
			return false;
		}
		if (!Add(7, "SetTexture", Method, ENativeType::Void, {ENativeType::HostObject, ENativeType::HostObject},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto* T = Get<std::shared_ptr<ITexture>>(C.Args[0], E);
					 const auto* S = Get<std::shared_ptr<ISampler>>(C.Args[1], E);
					 if (!T || !S)
					 {
						 return false;
					 }
					 This<std::shared_ptr<FDrawList2D>>(C)->SetTexture(*T, *S);
					 return true;
				 },
				 "", {Names[2], Names[4]}))
		{
			return false;
		}
		if (!Add(7, "ResetTexture", Method, ENativeType::Void, {},
				 [](const FNativeCallContext& C, FValue&, std::string&)
				 {
					 This<std::shared_ptr<FDrawList2D>>(C)->SetTexture({});
					 return true;
				 }))
		{
			return false;
		}
		if (!Add(7, "AddGeometry", Method, ENativeType::Void, {ENativeType::HostObject},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto* G = Get<std::shared_ptr<IGeometry2D>>(C.Args[0], E);
					 return G && This<std::shared_ptr<FDrawList2D>>(C)->AddGeometry(*G, E);
				 },
				 "", {Names[6]}))
		{
			return false;
		}
		for (bool bSurface : {false, true})
		{
			for (bool bViewport : {false, true})
			{
				std::vector<ENativeType> Params{ENativeType::HostObject, ENativeType::HostObject, ENativeType::Bool,
												ENativeType::Double,	 ENativeType::Double,	  ENativeType::Double,
												ENativeType::Double};
				if (bViewport)
				{
					Params.insert(Params.end(), 4, ENativeType::Int64);
				}
				std::vector<std::string> HostNames(Params.size());
				HostNames[0] = bSurface ? Names[3] : Names[2];
				HostNames[1] = Names[7];
				if (!Add(
						5,
						bSurface ? (bViewport ? "RenderSurfaceViewport" : "RenderSurface")
								 : (bViewport ? "RenderTextureViewport" : "RenderTexture"),
						Method, bSurface ? ENativeType::Bool : ENativeType::Void, Params,
						[bSurface, bViewport](const FNativeCallContext& C, FValue& O, std::string& E)
						{
							const auto* L = Get<std::shared_ptr<FDrawList2D>>(C.Args[1], E);
							if (!L)
							{
								return false;
							}
							FRenderOptions2D Options;
							Options.bClear = std::get<bool>(C.Args[2]);
							float* Slots[] = {&Options.ClearColor.R, &Options.ClearColor.G, &Options.ClearColor.B,
											  &Options.ClearColor.A};
							for (unsigned I = 0; I < 4; ++I)
							{
								const double V = std::get<double>(C.Args[I + 3]);
								if (!std::isfinite(V) || V < 0 || V > 1)
								{
									E = "graphics.invalid_color: expected normalized clear color";
									return false;
								}
								*Slots[I] = static_cast<float>(V);
							}
							if (bViewport)
							{
								Options.bViewport = true;
								Options.Viewport = {
									std::get<std::int64_t>(C.Args[7]), std::get<std::int64_t>(C.Args[8]),
									std::get<std::int64_t>(C.Args[9]), std::get<std::int64_t>(C.Args[10])};
							}
							const auto& R = This<std::shared_ptr<IRenderer2D>>(C);
							if (bSurface)
							{
								const auto* T = Get<std::shared_ptr<ISurface>>(C.Args[0], E);
								bool P = false;
								if (!T || !R->RenderSurface(*T, **L, Options, P, E))
								{
									return false;
								}
								O = P;
								return true;
							}
							const auto* T = Get<std::shared_ptr<ITexture>>(C.Args[0], E);
							return T && R->RenderTexture(*T, **L, Options, E);
						},
						"", HostNames))
				{
					return false;
				}
			}
		}
		for (unsigned Type : {4u, 5u, 6u})
		{
			if (!AddConst(Type, "IsOpen", Method, ENativeType::Bool, {},
					 [Type](const FNativeCallContext& C, FValue& O, std::string&)
					 {
						 O = Type == 4	 ? This<std::shared_ptr<ISampler>>(C)->IsOpen()
							 : Type == 5 ? This<std::shared_ptr<IRenderer2D>>(C)->IsOpen()
										 : This<std::shared_ptr<IGeometry2D>>(C)->IsOpen();
						 return true;
					 }))
			{
				return false;
			}
			if (!Add(Type, "Close", Method, ENativeType::Void, {},
					 [Type](const FNativeCallContext& C, FValue&, std::string& E)
					 {
						 return Type == 4	? This<std::shared_ptr<ISampler>>(C)->Close(E)
								: Type == 5 ? This<std::shared_ptr<IRenderer2D>>(C)->Close(E)
											: This<std::shared_ptr<IGeometry2D>>(C)->Close(E);
					 }))
			{
				return false;
			}
		}
		for (bool bVertices : {true, false})
		{
			if (!AddConst(6, bVertices ? "GetVertexCount" : "GetIndexCount", Method, ENativeType::Int64, {},
					 [bVertices](const FNativeCallContext& C, FValue& O, std::string&)
					 {
						 const auto& G = This<std::shared_ptr<IGeometry2D>>(C);
						 O = bVertices ? G->GetVertexCount() : G->GetIndexCount();
						 return true;
					 }))
			{
				return false;
			}
		}
		if (!RegisterCompositionBindings(Candidate, State, Error) || !RegisterAsyncBindings(Candidate, State, Error))
		{
			return false;
		}
		Registry = std::move(Candidate);
		Error.clear();
		return true;
	}
}
