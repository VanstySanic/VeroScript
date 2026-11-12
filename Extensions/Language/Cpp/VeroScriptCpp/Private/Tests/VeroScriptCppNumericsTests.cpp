// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestEngine.h"
#include "VeroScriptEngine.h"
#include "VeroScriptHost.h"
#include "VeroScriptSession.h"
#include <iostream>
#include <sstream>

using namespace VeroScript;
int main()
{
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	FEngineConfig Config;
	VeroScript::Test::Configure(Config);
	Config.ExecutionBackend = EExecutionBackend::Vm;
	Config.NumericLimits.MaxValueBits = Numerics::HardMaxValueBits + 1;
	const auto Invalid = VeroScript::Test::FEngine::RunSource("void main(){}", "numeric-config.vs", Host, ERunMode::Script, Config);
	if (Invalid.bSuccess || Invalid.Error.MessageId != "diag.numeric_invalid_argument")
	{
		std::cerr << "invalid engine numeric configuration must be a structured failure\n";
		return 1;
	}
	Config.NumericLimits.MaxValueBits = 8;
	const auto Limited = VeroScript::Test::FEngine::RunSource("std::bigint value=std::bigint(256);void main(){}", "numeric-init.vs", Host, ERunMode::Script, Config);
	if (Limited.bSuccess || Limited.Error.MessageId != "diag.numeric_limit")
	{
		std::cerr << "engine numeric limits must apply to initialization\n";
		return 1;
	}
	VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
	Session.ApplyConfig(Config);
	FNumericError Error;
	FNumericLimits InvalidLimits;
	InvalidLimits.MaxValueBits = Numerics::HardMaxValueBits + 1;
	if (Session.SetNumericLimits(InvalidLimits, Error) || Session.GetNumericLimits().MaxValueBits != 8)
	{
		std::cerr << "invalid session limit setter changed configuration\n";
		return 1;
	}
	Config.NumericLimits = InvalidLimits;
	Session.ApplyConfig(Config);
	if (Session.GetNumericLimits().MaxValueBits != 8)
	{
		std::cerr << "invalid ApplyConfig changed numeric configuration\n";
		return 1;
	}
	std::cout << "C++ numeric configuration, validation and initialization checks passed\n";
	return 0;
}
