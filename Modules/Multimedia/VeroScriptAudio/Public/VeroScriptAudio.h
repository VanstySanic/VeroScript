// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VeroScript::Audio
{
	inline constexpr const char* ServiceId = "audio";
	inline constexpr const char* ContractId = "veroscript.audio";
	inline constexpr char ModuleVersion[] = "0.1.1";
	inline constexpr const char* ContractVersion = ModuleVersion;
	inline constexpr std::uint64_t MaxBufferBytes = 64 * 1024 * 1024;

	enum class EDirection : std::uint8_t
	{
		Playback,
		Recording
	};
	enum class EEventType : std::uint8_t
	{
		None,
		DeviceAdded,
		DeviceRemoved,
		DeviceChanged,
		TopologyReset
	};
	// Interleaved native float32 samples; fixed speaker layouts are specified in the Audio guide.
	struct FFormat
	{
		std::int32_t SampleRate = 48000, Channels = 2;
	};
	struct FBuffer
	{
		FFormat Format;
		std::vector<float> Samples;
	};
	struct FDeviceInfo
	{
		EDirection Direction = EDirection::Playback;
		std::uint64_t DeviceId = 0;
		std::string Name;
		FFormat PreferredFormat;
	};
	struct FEvent
	{
		EEventType Type = EEventType::None;
		FDeviceInfo Device;
	};
	struct FStreamOptions
	{
		EDirection Direction = EDirection::Playback;
		// Zero follows the operating system's default route. Nonzero IDs identify a physical connection.
		std::uint64_t DeviceId = 0;
		FFormat Format;
		std::int64_t MaxBufferedFrames = 96000;
	};
	struct FStreamState
	{
		// Playback: queued application frames. Recording: converted frames available to read.
		std::int64_t PendingFrames = 0;
		double Gain = 1;
		bool bPaused = true, bConnected = true, bOverflowed = false;
	};
	struct FCapabilities
	{
		bool bPlayback = false, bRecording = false, bDeviceEvents = false;
	};
	bool ValidateFormat(const FFormat& Format, std::string& Error);
	bool ValidateSamples(const FFormat& Format, const std::vector<float>& Samples, std::string& Error);
	bool ValidateStreamOptions(const FStreamOptions& Options, std::string& Error);
	bool ValidateGain(double Gain, std::string& Error);
	// RIFF/WAVE PCM 8/16/24/32 and IEEE float 32/64. Failure preserves Result. No device is required.
	bool DecodeWav(const std::vector<std::uint8_t>& Bytes, FBuffer& Result, std::string& Error);

	// Methods belong to the creating thread. New streams start paused; Close is idempotent.
	class IStream
	{
	public:
		virtual ~IStream() = default;
		virtual bool IsOpen() const noexcept = 0;
		virtual FStreamOptions GetOptions() const noexcept = 0;
		virtual bool GetState(FStreamState& Result, std::string& Error) = 0;
		virtual bool SetPaused(bool bPaused, std::string& Error) = 0;
		virtual bool SetGain(double Gain, std::string& Error) = 0;
		virtual bool Queue(const std::vector<float>& Samples, std::string& Error) = 0;
		// Nonblocking, whole frames only. Empty output means no data is currently available.
		virtual bool Read(std::int64_t MaxFrames, std::vector<float>& Result, std::string& Error) = 0;
		// Flush releases resampler lookahead, not a wait for audible completion. Playback only.
		virtual bool Flush(std::string& Error) = 0;
		virtual bool Clear(std::string& Error) = 0;
		virtual bool Close(std::string& Error) = 0;
	};
	class IService
	{
	public:
		virtual ~IService() = default;
		virtual FCapabilities GetCapabilities() const noexcept = 0;
		virtual std::string GetDriver() const = 0;
		// Enumeration and event polling may dispatch native owner-thread callbacks.
		virtual bool GetDevices(std::vector<FDeviceInfo>& Result, std::string& Error) = 0;
		// Independent topology snapshots; never consumes Platform's event queue.
		virtual bool PollEvent(FEvent& Result, bool& bAvailable, std::string& Error) = 0;
		virtual std::shared_ptr<IStream> OpenStream(const FStreamOptions& Options, std::string& Error) = 0;
		virtual bool CollectRetired(std::string& Error) = 0;
		virtual bool HasPendingRetirements(bool& bPending, std::string& Error) = 0;
	};
}
