// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMemoryInternal.h"
#include "VeroScriptNative.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptStandardLibrary.h"
#include "ExecutionSupport/VeroScriptVmNativeLeaves.h"

#include <algorithm>
#include <stdexcept>

namespace VeroScript::Memory
{
	using namespace Detail;
	namespace
	{
		enum class EOperation : std::uintptr_t
		{
			CreateBuffer,
			CreateView,
			CreateReadOnlyView,
			Size,
			Capacity,
			Get,
			Set,
			Resize,
			Reserve,
			Slice,
			ReadOnlySlice
		};
		bool Member(const FNativeCallContext& C, FValue& Out, std::string& Error)
		{
			const auto Op = static_cast<EOperation>(reinterpret_cast<std::uintptr_t>(C.UserData));
			if (Op == EOperation::CreateView || Op == EOperation::CreateReadOnlyView)
			{
				auto R = CreateEmptyView(Op == EOperation::CreateReadOnlyView, Out);
				if (!R)
				{
					Error = R.MessageId;
				}
				return static_cast<bool>(R);
			}
			if (!C.MemoryServices)
			{
				Error = "diag.memory_context";
				return false;
			}
			auto& Context = C.MemoryServices->GetMemoryContext();
			const auto Work = C.MemoryServices->GetMemoryWork();
			FMemoryResult R;
			std::int64_t N = 0;
			switch (Op)
			{
			case EOperation::CreateBuffer:
				{
					R = CreateBuffer(Context, C.ArgCount ? std::get<std::int64_t>(C.Args[0]) : 0, Out, Work);
					break;
				}
			case EOperation::Size:
				{
					R = Size(Context, C.Args[0], N);
					Out = N;
					break;
				}
			case EOperation::Capacity:
				{
					R = Capacity(Context, C.Args[0], N);
					Out = N;
					break;
				}
			case EOperation::Get:
				{
					R = ReadByte(Context, C.Args[0], std::get<std::int64_t>(C.Args[1]), Out, Work);
					break;
				}
			case EOperation::Set:
				{
					R = Write(Context, C.Args[0], std::get<std::int64_t>(C.Args[1]), "uint8",
					    EMemoryByteOrder::Little, C.Args[2], Work);
					break;
				}
			case EOperation::Resize:
			case EOperation::Reserve:
				{
					R = Resize(Context, C.Args[0], std::get<std::int64_t>(C.Args[1]),
					    Op == EOperation::Reserve, Work);
					break;
				}
			case EOperation::Slice:
			case EOperation::ReadOnlySlice:
				{
					R = Slice(Context, C.Args[0], std::get<std::int64_t>(C.Args[1]),
					    std::get<std::int64_t>(C.Args[2]), Op == EOperation::ReadOnlySlice, Out);
					break;
				}
			default:
				{
					R = Fail(EMemoryStatus::Unsupported, "diag.memory_context");
					break;
				}
			}
			if (!R)
			{
				Error = R.MessageId;
			}
			return static_cast<bool>(R);
		}
		FHostBindingRegistry BuildCatalog()
		{
			FHostBindingRegistry R;
			std::string Error;
			FHostTypeId Types[4];
			const auto Require = [&](bool Ok)
			{
				if (!Ok)
				{
					throw std::logic_error(Error);
				}
			};
			for (unsigned I = 0; I < 3; ++I)
			{
				Require(R.RegisterType(std::string(Names[I]),
				    I == 0 ? EHostTypeKind::Class : EHostTypeKind::Struct, Types[I], Error));
			}
			Require(R.RegisterType("std.memory.byte_order", EHostTypeKind::Enum, Types[3], Error));
			Require(R.SetEnumScoped(Types[3], true, Error));
			for (unsigned I = 0; I < 2; ++I)
			{
				FHostMemberDesc M;
				M.Kind = EHostMemberKind::EnumMember;
				M.Name = I == 0 ? "little" : "big";
				M.EnumValue = I;
				Require(R.RegisterMember(Types[3], std::move(M), Error));
			}
			for (unsigned I = 0; I < 3; ++I)
			{
				const auto Add = [&](std::string Name, EOperation Op, ENativeType Return,
				                     std::vector<ENativeType> Args, std::string ReturnHost = {},
				                     bool Constructor = false)
				{
					FHostMemberDesc M;
					M.Name = std::move(Name);
					M.Kind = Constructor ? EHostMemberKind::Constructor : EHostMemberKind::Method;
					M.bConstMethod = Op == EOperation::Size || Op == EOperation::Capacity ||
					    Op == EOperation::Get || Op == EOperation::ReadOnlySlice ||
					    (I == 2 && Op == EOperation::Slice);
					M.ReturnType = Return;
					M.ParamTypes = std::move(Args);
					M.ReturnHostTypeName = std::move(ReturnHost);
					M.Callback = Member;
					M.UserData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(Op));
					Require(R.RegisterMember(Types[I], std::move(M), Error));
				};
				Add("",
				    I == 0       ? EOperation::CreateBuffer
				        : I == 1 ? EOperation::CreateView
				                 : EOperation::CreateReadOnlyView,
				    ENativeType::HostObject, {}, std::string(Names[I]), true);
				if (I == 0)
				{
					Add("", EOperation::CreateBuffer, ENativeType::HostObject, {ENativeType::Int64},
					    std::string(Names[I]), true);
				}
				Add("size", EOperation::Size, ENativeType::Int64, {});
				Add("get", EOperation::Get, ENativeType::UInt8, {ENativeType::Int64});
				Add("slice", I == 2 ? EOperation::ReadOnlySlice : EOperation::Slice, ENativeType::HostObject,
				    {ENativeType::Int64, ENativeType::Int64}, std::string(Names[I == 2 ? 2 : 1]));
				if (I != 2)
				{
					Add("set", EOperation::Set, ENativeType::Void, {ENativeType::Int64, ENativeType::UInt8});
					Add("readonly_slice", EOperation::ReadOnlySlice, ENativeType::HostObject,
					    {ENativeType::Int64, ENativeType::Int64}, std::string(Names[2]));
				}
				if (I == 0)
				{
					Add("capacity", EOperation::Capacity, ENativeType::Int64, {});
					Add("resize", EOperation::Resize, ENativeType::Void, {ENativeType::Int64});
					Add("reserve", EOperation::Reserve, ENativeType::Void, {ENativeType::Int64});
				}
				else
				{
					Require(R.RegisterTypeClone(Types[I], CloneDescriptor, nullptr, Error));
					Require(R.RegisterTypeEquals(Types[I], EqualDescriptor, nullptr, Error));
					Require(R.RegisterTypeHash(Types[I], HashDescriptor, nullptr, Error));
				}
			}
			return R;
		}
		class FCatalog final : public IHostBindingCatalog
		{
		public:
			//~ Begin IHostBindingCatalog Interface
			const std::vector<FNativeFunction>& GetNativeFunctions() const override
			{
				return *Natives;
			}
			const std::vector<FHostFunctionDesc>& GetFunctions() const override
			{
				static const std::vector<FHostFunctionDesc> Empty;
				return Source ? Source->GetFunctions() : Empty;
			}
			const std::vector<FHostTypeDesc>& GetTypes() const override
			{
				return Types;
			}
			const std::vector<FHostTypeAliasDesc>& GetTypeAliases() const override
			{
				return Source ? Source->GetTypeAliases() : IHostBindingCatalog::GetTypeAliases();
			}
			const std::vector<FGenericHostTypeTemplateDesc>& GetGenericTypeTemplates() const override
			{
				return Source ? Source->GetGenericTypeTemplates()
				              : IHostBindingCatalog::GetGenericTypeTemplates();
			}
			const std::vector<FHostAnnotationDesc>& GetAnnotations() const override
			{
				return Source ? Source->GetAnnotations() : IHostBindingCatalog::GetAnnotations();
			}
			const FHostFunctionDesc* FindFunction(std::string_view N) const override
			{
				return Source ? Source->FindFunction(N) : nullptr;
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
			const FHostTypeDesc* FindType(FHostTypeId I) const override
			{
				for (const auto& T : Types)
				{
					if (T.Id == I)
					{
						return &T;
					}
				}
				return nullptr;
			}
			//~ End IHostBindingCatalog Interface

			const IHostBindingCatalog* Source = nullptr;
			std::vector<FHostTypeDesc> Types;
			std::vector<FNativeFunction> NativeStorage;
			const std::vector<FNativeFunction>* Natives = nullptr;
		};
	} // namespace
	const IHostBindingCatalog& GetBindingCatalog()
	{
		static const FHostBindingRegistry R = BuildCatalog();
		static const FCatalog C = []()
		{
			FCatalog Result;
			Result.Source = &R;
			Result.Natives = &R.GetNativeFunctions();
			Result.Types = R.GetTypes();
			for (std::size_t I = 0; I < Result.Types.size(); ++I)
			{
				Result.Types[I].Id.Value = TypeIdBase + static_cast<unsigned>(I);
			}
			return Result;
		}();
		return C;
	}

	bool IsValueLeaf(const FNativeFunction& Native, const FValue* Args, const std::size_t Count) noexcept
	{
		if (Native.Callback != Member || Native.bGenericHostObjectCast || !Args || Count != Native.ParamTypes.size() || Count == 0 || !NativeParamFlagsAreValid(Native) || !NativeFunctionContainerMetadataIsValid(Native) || std::any_of(Native.ParamFlags.begin(), Native.ParamFlags.end(), [](auto Flag)
		{
			return Flag != 0;
		}) ||
		    !IsRegion(Args[0]))
		{
			return false;
		}
		const auto* Host = std::get_if<FHostObjectHandle>(&Args[0]);
		if (Native.ParamHostTypeNames.empty() || Native.ParamHostTypeNames[0] != (*Host)->TypeName) return false;
		const auto Op = static_cast<EOperation>(reinterpret_cast<std::uintptr_t>(Native.UserData));
		if (Native.ParamTypes[0] != ENativeType::HostObject) return false;
		if (Op == EOperation::Size || Op == EOperation::Capacity)
		{
			return Count == 1 && Native.ReturnType == ENativeType::Int64;
		}
		return Op == EOperation::Get && Count == 2 && Native.ReturnType == ENativeType::UInt8 && Native.ParamTypes[1] == ENativeType::Int64 && std::holds_alternative<std::int64_t>(Args[1]);
	}

	bool TryInvokeValueLeaf(const std::uint16_t Index, IMemoryCallServices& Services,
	    const FValue* Args, const std::size_t Count,
	    FValue& Output, FMemoryResult& Result)
	{
		const auto& Catalog = GetBindingCatalog().GetNativeFunctions();
		if (Index >= Catalog.size() || !Args || Count == 0)
		{
			return false;
		}
		const auto& Native = Catalog[Index];
		const auto Operation = static_cast<EOperation>(reinterpret_cast<std::uintptr_t>(Native.UserData));
		const auto* Host = std::get_if<FHostObjectHandle>(&Args[0]);
		if (Count != Native.ParamTypes.size() || !Host || !*Host || Native.ParamHostTypeNames.empty() || Native.ParamHostTypeNames[0] != (*Host)->TypeName || !IsRegion(Args[0]))
		{
			return false;
		}
		if (Count == 1 && (Operation == EOperation::Size || Operation == EOperation::Capacity))
		{
			std::int64_t SizeValue = 0;
			auto& Context = Services.GetMemoryContext();
			Result = Operation == EOperation::Size
			    ? Size(Context, Args[0], SizeValue)
			    : Capacity(Context, Args[0], SizeValue);
			Output = SizeValue;
			return true;
		}
		if (Count == 2 && Operation == EOperation::Get)
		{
			std::int64_t Offset;
			if (const auto* Wide = std::get_if<std::int64_t>(&Args[1]))
			{
				Offset = *Wide;
			}
			else if (const auto* Narrow = std::get_if<std::int32_t>(&Args[1]))
			{
				Offset = *Narrow;
			}
			else
			{
				return false;
			}
			Result = ReadByte(Services.GetMemoryContext(), Args[0], Offset, Output, Services.GetMemoryWork());
			return true;
		}
		return false;
	}

	bool UsesRuntimeMemory(const FBytecodeModule& M, const std::vector<FNativeFunction>* Natives)
	{
		std::vector<FNativeFunction> Storage;
		const auto* All = StandardLibrary::MergeNativeFunctions(Natives, Storage);
		const auto First = All->size() - StandardLibrary::GetBindingCatalog().GetNativeFunctions().size();
		const auto End = First + GetBindingCatalog().GetNativeFunctions().size();
		const auto UsesCode = [&](const std::vector<std::uint8_t>& Code)
		{
			for (std::size_t I = 0; I < Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Code[I]);
				const auto N = Bytecode::InstructionSize(Op);
				if (!N || N > Code.size() - I)
				{
					return true;
				}
				if (Op == EOpcode::CallMemory && Bytecode::ReadU16LE(Code.data() + I + 1) < GetBindingCatalog().GetNativeFunctions().size())
				{
					return true;
				}
				if (Op == EOpcode::CallBuiltin &&
				    Code[I + 1] >= static_cast<unsigned>(EBuiltinId::MemoryAvailable) &&
				    Code[I + 1] <= static_cast<unsigned>(EBuiltinId::MemoryWrite))
				{
					return true;
				}
				if (Op == EOpcode::CallNative)
				{
					const auto Index =
					    static_cast<std::size_t>(Code[I + 1]) | (static_cast<std::size_t>(Code[I + 2]) << 8);
					if (Index >= First && Index < End)
					{
						return true;
					}
				}
				I += N;
			}
			return false;
		};
		const auto UsesType = [](const std::string& T)
		{
			return ContainsMemoryType(T);
		};
		if (UsesCode(M.InitCode))
		{
			return true;
		}
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
				if (UsesType(F.TypeName))
				{
					return true;
				}
			}
		}
		return false;
	}

	std::shared_ptr<const IHostBindingCatalog> MergeBindingCatalog(
	    const IHostBindingCatalog* Source, const std::vector<FNativeFunction>* Natives)
	{
		if (!Source && (!Natives || Natives->empty()))
		{
			static const std::shared_ptr<const IHostBindingCatalog> Standard(&GetBindingCatalog(),
			    [](const IHostBindingCatalog*)
			{
			});
			return Standard;
		}
		if (Source)
		{
			for (const auto& A : Source->GetTypeAliases())
			{
				if (IsTypeName(A.Name))
				{
					throw std::invalid_argument("diag.memory_reserved_type");
				}
			}
			for (const auto& T : Source->GetGenericTypeTemplates())
			{
				if (IsTypeName(T.Name))
				{
					throw std::invalid_argument("diag.memory_reserved_type");
				}
			}
		}
		auto C = std::make_shared<FCatalog>();
		C->Source = Source;
		if (Source)
		{
			C->Types = Source->GetTypes();
		}
		for (auto T : dynamic_cast<const FCatalog*>(Source) ? std::vector<FHostTypeDesc>{}
		                                                    : GetBindingCatalog().GetTypes())
		{
			if (Source && Source->FindType(T.Name))
			{
				throw std::invalid_argument("diag.memory_reserved_type");
			}
			const auto I = T.Name == "std.memory.byte_order" ? 3
			    : T.Name == Names[0]                         ? 0
			    : T.Name == Names[1]                         ? 1
			                                                 : 2;
			T.Id.Value = TypeIdBase + static_cast<unsigned>(I);
			if (Source && Source->FindType(T.Id))
			{
				throw std::invalid_argument("diag.memory_reserved_type");
			}
			C->Types.push_back(std::move(T));
		}
		C->Natives = MergeNativeFunctions(Natives ? Natives
		        : Source                          ? &Source->GetNativeFunctions()
		                                          : nullptr,
		    C->NativeStorage);
		return C;
	}

	const std::vector<FNativeFunction>* MergeNativeFunctions(
	    const std::vector<FNativeFunction>* Natives, std::vector<FNativeFunction>& Storage)
	{
		const auto& Standard = GetBindingCatalog().GetNativeFunctions();
		if (!Natives || Natives->empty())
		{
			return &Standard;
		}
		constexpr std::size_t NativeSlots = std::size_t{1} << 16;
		if (Natives->size() > NativeSlots)
		{
			throw std::invalid_argument("diag.memory_native_limit");
		}
		std::size_t Matches = 0;
		for (const auto& N : *Natives)
		{
			for (const auto& S : Standard)
			{
				if (N.Name == S.Name)
				{
					if (N.Callback != S.Callback || N.UserData != S.UserData ||
					    N.ReturnType != S.ReturnType || N.ParamTypes != S.ParamTypes ||
					    N.HostTypeName != S.HostTypeName || N.ReturnHostTypeName != S.ReturnHostTypeName ||
					    N.ParamHostTypeNames != S.ParamHostTypeNames || N.ParamFlags != S.ParamFlags ||
					    N.RuntimeReturnTypeName != S.RuntimeReturnTypeName ||
					    N.RuntimeParamTypeNames != S.RuntimeParamTypeNames ||
					    N.ReturnElementTypeName != S.ReturnElementTypeName ||
					    N.ParamElementTypeNames != S.ParamElementTypeNames ||
					    N.ReturnKeyTypeName != S.ReturnKeyTypeName ||
					    N.ReturnValueTypeName != S.ReturnValueTypeName ||
					    N.ParamKeyTypeNames != S.ParamKeyTypeNames ||
					    N.ParamValueTypeNames != S.ParamValueTypeNames ||
					    N.bHostStaticMethod != S.bHostStaticMethod || N.TypeParams != S.TypeParams ||
					    N.ScriptReturnTypeName != S.ScriptReturnTypeName ||
					    N.bGenericHostObjectCast != S.bGenericHostObjectCast ||
					    N.ReturnGenericTemplateId != S.ReturnGenericTemplateId ||
					    N.ReturnGenericTypeArguments != S.ReturnGenericTypeArguments ||
					    N.bHasStructuredTypeDescs != S.bHasStructuredTypeDescs)
					{
						throw std::invalid_argument("diag.memory_reserved_type");
					}
					++Matches;
				}
			}
		}
		if (Matches)
		{
			if (Matches != Standard.size() || Natives->size() < Standard.size())
			{
				throw std::invalid_argument("diag.memory_reserved_type");
			}
			const auto Offset = Natives->size() - Standard.size();
			for (std::size_t I = 0; I < Standard.size(); ++I)
			{
				if ((*Natives)[Offset + I].Name != Standard[I].Name)
				{
					throw std::invalid_argument("diag.memory_reserved_type");
				}
			}
			return Natives;
		}
		if (Natives->size() > NativeSlots - Standard.size())
		{
			throw std::invalid_argument("diag.memory_native_limit");
		}
		Storage = *Natives;
		Storage.insert(Storage.end(), Standard.begin(), Standard.end());
		return &Storage;
	}

} // namespace VeroScript::Memory
