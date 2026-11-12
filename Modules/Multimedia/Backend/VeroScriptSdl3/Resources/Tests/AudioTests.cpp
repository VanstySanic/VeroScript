// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include "VeroScriptAudio.h"
#include "../../Private/VeroScriptSdl3AudioDevices.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
	std::string Error;
	unsigned Failures = 0;
	void Check(bool V, const char* Name)
	{
		if (!V)
		{
			++Failures;
			std::cerr << Name << ": " << Error << '\n';
		}
	}
	int TestDisconnectedRoute()
	{
		using namespace VeroScript;
		using namespace Audio;
		bool bDisk = false;
		for (int I = 0; I < SDL_GetNumAudioDrivers(); ++I)
		{
			bDisk = bDisk || std::string(SDL_GetAudioDriver(I)) == "disk";
		}
		if (!bDisk)
		{
			return 77;
		}
		auto Service = Sdl3::CreateAudio({}, Error);
		std::vector<FDeviceInfo> Devices;
		if (!Service || !Service->GetDevices(Devices, Error))
		{
			std::cerr << Error;
			return 1;
		}
		FStreamOptions Options;
		for (const auto& Device : Devices)
		{
			if (Device.Direction == EDirection::Playback)
			{
				Options.DeviceId = Device.DeviceId;
				break;
			}
		}
		auto Explicit = Options.DeviceId ? Service->OpenStream(Options, Error) : nullptr;
		auto Default = Service->OpenStream({}, Error);
		if (!Explicit || !Default || !Explicit->SetPaused(false, Error) || !Default->SetPaused(false, Error))
		{
			std::cerr << Error;
			return 2;
		}
		bool bRemoved = false, bNotified = false;
		const auto End = SDL_GetTicks() + 2000;
		do
		{
			SDL_Delay(5);
			FEvent Event;
			bool bAvailable = false;
			if (!Service->PollEvent(Event, bAvailable, Error) || !Service->GetDevices(Devices, Error))
			{
				std::cerr << Error;
				return 3;
			}
			bNotified = bNotified ||
						(bAvailable && Event.Type == EEventType::DeviceRemoved &&
						 Event.Device.Direction == EDirection::Playback && Event.Device.DeviceId == Options.DeviceId);
			bRemoved =
				std::none_of(Devices.begin(), Devices.end(),
							 [&](const auto& Device)
							 {
								 return Device.Direction == EDirection::Playback && Device.DeviceId == Options.DeviceId;
							 });
		}
		while ((!bRemoved || !bNotified) && SDL_GetTicks() < End);
		Check(bRemoved && bNotified, "audio-only polling processes native device failure and removal");
		SDL_AudioSpec Spec;
		std::cout << "Retained disconnected native format: "
				  << SDL_GetAudioDeviceFormat(static_cast<SDL_AudioDeviceID>(Options.DeviceId), &Spec, nullptr) << '\n';
		for (auto& Stream : {Explicit, Default})
		{
			FStreamState State;
			Check(Stream->GetState(State, Error) && !State.bConnected && Stream->IsOpen(),
				  "retained explicit/default stream reports missing physical route");
			Check(!Stream->Queue({0, 0}, Error) && Error.find("audio.disconnected:") == 0,
				  "disconnected route rejects playback even if SDL retains its format");
			Check(Stream->Close(Error), "close disconnected stream");
		}
		Explicit.reset();
		Default.reset();
		Service.reset();
		Check(Sdl3::CollectRetired(Error) && !SDL_WasInit(SDL_INIT_AUDIO), "retire disconnected audio runtime");
		return Failures ? 1 : 0;
	}
}
int main(int ArgCount, char** Args)
{
	if (ArgCount == 2 && std::string(Args[1]) == "disconnect")
	{
		return TestDisconnectedRoute();
	}
	using namespace VeroScript;
	using namespace Audio;
	Sdl3::AudioDetail::FDeviceChanges Changes;
	FDeviceInfo Speaker{EDirection::Playback, 7, "speaker", {48000, 2}};
	Sdl3::AudioDetail::FDevices Initial{{{EDirection::Playback, 7}, Speaker}};
	Changes.Initialize(Initial);
	FEvent Changed;
	Check(!Changes.Pop(Changed), "initial topology does not duplicate events");
	auto Current = Initial;
	Current[{EDirection::Playback, 7}].PreferredFormat.SampleRate = 44100;
	Changes.Observe(Current);
	Check(Changes.Pop(Changed) && Changed.Type == EEventType::DeviceChanged &&
			  Changed.Device.PreferredFormat.SampleRate == 44100 &&
			  Initial.begin()->second.PreferredFormat.SampleRate == 48000,
		  "device format changes and immutable metadata");
	Current[{EDirection::Recording, 9}] = {EDirection::Recording, 9, "microphone", {48000, 1}};
	Current.erase({EDirection::Playback, 7});
	Changes.Observe(Current);
	Check(Changes.Pop(Changed) && Changed.Type == EEventType::DeviceRemoved && Changed.Device.DeviceId == 7,
		  "removed route precedes additions");
	Check(Changes.Pop(Changed) && Changed.Type == EEventType::DeviceAdded && Changed.Device.DeviceId == 9 &&
			  !Changes.Pop(Changed),
		  "independent recording hotplug snapshot");
	for (unsigned I = 0; I < 4100; ++I)
	{
		auto Snapshot = Current;
		Snapshot.begin()->second.PreferredFormat.SampleRate = 44100 + static_cast<int>(I % 2);
		Changes.Observe(std::move(Snapshot));
	}
	Check(Changes.Pop(Changed) && Changed.Type == EEventType::TopologyReset && !Changes.Pop(Changed) &&
			  Changes.GetDevices().size() == 1,
		  "topology overflow requests enumeration without losing current devices");
	auto Service = Sdl3::CreateAudio({}, Error);
	#if !VS_TEST_AUDIO
	Check(!Service && Error.find("audio.unsupported") == 0, "disabled audio factory");
	#else
	if (!Service)
	{
		std::cerr << Error;
		return 1;
	}
	Check(!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_WasInit(SDL_INIT_GAMEPAD),
		  "audio does not initialize video or controllers");
	Check(Service->GetCapabilities().bPlayback && Service->GetCapabilities().bRecording &&
			  Service->GetDriver() == "dummy",
		  "dummy backend capabilities");
	std::vector<FDeviceInfo> Devices;
	Check(Service->GetDevices(Devices, Error) &&
			  std::any_of(Devices.begin(), Devices.end(),
						  [](const auto& D)
						  {
							  return D.Direction == EDirection::Playback;
						  }) &&
			  std::any_of(Devices.begin(), Devices.end(),
						  [](const auto& D)
						  {
							  return D.Direction == EDirection::Recording;
						  }),
		  "playback and recording enumeration");
	FEvent Event;
	bool Available;
	Check(Service->PollEvent(Event, Available, Error) && !Available && Event.Type == EEventType::None,
		  "initial topology is a snapshot");
	{
		std::atomic<bool> bRan{false}, bScheduled{false};
		const auto Marker = SDL_RegisterEvents(1);
		SDL_Event HostEvent{};
		HostEvent.type = Marker;
		Check(Marker && SDL_PushEvent(&HostEvent), "queue independent host event");
		std::thread Schedule(
			[&]
			{
				bScheduled.store(SDL_RunOnMainThread(
					[](void* Data)
					{
						static_cast<std::atomic<bool>*>(Data)->store(true);
					},
					&bRan, false));
			});
		Schedule.join();
		Check(bScheduled.load() && !bRan.load(), "native main-thread callback awaits owner polling");
		Check(Service->PollEvent(Event, Available, Error) && bRan.load() && SDL_HasEvent(Marker),
			  "audio polling drains main callbacks without consuming host events");
		SDL_PumpEvents(); // Drain test userdata even if the preceding regression fails.
		SDL_PeepEvents(&HostEvent, 1, SDL_GETEVENT, Marker, Marker);
	}
	FStreamOptions O;
	O.MaxBufferedFrames = 8;
	auto A = Service->OpenStream(O, Error), B = Service->OpenStream(O, Error);
	if (!A || !B)
	{
		std::cerr << Error;
		return 1;
	}
	FStreamState State;
	Check(A->GetState(State, Error) && State.bPaused && State.bConnected, "new stream paused and connected");
	Check(A->Queue({0, 0, 0.25f, -0.25f}, Error) && A->GetState(State, Error) && State.PendingFrames == 2,
		  "queued application frames");
	Check(B->GetState(State, Error) && State.PendingFrames == 0, "streams independent");
	Check(!A->Queue(std::vector<float>(16), Error) && A->GetState(State, Error) && State.PendingFrames == 2,
		  "queue backpressure preserves state");
	Check(!A->Queue({0}, Error) && !A->Queue({0, std::numeric_limits<float>::quiet_NaN()}, Error),
		  "invalid samples rejected");
	Check(A->SetGain(0.25, Error) && A->GetState(State, Error) && State.Gain == 0.25 && !A->SetGain(-1, Error) &&
			  !A->SetGain(std::numeric_limits<double>::infinity(), Error),
		  "gain validation");
	std::vector<float> Samples{17};
	Check(!A->Read(1, Samples, Error) && Samples == std::vector<float>{17}, "wrong-direction read preserves output");
	Check(A->Flush(Error) && A->Clear(Error) && A->GetState(State, Error) && State.PendingFrames == 0,
		  "flush and clear");
	Check(A->SetPaused(false, Error) && A->GetState(State, Error) && !State.bPaused && A->SetPaused(true, Error),
		  "pause and resume");
	O.Direction = EDirection::Recording;
	auto Recording = Service->OpenStream(O, Error);
	if (!Recording)
	{
		std::cerr << Error;
		return 1;
	}
	Check(!Recording->Queue({0, 0}, Error) && !Recording->Flush(Error), "recording direction enforced");
	Check(!Recording->Read(-1, Samples, Error) && Samples == std::vector<float>{17}, "negative capture count rejected");
	Check(Recording->Read(0, Samples, Error) && Samples.empty(), "zero-frame nonblocking read");
	Check(Recording->SetPaused(false, Error), "start capture");
	const auto Deadline = SDL_GetTicks() + 2000;
	do
	{
		SDL_Delay(5);
		Check(Recording->GetState(State, Error), "capture state");
	}
	while (!State.bOverflowed && SDL_GetTicks() < Deadline);
	Check(State.bOverflowed && State.PendingFrames <= 8, "unread capture is bounded on native audio thread");
	Check(Recording->SetPaused(true, Error) && Recording->Clear(Error) && Recording->GetState(State, Error) &&
			  !State.bOverflowed && State.PendingFrames == 0,
		  "capture reset");
	{
		FStreamOptions Capture;
		Capture.Direction = EDirection::Recording;
		Capture.Format = {44100, 1};
		auto Reader = Service->OpenStream(Capture, Error);
		Check(Reader && Reader->SetPaused(false, Error), "start converted recording stream");
		if (Reader)
		{
			const auto End = SDL_GetTicks() + 2000;
			do
			{
				SDL_Delay(5);
				Check(Reader->GetState(State, Error), "converted recording state");
			}
			while (State.PendingFrames < 16 && SDL_GetTicks() < End);
			Check(Reader->Read(16, Samples, Error) && Samples.size() == 16 &&
					  std::all_of(Samples.begin(), Samples.end(),
								  [](float V)
								  {
									  return V == 0;
								  }),
				  "nonblocking recording reads complete converted silent frames");
			Check(Reader->Close(Error), "close converted recording stream");
		}
	}
	for (const auto& D : Devices)
	{
		O.Direction = D.Direction;
		O.DeviceId = D.DeviceId;
		O.Format = {44100, 1};
		auto Explicit = Service->OpenStream(O, Error);
		Check(Explicit && Explicit->GetOptions().Format.Channels == 1,
			  "explicit physical device and conversion format");
		if (Explicit)
		{
			Check(Explicit->Close(Error), "close explicit stream");
		}
		O.Direction = D.Direction == EDirection::Playback ? EDirection::Recording : EDirection::Playback;
		Check(!Service->OpenStream(O, Error), "device direction mismatch rejected");
	}
	O.DeviceId = std::numeric_limits<std::uint64_t>::max();
	Check(!Service->OpenStream(O, Error), "device ID narrowing checked");
	bool Accepted = true;
	std::thread Wrong(
		[&]
		{
			std::string E;
			Accepted = A->SetGain(1, E);
		});
	Wrong.join();
	Check(!Accepted, "wrong thread rejected");
	Check(A->Close(Error) && A->Close(Error) && !A->IsOpen() && !A->Queue({}, Error),
		  "idempotent close and closed-stream validation");
	A.reset();
	B.reset();
	Check(Recording->SetPaused(false, Error), "active recording before foreign retirement");
	std::thread Drop(
		[S = std::move(Recording)]() mutable
		{
			S.reset();
		});
	Drop.join();
	SDL_Delay(25); // The native callback can run while the owner has not drained retirement.
	bool Pending = false;
	Check(Service->HasPendingRetirements(Pending, Error) && Pending && Service->CollectRetired(Error) &&
			  Service->HasPendingRetirements(Pending, Error) && !Pending,
		  "foreign final release retired on owner");
	Service.reset();
	Check(Sdl3::CollectRetired(Error) && !SDL_WasInit(SDL_INIT_AUDIO), "final audio subsystem shutdown");
	#endif
	std::cout << "SDL audio failures: " << Failures << '\n';
	return Failures ? 1 : 0;
}
