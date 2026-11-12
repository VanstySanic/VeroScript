// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVm.h"
#include "VeroScriptFontBackend.h"
#include "VeroScriptFontBindings.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	void Run(const FHostBindingRegistry &Bindings, std::string_view Source, std::string_view Failure = {})
	{
		const auto Factory = BytecodeVm::CreateFactory(GetVmHostServices());
		if (!Factory)
		{
			throw std::runtime_error("could not create the explicit Font test VM backend");
		}
		FCompileOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = Factory;
		Options.Bindings = &Bindings;
		auto Compiled = VeroScriptCompile::CompileSource(Source, "font-bindings.vs", Options);
		if (!Compiled.bSuccess || !Compiled.Module)
		{
			throw std::runtime_error(Compiled.Error.Message);
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		std::string Error;
		if (!Vm.SetVmBackendFactory(Factory, Error))
		{
			throw std::runtime_error(Error);
		}
		const auto Ok = Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Bindings.GetNativeFunctions());
		if (!Vm.GetVmBackendStats().Entries)
		{
			throw std::runtime_error("Font script did not enter the selected VM backend");
		}
		if (Failure.empty())
		{
			if (!Ok)
			{
				throw std::runtime_error(Vm.GetError().Message);
			}
			if (Vm.GetProcessExitCode() != 0)
			{
				throw std::runtime_error("font script returned a failed assertion");
			}
		}
		else if (Ok || Vm.GetError().Message.find(Failure) == std::string::npos)
		{
			throw std::runtime_error("font script lost its native failure: " + Vm.GetError().Message);
		}
	}
} // namespace

int main()
{
	try
	{
		using namespace VeroScript;
		FHostBindingRegistry Bindings;
		std::string Error;
		if (!Font::RegisterScriptBindings(Bindings, [](const auto &Backend, auto &Failure)
		{
			Failure.clear();
			if (Backend == FontBackend::BackendId) return FontBackend::CreateFontService();
			Failure = "test.font_backend_unavailable";
			return std::shared_ptr<Font::IService>{};
		}, Error))
		{
			throw std::runtime_error(Error);
		}
		Run(Bindings, R"(
class FontHolder
{
public:
    VS::Font::Face Face;
    FontHolder(VS::Font::Face value) { Face = value; }
    int64 Glyph(int64 character) { return Face.GetGlyphIndex(character); }
}
int main()
{
    VS::Font::Service fonts = VS::Font::Service::Create("font-backend");
    VS::Font::Face face = fonts.LoadDefault();
    FontHolder holder = FontHolder(face);
    face = nullptr;
    fonts = nullptr;
    for (int i = 0; i < 32; ++i)
        if (holder.Glyph(65 + i % 26) <= 0) return 1;
    VS::Font::ShapedText text = holder.Face.Shape("office", 24);
    if (text.GetGlyphCount() <= 0 || text.GetGlyphCount() >= 6 || text.GetAdvanceX(0) <= 0) return 2;
    VS::Font::ShapedText rtl = holder.Face.Shape("abc", 24, "en", "rtl");
    if (rtl.GetCluster(0) != 2) return 3;
    VS::Font::Bitmap bitmap = holder.Face.Rasterize(holder.Glyph(65), 24);
    VS::Font::Metrics metrics = holder.Face.GetMetrics(24);
    if (bitmap.GetByteCount() <= 0 || bitmap.IsColor() || metrics.GetAscent() <= 0) return 4;
    return 0;
}
)");
		Run(Bindings, R"(
int initializations = 0;
VS::Font::Face MakeFace()
{
    ++initializations;
    return VS::Font::Service::Create("font-backend").LoadDefault();
}
VS::Font::Face LoadCachedFace()
{
    static const VS::Font::Face cached = MakeFace();
    return cached;
}
function() -> int64 MakeValueReader()
{
    VS::Font::Face face = MakeFace();
    return [face]() -> int64 { return face.GetGlyphIndex(65); };
}
function() -> int64 MakeReferenceReader()
{
    VS::Font::Face face = nullptr;
    auto reader = [&face]() -> int64 { return face.GetGlyphIndex(65); };
    face = MakeFace();
    return reader;
}
int main()
{
    auto value = MakeValueReader();
    auto reference = MakeReferenceReader();
    auto initialized = [face = MakeFace()]() -> int64
    {
        return face.Shape("office", 24).GetGlyphCount();
    };
    int before = initializations;
    int64 glyph = value();
    if (glyph <= 0 || reference() != glyph) return 1;
    int64 count = initialized();
    if (count <= 0 || count >= 6 || initializations != before) return 2;
    if (LoadCachedFace().GetGlyphIndex(65) != glyph || LoadCachedFace().GetGlyphIndex(65) != glyph ||
        initializations != before + 1) return 3;
    return 0;
}
)");
		Run(Bindings, R"(
int main()
{
    VS::Font::Face face = VS::Font::Service::Create("font-backend").LoadDefault();
    face.Shape("x", 0);
    return 0;
}
)",
		    "font.invalid_size");
		Run(Bindings, R"(
int main()
{
    VS::Font::ShapedText text = VS::Font::Service::Create("font-backend").LoadDefault().Shape("x", 24);
    text.GetCluster(-1);
    return 0;
}
)",
		    "font.index_out_of_range");
		Run(Bindings, R"(
int main()
{
    VS::Font::Service::Create("missing");
    return 0;
}
)",
		    "test.font_backend_unavailable");
		std::cout << "Font bindings through the compiler and VM passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
