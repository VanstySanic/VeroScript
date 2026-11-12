// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScript.h"
#include "VeroScriptVm.h"
#include <iostream>
#include <sstream>

using namespace VeroScript;
using namespace VeroScript::I18n;

// Publish a new immutable instance when your resource source changes.
class FMessages final : public II18nResourceSnapshot
{
public:
	EI18nLookupResult Lookup(const FI18nLookupRequest& R, const FI18nWork&,
		FI18nMessage& Out, FI18nError&) const override
	{
		if (R.Domain != "example" || R.Key != "items") return EI18nLookupResult::NotFound;
		if (R.Language == "en-US")
		{
			Out = {"{count} items", "vero.named"};
		}

		else if (R.Language == "zh-CN")
		{
			Out = {"共有 {count} 项", "vero.named"};
		}

		else
		{
			return EI18nLookupResult::NotFound;
		}

		return EI18nLookupResult::Found;
	}
};

int main()
{
	auto Services = std::make_shared<FI18nServices>();
	FI18nError Error;
	if (!Services->PublishResources(std::make_shared<FMessages>(), Error)) return 1;
	FCompileOptions Options;
	Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	const auto Compiled = VeroScriptCompile::CompileSource(R"VS(
void main() {
    std::i18n::arguments args = std::i18n::arguments();
    args.set("count", std::i18n::argument(18446744073709551615u64));
    std::i18n::text message = std::i18n::text::message("example", "items", args);
    std::i18n::context en = std::i18n::context();
    std::i18n::context zh = en.clone();
    zh.set_language("zh-CN");
    assert(en.resolve(message) == "18446744073709551615 items");
    assert(zh.resolve(message) == "共有 18446744073709551615 项");
    print("{}", zh.resolve(message));
})VS",
		"i18n-example.vs", Options);
	if (!Compiled.bSuccess)
	{
		std::cerr << Compiled.Error.Message << '\n';
		return 1;
	}
	std::istringstream Input;
	VeroScriptVm Vm(std::cout, Input, std::cerr);
	VeroScript::BytecodeVm::Configure(Vm);
	Vm.SetI18nServices(Services, {4096});
	if (!Vm.Execute(*Compiled.Module, ERunMode::Script))
	{
		std::cerr << Vm.GetError().Message << '\n';
		return 1;
	}
	return 0;
}
