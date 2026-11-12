// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include "VeroScriptGraphics.h"
#include <chrono>
#include <iostream>
#include <thread>

int main(int Count, char** Args)
{
	using namespace VeroScript;
	using namespace Graphics;
	std::string Error;
	unsigned Failures = 0;
	const auto Check = [&](bool Value, const char* Name)
	{
		if (!Value)
		{
			++Failures;
			std::cerr << Name << ": " << Error << '\n';
		}
	};
	const auto Complete = [&](auto& Request)
	{
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		ERequestStatus Status = ERequestStatus::Pending;
		while ((Status = Request->Poll(Error)) == ERequestStatus::Pending &&
			   std::chrono::steady_clock::now() < Deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return Status == ERequestStatus::Succeeded;
	};
	auto Service = Sdl3::CreateGraphics({}, Error);
	FDeviceOptions Options;
	Options.Driver = Count > 1 ? Args[1] : "";
	auto Creation = Service ? Service->CreateDeviceAsync(Options, Error) : nullptr;
	if (!Creation || !Complete(Creation))
	{
		std::cerr << Error;
		return Count > 1 ? 1 : 77;
	}
	auto Device = Creation->GetDevice(Error);
	if (!Device)
	{
		std::cerr << Error;
		return 1;
	}
	Check(Device->Poll(Error), "device nonblocking poll");
	auto Texture = Device->CreateTexture({3, 2, ETextureFormat::Rgba8Srgb}, Error);
	const std::vector<std::uint8_t> Bytes{1,   3,	7,	 255, 11,  21,	31,	 128, 63,  95,	127, 255,
										  191, 192, 193, 64,  200, 210, 220, 255, 251, 252, 253, 254};
	if (!Texture || !Device->UpdateTexture(Texture, 0, 0, 3, 2, Bytes, Error))
	{
		std::cerr << Error;
		return 1;
	}
	auto Read = Device->ReadTextureAsync(Texture, Error);
	if (!Read)
	{
		std::cerr << Error;
		return 1;
	}
	std::vector<std::uint8_t> Pixels;
	Check(Read && Complete(Read) && Read->GetPixels(Pixels, Error) && Pixels == Bytes,
		  "asynchronous readback preserves row order and encoded bytes");
	bool WrongThreadAccepted = false;
	std::thread WrongThread(
		[&]
		{
			std::string E;
			WrongThreadAccepted = Creation->Poll(E) != ERequestStatus::Failed || Read->Cancel(E);
		});
	WrongThread.join();
	Check(!WrongThreadAccepted && Read->Poll(Error) == ERequestStatus::Succeeded,
		  "foreign request operations preserve owner state");
	Check(Read->Cancel(Error) && Read->Poll(Error) == ERequestStatus::Cancelled, "readback cancellation");
	Pixels = {42};
	Check(!Read->GetPixels(Pixels, Error) && Pixels == std::vector<std::uint8_t>{42},
		  "cancelled output remains unchanged");
	auto PendingRead = Device->ReadTextureAsync(Texture, Error);
	Check(PendingRead && PendingRead->Cancel(Error) && PendingRead->Cancel(Error) &&
			  PendingRead->Poll(Error) == ERequestStatus::Cancelled,
		  "readback cancellation before polling is idempotent");
	PendingRead.reset();
	auto AbandonedRead = Device->ReadTextureAsync(Texture, Error);
	Check(static_cast<bool>(AbandonedRead), "abandoned readback creation");
	std::thread DropRead(
		[R = std::move(AbandonedRead)]() mutable
		{
			R.reset();
		});
	DropRead.join();
	Check(Device->Poll(Error), "owner drains abandoned and cancelled readbacks without invalidating the device");
	auto Cancelled = Service->RecoverDeviceAsync(Device, Options, Error);
	Check(Cancelled && Cancelled->Cancel(Error) && Device->IsOpen(), "cancelled recovery preserves the old device");
	Cancelled.reset();
	Check(!Device->Close(Error) && Device->IsOpen(), "device close rejects live resources");
	auto Recovery = Service->RecoverDeviceAsync(Device, Options, Error);
	Check(Recovery && Complete(Recovery), "asynchronous device recovery");
	auto Replacement = Recovery ? Recovery->GetDevice(Error) : nullptr;
	Check(Replacement && Replacement->IsOpen() && !Device->IsOpen() && !Texture->IsOpen() &&
			  Replacement->GetGeneration() != Device->GetGeneration(),
		  "recovery commits replacement and invalidates old resources");
	Check(Texture->Close(Error) && Device->Close(Error), "invalidated resources remain closable");
	auto ClosingTexture = Replacement ? Replacement->CreateTexture({1, 1}, Error) : nullptr;
	auto ClosingRead = ClosingTexture && Replacement->ClearTexture(ClosingTexture, {}, Error)
						   ? Replacement->ReadTextureAsync(ClosingTexture, Error)
						   : nullptr;
	Check(ClosingRead && ClosingTexture->Close(Error) && Replacement->Close(Error),
		  "device closes after texture release while a readback request still exists");
	Check(ClosingRead && ClosingRead->Cancel(Error) && ClosingRead->Cancel(Error) &&
			  ClosingRead->Poll(Error) == ERequestStatus::Cancelled,
		  "readback remains safely cancellable after device close");
	ClosingRead.reset();
	ClosingTexture.reset();
	Read.reset();
	Recovery.reset();
	Texture.reset();
	Replacement.reset();
	Device.reset();
	std::thread Worker(
		[R = std::move(Creation)]() mutable
		{
			R.reset();
		});
	Worker.join();
	Check(Sdl3::CollectRetired(Error), "owner drains foreign request retirement");
	Service.reset();
	bool Pending = true;
	const auto ShutdownDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (Pending && std::chrono::steady_clock::now() < ShutdownDeadline)
	{
		if (!Sdl3::HasPendingRetirements(Pending, Error))
		{
			break;
		}
		if (Pending)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	Check(!Pending, "final native callback retirement drain");
	std::cout << "Async graphics failures: " << Failures << '\n';
	return Failures ? 1 : 0;
}
