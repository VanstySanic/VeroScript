// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptPath.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace VeroScript;
using namespace VeroScript::Tooling;
namespace
{
	class FGetter final : public IAnnotationProcessor
	{
	public:
		void Process(const FAnnotationContext& Context, FAnnotationOutput& Output) override
		{
			std::string Name;
			if (!Context.ReadDependency(Dependency, Name))
			{
				Name = "Read";
			}

			if (bVolatile)
			{
				Name = (++*Calls & 1u) ? "Read" : "Changed";
			}

			if (Name != "Read" && Name != "Changed")
			{
				Name = "Read";
			}

			Output.SubmitMethod(Context.Owner->Id, EMemberAccess::Public,
			    Context.Declaration.TypeName + " " + Name + "() const {return " + Context.Declaration.Name +
			        ";}");
		}

		FGetter(std::string Path, std::shared_ptr<std::atomic<unsigned>> Count, bool Volatile)
		    : Dependency(std::move(Path)), Calls(std::move(Count)), bVolatile(Volatile)
		{
		}

	private:
		std::string Dependency;
		std::shared_ptr<std::atomic<unsigned>> Calls;
		bool bVolatile;
	};
}
int main(int Argc, char** Argv)
{
	if (Argc != 4) return 2;
	#ifdef _WIN32
	// LSP/DAP frame lengths and CRLF separators describe raw bytes.
	if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
	#endif
	const std::string Dependency = Argv[1];
	const bool Volatile = std::string_view(Argv[2]) == "volatile";
	auto Calls = std::make_shared<std::atomic<unsigned>>(0);
	auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
	std::string Error;
	FAnnotationProcessorDesc Desc{"@Getter", "tooling.test.getter", "1", "",
	    AnnotationTargetMask(EAnnotationTarget::Field), 1, 0, [Dependency, Calls, Volatile]
	{
		return std::make_unique<FGetter>(Dependency, Calls, Volatile);
	}};
	Desc.Cacheable = !Volatile;
	if (!Registry->Register(std::move(Desc), Error)) return 2;
	Registry->Freeze();
	FAnnotationProcessingOptions Processing;
	Processing.Registry = Registry;
	Processing.ReadDependency = [](std::string_view Path, std::string& Text)
	{
		std::ifstream Input(PathUtil::FromUtf8(Path), std::ios::binary);
		if (!Input) return false;
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		Text = Buffer.str();
		return !Input.bad();
	};
	if (std::string_view(Argv[3]) == "dap")
	{
		FDapStdioRunOptions Options;
		Options.AnnotationProcessing = std::move(Processing);
		return VeroScript::Test::FDapStdio::Run(std::cin, std::cout, Options);
	}
	FLspStdioRunOptions Options;
	Options.AnnotationProcessing = std::move(Processing);
	return VeroScript::Test::FLspStdio::Run(std::cin, std::cout, Options);
}
