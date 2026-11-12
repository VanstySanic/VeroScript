// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAudioBindings.h"
#include "VeroScriptAudioBindingState.h"

namespace VeroScript::Audio
{
	using namespace BindingDetail;
	namespace
	{
		bool Format(std::int64_t Rate, std::int64_t Channels, FFormat& Out, std::string& Error)
		{
			if (Rate < 1 || Rate > 384000 || Channels < 1 || Channels > 8)
			{
				Error = "audio.invalid_format: rate must be 1..384000 Hz and channels 1..8";
				return false;
			}
			Out = {static_cast<std::int32_t>(Rate), static_cast<std::int32_t>(Channels)};
			return true;
		}
		bool Samples(const FValue& Value, const FFormat& F, std::vector<float>& Out, std::string& Error)
		{
			const auto& A = Array(Value);
			if (A.ElementCount() > MaxBufferBytes / sizeof(float) || A.ElementCount() % F.Channels)
			{
				Error = "audio.invalid_samples: expected whole frames within 64 MiB";
				return false;
			}
			std::vector<float> Copy;
			Copy.reserve(A.ElementCount());
			for (std::size_t I = 0; I < A.ElementCount(); ++I)
			{
				const double V = Element<double>(A, I);
				if (!std::isfinite(V) || std::abs(V) > std::numeric_limits<float>::max())
				{
					Error = "audio.invalid_samples: sample must fit a finite float32";
					return false;
				}
				Copy.push_back(static_cast<float>(V));
			}
			Out = std::move(Copy);
			return true;
		}
	}
	bool RegisterScriptBindings(FHostBindingRegistry& Registry, FServiceFactory Factory, std::string& Error)
	{
		if (!Factory)
		{
			Error = "audio.missing_factory: host must provide a backend resolver";
			return false;
		}
		FHostBindingRegistry Candidate = Registry;
		auto State = std::make_shared<FState>();
		State->Factory = std::move(Factory);
		for (unsigned I = 0; I < Names.size(); ++I)
		{
			if (!Candidate.RegisterType(Names[I], EHostTypeKind::Class, State->Ids[I], Error))
			{
				return false;
			}
		}
		for (bool Events : {false, true})
		{
			FHostTypeId Id;
			const std::vector<const char*> Members =
				Events
					? std::vector<const char*>{"None", "DeviceAdded", "DeviceRemoved", "DeviceChanged", "TopologyReset"}
					: std::vector<const char*>{"Playback", "Recording"};
			if (!Candidate.RegisterType(Events ? "VS.Audio.EventType" : "VS.Audio.Direction", EHostTypeKind::Enum, Id,
										Error) ||
				!Candidate.SetEnumScoped(Id, true, Error))
			{
				return false;
			}
			for (std::size_t I = 0; I < Members.size(); ++I)
			{
				FHostMemberDesc M;
				M.Kind = EHostMemberKind::EnumMember;
				M.Name = Members[I];
				M.EnumValue = static_cast<std::int64_t>(I);
				if (!Candidate.RegisterMember(Id, std::move(M), Error))
				{
					return false;
				}
			}
		}
		const FRegistrar Add(Candidate, State, Error);
		const FRegistrar AddConst(Candidate, State, Error, true);
		const auto Static = EHostMemberKind::StaticMethod, Method = EHostMemberKind::Method,
				   Getter = EHostMemberKind::PropertyGetter;
		if (!Add(
				0, "Create", Static, ENativeType::HostObject, {ENativeType::String},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					auto S = State->Factory(std::get<std::string>(C.Args[0]), E);
					if (!S)
					{
						return false;
					}
					Out = Make(State, 0, std::move(S));
					return true;
				},
				Names[0]))
		{
			return false;
		}
		for (unsigned Flag = 0; Flag < 3; ++Flag)
		{
			if (!AddConst(0,
					 Flag == 0	 ? "SupportsPlayback"
					 : Flag == 1 ? "SupportsRecording"
								 : "SupportsDeviceEvents",
					 Method, ENativeType::Bool, {},
					 [Flag](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto Caps = This<std::shared_ptr<IService>>(C)->GetCapabilities();
						 Out = Flag == 0 ? Caps.bPlayback : Flag == 1 ? Caps.bRecording : Caps.bDeviceEvents;
						 return true;
					 }))
			{
				return false;
			}
		}
		if (!AddConst(0, "GetDriver", Method, ENativeType::String, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<std::shared_ptr<IService>>(C)->GetDriver();
					 return true;
				 }))
		{
			return false;
		}
		if (!AddConst(
				0, "GetDevices", Method, ENativeType::HostObject, {},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					std::vector<FDeviceInfo> D;
					if (!This<std::shared_ptr<IService>>(C)->GetDevices(D, E))
					{
						return false;
					}
					Out = Make(State, 3, std::move(D));
					return true;
				},
				Names[3]))
		{
			return false;
		}
		if (!Add(
				0, "PollEvent", Method, ENativeType::HostObject, {},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					FEvent Event;
					bool Available;
					if (!This<std::shared_ptr<IService>>(C)->PollEvent(Event, Available, E))
					{
						return false;
					}
					Out = Make(State, 5, std::move(Event));
					return true;
				},
				Names[5]))
		{
			return false;
		}
		if (!Add(0, "OpenStream", Method, ENativeType::HostObject,
				 {ENativeType::Enum, ENativeType::UInt64, ENativeType::Int64, ENativeType::Int64, ENativeType::Int64},
				 [State](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 FStreamOptions O;
					 const auto Direction = std::get<FEnumValue>(C.Args[0]).Value;
					 if (Direction < 0 || Direction > 1)
					 {
						 E = "audio.invalid_direction: unknown direction";
						 return false;
					 }
					 O.Direction = static_cast<EDirection>(Direction);
					 O.DeviceId = std::get<std::uint64_t>(C.Args[1]);
					 if (!Format(std::get<std::int64_t>(C.Args[2]), std::get<std::int64_t>(C.Args[3]), O.Format, E))
					 {
						 return false;
					 }
					 O.MaxBufferedFrames = std::get<std::int64_t>(C.Args[4]);
					 if (!ValidateStreamOptions(O, E))
					 {
						 return false;
					 }
					 auto S = This<std::shared_ptr<IService>>(C)->OpenStream(O, E);
					 if (!S)
					 {
						 return false;
					 }
					 Out = Make(State, 1, std::move(S));
					 return true;
				 },
				 Names[1], {"VS.Audio.Direction", "", "", "", ""}))
		{
			return false;
		}
		if (!Add(0, "CollectRetired", Method, ENativeType::Void, {},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IService>>(C)->CollectRetired(E);
				 }) ||
			!AddConst(0, "HasPendingRetirements", Method, ENativeType::Bool, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 bool Pending;
					 if (!This<std::shared_ptr<IService>>(C)->HasPendingRetirements(Pending, E))
					 {
						 return false;
					 }
					 Out = Pending;
					 return true;
				 }))
		{
			return false;
		}

		if (!Add(2, "Create", Static, ENativeType::HostObject,
				 {ENativeType::Int64, ENativeType::Int64, ENativeType::Array},
				 [State](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 FBuffer B;
					 if (!Format(std::get<std::int64_t>(C.Args[0]), std::get<std::int64_t>(C.Args[1]), B.Format, E) ||
						 !Samples(C.Args[2], B.Format, B.Samples, E))
					 {
						 return false;
					 }
					 Out = Make(State, 2, std::move(B));
					 return true;
				 },
				 Names[2], {}, {"", "", "double"}))
		{
			return false;
		}
		if (!Add(2, "FromWav", Static, ENativeType::HostObject, {ENativeType::Array},
				 [State](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 const auto& A = Array(C.Args[0]);
					 if (A.ElementCount() > MaxBufferBytes)
					 {
						 E = "audio.resource_limit: WAV input exceeds 64 MiB";
						 return false;
					 }
					 std::vector<std::uint8_t> Bytes;
					 Bytes.reserve(A.ElementCount());
					 for (std::size_t I = 0; I < A.ElementCount(); ++I)
					 {
						 Bytes.push_back(Element<std::uint8_t>(A, I));
					 }
					 FBuffer B;
					 if (!DecodeWav(Bytes, B, E))
					 {
						 return false;
					 }
					 Out = Make(State, 2, std::move(B));
					 return true;
				 },
				 Names[2], {}, {"uint8"}))
		{
			return false;
		}
		for (unsigned Field = 0; Field < 3; ++Field)
		{
			if (!Add(2,
					 Field == 0	  ? "FrameCount"
					 : Field == 1 ? "SampleRate"
								  : "Channels",
					 Getter, ENativeType::Int64, {},
					 [Field](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto& B = This<FBuffer>(C);
						 Out = Field == 0	? static_cast<std::int64_t>(B.Samples.size() / B.Format.Channels)
							   : Field == 1 ? static_cast<std::int64_t>(B.Format.SampleRate)
											: static_cast<std::int64_t>(B.Format.Channels);
						 return true;
					 }))
			{
				return false;
			}
		}
		if (!AddConst(
				2, "GetSamples", Method, ENativeType::Array, {},
				[](const FNativeCallContext& C, FValue& Out, std::string&)
				{
					const auto& B = This<FBuffer>(C);
					auto A = std::make_shared<FDynamicArray>();
					A->ElementTypeName = "double";
					A->ScalarElements = std::vector<double>(B.Samples.begin(), B.Samples.end());
					Out = std::move(A);
					return true;
				},
				"", {}, {}, "double"))
		{
			return false;
		}
		if (!AddConst(2, "GetSample", Method, ENativeType::Double, {ENativeType::Int64},
				 [](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 const auto& B = This<FBuffer>(C);
					 const auto I = std::get<std::int64_t>(C.Args[0]);
					 if (I < 0 || static_cast<std::uint64_t>(I) >= B.Samples.size())
					 {
						 E = "audio.invalid_index: sample index out of range";
						 return false;
					 }
					 Out = static_cast<double>(B.Samples[static_cast<std::size_t>(I)]);
					 return true;
				 }))
		{
			return false;
		}
		if (!AddConst(1, "IsOpen", Method, ENativeType::Bool, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<std::shared_ptr<IStream>>(C)->IsOpen();
					 return true;
				 }))
		{
			return false;
		}
		if (!AddConst(
				1, "GetState", Method, ENativeType::HostObject, {},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					FStreamState S;
					if (!This<std::shared_ptr<IStream>>(C)->GetState(S, E))
					{
						return false;
					}
					Out = Make(State, 6, S);
					return true;
				},
				Names[6]))
		{
			return false;
		}
		if (!Add(1, "SetPaused", Method, ENativeType::Void, {ENativeType::Bool},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IStream>>(C)->SetPaused(std::get<bool>(C.Args[0]), E);
				 }) ||
			!Add(1, "SetGain", Method, ENativeType::Void, {ENativeType::Double},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto Gain = std::get<double>(C.Args[0]);
					 return ValidateGain(Gain, E) && This<std::shared_ptr<IStream>>(C)->SetGain(Gain, E);
				 }))
		{
			return false;
		}
		if (!Add(1, "QueueSamples", Method, ENativeType::Void, {ENativeType::Array},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto& S = This<std::shared_ptr<IStream>>(C);
					 std::vector<float> Copy;
					 if (!Samples(C.Args[0], S->GetOptions().Format, Copy, E))
					 {
						 return false;
					 }
					 return S->Queue(Copy, E);
				 },
				 "", {}, {"double"}))
		{
			return false;
		}
		if (!Add(1, "Queue", Method, ENativeType::Void, {ENativeType::HostObject},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto* B = Get<FBuffer>(C.Args[0], E);
					 if (!B)
					 {
						 return false;
					 }
					 const auto& S = This<std::shared_ptr<IStream>>(C);
					 const auto F = S->GetOptions().Format;
					 if (F.SampleRate != B->Format.SampleRate || F.Channels != B->Format.Channels)
					 {
						 E = "audio.format_mismatch: buffer must match the stream application format";
						 return false;
					 }
					 return S->Queue(B->Samples, E);
				 },
				 "", {Names[2]}))
		{
			return false;
		}
		if (!Add(
				1, "Read", Method, ENativeType::HostObject, {ENativeType::Int64},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					const auto& S = This<std::shared_ptr<IStream>>(C);
					FBuffer B;
					const auto Options = S->GetOptions();
					B.Format = Options.Format;
					const auto Count = std::get<std::int64_t>(C.Args[0]);
					if (Count < 0 || Count > Options.MaxBufferedFrames)
					{
						E = "audio.invalid_count: recording read exceeds the stream budget";
						return false;
					}
					if (!S->Read(Count, B.Samples, E))
					{
						return false;
					}
					Out = Make(State, 2, std::move(B));
					return true;
				},
				Names[2]))
		{
			return false;
		}
		for (unsigned Op = 0; Op < 3; ++Op)
		{
			if (!Add(1,
					 Op == 0   ? "Flush"
					 : Op == 1 ? "Clear"
							   : "Close",
					 Method, ENativeType::Void, {},
					 [Op](const FNativeCallContext& C, FValue&, std::string& E)
					 {
						 const auto& S = This<std::shared_ptr<IStream>>(C);
						 return Op == 0 ? S->Flush(E) : Op == 1 ? S->Clear(E) : S->Close(E);
					 }))
			{
				return false;
			}
		}
		if (!Add(3, "Count", Getter, ENativeType::Int64, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = static_cast<std::int64_t>(This<std::vector<FDeviceInfo>>(C).size());
					 return true;
				 }) ||
			!AddConst(
				3, "Get", Method, ENativeType::HostObject, {ENativeType::Int64},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					const auto& D = This<std::vector<FDeviceInfo>>(C);
					const auto I = std::get<std::int64_t>(C.Args[0]);
					if (I < 0 || static_cast<std::uint64_t>(I) >= D.size())
					{
						E = "audio.invalid_index: device index out of range";
						return false;
					}
					Out = Make(State, 4, D[static_cast<std::size_t>(I)]);
					return true;
				},
				Names[4]))
		{
			return false;
		}
		if (!Add(
				4, "Direction", Getter, ENativeType::Enum, {},
				[](const FNativeCallContext& C, FValue& Out, std::string&)
				{
					Out = FEnumValue{"VS.Audio.Direction", static_cast<std::int64_t>(This<FDeviceInfo>(C).Direction)};
					return true;
				},
				"VS.Audio.Direction") ||
			!Add(4, "DeviceId", Getter, ENativeType::UInt64, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<FDeviceInfo>(C).DeviceId;
					 return true;
				 }) ||
			!Add(4, "Name", Getter, ENativeType::String, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<FDeviceInfo>(C).Name;
					 return true;
				 }))
		{
			return false;
		}
		for (bool Channels : {false, true})
		{
			if (!Add(4, Channels ? "Channels" : "SampleRate", Getter, ENativeType::Int64, {},
					 [Channels](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto F = This<FDeviceInfo>(C).PreferredFormat;
						 Out = static_cast<std::int64_t>(Channels ? F.Channels : F.SampleRate);
						 return true;
					 }))
			{
				return false;
			}
		}
		if (!Add(
				5, "Type", Getter, ENativeType::Enum, {},
				[](const FNativeCallContext& C, FValue& Out, std::string&)
				{
					Out = FEnumValue{"VS.Audio.EventType", static_cast<std::int64_t>(This<FEvent>(C).Type)};
					return true;
				},
				"VS.Audio.EventType") ||
			!Add(
				5, "Device", Getter, ENativeType::HostObject, {},
				[State](const FNativeCallContext& C, FValue& Out, std::string&)
				{
					Out = Make(State, 4, This<FEvent>(C).Device);
					return true;
				},
				Names[4]))
		{
			return false;
		}
		if (!Add(6, "PendingFrames", Getter, ENativeType::Int64, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<FStreamState>(C).PendingFrames;
					 return true;
				 }) ||
			!Add(6, "Gain", Getter, ENativeType::Double, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<FStreamState>(C).Gain;
					 return true;
				 }))
		{
			return false;
		}
		for (unsigned Field = 0; Field < 3; ++Field)
		{
			if (!Add(6,
					 Field == 0	  ? "Paused"
					 : Field == 1 ? "Connected"
								  : "Overflowed",
					 Getter, ENativeType::Bool, {},
					 [Field](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto& S = This<FStreamState>(C);
						 Out = Field == 0 ? S.bPaused : Field == 1 ? S.bConnected : S.bOverflowed;
						 return true;
					 }))
			{
				return false;
			}
		}
		Registry = std::move(Candidate);
		Error.clear();
		return true;
	}
}
