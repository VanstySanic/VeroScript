// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptAudioBindings.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include <iostream>
#include <sstream>

namespace
{
	using namespace VeroScript;
	using namespace Audio;
	class FStream final : public IStream
	{
	public:
		//~ Begin IStream Interface
		bool IsOpen() const noexcept override
		{
			return Open;
		}
		FStreamOptions GetOptions() const noexcept override
		{
			return Options;
		}
		bool GetState(FStreamState& Out, std::string&) override
		{
			Out = State;
			return true;
		}
		bool SetPaused(bool V, std::string&) override
		{
			State.bPaused = V;
			return true;
		}
		bool SetGain(double V, std::string&) override
		{
			State.Gain = V;
			return true;
		}
		bool Queue(const std::vector<float>& S, std::string&) override
		{
			State.PendingFrames += static_cast<std::int64_t>(S.size() / Options.Format.Channels);
			return true;
		}
		bool Read(std::int64_t Max, std::vector<float>& Out, std::string&) override
		{
			Out.assign(static_cast<std::size_t>(Max) * Options.Format.Channels, 0.25f);
			return true;
		}
		bool Flush(std::string&) override
		{
			return true;
		}
		bool Clear(std::string&) override
		{
			State.PendingFrames = 0;
			return true;
		}
		bool Close(std::string&) override
		{
			Open = false;
			return true;
		}
		//~ End IStream Interface

		explicit FStream(FStreamOptions O) : Options(O)
		{
		}

	private:
		FStreamOptions Options;
		FStreamState State;
		bool Open = true;
	};
	class FService final : public IService
	{
	public:
		//~ Begin IService Interface
		FCapabilities GetCapabilities() const noexcept override
		{
			return {true, true, true};
		}
		std::string GetDriver() const override
		{
			return "memory";
		}
		bool GetDevices(std::vector<FDeviceInfo>& Out, std::string&) override
		{
			Out = {{EDirection::Playback, std::uint64_t{1} << 40, "memory device", {48000, 2}}};
			return true;
		}
		bool PollEvent(FEvent& Out, bool& Available, std::string&) override
		{
			Out = {};
			Available = false;
			return true;
		}
		std::shared_ptr<IStream> OpenStream(const FStreamOptions& O, std::string&) override
		{
			return std::make_shared<FStream>(O);
		}
		bool CollectRetired(std::string&) override
		{
			return true;
		}
		bool HasPendingRetirements(bool& Pending, std::string&) override
		{
			Pending = false;
			return true;
		}
		//~ End IService Interface
	};
}
int main()
{
	using namespace VeroScript;
	std::string Error;
	FHostBindingRegistry Registry;
	const Audio::FServiceFactory Factory = [](const std::string& Backend,
	                                           std::string& E) -> std::shared_ptr<Audio::IService>
	{
		if (Backend != "memory")
		{
			E = "unknown audio backend";
			return {};
		}
		return std::make_shared<FService>();
	};
	if (!Audio::RegisterScriptBindings(Registry, Factory, Error))
	{
		std::cerr << Error;
		return 1;
	}
	const auto Types = Registry.GetTypes().size();
	if (Audio::RegisterScriptBindings(Registry, Factory, Error) || Registry.GetTypes().size() != Types)
	{
		return 2;
	}
	const std::string Source = R"(
int main()
{
    VS::Audio::Service service=VS::Audio::Service::Create("memory");
    assert(service.SupportsPlayback() && service.SupportsRecording() && service.SupportsDeviceEvents());
    assert(service.GetDriver()=="memory");
    VS::Audio::Devices devices=service.GetDevices();
    assert(devices.Count==1);
    VS::Audio::Device device=devices.Get(0);
    assert(device.DeviceId==1099511627776u64 && device.Direction==VS::Audio::Direction::Playback && device.SampleRate==48000 && device.Channels==2);
    array<double> samples={0.0,0.25,-0.25,0.5};
    VS::Audio::Buffer buffer=VS::Audio::Buffer::Create(48000,2,samples);
    assert(buffer.FrameCount==2 && buffer.SampleRate==48000 && buffer.Channels==2 && buffer.GetSample(1)==0.25);
    array<double> copy=buffer.GetSamples(); assert(len(copy)==4 && copy[2]==-0.25);
    array<uint8> wav={82,73,70,70,38,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,1,0,128,187,0,0,128,187,0,0,1,0,8,0,100,97,116,97,2,0,0,0,0,255};
    VS::Audio::Buffer decoded=VS::Audio::Buffer::FromWav(wav); assert(decoded.FrameCount==2 && decoded.GetSample(0)==-1.0);
    VS::Audio::Stream stream=service.OpenStream(VS::Audio::Direction::Playback,device.DeviceId,48000,2,96000);
    stream.Queue(buffer); stream.QueueSamples(samples); stream.SetGain(0.5); stream.SetPaused(false);
    VS::Audio::StreamState saved=stream.GetState(); assert(saved.PendingFrames==4 && saved.Gain==0.5 && !saved.Paused && saved.Connected && !saved.Overflowed);
    auto query=[service,devices,buffer,stream]() -> bool
    {
        return service.SupportsPlayback() && service.GetDriver()=="memory" && service.GetDevices().Count==1 &&
               devices.Get(0).Channels==2 && buffer.GetSample(1)==0.25 && len(buffer.GetSamples())==4 &&
               stream.IsOpen() && stream.GetState().PendingFrames==4;
    };
    assert(query());
    stream.Flush(); stream.Clear(); assert(stream.GetState().PendingFrames==0 && saved.PendingFrames==4);
    stream.Close(); assert(!stream.IsOpen());
    VS::Audio::Stream recording=service.OpenStream(VS::Audio::Direction::Recording,0u64,48000,1,96000);
    VS::Audio::Buffer recorded=recording.Read(2); assert(recorded.FrameCount==2 && recorded.GetSample(0)==0.25); recording.Close();
    VS::Audio::Event event=service.PollEvent(); assert(event.Type==VS::Audio::EventType::None);
    assert(!service.HasPendingRetirements()); service.CollectRetired();
    return 0;
}
)";
	FCompileOptions Options;
	VeroScript::Test::Configure(Options);
	Options.Bindings = &Registry;
	auto Compiled = VeroScriptCompile::CompileSource(Source, "audio-memory.vs", Options);
	if (!Compiled.bSuccess)
	{
		std::cerr << Compiled.Error.Message;
		return 3;
	}
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	VeroScript::Test::Configure(Vm);
	Vm.SetMemoryHostBindings(&Registry);
	if (!Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Registry.GetNativeFunctions()))
	{
		std::cerr << Vm.GetError().Message;
		return 4;
	}
	for (const std::string Statement :
	    {"VS::Audio::Buffer::Create(4295015296i64,2,samples);",
	        "VS::Audio::Buffer::Create(48000,4294967298i64,samples);", "VS::Audio::Buffer::Create(48000,3,samples);",
	        "VS::Audio::Service s=VS::Audio::Service::Create(\"memory\");VS::Audio::Stream "
	        "r=s.OpenStream(VS::Audio::Direction::Recording,0u64,48000,1,4);r.Read(-1);",
	        "VS::Audio::Service s=VS::Audio::Service::Create(\"memory\");VS::Audio::Stream "
	        "r=s.OpenStream(VS::Audio::Direction::Recording,0u64,48000,1,4);r.Read(5);",
	        "VS::Audio::Service s=VS::Audio::Service::Create(\"memory\");VS::Audio::Stream "
	        "r=s.OpenStream(VS::Audio::Direction::Playback,0u64,48000,1,4);r.SetGain(-1.0);",
	        "VS::Audio::Service s=VS::Audio::Service::Create(\"memory\");VS::Audio::Stream "
	        "r=s.OpenStream(VS::Audio::Direction::Playback,0u64,48000,2,4);r.Queue(VS::Audio::Buffer::Create(48000,1,"
	        "samples));",
	        "VS::Audio::Buffer b=VS::Audio::Buffer::Create(48000,2,samples); b.GetSample(-1);",
	        "VS::Audio::Buffer b=VS::Audio::Buffer::Create(48000,2,samples); b.GetSample(4);",
	        "VS::Audio::Service s=VS::Audio::Service::Create(\"memory\");s.GetDevices().Get(-1);"})
	{
		auto Bad = VeroScriptCompile::CompileSource(
		    "int main(){array<double> samples={0.0,0.25};" + Statement + "return 0;}", "audio-invalid.vs", Options);
		if (!Bad.bSuccess)
		{
			std::cerr << Bad.Error.Message;
			return 5;
		}
		VeroScriptVm Reject(Output);
		VeroScript::Test::Configure(Reject);
		Reject.SetMemoryHostBindings(&Registry);
		if (Reject.Execute(*Bad.Module, ERunMode::Script, {}, &Registry.GetNativeFunctions()))
		{
			return 6;
		}
	}
	if (VeroScriptCompile::CompileSource(
	        "int main(){VS::Audio::Service s=VS::Audio::Service::Create(\"memory\");"
	        "auto change=[stream=s.OpenStream(VS::Audio::Direction::Playback,0u64,48000,1,4)]() -> void "
	        "{stream.SetGain(0.5);};return 0;}",
	        "<read-only-audio-capture>", Options)
	        .bSuccess)
	{
		std::cerr << "A read-only capture unexpectedly allowed stream mutation\n";
		return 7;
	}
	std::cout << "Independent audio script bindings passed\n";
	return 0;
}
