// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGraphicsBindingState.h"

namespace VeroScript::Graphics::BindingDetail
{
	namespace
	{
		bool Scalar(double Value, float Minimum, float Maximum, float& Out, std::string& Error)
		{
			if (!std::isfinite(Value) || Value < Minimum || Value > Maximum)
			{
				Error = "graphics.invalid_effect: numeric parameter is nonfinite or outside its range";
				return false;
			}
			Out = static_cast<float>(Value);
			return true;
		}
		bool Matrix(const FValue& Value, FEffect2D& Effect, std::string& Error)
		{
			const auto& Values = Array(Value);
			if (Values.ElementCount() != 20)
			{
				Error = "graphics.invalid_effect: expected twenty row-major color matrix values";
				return false;
			}
			Effect.Type = EEffect2D::ColorMatrix;
			for (unsigned I = 0; I < 20; ++I)
			{
				if (!Scalar(Element<double>(Values, I), -1000000, 1000000, Effect.Matrix[I], Error))
				{
					return false;
				}
			}
			return true;
		}
		bool Color(const FNativeCallContext& C, unsigned Start, FColor& Out, std::string& Error)
		{
			float* Fields[]{&Out.R, &Out.G, &Out.B, &Out.A};
			for (unsigned I = 0; I < 4; ++I)
			{
				if (!Scalar(std::get<double>(C.Args[Start + I]), 0, 1, *Fields[I], Error))
				{
					return false;
				}
			}
			return true;
		}
	}
	bool RegisterCompositionBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FState>& State,
									 std::string& Error)
	{
		const FRegistrar Add(Registry, State, Error);
		const FRegistrar AddConst(Registry, State, Error, true);
		const auto Method = EHostMemberKind::Method;
		if (!AddConst(1, "GetGeneration", Method, ENativeType::UInt64, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<std::shared_ptr<IDevice>>(C)->GetGeneration();
					 return true;
				 }) ||
			!Add(1, "Invalidate", Method, ENativeType::Void, {},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IDevice>>(C)->Invalidate(E);
				 }) ||
			!AddConst(1, "SupportsDeviceRecovery", Method, ENativeType::Bool, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<std::shared_ptr<IDevice>>(C)->GetCapabilities().bDeviceRecovery;
					 return true;
				 }) ||
			!Add(0, "RecoverDevice", Method, ENativeType::HostObject, {ENativeType::HostObject, ENativeType::String},
				 [State](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 const auto* Previous = Get<std::shared_ptr<IDevice>>(C.Args[0], E);
					 if (!Previous)
					 {
						 return false;
					 }
					 FDeviceOptions Options;
					 Options.Driver = std::get<std::string>(C.Args[1]);
					 auto Replacement = This<std::shared_ptr<IService>>(C)->RecoverDevice(*Previous, Options, E);
					 if (!Replacement)
					 {
						 return false;
					 }
					 Out = Make(State, 1, std::move(Replacement));
					 return true;
				 },
				 Names[1], {Names[1], ""}))
		{
			return false;
		}
		if (!Add(
				8, "Create", EHostMemberKind::StaticMethod, ENativeType::HostObject, {},
				[State](const FNativeCallContext&, FValue& Out, std::string&)
				{
					Out = Make(State, 8, std::make_shared<FComposition2D>());
					return true;
				},
				Names[8]))
		{
			return false;
		}
		for (const bool bLimits : {false, true})
		{
			if (!Add(
					1, bLimits ? "CreateCompositor2DWithLimits" : "CreateCompositor2D", Method, ENativeType::HostObject,
					bLimits ? std::vector<ENativeType>{ENativeType::Int64, ENativeType::Int64}
							: std::vector<ENativeType>{},
					[State, bLimits](const FNativeCallContext& C, FValue& Out, std::string& E)
					{
						FCompositorOptions2D Options;
						if (bLimits)
						{
							const auto Bytes = std::get<std::int64_t>(C.Args[0]);
							if (Bytes <= 0)
							{
								E = "graphics.invalid_compositor: pool budget must be positive";
								return false;
							}
							Options.MaxPooledBytes = static_cast<std::uint64_t>(Bytes);
							Options.MaxDepth = std::get<std::int64_t>(C.Args[1]);
						}
						auto Compositor = CreateCompositor2D(This<std::shared_ptr<IDevice>>(C), Options, E);
						if (!Compositor)
						{
							return false;
						}
						Out = Make(State, 9, std::move(Compositor));
						return true;
					},
					Names[9]))
			{
				return false;
			}
		}
		for (unsigned Operation = 0; Operation < 6; ++Operation)
		{
			const char* Methods[]{"Clear", "EndLayer", "PopMask", "Close", "Trim", "ResetEffect"};
			if (!Add(Operation < 3	 ? 8
					 : Operation < 5 ? 9
									 : 7,
					 Methods[Operation], Method, ENativeType::Void, {},
					 [Operation](const FNativeCallContext& C, FValue&, std::string& E)
					 {
						 if (Operation == 0)
						 {
							 This<std::shared_ptr<FComposition2D>>(C)->Clear();
							 return true;
						 }
						 if (Operation == 1)
						 {
							 return This<std::shared_ptr<FComposition2D>>(C)->EndLayer(E);
						 }
						 if (Operation == 2)
						 {
							 return This<std::shared_ptr<FComposition2D>>(C)->PopMask(E);
						 }
						 if (Operation == 3)
						 {
							 return This<std::shared_ptr<ICompositor2D>>(C)->Close(E);
						 }
						 if (Operation == 4)
						 {
							 return This<std::shared_ptr<ICompositor2D>>(C)->Trim(E);
						 }
						 return This<std::shared_ptr<FDrawList2D>>(C)->SetEffect({}, E);
					 }))
			{
				return false;
			}
		}
		if (!AddConst(9, "GetPooledBytes", Method, ENativeType::Int64, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<std::shared_ptr<ICompositor2D>>(C)->GetPooledBytes();
					 return true;
				 }) ||
			!Add(8, "BeginLayer", Method, ENativeType::Void, {ENativeType::Double},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 float Opacity;
					 return Scalar(std::get<double>(C.Args[0]), 0, 1, Opacity, E) &&
							This<std::shared_ptr<FComposition2D>>(C)->BeginLayer(Opacity, E);
				 }))
		{
			return false;
		}
		for (unsigned Operation = 0; Operation < 3; ++Operation)
		{
			const char* Methods[]{"AddDraw", "PushMask", "AddBackdropBlur"};
			std::vector<ENativeType> Params{ENativeType::HostObject};
			if (Operation)
			{
				Params.push_back(Operation == 1 ? ENativeType::Bool : ENativeType::Double);
			}
			if (!Add(8, Methods[Operation], Method, ENativeType::Void, Params,
					 [Operation](const FNativeCallContext& C, FValue&, std::string& E)
					 {
						 const auto* Draws = Get<std::shared_ptr<FDrawList2D>>(C.Args[0], E);
						 if (!Draws)
						 {
							 return false;
						 }
						 const auto& Frame = This<std::shared_ptr<FComposition2D>>(C);
						 if (Operation == 0)
						 {
							 return Frame->AddDraw(**Draws, E);
						 }
						 if (Operation == 1)
						 {
							 return Frame->PushMask(**Draws, std::get<bool>(C.Args[1]), E);
						 }
						 float Radius;
						 return Scalar(std::get<double>(C.Args[1]), 0, 64, Radius, E) &&
								Frame->AddBackdropBlur(**Draws, Radius, E);
					 },
					 "", {Names[7]}))
			{
				return false;
			}
		}
		if (!Add(8, "AddBlur", Method, ENativeType::Void, {ENativeType::Double},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 float Radius;
					 return Scalar(std::get<double>(C.Args[0]), 0, 64, Radius, E) &&
							This<std::shared_ptr<FComposition2D>>(C)->AddBlur(Radius, E);
				 }) ||
			!Add(8, "AddShadow", Method, ENativeType::Void, std::vector<ENativeType>(7, ENativeType::Double),
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 FEffect2D Effect;
					 Effect.Type = EEffect2D::Shadow;
					 return Scalar(std::get<double>(C.Args[0]), 0, 64, Effect.Radius, E) &&
							Scalar(std::get<double>(C.Args[1]), -1000000, 1000000, Effect.OffsetX, E) &&
							Scalar(std::get<double>(C.Args[2]), -1000000, 1000000, Effect.OffsetY, E) &&
							Color(C, 3, Effect.Color, E) &&
							This<std::shared_ptr<FComposition2D>>(C)->AddEffect(Effect, E);
				 }))
		{
			return false;
		}
		for (const bool bComposition : {false, true})
		{
			if (!Add(bComposition ? 8 : 7, bComposition ? "AddColorMatrix" : "SetColorMatrix", Method,
					 ENativeType::Void,
					 bComposition ? std::vector<ENativeType>{ENativeType::Array}
								  : std::vector<ENativeType>{ENativeType::Array, ENativeType::Bool},
					 [bComposition](const FNativeCallContext& C, FValue&, std::string& E)
					 {
						 FEffect2D Effect;
						 if (!Matrix(C.Args[0], Effect, E))
						 {
							 return false;
						 }
						 if (bComposition)
						 {
							 return This<std::shared_ptr<FComposition2D>>(C)->AddEffect(Effect, E);
						 }
						 Effect.bSourcePremultiplied = std::get<bool>(C.Args[1]);
						 return This<std::shared_ptr<FDrawList2D>>(C)->SetEffect(Effect, E);
					 },
					 "", {}, {"double"}))
			{
				return false;
			}
		}
		if (!Add(7, "SetAlphaMask", Method, ENativeType::Void,
				 {ENativeType::HostObject, ENativeType::Bool, ENativeType::Bool},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto* Mask = Get<std::shared_ptr<ITexture>>(C.Args[0], E);
					 if (!Mask)
					 {
						 return false;
					 }
					 FEffect2D Effect;
					 Effect.Type = EEffect2D::AlphaMask;
					 Effect.Mask = *Mask;
					 Effect.bInvert = std::get<bool>(C.Args[1]);
					 Effect.bSourcePremultiplied = std::get<bool>(C.Args[2]);
					 return This<std::shared_ptr<FDrawList2D>>(C)->SetEffect(Effect, E);
				 },
				 "", {Names[2], "", ""}) ||
			!Add(7, "SetBlur", Method, ENativeType::Void, {ENativeType::Double, ENativeType::Bool, ENativeType::Bool},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 FEffect2D Effect;
					 Effect.Type = EEffect2D::Blur;
					 Effect.bVertical = std::get<bool>(C.Args[1]);
					 Effect.bSourcePremultiplied = std::get<bool>(C.Args[2]);
					 return Scalar(std::get<double>(C.Args[0]), 0, 64, Effect.Radius, E) &&
							This<std::shared_ptr<FDrawList2D>>(C)->SetEffect(Effect, E);
				 }))
		{
			return false;
		}
		for (const bool bSurface : {false, true})
		{
			for (const bool bViewport : {false, true})
			{
				std::vector<ENativeType> Params{ENativeType::HostObject};
				if (bSurface)
				{
					Params.insert(Params.end(), 2, ENativeType::Int64);
				}
				const unsigned FrameIndex = bSurface ? 3 : 1;
				Params.push_back(ENativeType::HostObject);
				Params.push_back(ENativeType::Bool);
				Params.insert(Params.end(), 4, ENativeType::Double);
				if (bViewport)
				{
					Params.insert(Params.end(), 4, ENativeType::Int64);
				}
				std::vector<std::string> Hosts(Params.size());
				Hosts[0] = bSurface ? Names[3] : Names[2];
				Hosts[FrameIndex] = Names[8];
				if (!Add(
						9,
						bSurface ? (bViewport ? "RenderSurfaceViewport" : "RenderSurface")
								 : (bViewport ? "RenderTextureViewport" : "RenderTexture"),
						Method, bSurface ? ENativeType::Bool : ENativeType::Void, Params,
						[bSurface, bViewport, FrameIndex](const FNativeCallContext& C, FValue& Out, std::string& E)
						{
							const auto* Frame = Get<std::shared_ptr<FComposition2D>>(C.Args[FrameIndex], E);
							if (!Frame)
							{
								return false;
							}
							FRenderOptions2D Options;
							Options.bClear = std::get<bool>(C.Args[FrameIndex + 1]);
							if (!Color(C, FrameIndex + 2, Options.ClearColor, E))
							{
								return false;
							}
							if (bViewport)
							{
								const auto I = FrameIndex + 6;
								Options.bViewport = true;
								Options.Viewport = {
									std::get<std::int64_t>(C.Args[I]), std::get<std::int64_t>(C.Args[I + 1]),
									std::get<std::int64_t>(C.Args[I + 2]), std::get<std::int64_t>(C.Args[I + 3])};
							}
							const auto& Compositor = This<std::shared_ptr<ICompositor2D>>(C);
							if (!bSurface)
							{
								const auto* Target = Get<std::shared_ptr<ITexture>>(C.Args[0], E);
								return Target && Compositor->RenderTexture(*Target, **Frame, Options, E);
							}
							const auto* Target = Get<std::shared_ptr<ISurface>>(C.Args[0], E);
							bool bPresented = false;
							if (!Target || !Compositor->RenderSurface(*Target, std::get<std::int64_t>(C.Args[1]),
																	  std::get<std::int64_t>(C.Args[2]), **Frame,
																	  Options, bPresented, E))
							{
								return false;
							}
							Out = bPresented;
							return true;
						},
						"", Hosts))
				{
					return false;
				}
			}
		}
		return true;
	}
}
