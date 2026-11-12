// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiPaintBindings.h"

namespace VeroScript::Gui::ScriptBindings
{
	bool RegisterPaintBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
	    std::string& Error)
	{
		const auto Double = FNativeTypeDesc::Scalar(ENativeType::Double);
		const auto Void = FNativeTypeDesc::Scalar(ENativeType::Void);
		const auto Color = FNativeTypeDesc::Host(TypeNames[3]);
		const auto String = FNativeTypeDesc::Scalar(ENativeType::String);
		if (!Member(
		        Registry, Bridge->Types[0], "DisablePaint", EHostMemberKind::Method, Void, {},
		        [](const auto& Call, auto&, auto& Failure)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			return Widget && Widget->SetPaintCallback({}, Failure);
		},
		        Error))
		{
			return false;
		}
		const auto Register =
		    [&](const char* Name, std::vector<FNativeTypeDesc> Parameters, FCall Callback, FNativeTypeDesc Result)
		{
			return Member(
			    Registry, Bridge->Types[9], Name, EHostMemberKind::Method, Result, std::move(Parameters),
			    [Callback = std::move(Callback)](const auto& Call, auto& Out, auto& Failure)
			{
				const auto& Canvas = *static_cast<const std::shared_ptr<FScriptPaint>*>(Call.HostThis);
				if (!Canvas->Active)
				{
					Failure = "gui.paint_context_expired";
					return false;
				}
				return Callback(Call, Out, Failure);
			},
			    Error);
		};
		const auto Canvas = [](const auto& Call) -> FPaintContext&
		{
			return *(*static_cast<const std::shared_ptr<FScriptPaint>*>(Call.HostThis))->Active;
		};
		const auto Rect = [](const auto& Call)
		{
			return FRect{std::get<double>(Call.Args[1]), std::get<double>(Call.Args[2]), std::get<double>(Call.Args[3]),
			    std::get<double>(Call.Args[4])};
		};
		return Register(
		           "FillRect", {Double, Double, Double, Double, Color}, [Canvas, Rect](const auto& C, auto&, auto& E)
		{
			return Canvas(C).FillRect(Rect(C), Payload<Graphics::FColor>(C.Args[5]), E);
		}, Void) &&
		    Register("DrawLine", {Double, Double, Double, Double, Double, Color}, [Canvas](const auto& C, auto&, auto& E)
		{
			return Canvas(C).DrawLine(std::get<double>(C.Args[1]), std::get<double>(C.Args[2]),
			    std::get<double>(C.Args[3]), std::get<double>(C.Args[4]),
			    std::get<double>(C.Args[5]), Payload<Graphics::FColor>(C.Args[6]), E);
		}, Void) &&
		    Register("DrawImage", {Double, Double, Double, Double, String, Color}, [Bridge, Canvas, Rect](const auto& C, auto&, auto& E)
		{
			const auto& Reference = std::get<std::string>(C.Args[5]);
			std::string Identity;
			const auto From = C.ScriptInvoker ? C.ScriptInvoker->GetCallingModulePath(0) : std::string{};
			const auto& Resolve = Bridge->Services.ResolveResourceReference;
			if (!(Resolve ? Resolve(From, Reference, Identity, E)
			              : ResolveGuiResourceReference(From, Reference, Identity, E)))
			{
				return false;
			}
			return Canvas(C).DrawImage(Rect(C), std::move(Identity), Payload<Graphics::FColor>(C.Args[6]),
			    E);
		}, Void) &&
		    Register("PushClip", {Double, Double, Double, Double}, [Canvas, Rect](const auto& C, auto&, auto& E)
		{
			return Canvas(C).PushClip(Rect(C), E);
		}, Void) &&
		    Register("PopClip", {}, [Canvas](const auto& C, auto&, auto& E)
		{
			return Canvas(C).PopClip(E);
		}, Void) &&
		    Register("GetWidth", {}, [Canvas](const auto& C, auto& Out, auto&)
		{
			Out = Canvas(C).GetBounds().Width;
			return true;
		}, Double) &&
		    Register("GetHeight", {}, [Canvas](const auto& C, auto& Out, auto&)
		{
			Out = Canvas(C).GetBounds().Height;
			return true;
		}, Double);
	}
} // namespace VeroScript::Gui::ScriptBindings
