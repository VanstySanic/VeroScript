// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include "VeroScriptAudio.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <iostream>

namespace
{
	EM_BOOL Start(int, const EmscriptenMouseEvent*, void*)
	{
		try
		{
			std::string Error;
			auto Service = VeroScript::Sdl3::CreateAudio({}, Error);
			std::vector<VeroScript::Audio::FDeviceInfo> Devices;
			VeroScript::Audio::FEvent Event;
			bool bAvailable = false;
			const bool bPolling = Service && Service->GetDevices(Devices, Error) &&
			    Service->PollEvent(Event, bAvailable, Error);
			auto Stream = bPolling ? Service->OpenStream({}, Error) : nullptr;
			VeroScript::Audio::FStreamState State;
			if (!Stream || !Stream->Queue({0, 0, 0, 0}, Error) || !Stream->Flush(Error) ||
			    !Stream->SetGain(0.5, Error) || !Stream->SetPaused(false, Error) || !Stream->GetState(State, Error) ||
			    State.bPaused || !State.bConnected || !Stream->Clear(Error) || !Stream->Close(Error))
			{
				std::cerr << "Browser audio: " << Error << '\n';
				EM_ASM({ document.body.dataset.result = 'failed'; });
				return EM_TRUE;
			}
			Stream.reset();
			Service.reset();
			if (!VeroScript::Sdl3::CollectRetired(Error))
			{
				EM_ASM({ document.body.dataset.result = 'failed'; });
				return EM_TRUE;
			}
			EM_ASM({ document.body.dataset.result = 'passed'; });
		}
		catch (...)
		{
			EM_ASM({ document.body.dataset.result = 'failed'; });
		}
		return EM_TRUE;
	}
}
int main()
{
	EM_ASM({
		const button = document.createElement('button');
		button.id = 'start-audio';
		button.textContent = 'Start audio test';
		document.body.appendChild(button);
	});
	emscripten_set_click_callback("#start-audio", nullptr, true, Start);
	emscripten_exit_with_live_runtime();
}
