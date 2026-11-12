// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVm.h"
#include "VeroScriptImageBindings.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
	using namespace VeroScript;

	void Require(bool Condition, const char *Message)
	{
		if (!Condition)
		{
			throw std::runtime_error(Message);
		}
	}

	class FImageService final : public Image::IService
	{
	public:
		mutable std::size_t Calls = 0, LastSize = 0;
		mutable std::uint8_t SecondByte = 0xff;
		bool bInvalidOutput = false;

		const char *GetBackendId() const noexcept override
		{
			return "mock";
		}
		bool Decode(const std::vector<std::uint8_t> &Bytes, Image::FDecodedImage &Result,
		    std::string &Error, const Image::FDecodeOptions &) const override
		{
			++Calls;
			LastSize = Bytes.size();
			SecondByte = Bytes.size() > 1 ? Bytes[1] : 0xff;
			Error.clear();
			Result = {1, 1, {65, 0, 66, 255}};
			if (bInvalidOutput)
			{
				Result.Rgba.pop_back();
			}
			return true;
		}
	};

	bool Call(const FHostBindingRegistry &Registry, const char *TypeName, const char *Name,
	    bool bInstance, std::vector<FValue> Args, FValue &Result, std::string &Error)
	{
		const auto *Type = Registry.FindType(TypeName);
		Require(Type != nullptr, "missing Image binding type");
		for (const auto &Member : Type->Members)
		{
			if (Member.Name != Name)
			{
				continue;
			}
			Require(Member.Callback != nullptr, "missing Image binding callback");
			FNativeCallContext Context;
			Context.Args = Args.data();
			Context.ArgCount = Args.size();
			Context.UserData = Member.UserData;
			if (bInstance)
			{
				Context.HostThis = std::get<FHostObjectHandle>(Args.at(0))->Payload;
			}
			return Member.Callback(Context, Result, Error);
		}
		throw std::runtime_error("missing Image binding member");
	}
	void CheckScriptCalls(const FHostBindingRegistry &Registry)
	{
		const auto Factory = BytecodeVm::CreateFactory(GetVmHostServices());
		Require(Factory != nullptr, "could not create the explicit Image test VM backend");
		FCompileOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = Factory;
		Options.Bindings = &Registry;
		const auto Run = [&](std::string_view Source, bool InvalidIndex)
		{
			auto Compiled = VeroScriptCompile::CompileSource(Source, "image-bindings.vs", Options);
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
			const auto Ok = Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Registry.GetNativeFunctions());
			Require(Vm.GetVmBackendStats().Entries != 0, "Image script did not enter the selected VM backend");
			if (InvalidIndex)
			{
				Require(!Ok && Vm.GetError().Message.find("image.index_out_of_range") != std::string::npos,
				    "script call lost its invalid pixel index error");
			}
			else
			{
				if (!Ok)
				{
					throw std::runtime_error(Vm.GetError().Message);
				}
				Require(Vm.GetProcessExitCode() == 0, "script image query returned incorrect results");
			}
		};
		for (const bool InvalidIndex : {false, true})
		{
			const auto Source = InvalidIndex ? R"(
int main() { VS::Image::Service::Create("mock").Decode("A\0B").GetByte(-1); return 0; }
)"
			                                 : R"(
int main()
{
    VS::Image::Service service = VS::Image::Service::Create("mock");
    VS::Image::Bitmap image = service.Decode("A\0B");
    service = nullptr;
    if (image.GetWidth() != 1 || image.GetHeight() != 1 || image.GetByteCount() != 4) return 1;
    int64 total = 0;
    for (int i = 0; i < 32; ++i) total += image.GetByte(i % 4);
    return total == 3088 ? 0 : 2;
}
)";
			Run(Source, InvalidIndex);
		}
		// Exercise host handles through the Core's closure cells and local-static roots.
		// Create captured handles separately so static storage cannot keep them alive.
		Run(R"(
int initializations = 0;
VS::Image::Bitmap LoadCachedBitmap()
{
    static const VS::Image::Bitmap cached = MakeBitmap();
    return cached;
}
VS::Image::Bitmap MakeBitmap()
{
    ++initializations;
    return VS::Image::Service::Create("mock").Decode("A\0B");
}
function() -> int64 MakeValueReader()
{
    VS::Image::Bitmap image = MakeBitmap();
    return [image]() -> int64 { return image.GetByte(2); };
}
function() -> int64 MakeReferenceReader()
{
    VS::Image::Bitmap image = nullptr;
    auto reader = [&image]() -> int64 { return image.GetByteCount(); };
    image = MakeBitmap();
    return reader;
}
int main()
{
    auto value = MakeValueReader();
    auto reference = MakeReferenceReader();
    auto initialized = [image = MakeBitmap()]() -> int64 { return image.GetWidth(); };
    int before = initializations;
    if (value() != 66 || reference() != 4 || initialized() != 1) return 1;
    if (initializations != before) return 2;
    if (LoadCachedBitmap().GetByte(2) != 66 || LoadCachedBitmap().GetWidth() != 1 ||
        initializations != before + 1) return 3;
    return 0;
}
)",
		    false);
	}
} // namespace

int main()
{
	try
	{
		using namespace VeroScript;
		FHostBindingRegistry Registry;
		std::string Error;
		auto Service = std::make_shared<FImageService>();
		std::size_t Reads = 0;
		bool bOversizedSource = false;
		const auto Limit = static_cast<std::size_t>(Image::FDecodeOptions{}.MaxSourceBytes);
		Require(Image::RegisterScriptBindings(Registry,
		            [Service](const auto &, auto &)
		{
			return Service;
		}, Error,
		            [&](const auto &, auto &Bytes, auto &Failure)
		{
			++Reads;
			Failure.clear();
			Bytes.assign(bOversizedSource ? Limit + 1 : 3, 0);
			return true;
		}),
		    "Image binding registration failed");
		FValue Handle;
		Require(Call(Registry, "VS.Image.Service", "Create", false, {std::string("mock")}, Handle, Error),
		    "Image service creation failed");
		FValue Bitmap;
		Require(Call(Registry, "VS.Image.Service", "Decode", true,
		            {Handle, std::string("A\0B", 3)}, Bitmap, Error),
		    "binary Image decode failed");
		Require(Service->LastSize == 3 && Service->SecondByte == 0, "binary input was truncated at NUL");
		FValue Byte;
		Require(Call(Registry, "VS.Image.Bitmap", "GetByte", true,
		            {Bitmap, std::int64_t{2}}, Byte, Error) &&
		        std::get<std::int64_t>(Byte) == 66,
		    "decoded pixels are unavailable");
		Require(!Call(Registry, "VS.Image.Bitmap", "GetByte", true,
		            {Bitmap, std::int64_t{-1}}, Byte, Error),
		    "negative pixel index was accepted");
		Require(!Call(Registry, "VS.Image.Bitmap", "GetByte", true,
		            {Bitmap, std::int64_t{4}}, Byte, Error),
		    "past-end pixel index was accepted");

		const auto CallsBefore = Service->Calls;
		{
			// Move the input so this case needs only one large source allocation.
			std::vector<FValue> Args{Handle};
			Args.emplace_back(std::string(Limit + 1, 'x'));
			Require(!Call(Registry, "VS.Image.Service", "Decode", true, std::move(Args), Bitmap, Error) && Error == "image.invalid_image" && Service->Calls == CallsBefore,
			    "oversized binary input reached the backend");
		}
		Require(Call(Registry, "VS.Image.Service", "Load", true,
		            {Handle, std::string("memory:image")}, Bitmap, Error),
		    "resource reader load failed");
		const auto ReadsBefore = Reads;
		Require(!Call(Registry, "VS.Image.Service", "Load", true,
		            {Handle, std::string("bad\0path", 8)}, Bitmap, Error) &&
		        Reads == ReadsBefore,
		    "invalid resource name reached the reader");
		bOversizedSource = true;
		const auto CallsBeforeRead = Service->Calls;
		Require(!Call(Registry, "VS.Image.Service", "Load", true,
		            {Handle, std::string("memory:large")}, Bitmap, Error) &&
		        Error == "image.invalid_image" && Service->Calls == CallsBeforeRead,
		    "oversized reader input reached the backend");
		Service->bInvalidOutput = true;
		FValue Unchanged = std::int64_t{23};
		Require(!Call(Registry, "VS.Image.Service", "Decode", true,
		            {Handle, std::string("input")}, Unchanged, Error) &&
		        std::get<std::int64_t>(Unchanged) == 23,
		    "invalid decoded pixels changed the return value");
		Service->bInvalidOutput = false;
		CheckScriptCalls(Registry);
		Require(Service->LastSize == 3 && Service->SecondByte == 0, "script binary input was truncated at NUL");
		std::cout << "Independent Image bindings tests passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
