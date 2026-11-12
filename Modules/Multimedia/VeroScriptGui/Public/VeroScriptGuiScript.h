// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiMarkup.h"
#include "VeroScriptRun.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptGuiApplication.h"
#include "VeroScriptGuiCompilation.h"

namespace VeroScript::Gui
{
	struct FScriptServices
	{
		FApplicationFactories Application;
		std::function<std::shared_ptr<FContext>(const std::string&, const FViewport&, std::string&)> CreateContext;
		// Optional owner-thread execution bridge for native host-driven events. Supply
		// both callbacks and keep the session alive until the bridge is released.
		// Independent Session.CallFunction entries need SetHotReloadEnabled(true)
		// when they access GUI globals created by a preceding host entry.
		std::function<bool(std::string_view, std::vector<FValue>&, FValue&, std::string&)> Invoke;
		std::function<bool(const std::weak_ptr<const void>&, std::uint64_t)> IsCurrentProgram;
		// Optional module/resource-provider resolver. The fallback resolves filesystem references against the caller.
		std::function<bool(const std::string& Requester, const std::string& Reference, std::string& Identity,
						   std::string& Error)>
			ResolveResourceReference;
		std::function<bool(const std::string&)> IsFrameworkModule;
		FResourceReader ReadResource;
		// Used only when the script leaves its application backend option unspecified.
		std::function<std::string(std::string_view)> DefaultBackend;
		// Optional late provider discovery for contexts, applications and standalone resource services;
		// keep absent optional factories empty and report resolution failures through Error.
		std::function<bool(FApplicationFactories&, std::string&)> ResolveApplicationFactories;
	};

	/** Register on an unpublished catalog, then configure the embedding engine before loading sources. */
	bool RegisterScriptBindings(FHostBindingRegistry& Registry, std::string& Error);
	bool RegisterScriptBindings(FHostBindingRegistry& Registry, FScriptServices Services, std::string& Error);
	bool ConfigureGuiScriptEngine(FEngineConfig& Config, std::string& Error);
	bool ConfigureGuiScriptEngine(FEngineConfig& Config, const FGuiCompilationConfiguration& Configuration,
								  std::string& Error);
	bool ResolveGuiResourceReference(const std::string& Requester, const std::string& Reference, std::string& Identity,
									 std::string& Error);
	/** Accepts the internal handle returned by Widget.__GetNativeHandle(). */
	std::shared_ptr<FWidget> GetScriptWidget(const FValue& Handle, std::string& Error);
	std::shared_ptr<FContext> GetScriptContext(const FValue& Handle, std::string& Error);
	FValue MakeScriptContext(const IHostBindingCatalog& Catalog, std::shared_ptr<FContext> Context, std::string& Error);
	// An owned input snapshot; event control methods become valid only during routed delivery.
	FValue MakeScriptInput(const IHostBindingCatalog& Catalog, const Platform::FEvent& Input, std::string& Error);

	struct FMarkupScriptOptions
	{
		// Copied by the adapter. Hosts retain the resources referenced by their engine configuration.
		FEngineConfig Engine;
		std::function<void(std::string_view)> Print;
	};

	/** Optional Core/C++ embedding adapter; generated getters/events use ordinary VS type checking and execution. */
	FMarkupScriptFactory MakeMarkupScriptFactory(FMarkupScriptOptions Options = {});
	/** Call on the GUI owner thread before shutting down a host that released widgets on other threads. */
	void CollectMarkupScripts();
} // namespace VeroScript::Gui
