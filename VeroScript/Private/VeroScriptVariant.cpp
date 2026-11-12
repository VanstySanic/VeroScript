// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVariantInternal.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptNative.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptAst.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptModuleMeta.h"
#include <unordered_set>
#include <type_traits>

namespace VeroScript::Variant
{
	namespace
	{
		bool NeedsCopyContext(const FValue& Value, std::size_t Depth = 0)
		{
			if (Depth >= 128) return true;
			if (const auto* H = std::get_if<FObjectHandle>(&Value); H && *H && (*H)->Kind == EScriptObjectKind::Struct)
			{
				const auto& Object = **H;
				if (Object.RuntimeTypeModule) return true;
				for (const auto& Field : Object.Fields)
				{
					if (NeedsCopyContext(Field, Depth + 1)) return true;
				}
			}
			if (const auto* A = std::get_if<FNativeArray>(&Value))
			{
				for (const auto& Element : A->Storage().Elements)
				{
					if (NeedsCopyContext(Element, Depth + 1)) return true;
				}
			}
			return false;
		}
		template <class FVisit>
		bool InspectType(std::string_view Name, const FVisit& Visit, std::size_t Depth)
		{
			if (Depth > 128) return true;
			Name = TrimNativeTypeName(Runtime::StripReferenceTypeName(Name));
			if (Name.substr(0, 6) == "const ")
			{
				Name.remove_prefix(6);
			}
			if (Name == "variant" || Name == "std.variant" || Name == "std::variant") return true;
			if (Visit(Name)) return true;
			std::vector<std::string> Parameters;
			std::string Return;
			if (Runtime::ParseFunctionType(Name, Parameters, Return))
			{
				if (InspectType(Return, Visit, Depth + 1)) return true;
				for (const auto& P : Parameters)
				{
					if (InspectType(P, Visit, Depth + 1)) return true;
				}
				return false;
			}
			if (const auto Bracket = Name.find('['); Bracket != std::string_view::npos && Name.back() == ']')
			{
				return InspectType(Name.substr(0, Bracket), Visit, Depth + 1);
			}
			Name.remove_prefix(FindLinkageIdentityStart(Name));
			const auto Open = Name.find('<');
			if (Open == std::string_view::npos || Name.back() != '>') return false;
			int Angles = 0, Parens = 0;
			std::size_t Start = Open + 1;
			for (std::size_t I = Start; I < Name.size() - 1; ++I)
			{
				const char C = Name[I];
				if (C == '<')
				{
					++Angles;
				}
				else if (C == '>' && (I == 0 || Name[I - 1] != '-'))
				{
					--Angles;
				}
				else if (C == '(')
				{
					++Parens;
				}
				else if (C == ')')
				{
					--Parens;
				}
				else if (C == ',' && !Angles && !Parens)
				{
					if (InspectType(Name.substr(Start, I - Start), Visit, Depth + 1)) return true;
					Start = I + 1;
				}
			}
			return InspectType(Name.substr(Start, Name.size() - Start - 1), Visit, Depth + 1);
		}
	}
	bool CopyRequiresVm(const FBytecodeModule& Module, const FValue& Value)
	{
		std::unordered_set<std::string> Visiting;
		const auto TypeRequires = [&](auto&& Self, std::string Name, std::size_t Depth) -> bool
		{
			if (Depth >= 128) return true;
			if (Runtime::IsReferenceTypeName(Name)) return false;
			std::string Element;
			std::uint32_t Length = 0;
			if (Runtime::ParseNativeArrayType(Name, Element, Length)) return Self(Self, Element, Depth + 1);
			std::uint16_t Index = 0xFFFF;
			// An interface can hold a struct value whose dynamic copy constructor
			// is unavailable to this context-free operation.
			if (ModuleMeta::FindInterfaceIndex(Module, Name, Index)) return true;
			if (!ModuleMeta::FindTypeIndex(Module, Name, Index)) return false;
			const auto& Type = Module.Types[Index];
			if (Type.Kind != EScriptObjectKind::Struct || !Visiting.insert(Type.Name).second) return false;
			if (Type.CopyConstructorIndex < Module.Functions.size())
			{
				const auto& Function = Module.Functions[Type.CopyConstructorIndex];
				if (!Function.bDefaulted || Function.bDeleted || Function.bExplicitCopy || Function.Access != EMemberAccess::Public || !Function.UnavailableReason.empty())
				{
					return true;
				}
			}
			for (const auto Base : Type.BaseTypeIndices)
			{
				if (Self(Self, Module.Types[Base].Name, Depth + 1)) return true;
			}
			for (const auto& Field : Type.Fields)
			{
				if (!Runtime::IsReferenceTypeName(Field.DeclaredTypeName) && Self(Self, Field.TypeName, Depth + 1)) return true;
			}
			Visiting.erase(Type.Name);
			return false;
		};
		if (const auto* Object = std::get_if<FObjectHandle>(&Value); Object && *Object)
		{
			return TypeRequires(TypeRequires, (*Object)->GetTypeName(), 0);
		}
		if (const auto* Array = std::get_if<FNativeArray>(&Value))
		{
			return TypeRequires(TypeRequires, Array->ElementTypeName(), 0);
		}
		return false;
	}
	bool ContainsType(std::string_view Type) noexcept
	{
		try
		{
			return InspectType(Type, [](std::string_view)
			{
				return false;
			}, 0);
		}
		catch (...)
		{
			return true;
		}
	}
	bool ContainsType(const FBytecodeModule& Module, std::string_view Type)
	{
		std::unordered_set<std::string> Seen;
		const auto Inspect = [&](auto& Self, std::string_view Name, std::size_t Depth) -> bool
		{
			if (Depth > 128) return true;
			return InspectType(Name, [&](std::string_view Node)
			{
				if (!Seen.insert(std::string(Node)).second) return false;
				for (const auto& T : Module.Types)
				{
					if (T.Name == Node)
					{
						for (const auto& F : T.Fields)
						{
							if (Self(Self, F.TypeName, Depth + 1)) return true;
						}
					}
				}
				return false;
			}, Depth);
		};
		return Inspect(Inspect, Type, 0);
	}
	bool UsesRuntime(const FBytecodeModule& Module, const std::vector<FNativeFunction>* Natives)
	{
		for (const auto& T : Module.GlobalTypeNames)
		{
			if (ContainsType(T)) return true;
		}
		for (const auto& T : Module.Types)
		{
			if (ContainsType(T.Name)) return true;
			for (const auto& F : T.Fields)
			{
				if (ContainsType(F.TypeName)) return true;
			}
		}
		const auto InspectCode = [&](const std::vector<std::uint8_t>& Code)
		{
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Code[Ip]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (!Size || Ip + Size > Code.size()) return true;
				if (Op == EOpcode::BoxVariant || Op == EOpcode::VariantIs) return true;
				if (Op == EOpcode::CallBuiltin && Size >= 4)
				{
					const auto Id = static_cast<EBuiltinId>(Bytecode::ReadU16LE(Code.data() + Ip + 1));
					if (Id == EBuiltinId::VariantEmpty || Id == EBuiltinId::VariantHasValue || Id == EBuiltinId::VariantTypeName) return true;
				}
				if (Op == EOpcode::CallNative && Natives && Size >= 4)
				{
					const auto Index = Bytecode::ReadU16LE(Code.data() + Ip + 1);
					if (Index < Natives->size())
					{
						const auto& N = (*Natives)[Index];
						if (ContainsType(ResolveNativeReturnTypeName(N))) return true;
						for (std::size_t A = 0; A < N.ParamTypes.size(); ++A)
						{
							if (ContainsType(ResolveNativeParamTypeName(N, A))) return true;
						}
					}
				}
				Ip += Size;
			}
			return false;
		};
		if (InspectCode(Module.InitCode)) return true;
		for (const auto& F : Module.Functions)
		{
			if (ContainsType(F.ClosureTypeName) || InspectCode(F.Code)) return true;
		}
		return false;
	}
	bool UsesRuntime(const FProgram& Program)
	{
		bool Found = false;
		const auto Type = [&](std::string_view Name)
		{
			Found |= ContainsType(Name);
		};
		const auto Parameters = [&](const auto& Params)
		{
			for (const auto& P : Params)
			{
				Type(P.TypeName);
			}
		};
		const auto Types = [&](const auto& Declarations)
		{
			for (const auto& D : Declarations)
			{
				for (const auto& T : D.TypeParamDefaults)
				{
					Type(T);
				}
				for (const auto& F : D.Fields)
				{
					Type(F.TypeName);
				}
				for (const auto& M : D.Methods)
				{
					Type(M.ReturnType);
					Parameters(M.Parameters);
				}
				if constexpr (!std::is_same_v<std::decay_t<decltype(D)>, FInterfaceDecl>)
				{
					for (const auto& C : D.Constructors)
					{
						Parameters(C.Parameters);
					}
				}
			}
		};
		Types(Program.Structs);
		Types(Program.Classes);
		Types(Program.Interfaces);
		for (const auto& F : Program.Functions)
		{
			Type(F.ReturnType);
			Parameters(F.Parameters);
			for (const auto& T : F.TypeParamDefaults)
			{
				Type(T);
			}
		}
		for (const auto& A : Program.TypeAliases)
		{
			Type(A.TypeName);
		}
		VisitProgramNodes(Program, [&](const FExpr& E)
		{
			if (E.Kind == EExprKind::Call)
			{
				const auto& C = static_cast<const FCallExpr&>(E);
				Type(C.Callee);
				Found |= C.Callee == "variant_has_value" || C.Callee == "variant_type_name" || C.Callee == "variant_is";
				for (const auto& T : C.TypeArgs)
				{
					Type(T);
				}
			}
			else if (E.Kind == EExprKind::MethodCall)
			{
				for (const auto& T : static_cast<const FMethodCallExpr&>(E).TypeArgs)
				{
					Type(T);
				}
			}
			else if (E.Kind == EExprKind::ArrayInit)
			{
				Type(static_cast<const FArrayInitExpr&>(E).TypeName);
			}
			else if (E.Kind == EExprKind::MapInit)
			{
				Type(static_cast<const FMapInitExpr&>(E).TypeName);
			}
			else if (E.Kind == EExprKind::Lambda)
			{
				const auto& L = static_cast<const FLambdaExpr&>(E);
				Type(L.ReturnType);
				Parameters(L.Parameters);
			}
		}, [&](const FStmt& S)
		{
			if (S.Kind == EStmtKind::VarDecl)
			{
				Type(static_cast<const FVarDeclStmt&>(S).TypeName);
			}
			else if (S.Kind == EStmtKind::TypeAlias)
			{
				Type(static_cast<const FTypeAliasStmt&>(S).TypeName);
			}
			else if (S.Kind == EStmtKind::ForEach)
			{
				Type(static_cast<const FForEachStmt&>(S).ElementTypeName);
			}
			else if (S.Kind == EStmtKind::Try)
			{
				for (const auto& C : static_cast<const FTryStmt&>(S).Catches)
				{
					Type(C.TypeName);
				}
			}
		});
		return Found;
	}
	namespace
	{
		bool IsInternalReference(const FValue& V)
		{
			return std::holds_alternative<FLocalRef>(V) || std::holds_alternative<FGlobalRef>(V) || std::holds_alternative<FThisFieldRef>(V) || std::holds_alternative<FIndexRef>(V) || std::holds_alternative<FMapEntryRef>(V) || IsClosureReferenceCell(V);
		}
	}
	FValue Empty() noexcept
	{
		return FScriptVariantHandle{};
	}
	bool IsVariant(const FValue& Value) noexcept
	{
		return std::holds_alternative<FScriptVariantHandle>(Value);
	}
	bool HasValue(const FValue& Value) noexcept
	{
		const auto* Box = std::get_if<FScriptVariantHandle>(&Value);
		return Box && *Box;
	}
	std::string_view TypeName(const FValue& Value) noexcept
	{
		const auto* Box = std::get_if<FScriptVariantHandle>(&Value);
		return Box && *Box ? std::string_view((*Box)->CanonicalTypeName) : std::string_view{};
	}
	bool Is(const FValue& Value, std::string_view Type) noexcept
	{
		return HasValue(Value) && TypeName(Value) == Type;
	}
	bool Validate(const FValue& Value, std::string& Error)
	{
		const auto* Box = std::get_if<FScriptVariantHandle>(&Value);
		if (!Box)
		{
			Error = "expected variant";
			return false;
		}
		if (!*Box) return true;
		const auto& Type = (*Box)->CanonicalTypeName;
		if (Type.empty() || Type == "void" || Type == "nullptr" || Type == "variant" || IsInternalReference((*Box)->Value) || IsVariant((*Box)->Value))
		{
			Error = "invalid variant storage";
			return false;
		}
		return true;
	}
	bool Box(const FVariantTypeDescriptor& Type, const FValue& Value, FValue& Output, std::string& Error)
	{
		if (NeedsCopyContext(Value))
		{
			Error = "variant value copy requires its owning VM/session";
			return false;
		}
		const auto& Name = FVariantAccess::Name(Type);
		if (Name.empty() || Name == "void" || Name == "nullptr" || Name == "variant" || IsInternalReference(Value) || IsVariant(Value))
		{
			Error = "variant boxing requires a validated concrete value type";
			return false;
		}
		auto Storage = std::make_shared<FScriptVariantStorage>();
		Storage->CanonicalTypeName = Name;
		Storage->Source = FVariantAccess::Source(Type);
		if (!Runtime::TryCloneValue(Value, Storage->Value))
		{
			Error = "variant payload clone failed";
			return false;
		}
		Output = std::move(Storage);
		return true;
	}
	bool ValidateSource(const FValue& Value, const std::shared_ptr<const void>& Owner,
	    std::uint64_t Generation, std::string& Error)
	{
		if (!Validate(Value, Error)) return false;
		const auto& Box = std::get<FScriptVariantHandle>(Value);
		if (Box && Box->Source && (Box->Source->ProgramOwner != Owner || Box->Source->Generation != Generation))
		{
			Error = "variant belongs to a foreign or stale type environment";
			return false;
		}
		return true;
	}
	bool Snapshot(const FValue& Value, FValue& Output, std::string& Error)
	{
		if (!Validate(Value, Error)) return false;
		const auto& Box = std::get<FScriptVariantHandle>(Value);
		if (!Box)
		{
			Error = "cannot read an empty variant";
			return false;
		}
		if (Box->bCopyRequiresVm)
		{
			Error = "variant value copy requires its owning VM/session";
			return false;
		}
		FValue Prepared;
		if (!Runtime::TryCloneValue(Box->Value, Prepared))
		{
			Error = "variant payload clone failed";
			return false;
		}
		Output = std::move(Prepared);
		return true;
	}
}
