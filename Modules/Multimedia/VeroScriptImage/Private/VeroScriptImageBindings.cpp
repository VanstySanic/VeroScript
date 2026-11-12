// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptImageBindings.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace VeroScript::Image
{
	namespace
	{
		using FCall = std::function<bool(const FNativeCallContext &, FValue &, std::string &)>;
		struct FState
		{
			std::vector<FHostTypeId> Types;
			std::vector<std::string> Names;
			FResourceReader Reader;
			FServiceFactory Factory;
		};
		template <class T> struct FDelete
		{
			void operator()(FHostObject *Object) const noexcept
			{
				delete static_cast<T *>(Object->Payload);
				delete Object;
			}
		};
		template <class T> FValue Make(const std::shared_ptr<FState> &State, std::size_t Index, T Value)
		{
			auto Payload = std::make_unique<T>(std::move(Value));
			auto Object = std::make_unique<FHostObject>();
			Object->TypeId = State->Types[Index].Value;
			Object->TypeName = State->Names[Index];
			Object->ModuleOwner = State;
			Object->Payload = Payload.release();
			return FHostObjectHandle(Object.release(), FDelete<T>{});
		}
		bool Invoke(const FNativeCallContext &Input, FValue &Result, std::string &Error)
		{
			try
			{
				auto Call = Input;
				if (Call.HostThis)
				{
					++Call.Args;
					--Call.ArgCount;
				}
				return (*static_cast<const FCall *>(Call.UserData))(Call, Result, Error);
			}
			catch (const std::exception &Exception)
			{
				Error = Exception.what();
				return false;
			}
			catch (...)
			{
				Error = "image.native_exception";
				return false;
			}
		}
		bool Member(FHostBindingRegistry &Registry, const std::shared_ptr<FState> &State, unsigned Index,
					const char *Name, EHostMemberKind Kind, FNativeTypeDesc Return, std::vector<ENativeType> Params,
					FCall Callback, std::string &Error)
		{
			auto Owner = std::make_shared<FCall>(std::move(Callback));
			FHostMemberDesc Desc;
			Desc.Name = Name;
			Desc.Kind = Kind;
			Desc.ReturnType = Return.Type;
			Desc.ReturnHostTypeName = Return.HostTypeName;
			Desc.ParamTypes = std::move(Params);
			Desc.Callback = Invoke;
			Desc.UserData = Owner.get();
			Desc.CallbackOwner = Owner;
			Desc.bConstMethod = Kind == EHostMemberKind::Method;
			return Registry.RegisterMember(State->Types[Index], std::move(Desc), Error);
		}
		bool Read(const FState &State, const std::string &Source, std::vector<std::uint8_t> &Bytes, std::string &Error)
		{
			Error.clear();
			if (Source.empty() || Source.find('\0') != std::string::npos)
			{
				Error = "image.invalid_source";
				return false;
			}
			if (State.Reader)
			{
				if (State.Reader(Source, Bytes, Error))
				{
					if (Bytes.size() > FDecodeOptions{}.MaxSourceBytes)
					{
						Error = "image.invalid_image";
						return false;
					}
					return true;
				}
				if (Error.empty())
				{
					Error = "image.read_failed: " + Source;
				}
				return false;
			}
			std::ifstream File(std::filesystem::u8path(Source), std::ios::binary | std::ios::ate);
			const auto Size = File.tellg();
			if (!File || Size < 0 || Size > 64 * 1024 * 1024)
			{
				Error = "image.read_failed: " + Source;
				return false;
			}
			Bytes.resize(static_cast<std::size_t>(Size));
			File.seekg(0);
			if (!Bytes.empty() && !File.read(reinterpret_cast<char *>(Bytes.data()), Size))
			{
				Error = "image.read_failed: " + Source;
				return false;
			}
			return true;
		}
		std::size_t Index(std::int64_t Value, std::size_t Size)
		{
			if (Value < 0 || static_cast<std::uint64_t>(Value) >= Size)
			{
				throw std::out_of_range("image.index_out_of_range");
			}
			return static_cast<std::size_t>(Value);
		}

	} // namespace

	bool RegisterScriptBindings(FHostBindingRegistry &Registry, FServiceFactory Factory, std::string &Error,
								FResourceReader Reader)
	{
		auto Candidate = Registry;
		auto State = std::make_shared<FState>();
		State->Reader = std::move(Reader);
		State->Factory = std::move(Factory);
		State->Names = {"VS.Image.Bitmap", "VS.Image.Service"};
		State->Types.resize(State->Names.size());
		for (std::size_t I = 0; I < State->Names.size(); ++I)
		{
			if (!Candidate.RegisterType(State->Names[I], EHostTypeKind::Class, State->Types[I], Error))
			{
				return false;
			}
		}
		const auto Int64 = FNativeTypeDesc::Scalar(ENativeType::Int64);
		if (!Member(
				Candidate, State, 1, "Load", EHostMemberKind::Method, FNativeTypeDesc::Host(State->Names[0]),
				{ENativeType::String},
				[State](const auto &Call, auto &Out, auto &Failure)
				{
					std::vector<std::uint8_t> Bytes;
					FDecodedImage Image;
					if (!Read(*State, std::get<std::string>(Call.Args[0]), Bytes, Failure) ||
						!(*static_cast<const std::shared_ptr<IService> *>(Call.HostThis))
							 ->Decode(Bytes, Image, Failure))
					{
						return false;
					}
					if (!Validate(Image, Failure))
					{
						return false;
					}
					Out = Make(State, 0, std::move(Image));
					return true;
				},
				Error))
		{
			return false;
		}
		if (!Member(
				Candidate, State, 1, "Decode", EHostMemberKind::Method, FNativeTypeDesc::Host(State->Names[0]),
				{ENativeType::String},
				[State](const auto &Call, auto &Out, auto &Failure)
				{
					const auto &Bytes = std::get<std::string>(Call.Args[0]);
					// Reject before materializing the backend's owned byte vector.
					if (Bytes.size() > FDecodeOptions{}.MaxSourceBytes)
					{
						Failure = "image.invalid_image";
						return false;
					}
					FDecodedImage Image;
					if (!(*static_cast<const std::shared_ptr<IService> *>(Call.HostThis))
							 ->Decode({Bytes.begin(), Bytes.end()}, Image, Failure))
					{
						return false;
					}
					if (!Validate(Image, Failure))
					{
						return false;
					}
					Out = Make(State, 0, std::move(Image));
					return true;
				},
				Error))
		{
			return false;
		}
		for (int Field = 0; Field < 3; ++Field)
		{
			const char *Names[] = {"GetWidth", "GetHeight", "GetByteCount"};
			if (!Member(
					Candidate, State, 0, Names[Field], EHostMemberKind::Method, Int64, {},
					[Field](const auto &Call, auto &Out, auto &)
					{
						const auto &Image = *static_cast<const FDecodedImage *>(Call.HostThis);
						Out = static_cast<std::int64_t>(Field == 0	 ? Image.Width
														: Field == 1 ? Image.Height
																	 : Image.Rgba.size());
						return true;
					},
					Error))
			{
				return false;
			}
		}
		if (!Member(
				Candidate, State, 0, "GetByte", EHostMemberKind::Method, Int64, {ENativeType::Int64},
				[](const auto &Call, auto &Out, auto &)
				{
					const auto &Pixels = static_cast<const FDecodedImage *>(Call.HostThis)->Rgba;
					Out = static_cast<std::int64_t>(Pixels[Index(std::get<std::int64_t>(Call.Args[0]), Pixels.size())]);
					return true;
				},
				Error))
		{
			return false;
		}
		if (!Member(
				Candidate, State, 1, "Create", EHostMemberKind::StaticMethod, FNativeTypeDesc::Host(State->Names[1]),
				{ENativeType::String},
				[State](const auto &Call, auto &Out, auto &Failure)
				{
					if (!State->Factory)
					{
						Failure = "image.service_factory_unavailable";
						return false;
					}
					auto Service = State->Factory(std::get<std::string>(Call.Args[0]), Failure);
					if (!Service)
					{
						if (Failure.empty())
						{
							Failure = "image.service_factory_unavailable";
						}
						return false;
					}
					Out = Make(State, 1, std::move(Service));
					return true;
				},
				Error))
		{
			return false;
		}
		Registry = std::move(Candidate);
		return true;
	}
} // namespace VeroScript::Image
