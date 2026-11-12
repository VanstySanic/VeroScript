// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericBindings.h"
#include "VeroScriptNumericsInternal.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptStandardLibrary.h"
#include "ExecutionSupport/VeroScriptVmNativeLeaves.h"

#include <algorithm>
#include <stdexcept>

namespace VeroScript::Numerics
{
	namespace
	{
		constexpr std::uint32_t TypeBase = 0xffffffa0u;
		constexpr const char* Names[] = {"std.bigint", "std.rational", "std.numeric.rounding_mode"};
		enum class EOp : unsigned
		{
			Construct,
			Parse,
			ParseBase,
			TryParse,
			TryParseBase,
			FromDouble,
			FromDoubleTrunc,
			Zero,
			Negative,
			Sign,
			Bits,
			Abs,
			Text,
			TextBase,
			Int64,
			UInt64,
			Double,
			DoubleExact,
			Float,
			FloatExact,
			TryInt64,
			TryUInt64,
			TryDouble,
			TryDoubleExact,
			TryFloat,
			TryFloatExact,
			Numerator,
			Denominator,
			Integer,
			Reciprocal,
			Truncate,
			Floor,
			Ceil,
			Round,
			RoundMode,
			Decimal,
			DecimalMode,
			FiniteDecimal,
			Plus,
			Negate,
			Not,
			Add,
			Subtract,
			Multiply,
			Divide,
			Remainder,
			And,
			Or,
			Xor,
			Left,
			Right,
			Equal,
			NotEqual,
			Less,
			LessEqual,
			Greater,
			GreaterEqual,
			Gcd,
			Lcm,
			Pow,
			DivMod
		};
		template <class T>
		constexpr unsigned TypeIndex()
		{
			return std::is_same_v<T, FBigInteger> ? 0 : 1;
		}
		template <class T>
		struct FNumericBox final : FHostObject
		{
			explicit FNumericBox(const T& InValue) : Value(InValue)
			{
				Payload = &Value;
			}

			T Value;
		};
		template <class T>
		struct FDelete
		{
			const FHostObject* Object = nullptr;

			void operator()(FHostObject* H) const noexcept
			{
				delete static_cast<FNumericBox<T>*>(H);
			}
		};
		template <class T>
		const T* Get(const FValue& V) noexcept
		{
			auto H = std::get_if<FHostObjectHandle>(&V);
			if (!H || !*H || (*H)->TypeName != Names[TypeIndex<T>()] || (*H)->TypeId != TypeBase + TypeIndex<T>())
			{
				return nullptr;
			}
			const auto* Owner = std::get_deleter<FDelete<T>>(*H);
			if (!Owner || Owner->Object != H->get()) return nullptr;
			const auto* Value = &static_cast<const FNumericBox<T>*>(H->get())->Value;
			return (*H)->Payload == Value ? Value : nullptr;
		}
		bool Clone(const FNativeCallContext&, FValue&, std::string&);
		bool Equal(const FNativeCallContext&, FValue&, std::string&);
		bool Hash(const FNativeCallContext&, FValue&, std::string&);
		template <class T>
		FValue Make(const T& V)
		{
			// Keep wrapper and immutable payload in one allocation. Each language
			// value still has its own wrapper/address and ordinary clone ownership.
			auto H = std::make_unique<FNumericBox<T>>(V);
			H->TypeId = TypeBase + TypeIndex<T>();
			H->TypeName = Names[TypeIndex<T>()];
			H->CloneCallback = Clone;
			H->EqualsCallback = Equal;
			H->HashCallback = Hash;
			const auto* Object = H.get();
			return FHostObjectHandle(H.release(), FDelete<T>{Object});
		}
		bool Report(const FNativeCallContext& C, const FNumericError& E, std::string& Error)
		{
			Error = E.MessageId ? E.MessageId : "diag.numeric_internal";
			if (C.DiagnosticOutput)
			{
				C.DiagnosticOutput->MessageId = Error;
				C.DiagnosticOutput->Message = Error;
				C.DiagnosticOutput->MessageArgs = {E.Reason ? E.Reason : "", std::to_string(E.Offset)};
			}
			return false;
		}
		bool Clone(const FNativeCallContext& C, FValue& Out, std::string& Error)
		{
			FNumericError E;
			if (C.ArgCount == 1)
			{
				if (auto V = Get<FBigInteger>(C.Args[0]))
				{
					return ToScriptValue(*V, Out, E) || Report(C, E, Error);
				}
				if (auto V = Get<FRational>(C.Args[0]))
				{
					return ToScriptValue(*V, Out, E) || Report(C, E, Error);
				}
			}
			E = {ENumericStatus::TypeMismatch, "diag.numeric_type_mismatch", "numeric clone"};
			return Report(C, E, Error);
		}
		bool Equal(const FNativeCallContext& C, FValue& Out, std::string&)
		{
			bool Result = false;
			if (C.ArgCount == 2)
			{
				if (auto L = Get<FBigInteger>(C.Args[0]))
				{
					if (auto R = Get<FBigInteger>(C.Args[1]))
					{
						Result = *L == *R;
					}
				}
				else if (auto Fraction = Get<FRational>(C.Args[0]))
				{
					if (auto R = Get<FRational>(C.Args[1]))
					{
						Result = *Fraction == *R;
					}
				}
			}
			Out = Result;
			return true;
		}
		bool Hash(const FNativeCallContext& C, FValue& Out, std::string&)
		{
			std::uint64_t Result = 0;
			if (C.ArgCount == 1)
			{
				if (auto V = Get<FBigInteger>(C.Args[0]))
				{
					Result = V->Hash();
				}
				else if (auto Fraction = Get<FRational>(C.Args[0]))
				{
					Result = Fraction->Hash();
				}
			}
			Out = Result;
			return true;
		}
		FBigInteger Scalar(const FValue& V)
		{
			return std::visit(
			    [](const auto& X) -> FBigInteger
			{
				using T = std::decay_t<decltype(X)>;
				if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
				{
					if constexpr (std::is_signed_v<T>)
					{
						if (X < 0)
						{
							return FNumericAccess::Small(0ull - static_cast<std::uint64_t>(X), true);
						}
					}
					return FNumericAccess::Small(static_cast<std::uint64_t>(X));
				}
				else
				{
					Detail::Fail(ENumericStatus::TypeMismatch, "integer source type");
				}
			},
			    V);
		}
		bool ParseFailure(ENumericStatus S, EOp Op) noexcept
		{
			if (Op == EOp::TryParse || Op == EOp::TryParseBase)
			{
				return S == ENumericStatus::InvalidText;
			}
			return S == ENumericStatus::OutOfRange || S == ENumericStatus::InexactConversion;
		}
		ENumericRoundingMode Rounding(const FValue& V)
		{
			const auto& E = std::get<FEnumValue>(V);
			if (E.Value < 0 || E.Value > 5)
			{
				Detail::Fail(ENumericStatus::InvalidArgument, "rounding mode");
			}
			return static_cast<ENumericRoundingMode>(E.Value);
		}
		template <class T>
		bool Invoke(EOp Op, const FNativeCallContext& C, FValue& Out)
		{
			FNumericOperationContext Context = C.NumericServices->GetNumericOperationContext();
			{
				Detail::FArea Area(Context);
				Area.Check();
			}
			FNumericError E;
			T Value;
			const T* This = C.ArgCount ? Get<T>(C.Args[0]) : nullptr;
			const auto Need = [&](bool Success)
			{
				if (!Success)
				{
					throw Detail::FFailure{E};
				}
			};
			const auto Store = [&](const T& V)
			{
				Need(ToScriptValue(V, Out, E));
			};
			if (Op == EOp::Construct)
			{
				if (C.ArgCount == 0)
				{
					Store(Value);
					return true;
				}
				if (auto Text = std::get_if<std::string>(&C.Args[0]))
				{
					Need(TryParse(*Text, Value, Context, E));
				}
				else if constexpr (std::is_same_v<T, FBigInteger>)
				{
					Value = Scalar(C.Args[0]);
					Detail::FArea Area(Context);
					Area.Input(Value);
					Area.Check();
				}
				else
				{
					auto N = Get<FBigInteger>(C.Args[0]) ? *Get<FBigInteger>(C.Args[0]) : Scalar(C.Args[0]);
					auto D = C.ArgCount < 2           ? FNumericAccess::Small(1)
					    : Get<FBigInteger>(C.Args[1]) ? *Get<FBigInteger>(C.Args[1])
					                                  : Scalar(C.Args[1]);
					if (Get<FBigInteger>(C.Args[0]))
					{
						Need(TryMakeRational(N, D, Value, Context, E));
					}
					else
					{
						// Fixed-width constructor inputs are temporary operands; the reduced
						// numerator and denominator determine the published value size.
						Need(Detail::Boundary(
						    Context, E,
						    [&](Detail::FArea& A)
						{
							const auto Working = [&](const FBigInteger& V)
							{
								const auto Magnitude =
								    static_cast<std::uint64_t>(FNumericAccess::Word(V, 0)) |
								    (static_cast<std::uint64_t>(FNumericAccess::Word(V, 1)) << 32);
								return Detail::Small(A, Magnitude, V.IsNegative());
							};
							auto Prepared = Detail::PublishRational(Working(N), Working(D));
							A.Check();
							Value.Swap(Prepared);
						}));
					}
				}
				Store(Value);
				return true;
			}
			if (Op == EOp::Parse || Op == EOp::ParseBase || Op == EOp::TryParse || Op == EOp::TryParseBase)
			{
				bool Try = Op == EOp::TryParse || Op == EOp::TryParseBase;
				bool Ok;
				if constexpr (std::is_same_v<T, FBigInteger>)
				{
					Ok = TryParse(std::get<std::string>(C.Args[0]), Value, Context, E,
					    Op == EOp::ParseBase          ? std::get<std::int32_t>(C.Args[1])
					        : Op == EOp::TryParseBase ? std::get<std::int32_t>(C.Args[2])
					                                  : 10);
				}
				else
				{
					Ok = TryParse(std::get<std::string>(C.Args[0]), Value, Context, E);
				}
				if (!Ok && Try && ParseFailure(E.Status, Op))
				{
					Out = false;
					return true;
				}
				Need(Ok);
				if (Try)
				{
					auto Slot = C.MutableArg(1);
					if (!Slot)
					{
						Detail::Fail(ENumericStatus::InvalidArgument, "missing parse output reference");
					}
					FValue Prepared;
					Need(ToScriptValue(Value, Prepared, E));
					*Slot = std::move(Prepared);
					Out = true;
				}
				else
				{
					Store(Value);
				}
				return true;
			}
			if (Op == EOp::FromDouble || Op == EOp::FromDoubleTrunc)
			{
				if constexpr (std::is_same_v<T, FBigInteger>)
				{
					Need(
					    TryFromDouble(std::get<double>(C.Args[0]), Value, Op == EOp::FromDouble, Context, E));
				}
				else
				{
					Need(TryFromDouble(std::get<double>(C.Args[0]), Value, Context, E));
				}
				Store(Value);
				return true;
			}
			if (Op == EOp::Gcd || Op == EOp::Lcm || Op == EOp::DivMod)
			{
				if constexpr (std::is_same_v<T, FBigInteger>)
				{
					auto Right = Get<FBigInteger>(C.Args[1]);
					if (!This || !Right)
					{
						Detail::Fail(ENumericStatus::TypeMismatch, "integer arguments");
					}
					if (Op == EOp::DivMod)
					{
						FBigInteger R;
						Need(TryDivMod(*This, *Right, Value, R, Context, E));
						auto QSlot = C.MutableArg(2), RSlot = C.MutableArg(3);
						if (!QSlot || !RSlot || QSlot == RSlot)
						{
							Detail::Fail(ENumericStatus::InvalidArgument, "divmod output references");
						}
						FValue QV, RV;
						Need(ToScriptValue(Value, QV, E));
						Need(ToScriptValue(R, RV, E));
						*QSlot = std::move(QV);
						*RSlot = std::move(RV);
						return true;
					}
					Need(Op == EOp::Gcd ? TryGcd(*This, *Right, Value, Context, E)
					                    : TryLcm(*This, *Right, Value, Context, E));
					Store(Value);
					return true;
				}
			}
			if (!This)
			{
				Detail::Fail(ENumericStatus::TypeMismatch, "numeric receiver");
			}
			if (Op != EOp::Zero && Op != EOp::Negative && Op != EOp::Sign && Op != EOp::Bits &&
			    Op != EOp::Numerator && Op != EOp::Denominator && Op != EOp::Integer && Op != EOp::Plus)
			{
				Detail::FArea Area(Context);
				Area.Input(*This);
			}
			if (Op == EOp::Zero)
			{
				Out = This->IsZero();
				return true;
			}
			if (Op == EOp::Negative)
			{
				Out = This->IsNegative();
				return true;
			}
			if (Op == EOp::Sign)
			{
				Out = static_cast<std::int32_t>(This->Sign());
				return true;
			}
			if (Op == EOp::Text || Op == EOp::TextBase)
			{
				std::string Text;
				if constexpr (std::is_same_v<T, FBigInteger>)
				{
					Need(TryToString(*This, Text, Context, E,
					    Op == EOp::TextBase ? std::get<std::int32_t>(C.Args[1]) : 10));
				}
				else
				{
					Need(TryToString(*This, Text, Context, E));
				}
				Out = std::move(Text);
				return true;
			}
			if (Op >= EOp::Int64 && Op <= EOp::TryFloatExact)
			{
				bool Try = Op >= EOp::TryInt64;
				auto Conversion =
				    Try ? static_cast<EOp>(static_cast<unsigned>(Op) - static_cast<unsigned>(EOp::TryInt64) +
				              static_cast<unsigned>(EOp::Int64))
				        : Op;
				FValue Prepared;
				bool Ok;
				if (Conversion == EOp::Int64)
				{
					std::int64_t V = 0;
					Ok = TryToInt64(*This, V, Context, E);
					Prepared = V;
				}
				else if (Conversion == EOp::UInt64)
				{
					std::uint64_t V = 0;
					Ok = TryToUInt64(*This, V, Context, E);
					Prepared = V;
				}
				else if (Conversion == EOp::Double || Conversion == EOp::DoubleExact)
				{
					double V = 0;
					Ok = TryToDouble(*This, V, Conversion == EOp::DoubleExact, Context, E);
					Prepared = V;
				}
				else
				{
					float V = 0;
					Ok = TryToFloat(*This, V, Conversion == EOp::FloatExact, Context, E);
					Prepared = V;
				}
				if (!Ok && Try && ParseFailure(E.Status, Op))
				{
					Out = false;
					return true;
				}
				Need(Ok);
				if (Try)
				{
					auto Slot = C.MutableArg(1);
					if (!Slot)
					{
						Detail::Fail(ENumericStatus::InvalidArgument, "missing conversion output reference");
					}
					*Slot = std::move(Prepared);
					Out = true;
				}
				else
				{
					Out = std::move(Prepared);
				}
				return true;
			}
			if (Op == EOp::Plus)
			{
				Store(*This);
				return true;
			}
			if (Op == EOp::Abs || Op == EOp::Negate)
			{
				Need(Op == EOp::Abs ? TryAbs(*This, Value, Context, E) : TryNegate(*This, Value, Context, E));
				Store(Value);
				return true;
			}
			if (Op == EOp::Pow)
			{
				Need(TryPow(*This, std::get<std::int64_t>(C.Args[1]), Value, Context, E));
				Store(Value);
				return true;
			}
			if (Op >= EOp::Add && Op <= EOp::Xor)
			{
				auto Right = Get<T>(C.Args[1]);
				if (!Right)
				{
					Detail::Fail(ENumericStatus::TypeMismatch, "numeric operand");
				}
				Need(TryBinary(static_cast<ENumericBinaryOperation>(static_cast<unsigned>(Op) -
				                   static_cast<unsigned>(EOp::Add)),
				    *This, *Right, Value, Context, E));
				Store(Value);
				return true;
			}
			if (Op >= EOp::Equal && Op <= EOp::GreaterEqual)
			{
				auto Right = Get<T>(C.Args[1]);
				if (!Right)
				{
					Detail::Fail(ENumericStatus::TypeMismatch, "numeric operand");
				}
				int Comparison = 0;
				Need(TryCompare(*This, *Right, Comparison, Context, E));
				Out = Op == EOp::Equal     ? Comparison == 0
				    : Op == EOp::NotEqual  ? Comparison != 0
				    : Op == EOp::Less      ? Comparison < 0
				    : Op == EOp::LessEqual ? Comparison <= 0
				    : Op == EOp::Greater   ? Comparison > 0
				                           : Comparison >= 0;
				return true;
			}
			if constexpr (std::is_same_v<T, FBigInteger>)
			{
				if (Op == EOp::Bits)
				{
					Out = static_cast<std::int64_t>(This->BitLength());
					return true;
				}
				if (Op == EOp::Not)
				{
					Need(TryNot(*This, Value, Context, E));
					Store(Value);
					return true;
				}
				if (Op == EOp::Left || Op == EOp::Right)
				{
					Need(TryShift(*This, std::get<std::int64_t>(C.Args[1]), Op == EOp::Left, Value, Context,
					    E));
					Store(Value);
					return true;
				}
			}
			else
			{
				if (Op == EOp::Numerator || Op == EOp::Denominator)
				{
					Need(ToScriptValue(Op == EOp::Numerator ? This->Numerator() : This->Denominator(), Out,
					    E));
					return true;
				}
				if (Op == EOp::Integer)
				{
					Out = This->IsInteger();
					return true;
				}
				if (Op == EOp::Reciprocal)
				{
					Need(TryReciprocal(*This, Value, Context, E));
					Store(Value);
					return true;
				}
				if (Op == EOp::FiniteDecimal)
				{
					bool V = false;
					Need(TryHasFiniteDecimal(*This, V, Context, E));
					Out = V;
					return true;
				}
				if (Op >= EOp::Truncate && Op <= EOp::RoundMode)
				{
					auto Mode = Op == EOp::Truncate ? ENumericRoundingMode::TowardZero
					    : Op == EOp::Floor          ? ENumericRoundingMode::Floor
					    : Op == EOp::Ceil           ? ENumericRoundingMode::Ceil
					    : Op == EOp::Round          ? ENumericRoundingMode::HalfEven
					                                : Rounding(C.Args[1]);
					FBigInteger V;
					Need(TryRound(*This, Mode, V, Context, E));
					Need(ToScriptValue(V, Out, E));
					return true;
				}
				if (Op == EOp::Decimal || Op == EOp::DecimalMode)
				{
					auto Mode = Op == EOp::Decimal ? ENumericRoundingMode::HalfEven : Rounding(C.Args[2]);
					std::string Text;
					Need(TryToDecimal(*This, std::get<std::int64_t>(C.Args[1]), Mode, Text, Context, E));
					Out = std::move(Text);
					return true;
				}
			}
			Detail::Fail(ENumericStatus::InvalidArgument, "numeric operation");
		}
		bool Member(const FNativeCallContext& C, FValue& Out, std::string& Error)
		{
			FNumericError E;
			try
			{
				if (!C.NumericServices)
				{
					Detail::Fail(ENumericStatus::InvalidArgument, "active numeric services required");
				}
				auto Code = reinterpret_cast<std::uintptr_t>(C.UserData);
				return Code & 65536 ? Invoke<FRational>(static_cast<EOp>(Code & 65535), C, Out)
				                    : Invoke<FBigInteger>(static_cast<EOp>(Code), C, Out);
			}
			catch (const Detail::FFailure& X)
			{
				E = X.Error;
			}
			catch (const std::bad_alloc&)
			{
				E = {ENumericStatus::OutOfMemory, "diag.numeric_out_of_memory", "host wrapper"};
			}
			catch (...)
			{
				E = {ENumericStatus::InternalError, "diag.numeric_internal", "native numeric callback"};
			}
			return Report(C, E, Error);
		}
		FHostBindingRegistry Build()
		{
			FHostBindingRegistry R;
			FHostTypeId Types[3];
			std::string Error;
			const auto Need = [&](bool Ok)
			{
				if (!Ok)
				{
					throw std::logic_error(Error);
				}
			};
			for (unsigned T = 0; T < 3; ++T)
			{
				Need(R.RegisterType(Names[T], T < 2 ? EHostTypeKind::Struct : EHostTypeKind::Enum, Types[T],
				    Error));
			}
			Need(R.SetEnumScoped(Types[2], true, Error));
			const char* Rounding[] = {"toward_zero", "floor", "ceil",
			    "away_from_zero", "half_even", "half_away_from_zero"};
			for (unsigned I = 0; I < 6; ++I)
			{
				FHostMemberDesc M;
				M.Kind = EHostMemberKind::EnumMember;
				M.Name = Rounding[I];
				M.EnumValue = I;
				Need(R.RegisterMember(Types[2], std::move(M), Error));
			}
			const std::vector<ENativeType> IntegerTypes = {
			    ENativeType::Int8, ENativeType::UInt8, ENativeType::Int16, ENativeType::UInt16,
			    ENativeType::Int, ENativeType::Unsigned, ENativeType::Int64, ENativeType::UInt64};
			for (unsigned T = 0; T < 2; ++T)
			{
				Need(R.RegisterTypeClone(Types[T], Clone, nullptr, Error));
				Need(R.RegisterTypeEquals(Types[T], Equal, nullptr, Error));
				Need(R.RegisterTypeHash(Types[T], Hash, nullptr, Error));
				const auto Add =
				    [&](std::string Name, EOp Op, ENativeType Result, std::vector<ENativeType> Args = {},
				        std::string ResultHost = {}, std::vector<std::string> Hosts = {},
				        EHostMemberKind Kind = EHostMemberKind::Method, std::vector<std::uint8_t> Flags = {})
				{
					FHostMemberDesc M;
					M.Name = std::move(Name);
					M.Kind = Kind;
					M.bConstMethod = true;
					M.bOperator = M.Name.rfind("operator", 0) == 0;
					M.ReturnType = Result;
					M.ReturnHostTypeName = std::move(ResultHost);
					M.ParamTypes = std::move(Args);
					M.ParamHostTypeNames = std::move(Hosts);
					M.ParamFlags = std::move(Flags);
					M.Callback = Member;
					M.UserData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(Op) | (T ? 65536 : 0));
					Need(R.RegisterMember(Types[T], std::move(M), Error));
				};
				const auto Ctor = [&](std::vector<ENativeType> Args, std::vector<std::string> Hosts = {})
				{
					Add("", EOp::Construct, ENativeType::HostObject, std::move(Args), Names[T],
					    std::move(Hosts), EHostMemberKind::Constructor);
				};
				Ctor({});
				Ctor({ENativeType::String});
				for (auto I : IntegerTypes)
				{
					Ctor({I});
				}
				if (T)
				{
					Ctor({ENativeType::HostObject}, {Names[0]});
					Ctor({ENativeType::HostObject, ENativeType::HostObject}, {Names[0], Names[0]});
					for (auto I : IntegerTypes)
					{
						for (auto J : IntegerTypes)
						{
							Ctor({I, J});
						}
					}
				}
				Add("parse", EOp::Parse, ENativeType::HostObject, {ENativeType::String}, Names[T], {},
				    EHostMemberKind::StaticMethod);
				Add("try_parse", EOp::TryParse, ENativeType::Bool,
				    {ENativeType::String, ENativeType::HostObject}, {}, {"", Names[T]},
				    EHostMemberKind::StaticMethod, {0, 1});
				Add("from_double_exact", EOp::FromDouble, ENativeType::HostObject, {ENativeType::Double},
				    Names[T], {}, EHostMemberKind::StaticMethod);
				Add("is_zero", EOp::Zero, ENativeType::Bool);
				Add("is_negative", EOp::Negative, ENativeType::Bool);
				Add("sign", EOp::Sign, ENativeType::Int);
				Add("abs", EOp::Abs, ENativeType::HostObject, {}, Names[T]);
				Add("to_string", EOp::Text, ENativeType::String);
				const char* ConversionNames[] = {"int64", "uint64", "double",
				    "double_exact", "float", "float_exact"};
				ENativeType ConversionTypes[] = {ENativeType::Int64, ENativeType::UInt64,
				    ENativeType::Double, ENativeType::Double,
				    ENativeType::Float, ENativeType::Float};
				for (unsigned I = 0; I < 6; ++I)
				{
					Add(std::string("to_") + ConversionNames[I],
					    static_cast<EOp>(static_cast<unsigned>(EOp::Int64) + I), ConversionTypes[I]);
					Add(std::string("try_to_") + ConversionNames[I],
					    static_cast<EOp>(static_cast<unsigned>(EOp::TryInt64) + I), ENativeType::Bool,
					    {ConversionTypes[I]}, {}, {}, EHostMemberKind::Method, {1});
				}
				Add("operator+", EOp::Plus, ENativeType::HostObject, {}, Names[T]);
				Add("operator-", EOp::Negate, ENativeType::HostObject, {}, Names[T]);
				const char* Operators[] = {"+", "-", "*", "/", "%", "&", "|", "^"};
				for (unsigned I = 0; I < (T ? 5u : 8u); ++I)
				{
					Add(std::string("operator") + Operators[I],
					    static_cast<EOp>(static_cast<unsigned>(EOp::Add) + I), ENativeType::HostObject,
					    {ENativeType::HostObject}, Names[T], {Names[T]});
				}
				const char* Comparisons[] = {"==", "!=", "<", "<=", ">", ">="};
				for (unsigned I = 0; I < 6; ++I)
				{
					Add(std::string("operator") + Comparisons[I],
					    static_cast<EOp>(static_cast<unsigned>(EOp::Equal) + I), ENativeType::Bool,
					    {ENativeType::HostObject}, {}, {Names[T]});
				}
				if (!T)
				{
					Add("parse_base", EOp::ParseBase, ENativeType::HostObject,
					    {ENativeType::String, ENativeType::Int}, Names[0], {}, EHostMemberKind::StaticMethod);
					Add("try_parse_base", EOp::TryParseBase, ENativeType::Bool,
					    {ENativeType::String, ENativeType::HostObject, ENativeType::Int}, {},
					    {"", Names[0], ""}, EHostMemberKind::StaticMethod, {0, 1, 0});
					Add("from_double_trunc", EOp::FromDoubleTrunc, ENativeType::HostObject,
					    {ENativeType::Double}, Names[0], {}, EHostMemberKind::StaticMethod);
					Add("bit_length", EOp::Bits, ENativeType::Int64);
					Add("to_string_base", EOp::TextBase, ENativeType::String, {ENativeType::Int});
					Add("operator~", EOp::Not, ENativeType::HostObject, {}, Names[0]);
					Add("operator<<", EOp::Left, ENativeType::HostObject, {ENativeType::Int64}, Names[0]);
					Add("operator>>", EOp::Right, ENativeType::HostObject, {ENativeType::Int64}, Names[0]);
				}
				else
				{
					Add("numerator", EOp::Numerator, ENativeType::HostObject, {}, Names[0]);
					Add("denominator", EOp::Denominator, ENativeType::HostObject, {}, Names[0]);
					Add("is_integer", EOp::Integer, ENativeType::Bool);
					Add("reciprocal", EOp::Reciprocal, ENativeType::HostObject, {}, Names[1]);
					Add("truncate", EOp::Truncate, ENativeType::HostObject, {}, Names[0]);
					Add("floor", EOp::Floor, ENativeType::HostObject, {}, Names[0]);
					Add("ceil", EOp::Ceil, ENativeType::HostObject, {}, Names[0]);
					Add("round", EOp::Round, ENativeType::HostObject, {}, Names[0]);
					Add("round_with_mode", EOp::RoundMode, ENativeType::HostObject, {ENativeType::Enum},
					    Names[0], {Names[2]});
					Add("to_decimal", EOp::Decimal, ENativeType::String, {ENativeType::Int64});
					Add("to_decimal_with_mode", EOp::DecimalMode, ENativeType::String,
					    {ENativeType::Int64, ENativeType::Enum}, {}, {"", Names[2]});
					Add("has_finite_decimal", EOp::FiniteDecimal, ENativeType::Bool);
				}
			}
			const auto Function = [&](const char* Name, EOp Op, bool Rational, std::vector<ENativeType> Args,
			                          std::vector<std::string> Hosts, ENativeType Return,
			                          std::vector<std::uint8_t> Flags = {})
			{
				FHostFunctionDesc F;
				F.Name = Name;
				F.ReturnType = Return;
				F.ReturnHostTypeName = Return == ENativeType::HostObject ? Names[Rational ? 1 : 0] : "";
				F.ParamTypes = std::move(Args);
				F.ParamHostTypeNames = std::move(Hosts);
				F.ParamFlags = std::move(Flags);
				Need(R.RegisterNative(
				    std::move(F), Member,
				    reinterpret_cast<void*>(static_cast<std::uintptr_t>(Op) | (Rational ? 65536 : 0)),
				    Error));
			};
			Function("std.numeric.gcd", EOp::Gcd, false, {ENativeType::HostObject, ENativeType::HostObject},
			    {Names[0], Names[0]}, ENativeType::HostObject);
			Function("std.numeric.lcm", EOp::Lcm, false, {ENativeType::HostObject, ENativeType::HostObject},
			    {Names[0], Names[0]}, ENativeType::HostObject);
			Function("__std_numeric_bigint_pow", EOp::Pow, false,
			    {ENativeType::HostObject, ENativeType::Int64}, {Names[0], ""}, ENativeType::HostObject);
			Function("__std_numeric_rational_pow", EOp::Pow, true,
			    {ENativeType::HostObject, ENativeType::Int64}, {Names[1], ""}, ENativeType::HostObject);
			Function("__std_numeric_bigint_divmod", EOp::DivMod, false,
			    {ENativeType::HostObject, ENativeType::HostObject, ENativeType::HostObject,
			        ENativeType::HostObject},
			    {Names[0], Names[0], Names[0], Names[0]}, ENativeType::Void, {0, 0, 1, 1});
			return R;
		}
		class FCatalog final : public IHostBindingCatalog
		{
		public:
			const std::vector<FNativeFunction>& GetNativeFunctions() const override
			{
				return Source.GetNativeFunctions();
			}
			const std::vector<FHostFunctionDesc>& GetFunctions() const override
			{
				return Source.GetFunctions();
			}
			const std::vector<FHostTypeDesc>& GetTypes() const override
			{
				return Types;
			}
			const FHostFunctionDesc* FindFunction(std::string_view N) const override
			{
				return Source.FindFunction(N);
			}
			const FHostTypeDesc* FindType(std::string_view N) const override
			{
				for (const auto& T : Types)
				{
					if (T.Name == N)
					{
						return &T;
					}
				}
				return nullptr;
			}
			const FHostTypeDesc* FindType(FHostTypeId Id) const override
			{
				for (const auto& T : Types)
				{
					if (T.Id == Id)
					{
						return &T;
					}
				}
				return nullptr;
			}
			FCatalog() : Source(Build()), Types(Source.GetTypes())
			{
				for (unsigned I = 0; I < Types.size(); ++I)
				{
					Types[I].Id.Value = TypeBase + I;
				}
			}

		private:
			FHostBindingRegistry Source;
			std::vector<FHostTypeDesc> Types;
		};
	} // namespace
	const IHostBindingCatalog& GetBindingCatalog()
	{
		static const FCatalog Catalog;
		return Catalog;
	}
	bool IsTypeName(std::string_view N) noexcept
	{
		return N == Names[0] || N == Names[1];
	}
	static bool IsValueLeafImpl(const FNativeFunction& Native, const FValue* Args,
	    const std::size_t Count, const bool ValidateMetadata) noexcept
	{
		if (Native.Callback != Member || Native.bGenericHostObjectCast || Count != Native.ParamTypes.size() || (Count != 0 && !Args) || (ValidateMetadata && (!NativeParamFlagsAreValid(Native) || !NativeFunctionContainerMetadataIsValid(Native))) || std::any_of(Native.ParamFlags.begin(), Native.ParamFlags.end(), [](auto Flag)
		{
			return Flag != 0;
		}) ||
		    Native.ReturnType == ENativeType::Void)
		{
			return false;
		}
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			const auto& Value = Args[Index];
			switch (Native.ParamTypes[Index])
			{
			case ENativeType::HostObject:
				{
					const auto& Expected = Native.ParamHostTypeNames;
					if (Index >= Expected.size() || (Expected[Index] == Names[0] ? !Get<FBigInteger>(Value) : Expected[Index] == Names[1] ? !Get<FRational>(Value)
					                                                                                                                      : true))
					{
						return false;
					}
					break;
				}
			case ENativeType::Int:
				{
					if (!std::holds_alternative<std::int32_t>(Value)) return false;
					break;
				}
			case ENativeType::Unsigned:
				{
					if (!std::holds_alternative<std::uint32_t>(Value)) return false;
					break;
				}
			case ENativeType::Int64:
				{
					if (!std::holds_alternative<std::int64_t>(Value)) return false;
					break;
				}
			case ENativeType::UInt64:
				{
					if (!std::holds_alternative<std::uint64_t>(Value)) return false;
					break;
				}
			default:
				{
					return false;
				}
			}
		}
		return true;
	}
	bool IsValueLeaf(const FNativeFunction& Native, const FValue* Args, const std::size_t Count) noexcept
	{
		return IsValueLeafImpl(Native, Args, Count, true);
	}
	bool TryInvokeValueLeaf(const FNativeFunction& Native, const FNativeCallContext& Context,
	    FValue& Output, std::string& Error, bool& Success)
	{
		if (!IsValueLeafImpl(Native, Context.Args, Context.ArgCount, false)) return false;
		Success = Member(Context, Output, Error);
		return true;
	}
	bool IsValue(const FValue& V) noexcept
	{
		return Get<FBigInteger>(V) || Get<FRational>(V);
	}
	bool TryCopyValue(const FValue& Value, FValue& Out, FNumericError& Error, bool& Handled) noexcept
	{
		Handled = false;
		const auto* Host = std::get_if<FHostObjectHandle>(&Value);
		if (!Host || !*Host || (*Host)->CloneCallback != Clone || (*Host)->CloneUserData || (*Host)->EqualsCallback != Equal || (*Host)->EqualsUserData || (*Host)->HashCallback != Hash || (*Host)->HashUserData || (*Host)->CloneOwner || (*Host)->EqualsOwner || (*Host)->HashOwner || (*Host)->GenericTemplateId || !(*Host)->GenericTypeArguments.empty())
		{
			return true;
		}
		if (const auto* Integer = Get<FBigInteger>(Value))
		{
			Handled = true;
			return ToScriptValue(*Integer, Out, Error);
		}
		if (const auto* Fraction = Get<FRational>(Value))
		{
			Handled = true;
			return ToScriptValue(*Fraction, Out, Error);
		}
		return true;
	}
	template <class T>
	bool Wrap(const T& V, FValue& Out, FNumericError& E) noexcept
	{
		E = {};
		try
		{
			auto N = Make(V);
			Out = std::move(N);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			E = {ENumericStatus::OutOfMemory, "diag.numeric_out_of_memory", "host wrapper"};
		}
		catch (...)
		{
			E = {ENumericStatus::InternalError, "diag.numeric_internal", "host wrapper"};
		}
		return false;
	}
	bool ToScriptValue(const FBigInteger& V, FValue& Out, FNumericError& E) noexcept
	{
		return Wrap(V, Out, E);
	}
	bool ToScriptValue(const FRational& V, FValue& Out, FNumericError& E) noexcept
	{
		return Wrap(V, Out, E);
	}
	template <class T>
	bool Unwrap(const FValue& V, T& Out, FNumericError& E) noexcept
	{
		if (auto P = Get<T>(V))
		{
			Out = *P;
			E = {};
			return true;
		}
		E = {ENumericStatus::TypeMismatch, "diag.numeric_type_mismatch", "numeric payload origin"};
		return false;
	}
	bool FromScriptValue(const FValue& V, FBigInteger& Out, FNumericError& E) noexcept
	{
		return Unwrap(V, Out, E);
	}
	bool FromScriptValue(const FValue& V, FRational& Out, FNumericError& E) noexcept
	{
		return Unwrap(V, Out, E);
	}
	EArgumentRole ArgumentRole(const FNativeFunction& N, std::size_t I) noexcept
	{
		if (N.Callback != Member || I >= N.ParamTypes.size() || NativeParamFlagIsRef(NativeParamFlagAt(N, I)))
		{
			return EArgumentRole::Ordinary;
		}
		auto T = N.ParamTypes[I];
		if (T == ENativeType::Double || T == ENativeType::Float)
		{
			return EArgumentRole::Floating;
		}
		return T == ENativeType::Int || T == ENativeType::Unsigned || T == ENativeType::Int64 ||
		        T == ENativeType::UInt64 || T == ENativeType::Int8 || T == ENativeType::UInt8 ||
		        T == ENativeType::Int16 || T == ENativeType::UInt16
		    ? EArgumentRole::Integer
		    : EArgumentRole::Ordinary;
	}
	bool ValidateArgumentType(const FNativeFunction& N, std::size_t I, std::string_view Type) noexcept
	{
		auto Role = ArgumentRole(N, I);
		if (Role == EArgumentRole::Ordinary)
		{
			return true;
		}
		Type = Runtime::StripReferenceTypeName(Type);
		if (Role == EArgumentRole::Floating)
		{
			return Type == "double" || Type == "float";
		}
		return Type == "int8" || Type == "uint8" || Type == "int16" || Type == "uint16" || Type == "int" ||
		    Type == "unsigned int" || Type == "unsigned" || Type == "int64" || Type == "uint64";
	}
	bool ValidateArgument(const FNativeFunction& N, std::size_t I, const FValue& V, std::string& Error)
	{
		auto Role = ArgumentRole(N, I);
		if (Role == EArgumentRole::Ordinary)
		{
			return true;
		}
		if (Role == EArgumentRole::Floating)
		{
			if (std::holds_alternative<float>(V) || std::holds_alternative<double>(V))
			{
				return true;
			}
			Error = "diag.numeric_type_mismatch";
			return false;
		}
		if (!Runtime::IsIntegerValue(V))
		{
			Error = "diag.numeric_type_mismatch";
			return false;
		}
		auto X = Scalar(V);
		auto T = N.ParamTypes[I];
		bool Unsigned = T == ENativeType::Unsigned || T == ENativeType::UInt64 || T == ENativeType::UInt8 ||
		    T == ENativeType::UInt16;
		unsigned Bits = T == ENativeType::Int8 || T == ENativeType::UInt8 ? 8
		    : T == ENativeType::Int16 || T == ENativeType::UInt16         ? 16
		    : T == ENativeType::Int64 || T == ENativeType::UInt64         ? 64
		                                                                  : 32;
		auto Abs = static_cast<std::uint64_t>(FNumericAccess::Word(X, 0)) |
		    (static_cast<std::uint64_t>(FNumericAccess::Word(X, 1)) << 32);
		auto Limit = Unsigned ? Bits == 64 ? UINT64_MAX : (std::uint64_t(1) << Bits) - 1
		                      : (std::uint64_t(1) << (Bits - 1)) - (X.IsNegative() ? 0 : 1);
		if ((Unsigned && X.IsNegative()) || Abs > Limit)
		{
			Error = "diag.numeric_out_of_range";
			return false;
		}
		return true;
	}
	bool FormatValue(const FValue& V, std::string& Out, FNumericOperationContext& C,
	    FNumericError& E) noexcept
	{
		if (auto X = Get<FBigInteger>(V))
		{
			return TryToString(*X, Out, C, E);
		}
		if (auto X = Get<FRational>(V))
		{
			return TryToString(*X, Out, C, E);
		}
		E = {ENumericStatus::TypeMismatch, "diag.numeric_type_mismatch", "numeric value"};
		return false;
	}
	bool ConvertValue(const FValue& V, std::string_view Target, FValue& Out, FNumericOperationContext& C,
	    FNumericError& E) noexcept
	{
		if (Target != "double" && Target != "float" && Target != "int8" && Target != "uint8" &&
		    Target != "int16" && Target != "uint16" && Target != "int" && Target != "unsigned int" &&
		    Target != "int64" && Target != "uint64")
		{
			E = {ENumericStatus::TypeMismatch, "diag.numeric_type_mismatch", "conversion target"};
			return false;
		}
		const auto Convert = [&](const auto& X)
		{
			if (Target == "double")
			{
				double N = 0;
				if (!TryToDouble(X, N, false, C, E))
				{
					return false;
				}
				Out = N;
				return true;
			}
			if (Target == "float")
			{
				float N = 0;
				if (!TryToFloat(X, N, false, C, E))
				{
					return false;
				}
				Out = N;
				return true;
			}
			bool Unsigned =
			    Target == "uint64" || Target == "unsigned int" || Target == "uint8" || Target == "uint16";
			unsigned Width = Target == "int8" || Target == "uint8" ? 8
			    : Target == "int16" || Target == "uint16"          ? 16
			    : Target == "int64" || Target == "uint64"          ? 64
			                                                       : 32;
			if (Unsigned)
			{
				std::uint64_t N = 0;
				if (!TryToUInt64(X, N, C, E))
				{
					return false;
				}
				if (Width < 64 && N >= (std::uint64_t(1) << Width))
				{
					E = {ENumericStatus::OutOfRange, "diag.numeric_out_of_range", "integer conversion"};
					return false;
				}
				if (Width == 8)
				{
					Out = static_cast<std::uint8_t>(N);
				}
				else if (Width == 16)
				{
					Out = static_cast<std::uint16_t>(N);
				}
				else if (Width == 32)
				{
					Out = static_cast<std::uint32_t>(N);
				}
				else
				{
					Out = N;
				}
			}
			else
			{
				std::int64_t N = 0;
				if (!TryToInt64(X, N, C, E))
				{
					return false;
				}
				if (Width < 64 &&
				    (N < -(std::int64_t(1) << (Width - 1)) || N >= (std::int64_t(1) << (Width - 1))))
				{
					E = {ENumericStatus::OutOfRange, "diag.numeric_out_of_range", "integer conversion"};
					return false;
				}
				if (Width == 8)
				{
					Out = static_cast<std::int8_t>(N);
				}
				else if (Width == 16)
				{
					Out = static_cast<std::int16_t>(N);
				}
				else if (Width == 32)
				{
					Out = static_cast<std::int32_t>(N);
				}
				else
				{
					Out = N;
				}
			}
			return true;
		};
		if (auto X = Get<FBigInteger>(V))
		{
			return Convert(*X);
		}
		if (auto X = Get<FRational>(V))
		{
			return Convert(*X);
		}
		E = {ENumericStatus::TypeMismatch, "diag.numeric_type_mismatch", "numeric value"};
		return false;
	}
	bool UsesRuntimeNumerics(const FBytecodeModule& M, const std::vector<FNativeFunction>* Natives)
	{
		std::vector<FNativeFunction> Storage;
		const auto* All = StandardLibrary::MergeNativeFunctions(Natives, Storage);
		std::vector<FNativeFunction> Bound;
		std::string BindingError;
		if (!M.NativeReferences.empty())
		{
			if (!BindBytecodeNativeReferences(M, *All, Bound, BindingError)) return true;
			All = &Bound;
		}
		const auto UsesCode = [&](const std::vector<std::uint8_t>& Code)
		{
			for (std::size_t I = 0; I < Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Code[I]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (!Size || Size > Code.size() - I)
				{
					return true;
				}
				if (Op == EOpcode::CallNative)
				{
					const auto Index = Bytecode::ReadU16LE(Code.data() + I + 1);
					if (Index < All->size() && (*All)[Index].Callback == Member)
					{
						return true;
					}
				}
				else if (Op == EOpcode::CallMemory)
				{
					const auto Index = Bytecode::ReadU16LE(Code.data() + I + 1);
					const auto& Standard = StandardLibrary::GetBindingCatalog().GetNativeFunctions();
					if (Index < Standard.size() && Standard[Index].Callback == Member) return true;
				}
				I += Size;
			}
			return false;
		};
		if (UsesCode(M.InitCode))
		{
			return true;
		}
		const auto UsesType = [](std::string_view T)
		{
			const auto Identifier = [](char C)
			{
				return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
				    C == '_' || C == '.' || C == ':';
			};
			for (std::size_t I = 0; I < T.size();)
			{
				if (!Identifier(T[I]))
				{
					++I;
					continue;
				}
				const auto Begin = I++;
				while (I < T.size() && Identifier(T[I]))
				{
					++I;
				}
				const auto Token = T.substr(Begin, I - Begin);
				if (IsTypeName(Token) || Token == "std::bigint" || Token == "std::rational")
				{
					return true;
				}
			}
			return false;
		};
		for (const auto& F : M.Functions)
		{
			if (UsesType(F.ClosureTypeName) || UsesCode(F.Code))
			{
				return true;
			}
		}
		for (const auto& T : M.GlobalTypeNames)
		{
			if (UsesType(T))
			{
				return true;
			}
		}
		for (const auto& T : M.Types)
		{
			for (const auto& F : T.Fields)
			{
				if (UsesType(F.TypeName) || UsesCode(F.InitCode))
				{
					return true;
				}
			}
		}
		return false;
	}
} // namespace VeroScript::Numerics
