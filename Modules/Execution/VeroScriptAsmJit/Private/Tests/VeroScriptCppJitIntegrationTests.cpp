// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptHost.h"
#include "VeroScriptSession.h"
#include "VeroScriptAsmJit.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <variant>

namespace
{
	bool ReconfigureDuringRun(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue&,
		std::string&)
	{
		auto* const Session = static_cast<VeroScript::VeroScriptSession*>(Context.UserData);
		if (Session == nullptr)
		{
			return false;
		}
		Session->SetInputFromString("replaced\n");
		Session->SetUseBytecodeCache(true);
		return true;
	}

	int Fail(const char* const Message, const std::ostringstream& Diagnostics)
	{
		std::cerr << Message << '\n'
				  << Diagnostics.str();
		return 1;
	}
}

int main()
{
	using namespace VeroScript;

	std::ostringstream Output;
	std::ostringstream Diagnostics;
	FStreamEngineHost Host(Output, Diagnostics);
	VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
	Session.SetJitBackendFactory(CreateAsmJitBackendFactory());

	FJitPolicy Policy;
	Policy.MixedHotCallThreshold = 2;
	Policy.MaxCompileAttempts = 32;
	Policy.MaxCachedEntries = 32;
	Policy.EagerCompileBudget = 32;
	Policy.bReuseSessionCode = true;
	Session.SetJitPolicy(Policy);
	Session.SetExecutionBackend(EExecutionBackend::Jit);
	FSessionJitCacheLimits CacheLimits;
	CacheLimits.MaxNativeBytes = 4u * 1024u * 1024u;
	CacheLimits.MaxRetainedPayloadBytes = 8u * 1024u * 1024u;
	Session.SetJitCacheLimits(CacheLimits);

	if (Session.GetExecutionBackend() != EExecutionBackend::Jit
		|| Session.GetJitPolicy().MixedHotCallThreshold != Policy.MixedHotCallThreshold
		|| Session.GetJitPolicy().MaxCompileAttempts != Policy.MaxCompileAttempts
		|| Session.GetJitPolicy().MaxCachedEntries != Policy.MaxCachedEntries
		|| Session.GetJitPolicy().EagerCompileBudget != Policy.EagerCompileBudget
		|| !Session.GetJitPolicy().bReuseSessionCode
		|| Session.GetJitCacheLimits().MaxNativeBytes != CacheLimits.MaxNativeBytes
		|| Session.GetJitCacheLimits().MaxRetainedPayloadBytes
			!= CacheLimits.MaxRetainedPayloadBytes)
	{
		return Fail("C++ session did not retain its JIT configuration", Diagnostics);
	}

	const FRunResult Load = Session.LoadSource(
		"int add(int left, int right) { return left + right; }\n"
		"int main() { print(add(20, 22)); return 0; }\n",
		"<cpp-jit-integration>");
	if (!Load.bSuccess)
	{
		return Fail("C++ session failed to compile the JIT integration source", Diagnostics);
	}

	const FRunResult Run = Session.Run();
	if (!Run.bSuccess || Run.ExitCode != 0 || Output.str() != "42\n")
	{
		return Fail("C++ session JIT execution produced an unexpected result", Diagnostics);
	}

	const FJitStats& Stats = Session.GetLastJitStats();
	if (Stats.CompileAttempts == 0 || Stats.DispatchAttempts == 0)
	{
		return Fail("C++ session did not expose JIT compilation/dispatch statistics", Diagnostics);
	}

	const FSessionJitCacheStats RunCacheStats = Session.GetJitCacheStats();
	FValue ReturnValue;
	const FRunResult FirstCall = Session.CallFunction(
		"add", {std::int32_t{20}, std::int32_t{22}}, &ReturnValue);
	const auto* const FirstValue = std::get_if<std::int32_t>(&ReturnValue);
	const FSessionJitCacheStats FirstCacheStats = Session.GetJitCacheStats();
	if (!FirstCall.bSuccess || FirstValue == nullptr || *FirstValue != 42
		|| Session.GetLastJitStats().CompileAttempts != 0
		|| FirstCacheStats.CompileAttempts != RunCacheStats.CompileAttempts
		|| FirstCacheStats.CompileAttempts == 0
		|| FirstCacheStats.RetainedPayloadBytes == 0
		|| FirstCacheStats.bResourceLimitReached)
	{
		return Fail("C++ session did not populate its reusable JIT cache", Diagnostics);
	}

	ReturnValue = {};
	const FRunResult WarmCall = Session.CallFunction(
		"add", {std::int32_t{20}, std::int32_t{22}}, &ReturnValue);
	const auto* const WarmValue = std::get_if<std::int32_t>(&ReturnValue);
	const FSessionJitCacheStats WarmCacheStats = Session.GetJitCacheStats();
	if (!WarmCall.bSuccess || WarmValue == nullptr || *WarmValue != 42
		|| Session.GetLastJitStats().CompileAttempts != 0
		|| WarmCacheStats.CompileAttempts != FirstCacheStats.CompileAttempts
		|| WarmCacheStats.RetainedPayloadBytes != FirstCacheStats.RetainedPayloadBytes
		|| WarmCacheStats.NativeBytes != FirstCacheStats.NativeBytes)
	{
		return Fail("C++ session did not reuse its warm JIT cache", Diagnostics);
	}

	Session.Clear();
	const FJitStats& ClearedStats = Session.GetLastJitStats();
	const FSessionJitCacheStats ClearedCacheStats = Session.GetJitCacheStats();
	if (ClearedStats.CompileAttempts != 0 || ClearedStats.DispatchAttempts != 0
		|| ClearedCacheStats.CompileAttempts != 0
		|| ClearedCacheStats.RetainedPayloadBytes != 0
		|| ClearedCacheStats.NativeBytes != 0
		|| ClearedCacheStats.bResourceLimitReached)
	{
		return Fail("C++ session did not clear its last-run JIT statistics", Diagnostics);
	}

	Output.str({});
	Output.clear();
	Session.SetExecutionBackend(EExecutionBackend::Vm);
	Session.SetInputFromString("preserved\n");
	Session.SetUseBytecodeCache(false);
	if (!Session.RegisterNative(
			"reconfigure_during_run",
			ENativeType::Void,
			{},
			ReconfigureDuringRun,
			&Session))
	{
		return Fail("C++ session could not register its reentry guard probe", Diagnostics);
	}
	const FRunResult ReentryLoad = Session.LoadSource(
		"import \"io.vs\";\n"
		"void main() { reconfigure_during_run(); print(std::io::read_line()); }\n",
		"<cpp-session-reentry-guard>");
	if (!ReentryLoad.bSuccess)
	{
		return Fail("C++ session failed to compile the reentry guard source", Diagnostics);
	}
	const FRunResult ReentryRun = Session.Run();
	if (!ReentryRun.bSuccess
		|| Output.str() != "preserved\n"
		|| Session.GetUseBytecodeCache())
	{
		return Fail("running session setters changed active execution state", Diagnostics);
	}

	return 0;
}
