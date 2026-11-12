// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiResourceBindings.h"
#include "VeroScriptGuiScriptLibrary.h"
#include "VeroScriptPath.h"
#include "VeroScriptResources.h"
#include "Tools/VeroScriptGuiArtifact.h"
#include <filesystem>

namespace VeroScript::Gui::ScriptBindings
{
	namespace
	{
		std::string CaptureRequester(const std::shared_ptr<FBridge>& Bridge, IScriptCallInvoker* Invoker)
		{
			std::string Requester;
			if (!Invoker)
			{
				return Requester;
			}
			const auto FrameworkDirectory = PathUtil::FromUtf8(GetGuiScriptLibraryPath()).parent_path();
			for (std::size_t Depth = 0; Depth < 4096; ++Depth)
			{
				const auto Module = Invoker->GetCallingModulePath(Depth);
				if (Module.empty())
				{
					break;
				}
				if (IsGuiScriptLibraryPath(Module) ||
					(Bridge->Services.IsFrameworkModule && Bridge->Services.IsFrameworkModule(Module)))
				{
					continue;
				}
				const auto Parent = PathUtil::FromUtf8(Module).parent_path();
				if (Parent == FrameworkDirectory || Parent == FrameworkDirectory / "Gui" ||
					Module == "veroscript.gui.framework" || Module.rfind("veroscript.gui.framework/", 0) == 0)
				{
					continue;
				}
				Requester = Module;
				break;
			}
			return Requester;
		}
	} // namespace

	bool RegisterResourceBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
								  std::string& Error)
	{
		const auto String = FNativeTypeDesc::Scalar(ENativeType::String);
		if (!Member(
				Registry, Bridge->Types[8], "ResolveStyleImages", EHostMemberKind::StaticMethod, String,
				{String, String},
				[Bridge](const auto& Call, auto& Out, auto& Failure)
				{
					std::string Result;
					const auto& Requester = std::get<std::string>(Call.Args[0]);
					const auto& Value = std::get<std::string>(Call.Args[1]);
					auto Resolve = Bridge->Services.ResolveResourceReference;
					if (!Resolve)
					{
						Resolve = ResolveGuiResourceReference;
					}
					if (!ResolveGuiStyleImages(Requester, Value, Resolve, Result, Failure))
					{
						return false;
					}
					Out = std::move(Result);
					return true;
				},
				Error))
		{
			return false;
		}
		if (!Member(
				Registry, Bridge->Types[8], "CaptureRequester", EHostMemberKind::StaticMethod, String, {},
				[Bridge](const auto& Call, auto& Out, auto&)
				{
					Out = CaptureRequester(Bridge, Call.ScriptInvoker);
					return true;
				},
				Error) ||
			!Member(
				Registry, Bridge->Types[8], "ResolveFrom", EHostMemberKind::StaticMethod, String, {String, String},
				[Bridge](const auto& Call, auto& Out, auto& Failure)
				{
					auto Requester = std::get<std::string>(Call.Args[0]);
					if (Requester.rfind(GuiSourceMarker, 0) == 0)
					{
						Requester.erase(0, std::char_traits<char>::length(GuiSourceMarker));
					}
					const auto& Reference = std::get<std::string>(Call.Args[1]);
					if (Reference.empty() || Reference.rfind(GuiBakedMarker, 0) == 0)
					{
						Out = Reference;
						return true;
					}
					std::string Identity;
					const auto& Resolve = Bridge->Services.ResolveResourceReference;
					if (!(Resolve ? Resolve(Requester, Reference, Identity, Failure)
								  : ResolveGuiResourceReference(Requester, Reference, Identity, Failure)))
					{
						return false;
					}
					if (Identity.empty() || Identity.find('\0') != std::string::npos)
					{
						Failure = "gui.invalid_resource_identity";
						return false;
					}
					Out = std::move(Identity);
					return true;
				},
				Error))
		{
			return false;
		}
		return Member(
			Registry, Bridge->Types[8], "Resolve", EHostMemberKind::StaticMethod, String, {String},
			[Bridge](const auto& Call, auto& Out, auto& Error)
			{
				if (!Call.ScriptInvoker)
				{
					Error = "gui.script_invoker_missing";
					return false;
				}
				auto Reference = std::get<std::string>(Call.Args[0]);
				if (Reference.rfind(GuiBakedMarker, 0) == 0)
				{
					Out = std::move(Reference);
					return true;
				}
				if (Reference.rfind(GuiModuleMarker, 0) == 0)
				{
					Reference.erase(0, std::char_traits<char>::length(GuiModuleMarker));
				}
				if (Reference.empty() || Reference.find('\0') != std::string::npos)
				{
					Error = "gui.invalid_resource_reference";
					return false;
				}
				const auto Requester = CaptureRequester(Bridge, Call.ScriptInvoker);
				std::string Identity;
				if (Bridge->Services.ResolveResourceReference)
				{
					if (!Bridge->Services.ResolveResourceReference(Requester, Reference, Identity, Error))
					{
						return false;
					}
				}
				else if (!ResolveGuiResourceReference(Requester, Reference, Identity, Error))
				{
					return false;
				}
				if (Identity.empty())
				{
					Error = "gui.invalid_resource_identity";
					return false;
				}
				Out = std::move(Identity);
				return true;
			},
			Error);
	}
} // namespace VeroScript::Gui::ScriptBindings

namespace VeroScript::Gui
{
	bool ResolveGuiResourceReference(const std::string& Requester, const std::string& Reference, std::string& Identity,
									 std::string& Error)
	{
		Error.clear();
		Identity.clear();
		if (Reference.empty() || Reference.find('\0') != std::string::npos)
		{
			Error = "gui.invalid_resource_reference";
			return false;
		}
		if (IsResourceDependencyId(Reference))
		{
			FResourceRequest Request;
			std::string From;
			if (!ParseResourceDependencyId(Reference, Request, From))
			{
				Error = "gui.invalid_resource_identity";
				return false;
			}
			Identity = Reference;
		}
		else if (Reference.find(':') != std::string::npos && Reference.find(':') != 1)
		{
			Identity = Reference;
		}
		else if (IsResourceDependencyId(Requester))
		{
			FResourceRequest Request;
			std::string From;
			if (!ParseResourceDependencyId(Requester, Request, From))
			{
				Error = "gui.invalid_resource_identity";
				return false;
			}
			Request.Path = PathUtil::ToUtf8(
				(PathUtil::FromUtf8(Request.Path).parent_path() / PathUtil::FromUtf8(Reference)).lexically_normal());
			if (!ValidateResourceRequest(Request, Error))
			{
				return false;
			}
			Identity = MakeResourceDependencyId(Request, From);
		}
		else
		{
			auto Path = PathUtil::FromUtf8(Reference);
			if (!Path.is_absolute())
			{
				if (Requester.empty() || Requester.front() == '<')
				{
					Error = "gui.resource_resolver_required";
					return false;
				}
				Path = PathUtil::FromUtf8(Requester).parent_path() / Path;
			}
			Identity = PathUtil::IdentityKey(PathUtil::ToUtf8(Path.lexically_normal()));
		}
		return !Identity.empty();
	}
} // namespace VeroScript::Gui
