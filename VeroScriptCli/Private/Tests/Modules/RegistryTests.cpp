// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCliExtensions.h"
#include <iostream>
#include <sstream>

using namespace VeroScript;
using namespace VeroScript::Cli;
namespace
{
	unsigned Checks = 0, Failures = 0;
	void Expect(bool B, const std::string& Text)
	{
		++Checks;
		if (!B)
		{
			++Failures;
			std::cerr << Text << '\n';
		}
	}

	bool Configure(const FCommandLineContext&, const FCommandLineValues&, void*, std::string&)
	{
		return true;
	}

	int Run(const FCommandLineContext&, const std::vector<std::string>&, void*)
	{
		return 3;
	}

	FCommandLineModule Module(std::string Id = "test")
	{
		FCommandLineModule M;
		M.Id = std::move(Id);
		M.Configure = Configure;
		M.Options = {{"XXX", "An uppercase option", "value"}};
		M.Commands = {{"test-command", "Test", "", Run}};
		return M;
	}
}
int main()
{
	std::string Error;
	for (const auto* Name : {"install", "aot", "project", "compiler-package", "env", "uninstall"})
	{
		FCommandLineExtensions R;
		auto M = Module();
		M.Commands.front().Name = Name;
		Expect(!R.Register(M, Error) && Error.find("reserved") != Error.npos, "reserve command " + std::string(Name));
	}
	for (const auto* Name : {"help", "cli-module", "jit-backend", "path", "stdlib-path", "compiler-config", "offline", "requirement"})
	{
		FCommandLineExtensions R;
		auto M = Module();
		M.Options.front().Name = Name;
		Expect(!R.Register(M, Error), "reserve option " + std::string(Name));
	}
	for (const auto* Name : {"", "--bad", "bad.name", "bad/name", "bad\nname"})
	{
		FCommandLineExtensions R;
		auto M = Module();
		M.Options.front().Name = Name;
		Expect(!R.Register(M, Error), "reject invalid option name");
	}
	{
		FCommandLineExtensions R;
		Expect(R.Register(Module(), Error), "register module");
		Expect(R.ParseOption("--XXX=-42", {}, false, Error) == 1, "uppercase and signed value");
		Expect(R.ParseOption("--xxx=value", {}, false, Error) == 0, "case-sensitive option name");
		Expect(R.ParseOption("--XXX", "next", true, Error) == -1, "nonrepeatable option");
		Expect(R.ParseOption("--not-registered", {}, false, Error) == 0, "unknown option");
		std::ostringstream Output;
		FCommandLineContext C;
		C.Output = &Output;
		int Code = 0;
		Expect(R.RunCommand("test-command", {}, C, Code, Error) && Code == 3, "dispatch exit code");
		Expect(!R.Register(Module("late"), Error), "reject late registration");
		Expect(R.Configure(C, Error), "late rejection does not invalidate a committed invocation");
	}
	{
		FCommandLineExtensions R;
		Expect(R.Register(Module(), Error), "register first module");
		auto Other = Module("other");
		Other.Commands.front().Name = "other-command";
		Expect(!R.Register(Other, Error), "cross-module option conflict");
		Expect(!R.HasCommand("other-command"), "failed registration is atomic");
		Expect(!R.Configure({}, Error), "ignored registration failure remains fatal");
	}
	std::shared_ptr<const INativeExtensionService> Service;
	{
		FCommandLineExtensions R;
		Service = R.Binding().Service;
		Expect(static_cast<const ICommandLineExtensionService*>(Service.get())->Register(Module(), Error), "proxy registration");
	}
	Expect(!static_cast<const ICommandLineExtensionService*>(Service.get())->Register(Module(), Error) &&
			Error == "CLI invocation has ended",
		"retained service rejects expired invocation");
	std::cout << "CLI registry checks: " << Checks << ", failures: " << Failures << '\n';
	return Failures ? 1 : 0;
}
