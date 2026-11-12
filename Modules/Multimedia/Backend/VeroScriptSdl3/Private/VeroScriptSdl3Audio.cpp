// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3Runtime.h"
#if VEROSCRIPT_SDL3_AUDIO
#include "VeroScriptAudio.h"
#include "VeroScriptSdl3AudioDevices.h"
#endif
#include <algorithm>
#include <limits>

namespace VeroScript::Sdl3
{
	#if VEROSCRIPT_SDL3_AUDIO
	namespace
	{
		using namespace Audio;
		SDL_AudioDeviceID NativeId(const FStreamOptions& O)
		{
			return O.DeviceId                         ? static_cast<SDL_AudioDeviceID>(O.DeviceId)
			    : O.Direction == EDirection::Playback ? SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK
			                                          : SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
		}
		struct FStreamRetirement final : FRetirement
		{
			//~ Begin FRetirement Interface
			~FStreamRetirement() override
			{
				Release();
			}
			void Release() noexcept override
			{
				if (Native)
				{
					// Destroy synchronizes with the native callback before its userdata is destroyed.
					SDL_DestroyAudioStream(Native);
					Native = nullptr;
				}
			}
			//~ End FRetirement Interface

			SDL_AudioStream* Native = nullptr;
			std::int64_t MaxBytes = 0;
			std::atomic<bool> Overflowed{false}, Failed{false};
		};
		void SDLCALL CaptureQueued(void* Data, SDL_AudioStream* Stream, int, int) noexcept
		{
			auto& Lease = *static_cast<FStreamRetirement*>(Data);
			if (Lease.Failed.load())
			{
				SDL_ClearAudioStream(Stream);
				return;
			}
			const auto Available = SDL_GetAudioStreamAvailable(Stream), Queued = SDL_GetAudioStreamQueued(Stream);
			if (Available < 0 || Queued < 0)
			{
				Lease.Failed.store(true);
				SDL_ClearAudioStream(Stream);
			}
			else if (Available > Lease.MaxBytes || static_cast<std::uint64_t>(Queued) > MaxBufferBytes)
			{
				if (!SDL_ClearAudioStream(Stream))
				{
					Lease.Failed.store(true);
				}
				Lease.Overflowed.store(true);
			}
		}
		struct FStreamLock
		{
			explicit FStreamLock(SDL_AudioStream* S) : Native(S), bLocked(SDL_LockAudioStream(S))
			{
			}
			~FStreamLock()
			{
				if (bLocked)
				{
					SDL_UnlockAudioStream(Native);
				}
			}
			SDL_AudioStream* Native;
			bool bLocked;
		};
		class FStream final : public IStream
		{
		public:
			//~ Begin IStream Interface
			~FStream() override
			{
				Lease->Runtime->Retire(std::move(Lease));
			}
			bool IsOpen() const noexcept override
			{
				return bOpen.load();
			}
			FStreamOptions GetOptions() const noexcept override
			{
				return Options;
			}
			bool GetState(FStreamState& Result, std::string& Error) override
			{
				if (!Check(Error))
				{
					return false;
				}
				if (Lease->Failed.load())
				{
					Error = "audio.capture_failed: native recording queue failed; clear or close the stream";
					return false;
				}
				const auto Bytes = Options.Direction == EDirection::Playback
				    ? SDL_GetAudioStreamQueued(Lease->Native)
				    : SDL_GetAudioStreamAvailable(Lease->Native);
				if (Bytes < 0)
				{
					return SdlError("query audio queue", Error);
				}
				FStreamState Copy;
				Copy.PendingFrames = Bytes / (sizeof(float) * Options.Format.Channels);
				Copy.Gain = SDL_GetAudioStreamGain(Lease->Native);
				if (Copy.Gain < 0)
				{
					return SdlError("query audio gain", Error);
				}
				Copy.bPaused = SDL_AudioStreamDevicePaused(Lease->Native);
				if (!Connected(Copy.bConnected, Error))
				{
					return false;
				}
				Copy.bOverflowed = Lease->Overflowed.load();
				Result = Copy;
				Error.clear();
				return true;
			}
			bool SetPaused(bool Paused, std::string& Error) override
			{
				if (!Check(Error))
				{
					return false;
				}
				if (!(Paused ? SDL_PauseAudioStreamDevice(Lease->Native) : SDL_ResumeAudioStreamDevice(Lease->Native)))
				{
					return SdlError("set audio pause", Error);
				}
				Error.clear();
				return true;
			}
			bool SetGain(double Gain, std::string& Error) override
			{
				if (!Check(Error))
				{
					return false;
				}
				if (!ValidateGain(Gain, Error))
				{
					return false;
				}
				if (!SDL_SetAudioStreamGain(Lease->Native, static_cast<float>(Gain)))
				{
					return SdlError("set audio gain", Error);
				}
				Error.clear();
				return true;
			}
			bool Queue(const std::vector<float>& Samples, std::string& Error) override
			{
				if (!CheckDirection(EDirection::Playback, Error) || !ValidateSamples(Options.Format, Samples, Error))
				{
					return false;
				}
				bool bConnected = false;
				if (!Connected(bConnected, Error))
				{
					return false;
				}
				if (!bConnected)
				{
					Error = "audio.disconnected: playback route is unavailable";
					return false;
				}
				const auto Pending = SDL_GetAudioStreamQueued(Lease->Native);
				if (Pending < 0)
				{
					return SdlError("query queued audio", Error);
				}
				const auto Bytes = Samples.size() * sizeof(float);
				if (Pending > Lease->MaxBytes || Bytes > static_cast<std::uint64_t>(Lease->MaxBytes - Pending))
				{
					Error = "audio.buffer_full: playback queue exceeds its frame budget";
					return false;
				}
				if (Bytes && !SDL_PutAudioStreamData(Lease->Native, Samples.data(), static_cast<int>(Bytes)))
				{
					return SdlError("queue audio samples", Error);
				}
				Error.clear();
				return true;
			}
			bool Read(std::int64_t MaxFrames, std::vector<float>& Result, std::string& Error) override
			{
				if (!CheckDirection(EDirection::Recording, Error))
				{
					return false;
				}
				if (MaxFrames < 0 || MaxFrames > Options.MaxBufferedFrames)
				{
					Error = "audio.invalid_count: read count must be nonnegative and within the stream frame budget";
					return false;
				}
				if (Lease->Failed.load())
				{
					Error = "audio.capture_failed: clear or close the recording stream";
					return false;
				}
				FStreamLock Lock(Lease->Native);
				if (!Lock.bLocked)
				{
					return SdlError("lock recording stream", Error);
				}
				const auto Available = SDL_GetAudioStreamAvailable(Lease->Native);
				if (Available < 0)
				{
					return SdlError("query recorded audio", Error);
				}
				const auto FrameBytes = sizeof(float) * Options.Format.Channels;
				const auto Frames = std::min<std::int64_t>(MaxFrames, Available / FrameBytes);
				std::vector<float> Copy(static_cast<std::size_t>(Frames) * Options.Format.Channels);
				if (!Copy.empty())
				{
					const auto ReadBytes = SDL_GetAudioStreamData(Lease->Native, Copy.data(),
					    static_cast<int>(Copy.size() * sizeof(float)));
					if (ReadBytes < 0)
					{
						return SdlError("read recorded audio", Error);
					}
					if (ReadBytes % FrameBytes)
					{
						Error = "audio.invalid_data: native stream returned a partial frame";
						return false;
					}
					Copy.resize(static_cast<std::size_t>(ReadBytes) / sizeof(float));
				}
				Result = std::move(Copy);
				Error.clear();
				return true;
			}
			bool Flush(std::string& Error) override
			{
				if (!CheckDirection(EDirection::Playback, Error))
				{
					return false;
				}
				if (!SDL_FlushAudioStream(Lease->Native))
				{
					return SdlError("flush audio stream", Error);
				}
				Error.clear();
				return true;
			}
			bool Clear(std::string& Error) override
			{
				if (!Check(Error))
				{
					return false;
				}
				FStreamLock Lock(Lease->Native);
				if (!Lock.bLocked || !SDL_ClearAudioStream(Lease->Native))
				{
					return SdlError("clear audio stream", Error);
				}
				Lease->Overflowed.store(false);
				Lease->Failed.store(false);
				Error.clear();
				return true;
			}
			bool Close(std::string& Error) override
			{
				if (!Lease->Runtime->CheckThread(Error))
				{
					return false;
				}
				Lease->Release();
				bOpen.store(false);
				Error.clear();
				return true;
			}
			//~ End IStream Interface

			FStream(std::unique_ptr<FStreamRetirement> L, FStreamOptions O) : Lease(std::move(L)), Options(O)
			{
			}

		private:
			bool Check(std::string& Error)
			{
				if (!Lease->Runtime->CheckThread(Error))
				{
					return false;
				}
				Lease->Runtime->Drain();
				if (!IsOpen())
				{
					Error = "audio.closed_stream: stream is closed";
					return false;
				}
				Error.clear();
				return true;
			}
			bool CheckDirection(EDirection Direction, std::string& Error)
			{
				if (!Check(Error))
				{
					return false;
				}
				if (Options.Direction != Direction)
				{
					Error = "audio.wrong_direction: operation does not match the stream direction";
					return false;
				}
				return true;
			}
			bool Connected(bool& Result, std::string& Error) const
			{
				int Count = 0;
				std::unique_ptr<SDL_AudioDeviceID, decltype(&SDL_free)> Ids(Options.Direction == EDirection::Playback
				        ? SDL_GetAudioPlaybackDevices(&Count)
				        : SDL_GetAudioRecordingDevices(&Count),
				    SDL_free);
				if (!Ids)
				{
					return SdlError("query connected audio devices", Error);
				}
				// SDL retains a disconnected device's format while its logical streams remain alive.
				if (Options.DeviceId)
				{
					Result = std::find(Ids.get(), Ids.get() + Count,
					             static_cast<SDL_AudioDeviceID>(Options.DeviceId)) != Ids.get() + Count;
				}
				else
				{
					SDL_AudioSpec Spec;
					Result = Count > 0 && SDL_GetAudioDeviceFormat(NativeId(Options), &Spec, nullptr);
				}
				Error.clear();
				return true;
			}
			std::unique_ptr<FStreamRetirement> Lease;
			FStreamOptions Options;
			std::atomic<bool> bOpen{true};
		};
		using AudioDetail::FDevices;
		bool Enumerate(FDevices& Result, std::string& Error)
		{
			FDevices Copy;
			for (auto Direction : {EDirection::Playback, EDirection::Recording})
			{
				int Count = 0;
				std::unique_ptr<SDL_AudioDeviceID, decltype(&SDL_free)> Ids(Direction == EDirection::Playback
				        ? SDL_GetAudioPlaybackDevices(&Count)
				        : SDL_GetAudioRecordingDevices(&Count),
				    SDL_free);
				if (!Ids)
				{
					return SdlError("enumerate audio devices", Error);
				}
				if (Count < 0 || static_cast<std::size_t>(Count) > 4096 - Copy.size())
				{
					Error = "audio.resource_limit: too many audio devices";
					return false;
				}
				for (int I = 0; I < Count; ++I)
				{
					const auto Id = Ids.get()[I];
					SDL_AudioSpec Spec;
					const char* Name = SDL_GetAudioDeviceName(Id);
					if (!Name || !SDL_GetAudioDeviceFormat(Id, &Spec, nullptr))
					{
						continue; // Disconnected during discovery.
					}
					Copy.emplace(std::make_pair(Direction, Id),
					    FDeviceInfo{Direction, Id, Name, {Spec.freq, Spec.channels}});
				}
			}
			Result = std::move(Copy);
			Error.clear();
			return true;
		}
		class FAudioService final : public IService
		{
		public:
			//~ Begin IService Interface
			~FAudioService() override
			{
				Runtime->Retire(std::move(Lease));
			}
			FCapabilities GetCapabilities() const noexcept override
			{
				return {true, true, true};
			}
			std::string GetDriver() const override
			{
				return Driver;
			}
			bool GetDevices(std::vector<FDeviceInfo>& Result, std::string& Error) override
			{
				if (!Refresh(Error))
				{
					return false;
				}
				std::vector<FDeviceInfo> Copy;
				for (const auto& Entry : Changes.GetDevices())
				{
					Copy.push_back(Entry.second);
				}
				Result = std::move(Copy);
				return true;
			}
			bool PollEvent(FEvent& Result, bool& Available, std::string& Error) override
			{
				if (!Refresh(Error))
				{
					return false;
				}
				Available = Changes.Pop(Result);
				return true;
			}
			std::shared_ptr<IStream> OpenStream(const FStreamOptions& O, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error) || !ValidateStreamOptions(O, Error))
				{
					return {};
				}
				Runtime->Drain();
				if (O.DeviceId > std::numeric_limits<SDL_AudioDeviceID>::max() ||
				    (O.DeviceId && (!SDL_IsAudioDevicePhysical(static_cast<SDL_AudioDeviceID>(O.DeviceId)) || SDL_IsAudioDevicePlayback(static_cast<SDL_AudioDeviceID>(O.DeviceId)) != (O.Direction == EDirection::Playback) || O.DeviceId == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK || O.DeviceId == SDL_AUDIO_DEVICE_DEFAULT_RECORDING)))
				{
					Error = "audio.invalid_device: expected zero or a physical device of the requested direction";
					return {};
				}
				auto L = std::make_unique<FStreamRetirement>();
				L->Runtime = Runtime;
				L->MaxBytes = O.MaxBufferedFrames * O.Format.Channels * sizeof(float);
				SDL_AudioSpec Spec{SDL_AUDIO_F32, O.Format.Channels, O.Format.SampleRate};
				L->Native = SDL_OpenAudioDeviceStream(NativeId(O), &Spec, nullptr, nullptr);
				if (!L->Native)
				{
					SdlError("open audio stream", Error);
					return {};
				}
				if (O.Direction == EDirection::Recording &&
				    !SDL_SetAudioStreamPutCallback(L->Native, CaptureQueued, L.get()))
				{
					SdlError("bound recording queue", Error);
					L->Release();
					return {};
				}
				auto Stream = std::make_shared<FStream>(std::move(L), O);
				Error.clear();
				return Stream;
			}
			bool CollectRetired(std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				Error.clear();
				return true;
			}
			bool HasPendingRetirements(bool& Pending, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Pending = Runtime->HasPendingRetirements();
				Error.clear();
				return true;
			}
			//~ End IService Interface

			explicit FAudioService(std::shared_ptr<FRuntime> R)
			    : Runtime(std::move(R)), Lease(MakeServiceLease(Runtime))
			{
			}
			bool Initialize(std::string& Error)
			{
				FDevices Devices;
				if (!Enumerate(Devices, Error))
				{
					return false;
				}
				Changes.Initialize(std::move(Devices));
				return true;
			}

		private:
			bool Refresh(std::string& Error)
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				// Audio disconnect/default-route work is queued on SDL's main thread, even without video.
				// Pump callbacks and native notifications without removing the host's queued events.
				SDL_PumpEvents();
				FDevices Current;
				if (!Enumerate(Current, Error))
				{
					return false;
				}
				Changes.Observe(std::move(Current));
				Error.clear();
				return true;
			}
			std::shared_ptr<FRuntime> Runtime;
			std::unique_ptr<FRetirement> Lease;
			std::string Driver = SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "";
			AudioDetail::FDeviceChanges Changes;
		};
	}
	#endif
	std::shared_ptr<Audio::IService> CreateAudioImplementation(std::shared_ptr<const void> CodeOwner,
	    std::string& Error)
	{
		#if VEROSCRIPT_SDL3_AUDIO
		auto Runtime = AcquireRuntime(std::move(CodeOwner), Error);
		if (!Runtime || !Runtime->EnsureAudio(Error))
		{
			return {};
		}
		auto Service = std::make_shared<FAudioService>(Runtime);
		if (!Service->Initialize(Error))
		{
			return {};
		}
		Error.clear();
		return Service;
		#else
		(void)CodeOwner;
		Error = "audio.unsupported: SDL audio was disabled at build time";
		return {};
		#endif
	}
}
