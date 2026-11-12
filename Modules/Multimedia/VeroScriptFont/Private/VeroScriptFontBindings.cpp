// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFontBindings.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace VeroScript::Font
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
				Error = "font.native_exception";
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
				Error = "font.invalid_source";
				return false;
			}
			if (State.Reader)
			{
				if (State.Reader(Source, Bytes, Error))
				{
					return true;
				}
				if (Error.empty())
				{
					Error = "font.read_failed: " + Source;
				}
				return false;
			}
			std::ifstream File(std::filesystem::u8path(Source), std::ios::binary | std::ios::ate);
			const auto Size = File.tellg();
			if (!File || Size < 0 || Size > 128 * 1024 * 1024)
			{
				Error = "font.read_failed: " + Source;
				return false;
			}
			Bytes.resize(static_cast<std::size_t>(Size));
			File.seekg(0);
			if (!Bytes.empty() && !File.read(reinterpret_cast<char *>(Bytes.data()), Size))
			{
				Error = "font.read_failed: " + Source;
				return false;
			}
			return true;
		}
		std::size_t Index(std::int64_t Value, std::size_t Size)
		{
			if (Value < 0 || static_cast<std::uint64_t>(Value) >= Size)
			{
				throw std::out_of_range("font.index_out_of_range");
			}
			return static_cast<std::size_t>(Value);
		}
		std::int32_t PixelSize(std::int64_t Value)
		{
			if (Value < 1 || Value > 16384)
			{
				throw std::out_of_range("font.invalid_size");
			}
			return static_cast<std::int32_t>(Value);
		}
	} // namespace

	bool RegisterScriptBindings(FHostBindingRegistry &Registry, FServiceFactory Factory, std::string &Error,
								FResourceReader Reader)
	{
		auto Candidate = Registry;
		auto State = std::make_shared<FState>();
		State->Reader = std::move(Reader);
		State->Factory = std::move(Factory);
		State->Names = {"VS.Font.Face", "VS.Font.ShapedText", "VS.Font.Bitmap", "VS.Font.Metrics", "VS.Font.Service"};
		State->Types.resize(State->Names.size());
		for (std::size_t I = 0; I < State->Names.size(); ++I)
		{
			if (!Candidate.RegisterType(State->Names[I], EHostTypeKind::Class, State->Types[I], Error))
			{
				return false;
			}
		}
		const auto Int64 = FNativeTypeDesc::Scalar(ENativeType::Int64);
		const auto Double = FNativeTypeDesc::Scalar(ENativeType::Double);
		for (int Default = 0; Default < 2; ++Default)
		{
			if (!Member(
					Candidate, State, 4, Default ? "LoadDefault" : "Load", EHostMemberKind::Method,
					FNativeTypeDesc::Host(State->Names[0]),
					Default ? std::vector<ENativeType>{} : std::vector<ENativeType>{ENativeType::String},
					[State, Default](const auto &Call, auto &Out, auto &Failure)
					{
						std::vector<std::uint8_t> Bytes;
						if (!Default && !Read(*State, std::get<std::string>(Call.Args[0]), Bytes, Failure))
						{
							return false;
						}
						const auto &Source = Default ? GetDefaultFontData() : Bytes;
						auto Face =
							(*static_cast<const std::shared_ptr<IService> *>(Call.HostThis))->Load(Source, 0, Failure);
						if (!Face)
						{
							return false;
						}
						Out = Make(State, 0, std::move(Face));
						return true;
					},
					Error))
			{
				return false;
			}
		}
		if (!Member(
				Candidate, State, 0, "GetGlyphIndex", EHostMemberKind::Method, Int64, {ENativeType::Int64},
				[](const auto &Call, auto &Out, auto &)
				{
					auto Value = std::get<std::int64_t>(Call.Args[0]);
					if (Value < 0 || Value > 0x10ffff)
					{
						throw std::out_of_range("font.invalid_character");
					}
					Out = static_cast<std::int64_t>((*static_cast<const std::shared_ptr<IFace> *>(Call.HostThis))
														->GetGlyphIndex(static_cast<char32_t>(Value)));
					return true;
				},
				Error))
		{
			return false;
		}
		for (int Extended = 0; Extended < 2; ++Extended)
		{
			if (!Member(
					Candidate, State, 0, "Shape", EHostMemberKind::Method, FNativeTypeDesc::Host(State->Names[1]),
					Extended ? std::vector<ENativeType>{ENativeType::String, ENativeType::Int64, ENativeType::String,
														ENativeType::String}
							 : std::vector<ENativeType>{ENativeType::String, ENativeType::Int64},
					[State, Extended](const auto &Call, auto &Out, auto &Failure)
					{
						FShapeOptions Options;
						Options.PixelSize = PixelSize(std::get<std::int64_t>(Call.Args[1]));
						if (Extended)
						{
							Options.Language = std::get<std::string>(Call.Args[2]);
							const auto &Direction = std::get<std::string>(Call.Args[3]);
							if (Direction == "ltr")
							{
								Options.Direction = EDirection::LeftToRight;
							}
							else if (Direction == "rtl")
							{
								Options.Direction = EDirection::RightToLeft;
							}
							else if (Direction != "auto")
							{
								Failure = "font.invalid_direction";
								return false;
							}
						}
						FShapedText Text;
						if (!(*static_cast<const std::shared_ptr<IFace> *>(Call.HostThis))
								 ->Shape(std::get<std::string>(Call.Args[0]), Options, Text, Failure))
						{
							return false;
						}
						Out = Make(State, 1, std::move(Text));
						return true;
					},
					Error))
			{
				return false;
			}
		}
		if (!Member(
				Candidate, State, 0, "Rasterize", EHostMemberKind::Method, FNativeTypeDesc::Host(State->Names[2]),
				{ENativeType::Int64, ENativeType::Int64},
				[State](const auto &Call, auto &Out, auto &Failure)
				{
					const auto Id = std::get<std::int64_t>(Call.Args[0]);
					if (Id < 0 || static_cast<std::uint64_t>(Id) > UINT32_MAX)
					{
						Failure = "font.invalid_glyph";
						return false;
					}
					FBitmap Bitmap;
					if (!(*static_cast<const std::shared_ptr<IFace> *>(Call.HostThis))
							 ->Rasterize(static_cast<std::uint32_t>(Id),
										 PixelSize(std::get<std::int64_t>(Call.Args[1])), Bitmap, Failure))
					{
						return false;
					}
					if (!Validate(Bitmap, Failure))
					{
						return false;
					}
					Out = Make(State, 2, std::move(Bitmap));
					return true;
				},
				Error))
		{
			return false;
		}
		if (!Member(
				Candidate, State, 0, "GetMetrics", EHostMemberKind::Method, FNativeTypeDesc::Host(State->Names[3]),
				{ENativeType::Int64},
				[State](const auto &Call, auto &Out, auto &Failure)
				{
					FMetrics Metrics;
					if (!(*static_cast<const std::shared_ptr<IFace> *>(Call.HostThis))
							 ->GetMetrics(PixelSize(std::get<std::int64_t>(Call.Args[0])), Metrics, Failure))
					{
						return false;
					}
					Out = Make(State, 3, Metrics);
					return true;
				},
				Error))
		{
			return false;
		}
		if (!Member(
				Candidate, State, 1, "GetGlyphCount", EHostMemberKind::Method, Int64, {},
				[](const auto &Call, auto &Out, auto &)
				{
					Out = static_cast<std::int64_t>(static_cast<const FShapedText *>(Call.HostThis)->Glyphs.size());
					return true;
				},
				Error))
		{
			return false;
		}
		for (int Field = 0; Field < 6; ++Field)
		{
			const char *Names[] = {"GetGlyphIndex", "GetCluster", "GetAdvanceX",
								   "GetAdvanceY",	"GetOffsetX", "GetOffsetY"};
			if (!Member(
					Candidate, State, 1, Names[Field], EHostMemberKind::Method, Field < 2 ? Int64 : Double,
					{ENativeType::Int64},
					[Field](const auto &Call, auto &Out, auto &)
					{
						const auto &Items = static_cast<const FShapedText *>(Call.HostThis)->Glyphs;
						const auto &G = Items[Index(std::get<std::int64_t>(Call.Args[0]), Items.size())];
						if (Field < 2)
						{
							Out = static_cast<std::int64_t>(Field == 0 ? G.Glyph : G.Cluster);
						}
						else
						{
							Out = (Field == 2	? G.XAdvance
								   : Field == 3 ? G.YAdvance
								   : Field == 4 ? G.XOffset
												: G.YOffset) /
								  64.;
						}
						return true;
					},
					Error))
			{
				return false;
			}
		}
		for (int Field = 0; Field < 6; ++Field)
		{
			const char *Names[] = {"GetWidth", "GetHeight", "GetBearingX", "GetBearingY", "GetAdvance", "GetByteCount"};
			if (!Member(
					Candidate, State, 2, Names[Field], EHostMemberKind::Method, Int64, {},
					[Field](const auto &Call, auto &Out, auto &)
					{
						const auto &B = *static_cast<const FBitmap *>(Call.HostThis);
						// Keep signed glyph metrics signed on 32-bit targets too.
						Out = Field == 0   ? B.Width
							  : Field == 1 ? B.Height
							  : Field == 2 ? B.BearingX
							  : Field == 3 ? B.BearingY
							  : Field == 4 ? B.Advance
										   : static_cast<std::int64_t>(B.Pixels.size());
						return true;
					},
					Error))
			{
				return false;
			}
		}
		if (!Member(
				Candidate, State, 2, "GetByte", EHostMemberKind::Method, Int64, {ENativeType::Int64},
				[](const auto &Call, auto &Out, auto &)
				{
					const auto &B = static_cast<const FBitmap *>(Call.HostThis)->Pixels;
					Out = static_cast<std::int64_t>(B[Index(std::get<std::int64_t>(Call.Args[0]), B.size())]);
					return true;
				},
				Error))
		{
			return false;
		}
		if (!Member(
				Candidate, State, 2, "IsColor", EHostMemberKind::Method, FNativeTypeDesc::Scalar(ENativeType::Bool), {},
				[](const auto &Call, auto &Out, auto &)
				{
					Out = static_cast<const FBitmap *>(Call.HostThis)->bColor;
					return true;
				},
				Error))
		{
			return false;
		}
		for (int Field = 0; Field < 6; ++Field)
		{
			const char *Names[] = {"GetAscent",	 "GetDescent",			 "GetLineSpacing",
								   "GetXHeight", "GetUnderlinePosition", "GetUnderlineThickness"};
			if (!Member(
					Candidate, State, 3, Names[Field], EHostMemberKind::Method, Double, {},
					[Field](const auto &Call, auto &Out, auto &)
					{
						const auto &M = *static_cast<const FMetrics *>(Call.HostThis);
						Out = Field == 0   ? M.Ascent
							  : Field == 1 ? M.Descent
							  : Field == 2 ? M.LineSpacing
							  : Field == 3 ? M.XHeight
							  : Field == 4 ? M.UnderlinePosition
										   : M.UnderlineThickness;
						return true;
					},
					Error))
			{
				return false;
			}
		}
		if (!Member(
				Candidate, State, 4, "Create", EHostMemberKind::StaticMethod, FNativeTypeDesc::Host(State->Names[4]),
				{ENativeType::String},
				[State](const auto &Call, auto &Out, auto &Failure)
				{
					if (!State->Factory)
					{
						Failure = "font.service_factory_unavailable";
						return false;
					}
					auto Service = State->Factory(std::get<std::string>(Call.Args[0]), Failure);
					if (!Service)
					{
						if (Failure.empty())
						{
							Failure = "font.service_factory_unavailable";
						}
						return false;
					}
					Out = Make(State, 4, std::move(Service));
					return true;
				},
				Error))
		{
			return false;
		}
		Registry = std::move(Candidate);
		return true;
	}
} // namespace VeroScript::Font
