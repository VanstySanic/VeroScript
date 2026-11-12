// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeLinker.h"

#include "VeroScriptBytecodeIO.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptHostBinding.h"
#include "Processing/VeroScriptSourceMetadataIO.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <tuple>

namespace VeroScript
{
	namespace
	{
		constexpr std::uint16_t InvalidIndex = std::numeric_limits<std::uint16_t>::max();

		struct FIndexMaps
		{
			std::vector<std::uint16_t> Natives;
			std::vector<std::uint16_t> Strings;
			std::vector<std::uint16_t> WStrings;
			std::vector<std::uint16_t> Constants;
			std::vector<std::uint16_t> Types;
			std::vector<std::uint16_t> Interfaces;
			std::vector<std::uint16_t> Functions;
			std::vector<std::uint16_t> Globals;
			std::vector<bool> SkipGlobalInitializers;
			std::vector<std::uint16_t> DebugFiles;
		};

		bool RemapU16(
		    std::vector<std::uint8_t>& Code,
		    const std::size_t Offset,
		    const std::vector<std::uint16_t>& Mapping,
		    const char* Kind,
		    std::string& OutError)
		{
			const std::uint16_t Old = Bytecode::ReadU16LE(Code.data() + Offset);
			if (Old >= Mapping.size())
			{
				OutError = std::string("invalid ") + Kind + " index while linking";
				return false;
			}
			const std::uint16_t New = Mapping[Old];
			Code[Offset] = static_cast<std::uint8_t>(New & 0xffu);
			Code[Offset + 1] = static_cast<std::uint8_t>((New >> 8) & 0xffu);
			return true;
		}

		bool RemapCode(
		    std::vector<std::uint8_t>& Code,
		    const FIndexMaps& Maps,
		    std::string& OutError)
		{
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const EOpcode Op = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Code.size() - Ip)
				{
					OutError = "malformed instruction while linking";
					return false;
				}
				const std::size_t Operand = Ip + 1;
				bool bOk = true;
				switch (Op)
				{
				case EOpcode::CallNative:
					{
						bOk = RemapU16(Code, Operand, Maps.Natives, "native reference", OutError);
						break;
					}
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstStr:
					{
						bOk = RemapU16(Code, Operand, Maps.Constants, "constant", OutError);
						break;
					}
				case EOpcode::BeginLocalStatic:
				case EOpcode::CommitLocalStatic:
				case EOpcode::LoadGlobal:
				case EOpcode::StoreGlobal:
				case EOpcode::MakeGlobalRef:
					{
						bOk = RemapU16(Code, Operand, Maps.Globals, "global", OutError);
						break;
					}
				case EOpcode::LoadField:
				case EOpcode::StoreField:
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
				case EOpcode::LoadFieldKeep:
				case EOpcode::MakeThisFieldRef:
				case EOpcode::MakeFieldRef:
				case EOpcode::IncField:
				case EOpcode::CallMethod:
				case EOpcode::NewNativeArray:
				case EOpcode::NewUninitializedNativeArray:
				case EOpcode::NewDynamicArray:
				case EOpcode::NewMap:
				case EOpcode::NewSet:
				case EOpcode::NewQueue:
				case EOpcode::NewStack:
				case EOpcode::NewOrderedMap:
				case EOpcode::NewDeque:
				case EOpcode::NewChannel:
				case EOpcode::CheckedCast:
				case EOpcode::BoxVariant:
				case EOpcode::VariantIs:
				case EOpcode::CloneValueOfType:
					{
						bOk = RemapU16(Code, Operand, Maps.Strings, "string", OutError);
						break;
					}
				case EOpcode::NewObject:
				case EOpcode::NewUninitializedObject:
					{
						bOk = RemapU16(Code, Operand, Maps.Types, "type", OutError);
						break;
					}
				case EOpcode::TryCast:
					{
						bOk = RemapU16(Code, Operand, Maps.Strings, "string", OutError) && RemapU16(Code, Operand + 2, Maps.Types, "type", OutError);
						break;
					}
				case EOpcode::Call:
				case EOpcode::CallBoundMethod:
				case EOpcode::MakeClosure:
					{
						bOk = RemapU16(Code, Operand, Maps.Functions, "function", OutError);
						break;
					}
				default:
					{
						break;
					}
				}
				if (!bOk)
				{
					return false;
				}
				Ip += Size;
			}
			return true;
		}

		bool EqualConstant(
		    const FBytecodeConstant& A,
		    const FBytecodeConstant& B)
		{
			std::uint32_t AF32Bits = 0;
			std::uint32_t BF32Bits = 0;
			std::uint64_t ABits = 0;
			std::uint64_t BBits = 0;
			static_assert(sizeof(AF32Bits) == sizeof(A.F32), "unexpected float size");
			static_assert(sizeof(ABits) == sizeof(A.F64), "unexpected double size");
			std::memcpy(&AF32Bits, &A.F32, sizeof(AF32Bits));
			std::memcpy(&BF32Bits, &B.F32, sizeof(BF32Bits));
			std::memcpy(&ABits, &A.F64, sizeof(ABits));
			std::memcpy(&BBits, &B.F64, sizeof(BBits));
			return A.Kind == B.Kind && A.I8 == B.I8 && A.U8 == B.U8 && A.I16 == B.I16 && A.U16 == B.U16 && A.I32 == B.I32 && A.U32 == B.U32 && A.I64 == B.I64 && A.U64 == B.U64 && AF32Bits == BF32Bits && ABits == BBits && A.Bool == B.Bool && A.bIsLinkageString == B.bIsLinkageString && A.StringIndex == B.StringIndex;
		}

		bool IsGenericInstanceLinkage(const std::string& Name)
		{
			const std::size_t IdentityStart = FindLinkageIdentityStart(Name);
			const std::size_t FunctionMarker = Name.find("#fn#", IdentityStart);
			const std::size_t SearchEnd = FunctionMarker == std::string::npos
			    ? Name.size()
			    : FunctionMarker;
			for (std::size_t Marker = Name.find('#', IdentityStart);
			    Marker != std::string::npos && Marker < SearchEnd;
			    Marker = Name.find('#', Marker + 1))
			{
				std::size_t Cursor = Marker + 1;
				if (Cursor >= SearchEnd || !std::isdigit(static_cast<unsigned char>(Name[Cursor])))
				{
					continue;
				}
				while (Cursor < SearchEnd && std::isdigit(static_cast<unsigned char>(Name[Cursor])))
				{
					++Cursor;
				}
				if (Cursor < SearchEnd && Name[Cursor] == ':')
				{
					return true;
				}
			}
			return false;
		}

		bool IsCoreBuiltinSingletonType(const std::string_view Name)
		{
			// These non-generic value types are synthesized identically by every
			// compilation unit. They have one language-level identity and are
			// therefore coalesced like generic monomorphs; CopyTypes still performs
			// a strict normalized definition comparison before accepting them.
			return Name == "module_info" || Name == "function_info" || Name == "global_info" || Name == "source_node_info" || Name == "annotation_info" || Name == "annotation_argument_info" || Name == "comment_info" || Name == "annotation_definition_info";
		}

		bool IsCoreBuiltinSingletonFunction(const std::string_view Name)
		{
			const std::size_t Separator = Name.find("::");
			return Separator != std::string_view::npos && IsCoreBuiltinSingletonType(Name.substr(0, Separator));
		}

		bool IsDefaultConstructionFunction(const FBytecodeModule& Module, const FBytecodeFunction& Function)
		{
			if (Function.ParamCount != 0 || Function.bReturnsRef || Function.bIsVirtual || Function.bIsAbstract || Function.bDeleted || Function.bIsStaticMethod)
			{
				return false;
			}
			if (Function.bIsMethod)
			{
				if (!Function.bDefaulted || !Function.bReturnsVoid || Function.SpecialMemberKind != ESpecialMemberKind::None || Function.OwnerTypeIndex >= Module.Types.size())
				{
					return false;
				}
				const auto& Owner = Module.Types[Function.OwnerTypeIndex];
				return Function.Name == MakeConstructorFunctionName(Owner.Name, ModuleMeta::TypeSimpleName(Owner.Name), {}) && Function.ClosureTypeName == Runtime::MakeFunctionTypeName({}, "void");
			}
			if (Function.bReturnsVoid || Function.bDefaulted || Function.Name.size() <= DefaultConstructionFactorySuffix.size() || Function.Name.compare(Function.Name.size() - DefaultConstructionFactorySuffix.size(), DefaultConstructionFactorySuffix.size(), DefaultConstructionFactorySuffix) != 0)
			{
				return false;
			}
			const auto OwnerName = Function.Name.substr(0, Function.Name.size() - DefaultConstructionFactorySuffix.size());
			return Function.ClosureTypeName == Runtime::MakeFunctionTypeName({}, OwnerName) && std::any_of(Module.Types.begin(), Module.Types.end(), [&](const auto& Type)
			{
				return Type.Name == OwnerName && Type.Kind == EScriptObjectKind::Struct;
			});
		}

		bool EquivalentField(
		    const FBytecodeFieldDesc& Left,
		    const FBytecodeFieldDesc& Right)
		{
			return Left.Name == Right.Name && Left.TypeName == Right.TypeName && Left.DeclaredTypeName == Right.DeclaredTypeName && EqualConstant(Left.DefaultValue, Right.DefaultValue) && Left.bHasDefault == Right.bHasDefault && Left.InitCode == Right.InitCode && Left.bHasInitCode == Right.bHasInitCode && Left.bIsMutable == Right.bIsMutable && Left.bIsConst == Right.bIsConst && Left.bConstexpr == Right.bConstexpr && Left.Access == Right.Access && Left.InitLocalCount == Right.InitLocalCount && Left.DeclMeta == Right.DeclMeta;
		}

		bool EquivalentFields(
		    const std::vector<FBytecodeFieldDesc>& Left,
		    const std::vector<FBytecodeFieldDesc>& Right)
		{
			if (Left.size() != Right.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Left.size(); ++Index)
			{
				if (!EquivalentField(Left[Index], Right[Index]))
				{
					return false;
				}
			}
			return true;
		}

		bool EquivalentTypeScope(const FTypeDeclarationScope& Left, const FTypeDeclarationScope& Right)
		{
			const auto Key = [](const FTypeDeclarationScope& S)
			{
				return std::tie(S.Id, S.ScopeId, S.ParentScopeId, S.SimpleName, S.QualifiedName,
				    S.OwnerTypeName, S.OwnerFunctionName, S.AccessTypeName, S.AccessModulePath,
				    S.ModulePath, S.Kind, S.DisplayName, S.EnclosingTypeParameters, S.FunctionTypeParameters,
				    S.ConcreteTypeArguments, S.ForbiddenRuntimeNames, S.OwnTypeParameterCount,
				    S.Access, S.bLocal, S.bExported, S.bPublicPath);
			};
			return Key(Left) == Key(Right) && Left.OwnTemplateParameterCount == Right.OwnTemplateParameterCount && EqualTemplateParameters(Left.EnclosingTemplateParameters, Right.EnclosingTemplateParameters);
		}

		bool EquivalentNestedTypes(const std::vector<FNestedTypeDeclaration>& Left,
		    const std::vector<FNestedTypeDeclaration>& Right)
		{
			if (Left.size() != Right.size()) return false;
			for (std::size_t Index = 0; Index < Left.size(); ++Index)
			{
				if (!EquivalentTypeScope(Left[Index].Scope, Right[Index].Scope) || Left[Index].DeclarationId != Right[Index].DeclarationId)
				{
					return false;
				}
			}
			return true;
		}

		bool EquivalentTemplateInstance(const FTemplateInstanceInfo& Left, const FTemplateInstanceInfo& Right)
		{
			return Left.DeclarationId == Right.DeclarationId && Left.Arguments == Right.Arguments && Left.EnclosingArguments == Right.EnclosingArguments && Left.ValueTypeNames == Right.ValueTypeNames;
		}

		bool EquivalentType(
		    const FBytecodeTypeDesc& Left,
		    const FBytecodeTypeDesc& Right)
		{
			const auto EquivalentUsingMembers = [](
			                                        const std::vector<FBytecodeTypeDesc::FUsingMember>& A,
			                                        const std::vector<FBytecodeTypeDesc::FUsingMember>& B)
			{
				if (A.size() != B.size())
				{
					return false;
				}
				for (std::size_t Index = 0; Index < A.size(); ++Index)
				{
					if (A[Index].BaseTypeName != B[Index].BaseTypeName || A[Index].MemberName != B[Index].MemberName || A[Index].Access != B[Index].Access)
					{
						return false;
					}
				}
				return true;
			};
			return Left.Kind == Right.Kind && EquivalentTemplateInstance(Left.TemplateInstance, Right.TemplateInstance) && EquivalentTypeScope(Left.Scope, Right.Scope) && EquivalentNestedTypes(Left.NestedTypes, Right.NestedTypes) && Left.Name == Right.Name && Left.BaseTypeIndices == Right.BaseTypeIndices && Left.BaseInheritanceAccess == Right.BaseInheritanceAccess && EquivalentUsingMembers(Left.UsingMembers, Right.UsingMembers) && Left.HostBaseTypeName == Right.HostBaseTypeName && Left.HostBaseTypeId == Right.HostBaseTypeId && Left.ImplementedInterfaceIndices == Right.ImplementedInterfaceIndices && EquivalentFields(Left.Fields, Right.Fields) && Left.OwnFieldStartIndex == Right.OwnFieldStartIndex && Left.DestructorFunctionIndex == Right.DestructorFunctionIndex && Left.CopyConstructorIndex == Right.CopyConstructorIndex && Left.bDestructorVirtual == Right.bDestructorVirtual && Left.bDestructorPureVirtual == Right.bDestructorPureVirtual && Left.bIsAbstract == Right.bIsAbstract && Left.bIsStatic == Right.bIsStatic && Left.DeclMeta == Right.DeclMeta && EquivalentFields(Left.StaticFields, Right.StaticFields);
		}

		const char* FirstTypeDifference(
		    const FBytecodeTypeDesc& Left,
		    const FBytecodeTypeDesc& Right)
		{
			if (Left.Kind != Right.Kind)
			{
				return "kind";
			}
			if (Left.Name != Right.Name)
			{
				return "name";
			}
			if (Left.BaseTypeIndices != Right.BaseTypeIndices)
			{
				return "base types";
			}
			if (Left.BaseInheritanceAccess != Right.BaseInheritanceAccess)
			{
				return "base access";
			}
			if (Left.UsingMembers.size() != Right.UsingMembers.size())
			{
				return "using members";
			}
			for (std::size_t Index = 0; Index < Left.UsingMembers.size(); ++Index)
			{
				const auto& A = Left.UsingMembers[Index];
				const auto& B = Right.UsingMembers[Index];
				if (A.BaseTypeName != B.BaseTypeName || A.MemberName != B.MemberName || A.Access != B.Access)
				{
					return "using members";
				}
			}
			if (Left.HostBaseTypeName != Right.HostBaseTypeName)
			{
				return "host base name";
			}
			if (Left.HostBaseTypeId != Right.HostBaseTypeId)
			{
				return "host base id";
			}
			if (Left.ImplementedInterfaceIndices != Right.ImplementedInterfaceIndices)
			{
				return "interfaces";
			}
			if (!EquivalentFields(Left.Fields, Right.Fields))
			{
				return "fields";
			}
			if (Left.OwnFieldStartIndex != Right.OwnFieldStartIndex)
			{
				return "own field start";
			}
			if (Left.DestructorFunctionIndex != Right.DestructorFunctionIndex)
			{
				return "destructor";
			}
			if (Left.bDestructorVirtual != Right.bDestructorVirtual)
			{
				return "virtual destructor flag";
			}
			if (Left.bDestructorPureVirtual != Right.bDestructorPureVirtual)
			{
				return "pure virtual destructor flag";
			}
			if (Left.bIsAbstract != Right.bIsAbstract)
			{
				return "abstract flag";
			}
			if (Left.bIsStatic != Right.bIsStatic)
			{
				return "static type flag";
			}
			if (Left.DeclMeta != Right.DeclMeta)
			{
				return "declaration metadata";
			}
			if (!EquivalentFields(Left.StaticFields, Right.StaticFields))
			{
				return "static fields";
			}
			return "unknown data";
		}

		const char* FirstGlobalMetadataDifference(
		    const FBytecodeGlobalMeta& Left,
		    const FBytecodeGlobalMeta& Right)
		{
			if (Left.DeclaredTypeName != Right.DeclaredTypeName)
			{
				return "declared type";
			}
			if (Left.bConst != Right.bConst)
			{
				return "const flag";
			}
			if (Left.bConstexpr != Right.bConstexpr) return "constexpr flag";
			if (Left.ConstantValue && Right.ConstantValue && EncodeTemplateArgument({ETemplateParameterKind::Value, {}, *Left.ConstantValue}) != EncodeTemplateArgument({ETemplateParameterKind::Value, {}, *Right.ConstantValue})) return "constexpr value";
			if (Left.bExported != Right.bExported)
			{
				return "export flag";
			}
			if (Left.bStaticMember != Right.bStaticMember || Left.bLocalStatic != Right.bLocalStatic)
			{
				return "static-member flag";
			}
			if (Left.DeclMeta != Right.DeclMeta)
			{
				return "declaration metadata";
			}
			return "unknown data";
		}

		bool EquivalentInterface(
		    const FBytecodeInterfaceDesc& Left,
		    const FBytecodeInterfaceDesc& Right)
		{
			return Left.Name == Right.Name && EquivalentTemplateInstance(Left.TemplateInstance, Right.TemplateInstance) && Left.BaseInterfaceIndices == Right.BaseInterfaceIndices && EquivalentTypeScope(Left.Scope, Right.Scope) && EquivalentNestedTypes(Left.NestedTypes, Right.NestedTypes) && EquivalentFields(Left.Fields, Right.Fields) && EquivalentFields(Left.StaticFields, Right.StaticFields) && Left.DeclMeta == Right.DeclMeta;
		}

		bool EquivalentEnum(
		    const FBytecodeEnumDesc& Left,
		    const FBytecodeEnumDesc& Right)
		{
			if (Left.Name != Right.Name || !EquivalentTemplateInstance(Left.TemplateInstance, Right.TemplateInstance) || !EquivalentTypeScope(Left.Scope, Right.Scope) || Left.bScoped != Right.bScoped || Left.UnderlyingTypeName != Right.UnderlyingTypeName || Left.DeclMeta != Right.DeclMeta || Left.Members.size() != Right.Members.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Left.Members.size(); ++Index)
			{
				const FBytecodeEnumMemberDesc& A = Left.Members[Index];
				const FBytecodeEnumMemberDesc& B = Right.Members[Index];
				if (A.Name != B.Name || A.Value != B.Value || A.DeclMeta != B.DeclMeta)
				{
					return false;
				}
			}
			return true;
		}

		bool EquivalentFunction(
		    const FBytecodeFunction& Left,
		    const FBytecodeFunction& Right)
		{
			const auto EquivalentExceptionRegions = [](
			                                            const std::vector<FBytecodeExceptionRegion>& A,
			                                            const std::vector<FBytecodeExceptionRegion>& B)
			{
				if (A.size() != B.size())
				{
					return false;
				}
				for (std::size_t RegionIndex = 0; RegionIndex < A.size(); ++RegionIndex)
				{
					const FBytecodeExceptionRegion& L = A[RegionIndex];
					const FBytecodeExceptionRegion& R = B[RegionIndex];
					if (L.BeginOffset != R.BeginOffset || L.EndOffset != R.EndOffset || L.FinallyOffset != R.FinallyOffset || L.FinallyEndOffset != R.FinallyEndOffset || L.Catches.size() != R.Catches.size())
					{
						return false;
					}
					for (std::size_t CatchIndex = 0; CatchIndex < L.Catches.size(); ++CatchIndex)
					{
						const FBytecodeCatchHandler& LC = L.Catches[CatchIndex];
						const FBytecodeCatchHandler& RC = R.Catches[CatchIndex];
						if (LC.TypeName != RC.TypeName || LC.LocalSlot != RC.LocalSlot || LC.RethrowSlot != RC.RethrowSlot || LC.HandlerOffset != RC.HandlerOffset)
						{
							return false;
						}
					}
				}
				return true;
			};
			return Left.Name == Right.Name && EquivalentTemplateInstance(Left.TemplateInstance, Right.TemplateInstance) && Left.bReturnsVoid == Right.bReturnsVoid && Left.bReturnsRef == Right.bReturnsRef && Left.bIsMethod == Right.bIsMethod && Left.bIsVirtual == Right.bIsVirtual && Left.bIsAbstract == Right.bIsAbstract && Left.bDeleted == Right.bDeleted && Left.SpecialMemberKind == Right.SpecialMemberKind && Left.bDefaulted == Right.bDefaulted && Left.bExplicitCopy == Right.bExplicitCopy && Left.UnavailableReason == Right.UnavailableReason && Left.bIsConstMethod == Right.bIsConstMethod && Left.bConstexpr == Right.bConstexpr && Left.bIsStaticMethod == Right.bIsStaticMethod && Left.bUsesLocalStaticStorage == Right.bUsesLocalStaticStorage && Left.LocalStaticSemanticIdentity == Right.LocalStaticSemanticIdentity && Left.LocalStaticInitializers == Right.LocalStaticInitializers && Left.Access == Right.Access && Left.OwnerTypeIndex == Right.OwnerTypeIndex && Left.ParamCount == Right.ParamCount && Left.LocalCount == Right.LocalCount && Left.Code == Right.Code && Left.PauseMarkerOffsets == Right.PauseMarkerOffsets && EquivalentExceptionRegions(Left.ExceptionRegions, Right.ExceptionRegions) && Left.ClosureTypeName == Right.ClosureTypeName && Left.Captures == Right.Captures && Left.ParamFlags == Right.ParamFlags && Left.DeclMeta == Right.DeclMeta;
		}

		struct FConstantEqual
		{
			bool operator()(
			    const FBytecodeConstant& A,
			    const FBytecodeConstant& B) const
			{
				return EqualConstant(A, B);
			}
		};

		struct FConstantHash
		{
			std::size_t operator()(const FBytecodeConstant& Value) const
			{
				std::uint32_t FloatBits = 0;
				std::uint64_t FloatingBits = 0;
				std::memcpy(&FloatBits, &Value.F32, sizeof(FloatBits));
				std::memcpy(
				    &FloatingBits, &Value.F64, sizeof(FloatingBits));
				std::size_t Hash = static_cast<std::size_t>(1469598103934665603ull);
				const auto Combine = [&](const std::size_t Part)
				{
					Hash ^= Part + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
				};
				Combine(std::hash<std::uint8_t>{}(
				    static_cast<std::uint8_t>(Value.Kind)));
				Combine(std::hash<std::int8_t>{}(Value.I8));
				Combine(std::hash<std::uint8_t>{}(Value.U8));
				Combine(std::hash<std::int16_t>{}(Value.I16));
				Combine(std::hash<std::uint16_t>{}(Value.U16));
				Combine(std::hash<std::int32_t>{}(Value.I32));
				Combine(std::hash<std::uint32_t>{}(Value.U32));
				Combine(std::hash<std::int64_t>{}(Value.I64));
				Combine(std::hash<std::uint64_t>{}(Value.U64));
				Combine(std::hash<std::uint32_t>{}(FloatBits));
				Combine(std::hash<std::uint64_t>{}(FloatingBits));
				Combine(std::hash<bool>{}(Value.Bool));
				Combine(std::hash<bool>{}(Value.bIsLinkageString));
				Combine(std::hash<std::uint16_t>{}(Value.StringIndex));
				return Hash;
			}
		};

		bool CompatibleStub(
		    const FBytecodeFunction& Stub,
		    const FBytecodeFunction& Definition)
		{
			return Stub.Code.empty() && Stub.ParamCount == Definition.ParamCount && Stub.bReturnsVoid == Definition.bReturnsVoid && Stub.bReturnsRef == Definition.bReturnsRef && Stub.bIsMethod == Definition.bIsMethod && Stub.bIsAbstract == Definition.bIsAbstract && Stub.bDeleted == Definition.bDeleted && Stub.bIsConstMethod == Definition.bIsConstMethod && Stub.bConstexpr == Definition.bConstexpr && (Stub.ClosureTypeName.empty() || Definition.ClosureTypeName.empty() || Stub.ClosureTypeName == Definition.ClosureTypeName) && (Stub.ParamFlags.empty() || Definition.ParamFlags.empty() || Stub.ParamFlags == Definition.ParamFlags);
		}

		template <typename TValue>
		bool AllocateNamed(
		    const std::vector<TValue>& Source,
		    const std::vector<std::string>& SourceOwners,
		    std::vector<TValue>& Destination,
		    std::vector<std::string>& DestinationOwners,
		    std::unordered_map<std::string, std::uint16_t>& ByName,
		    std::vector<std::uint16_t>& Mapping,
		    const std::unordered_set<std::string>& NewPackageOwners,
		    const char* Kind,
		    const bool bAllowPlaceholder,
		    std::string& OutError)
		{
			Mapping.resize(Source.size(), InvalidIndex);
			for (std::size_t Index = 0; Index < Source.size(); ++Index)
			{
				const std::string SourceOwner =
				    Index < SourceOwners.size()
				    ? SourceOwners[Index]
				    : std::string{};
				const auto Existing = ByName.find(Source[Index].Name);
				if (Existing != ByName.end())
				{
					const std::string& DestinationOwner =
					    Existing->second < DestinationOwners.size()
					    ? DestinationOwners[Existing->second]
					    : std::string{};
					if (!SourceOwner.empty() && SourceOwner == DestinationOwner)
					{
						Mapping[Index] = Existing->second;
						continue;
					}
					if (IsGenericInstanceLinkage(Source[Index].Name) || (std::string_view(Kind) == "type" && IsCoreBuiltinSingletonType(Source[Index].Name)))
					{
						// Independently compiled consumers can materialize the same
						// open-generic monomorph. Copy* performs a strict normalized
						// definition comparison before accepting the shared instance.
						Mapping[Index] = Existing->second;
						continue;
					}
					if (!bAllowPlaceholder)
					{
						OutError = std::string("duplicate ") + Kind + " definition: " + Source[Index].Name;
						return false;
					}
					Mapping[Index] = Existing->second;
					continue;
				}
				if (!SourceOwner.empty() && NewPackageOwners.find(SourceOwner) == NewPackageOwners.end())
				{
					OutError = "package component contains unexpected " + std::string(Kind) + " definition: " + Source[Index].Name;
					return false;
				}
				if (Destination.size() >= InvalidIndex)
				{
					OutError = std::string(Kind) + " table overflow while linking";
					return false;
				}
				const auto NewIndex = static_cast<std::uint16_t>(Destination.size());
				Destination.emplace_back();
				DestinationOwners.push_back(SourceOwner);
				ByName.emplace(Source[Index].Name, NewIndex);
				Mapping[Index] = NewIndex;
			}
			return true;
		}

		class FLinker
		{
		public:
			bool Merge(
			    const FBytecodeModule& Source,
			    const bool bRoot,
			    std::string& OutError)
			{
				FIndexMaps Maps;
				for (const auto& Reference : Source.NativeReferences)
				{
					const auto Found = std::find(Result.NativeReferences.begin(), Result.NativeReferences.end(), Reference);
					if (Found == Result.NativeReferences.end())
					{
						if (Result.NativeReferences.size() >= InvalidIndex)
						{
							OutError = "native reference table overflow while linking";
							return false;
						}
						Maps.Natives.push_back(static_cast<std::uint16_t>(Result.NativeReferences.size()));
						Result.NativeReferences.push_back(Reference);
					}
					else
					{
						Maps.Natives.push_back(static_cast<std::uint16_t>(Found - Result.NativeReferences.begin()));
					}
				}
				CurrentNewPackageOwners.clear();
				if (!MergeExternalDependencies(Result.RequiredExternalDependencies, Source.RequiredExternalDependencies, OutError) || !MergePackageComponents(Source, OutError) || !CopyPackageTemplates(Source, OutError) || !AllocatePools(Source, Maps, OutError) || !AllocateNamed(Source.Interfaces, Source.PackageInterfaceOwners, Result.Interfaces, Result.PackageInterfaceOwners, InterfaceByName, Maps.Interfaces, CurrentNewPackageOwners, "interface", false, OutError) || !AllocateNamed(Source.Types, Source.PackageTypeOwners, Result.Types, Result.PackageTypeOwners, TypeByName, Maps.Types, CurrentNewPackageOwners, "type", false, OutError) || !AllocateFunctions(Source, bRoot, Maps, OutError) || !AllocateGlobals(Source, Maps, OutError) || !AllocateConstants(Source, Maps, OutError))
				{
					return false;
				}

				if (!CopyInterfaces(Source, Maps, OutError) || !CopyTypes(Source, Maps, OutError) || !CopyEnums(Source, OutError) || !CopyFunctions(Source, Maps, OutError) || !CopyInit(Source, Maps, OutError))
				{
					return false;
				}

				// FBytecodeFunction::Location keeps a string_view file label. Linked
				// functions are copied by value, so retain every source backing owner
				// instead of leaving those labels tied to the input module lifetime.
				for (const std::shared_ptr<const void>& Anchor : Source.LifetimeAnchors)
				{
					if (!Anchor)
					{
						continue;
					}
					const auto Existing = std::find_if(
					    Result.LifetimeAnchors.begin(),
					    Result.LifetimeAnchors.end(),
					    [&](const std::shared_ptr<const void>& Candidate)
					{
						// Compare control-block ownership, not only the stored pointer.
						// Distinct owners may legally expose the same address, while
						// aliasing shared_ptr instances with different addresses may
						// share the one lifetime that needs retaining.
						return !Candidate.owner_before(Anchor) && !Anchor.owner_before(Candidate);
					});
					if (Existing == Result.LifetimeAnchors.end())
					{
						Result.LifetimeAnchors.push_back(Anchor);
					}
				}

				if (bRoot && Source.MainFunctionIndex >= 0)
				{
					if (static_cast<std::size_t>(Source.MainFunctionIndex) >= Maps.Functions.size())
					{
						OutError = "root main function index is invalid";
						return false;
					}
					Result.MainFunctionIndex =
					    Maps.Functions[static_cast<std::size_t>(Source.MainFunctionIndex)];
				}
				if (bRoot)
				{
					Result.EntryModulePath = Source.EntryModulePath;
				}
				if (Source.SourceMetadata)
				{
					auto Snapshot = Result.SourceMetadata
					    ? std::make_shared<FSourceMetadataSnapshot>(*Result.SourceMetadata)
					    : std::make_shared<FSourceMetadataSnapshot>();
					auto Merge = [&](auto& Into, const auto& From, auto Select)
					{
						for (const auto& V : From)
						{
							const auto I = std::find_if(Into.begin(), Into.end(),
							    [&](const auto& X)
							{
								return X.Id == V.Id;
							});
							if (I == Into.end())
							{
								Into.push_back(V);
								continue;
							}
							FSourceMetadataSnapshot L, R;
							Select(L, *I);
							Select(R, V);
							std::vector<std::uint8_t> A, B;
							if (!EncodeSourceMetadata(L, A, OutError) ||
							    !EncodeSourceMetadata(R, B, OutError) || A != B)
							{
								OutError = "conflicting linked source metadata identity: " + V.Id;
								return false;
							}
						}
						return true;
					};
					if (!Merge(Snapshot->Documents, Source.SourceMetadata->Documents,
					        [](auto& S, const auto& V)
					{
						S.Documents.push_back(V);
					}) ||
					    !Merge(Snapshot->Nodes, Source.SourceMetadata->Nodes,
					        [](auto& S, const auto& V)
					{
						S.Nodes.push_back(V);
					}) ||
					    !Merge(Snapshot->Definitions, Source.SourceMetadata->Definitions,
					        [](auto& S, const auto& V)
					{
						S.Definitions.push_back(V);
					}))
					{
						return false;
					}

					if (bRoot)
					{
						Snapshot->Retention = Source.SourceMetadata->Retention;
					}

					Result.SourceMetadata = std::move(Snapshot);
				}
				return true;
			}

			std::unique_ptr<FBytecodeModule> Finish(std::string& OutError)
			{
				Result.Version = VEROSCRIPT_BC_VERSION;
				for (std::size_t Index = 0; Index < Result.Functions.size(); ++Index)
				{
					if (!Result.FunctionIndices.emplace(
					                               Result.Functions[Index].Name,
					                               static_cast<std::uint16_t>(Index))
					        .second)
					{
						OutError = "duplicate linked function name";
						return nullptr;
					}
				}
				for (std::size_t Index = 0; Index < Result.GlobalNames.size(); ++Index)
				{
					Result.GlobalSlots.emplace(
					    Result.GlobalNames[Index],
					    static_cast<std::uint16_t>(Index));
				}
				// Initializer slices may split a pause Nop from its following opcode.
				// Normalize after all slices/modules have reached their final positions.
				auto& Markers = Result.InitPauseMarkerOffsets;
				Markers.erase(std::remove_if(Markers.begin(), Markers.end(), [&](const auto Ip)
				{
					return Ip >= Result.InitCode.size() || Result.InitCode.size() - Ip <= 1 ||
					    static_cast<EOpcode>(Result.InitCode[Ip]) != EOpcode::Nop ||
					    static_cast<EOpcode>(Result.InitCode[Ip + 1]) == EOpcode::Nop;
				}),
				    Markers.end());
				if (!VeroScriptBytecodeIO::ValidateModule(Result, OutError))
				{
					OutError = "linked module validation failed: " + OutError;
					return nullptr;
				}
				return std::make_unique<FBytecodeModule>(std::move(Result));
			}

		private:
			bool MergePackageComponents(
			    const FBytecodeModule& Source,
			    std::string& OutError)
			{
				for (const FBytecodePackageComponent& Component :
				    Source.PackageComponents)
				{
					const auto Existing = PackageComponentFingerprints.find(
					    Component.Id);
					if (Existing != PackageComponentFingerprints.end())
					{
						if (Existing->second != Component.Fingerprint)
						{
							OutError = "conflicting builds use package identity: " + Component.Id;
							return false;
						}
						continue;
					}
					PackageComponentFingerprints.emplace(
					    Component.Id, Component.Fingerprint);
					CurrentNewPackageOwners.insert(Component.Id);
					Result.PackageComponents.push_back(Component);
				}
				return true;
			}

			static bool EqualTemplateModule(
			    const FBytecodePackageTemplateModule& Left,
			    const FBytecodePackageTemplateModule& Right)
			{
				if (Left.ModulePath != Right.ModulePath || Left.Source != Right.Source || Left.FacadePath != Right.FacadePath || Left.OwnerComponentId != Right.OwnerComponentId || Left.SourceOwnerId != Right.SourceOwnerId || Left.GeneratedOperations.size() != Right.GeneratedOperations.size() || Left.bDeclarationOnly != Right.bDeclarationOnly || Left.bFromPackage != Right.bFromPackage || Left.bPackageSurface != Right.bPackageSurface || Left.Imports.size() != Right.Imports.size())
				{
					return false;
				}
				std::vector<std::uint8_t> LeftOperations, RightOperations, LeftMetadata, RightMetadata;
				std::string Error;
				if (!EncodeCompileOperations(Left.CompileGeneratedOperations, LeftOperations, Error) ||
				    !EncodeCompileOperations(Right.CompileGeneratedOperations, RightOperations, Error) ||
				    LeftOperations != RightOperations)
				{
					return false;
				}

				const FSourceMetadataSnapshot Empty;
				if (!EncodeSourceMetadata(Left.InitialSourceMetadata ? *Left.InitialSourceMetadata : Empty,
				        LeftMetadata, Error) ||
				    !EncodeSourceMetadata(Right.InitialSourceMetadata ? *Right.InitialSourceMetadata : Empty,
				        RightMetadata, Error) ||
				    LeftMetadata != RightMetadata)
				{
					return false;
				}

				for (std::size_t I = 0; I < Left.GeneratedOperations.size(); ++I)
				{
					const auto& A = Left.GeneratedOperations[I];
					const auto& B = Right.GeneratedOperations[I];
					if (A.Kind != B.Kind || A.Access != B.Access || A.TargetId != B.TargetId || A.Source != B.Source || A.GenerationId != B.GenerationId || A.ProcessorId != B.ProcessorId) return false;
				}
				for (std::size_t Index = 0; Index < Left.Imports.size(); ++Index)
				{
					if (Left.Imports[Index].ResolvedModulePath != Right.Imports[Index].ResolvedModulePath || Left.Imports[Index].bReExport != Right.Imports[Index].bReExport)
					{
						return false;
					}
				}
				return true;
			}

			bool CopyPackageTemplates(
			    const FBytecodeModule& Source,
			    std::string& OutError)
			{
				for (const auto& Declaration : Source.TemplateDeclarations)
				{
					const auto Existing = std::find_if(Result.TemplateDeclarations.begin(), Result.TemplateDeclarations.end(),
					    [&](const auto& Candidate)
					{
						return Candidate.Id == Declaration.Id;
					});
					if (Existing == Result.TemplateDeclarations.end())
					{
						Result.TemplateDeclarations.push_back(Declaration);
					}
					else if (!EqualTemplateParameters(Existing->Parameters, Declaration.Parameters) || !EqualTemplateParameters(Existing->EnclosingParameters, Declaration.EnclosingParameters) || std::tie(Existing->Kind, Existing->Name, Existing->Owner, Existing->ModulePath, Existing->ReturnType, Existing->ParameterTypeNames, Existing->Access, Existing->bStatic, Existing->bConst) != std::tie(Declaration.Kind, Declaration.Name, Declaration.Owner, Declaration.ModulePath, Declaration.ReturnType, Declaration.ParameterTypeNames, Declaration.Access, Declaration.bStatic, Declaration.bConst))
					{
						OutError = "conflicting open template declaration: " + Declaration.Id;
						return false;
					}
				}

				for (const FBytecodePackageTemplateModule& Template :
				    Source.PackageTemplateModules)
				{
					const auto Existing = std::find_if(
					    Result.PackageTemplateModules.begin(),
					    Result.PackageTemplateModules.end(),
					    [&](const FBytecodePackageTemplateModule& Candidate)
					{
						return Candidate.ModulePath == Template.ModulePath;
					});
					if (Existing == Result.PackageTemplateModules.end())
					{
						Result.PackageTemplateModules.push_back(Template);
					}
					else if (!EqualTemplateModule(*Existing, Template))
					{
						OutError = "conflicting package template module: " + Template.ModulePath;
						return false;
					}
				}
				return true;
			}

			template <typename TValue>
			bool InternPool(
			    const std::vector<TValue>& Source,
			    std::vector<TValue>& Destination,
			    std::unordered_map<TValue, std::uint16_t>& Indices,
			    std::vector<std::uint16_t>& Mapping,
			    const char* Kind,
			    std::string& OutError)
			{
				Mapping.reserve(Source.size());
				for (const TValue& Value : Source)
				{
					const auto Existing = Indices.find(Value);
					if (Existing != Indices.end())
					{
						Mapping.push_back(Existing->second);
						continue;
					}
					if (Destination.size() >= InvalidIndex)
					{
						OutError = std::string(Kind) + " table overflow while linking";
						return false;
					}
					const auto NewIndex =
					    static_cast<std::uint16_t>(Destination.size());
					Mapping.push_back(NewIndex);
					Destination.push_back(Value);
					Indices.emplace(Destination.back(), NewIndex);
				}
				return true;
			}

			bool AllocatePools(
			    const FBytecodeModule& Source,
			    FIndexMaps& Maps,
			    std::string& OutError)
			{
				return InternPool(
				           Source.StringPool,
				           Result.StringPool,
				           StringPoolIndices,
				           Maps.Strings,
				           "string",
				           OutError) &&
				    InternPool(
				        Source.WStringPool,
				        Result.WStringPool,
				        WStringPoolIndices,
				        Maps.WStrings,
				        "wide string",
				        OutError) &&
				    InternPool(
				        Source.DebugFiles,
				        Result.DebugFiles,
				        DebugFileIndices,
				        Maps.DebugFiles,
				        "debug file",
				        OutError);
			}

			bool AllocateFunctions(
			    const FBytecodeModule& Source,
			    const bool bRoot,
			    FIndexMaps& Maps,
			    std::string& OutError)
			{
				Maps.Functions.resize(Source.Functions.size(), InvalidIndex);
				for (std::size_t Index = 0; Index < Source.Functions.size(); ++Index)
				{
					const FBytecodeFunction& Function = Source.Functions[Index];
					const std::string SourceOwner =
					    Index < Source.PackageFunctionOwners.size()
					    ? Source.PackageFunctionOwners[Index]
					    : std::string{};
					const auto Existing = FunctionByName.find(Function.Name);
					if (Existing != FunctionByName.end())
					{
						const std::string DestinationOwner =
						    Existing->second < Result.PackageFunctionOwners.size()
						    ? Result.PackageFunctionOwners[Existing->second]
						    : std::string{};
						if (!SourceOwner.empty() && SourceOwner == DestinationOwner)
						{
							Maps.Functions[Index] = Existing->second;
							continue;
						}
						if (IsGenericInstanceLinkage(Function.Name) || IsCoreBuiltinSingletonFunction(Function.Name) || (IsDefaultConstructionFunction(Source, Function) && IsDefaultConstructionFunction(Result, Result.Functions[Existing->second])))
						{
							Maps.Functions[Index] = Existing->second;
							continue;
						}
						if (!bRoot || !CompatibleStub(Function, Result.Functions[Existing->second]))
						{
							OutError = "duplicate function definition: " + Function.Name;
							return false;
						}
						Maps.Functions[Index] = Existing->second;
						continue;
					}
					if (!SourceOwner.empty() && CurrentNewPackageOwners.find(SourceOwner) == CurrentNewPackageOwners.end())
					{
						OutError = "package component contains unexpected function: " + Function.Name;
						return false;
					}
					if (Result.Functions.size() >= InvalidIndex)
					{
						OutError = "function table overflow while linking";
						return false;
					}
					const auto NewIndex =
					    static_cast<std::uint16_t>(Result.Functions.size());
					Result.Functions.emplace_back();
					Result.PackageFunctionOwners.push_back(SourceOwner);
					FunctionByName.emplace(Function.Name, NewIndex);
					Maps.Functions[Index] = NewIndex;
				}
				return true;
			}

			bool AllocateGlobals(
			    const FBytecodeModule& Source,
			    FIndexMaps& Maps,
			    std::string& OutError)
			{
				Maps.Globals.resize(Source.GlobalNames.size(), InvalidIndex);
				Maps.SkipGlobalInitializers.resize(Source.GlobalNames.size(), false);
				for (std::size_t Index = 0; Index < Source.GlobalNames.size(); ++Index)
				{
					const std::string& Name = Source.GlobalNames[Index];
					const std::string SourceOwner =
					    Index < Source.PackageGlobalOwners.size()
					    ? Source.PackageGlobalOwners[Index]
					    : std::string{};
					const auto Existing = GlobalByName.find(Name);
					if (Existing != GlobalByName.end())
					{
						const std::string DestinationOwner =
						    Existing->second < Result.PackageGlobalOwners.size()
						    ? Result.PackageGlobalOwners[Existing->second]
						    : std::string{};
						if (!SourceOwner.empty() && SourceOwner == DestinationOwner)
						{
							const std::string SourceType =
							    Index < Source.GlobalTypeNames.size()
							    ? Source.GlobalTypeNames[Index]
							    : std::string{};
							const std::string DestinationType =
							    Existing->second < Result.GlobalTypeNames.size()
							    ? Result.GlobalTypeNames[Existing->second]
							    : std::string{};
							if (SourceType != DestinationType)
							{
								OutError = "conflicting duplicate global definition: " + Name;
								return false;
							}
							const FBytecodeGlobalMeta SourceMeta =
							    Index < Source.GlobalMetadata.size()
							    ? Source.GlobalMetadata[Index]
							    : FBytecodeGlobalMeta{};
							const FBytecodeGlobalMeta DestinationMeta =
							    Existing->second < Result.GlobalMetadata.size()
							    ? Result.GlobalMetadata[Existing->second]
							    : FBytecodeGlobalMeta{};
							const auto HasReflectionMeta = [](const FBytecodeGlobalMeta& Meta)
							{
								return !Meta.ModulePath.empty() || !Meta.DeclaredTypeName.empty() || Meta.bConst || Meta.bExported || Meta.bStaticMember || Meta.bLocalStatic || !Meta.DeclMeta.empty();
							};
							const bool bSourceHasMeta = HasReflectionMeta(SourceMeta);
							const bool bDestinationHasMeta = HasReflectionMeta(DestinationMeta);
							if (bSourceHasMeta && bDestinationHasMeta &&
							    (SourceMeta.bConstexpr != DestinationMeta.bConstexpr ||
							        (SourceMeta.ConstantValue && DestinationMeta.ConstantValue &&
							            EncodeTemplateArgument({ETemplateParameterKind::Value, {}, *SourceMeta.ConstantValue}) !=
							                EncodeTemplateArgument({ETemplateParameterKind::Value, {}, *DestinationMeta.ConstantValue}))))
							{
								OutError = "conflicting duplicate constexpr global: " + Name;
								return false;
							}
							// The same package can arrive through multiple dependency branches.
							// Interface stubs may carry less reflection data than the implementation;
							// package ownership plus the runtime type above are the stable identity.
							if (bSourceHasMeta && (!bDestinationHasMeta || (!DestinationMeta.ConstantValue && SourceMeta.ConstantValue)) && Existing->second < Result.GlobalMetadata.size())
							{
								Result.GlobalMetadata[Existing->second] = SourceMeta;
							}
							Maps.Globals[Index] = Existing->second;
							Maps.SkipGlobalInitializers[Index] = true;
							continue;
						}
						if (IsGenericInstanceLinkage(Name))
						{
							const std::string SourceType =
							    Index < Source.GlobalTypeNames.size()
							    ? Source.GlobalTypeNames[Index]
							    : std::string{};
							const std::string DestinationType =
							    Existing->second < Result.GlobalTypeNames.size()
							    ? Result.GlobalTypeNames[Existing->second]
							    : std::string{};
							if (SourceType != DestinationType)
							{
								OutError = "conflicting duplicate generic global: " + Name;
								return false;
							}
							const FBytecodeGlobalMeta SourceMeta =
							    Index < Source.GlobalMetadata.size()
							    ? Source.GlobalMetadata[Index]
							    : FBytecodeGlobalMeta{};
							const FBytecodeGlobalMeta DestinationMeta =
							    Existing->second < Result.GlobalMetadata.size()
							    ? Result.GlobalMetadata[Existing->second]
							    : FBytecodeGlobalMeta{};
							const auto HasReflectionMeta = [](const FBytecodeGlobalMeta& Meta)
							{
								return !Meta.ModulePath.empty() || !Meta.DeclaredTypeName.empty() || Meta.bConst || Meta.bExported || Meta.bStaticMember || Meta.bLocalStatic || !Meta.DeclMeta.empty();
							};
							const bool bSourceHasMeta = HasReflectionMeta(SourceMeta);
							const bool bDestinationHasMeta = HasReflectionMeta(DestinationMeta);
							if (bSourceHasMeta && bDestinationHasMeta && (SourceMeta.DeclaredTypeName != DestinationMeta.DeclaredTypeName || SourceMeta.bConst != DestinationMeta.bConst || SourceMeta.bConstexpr != DestinationMeta.bConstexpr || (SourceMeta.ConstantValue && DestinationMeta.ConstantValue && EncodeTemplateArgument({ETemplateParameterKind::Value, {}, *SourceMeta.ConstantValue}) != EncodeTemplateArgument({ETemplateParameterKind::Value, {}, *DestinationMeta.ConstantValue})) || SourceMeta.bExported != DestinationMeta.bExported || SourceMeta.bStaticMember != DestinationMeta.bStaticMember || SourceMeta.bLocalStatic != DestinationMeta.bLocalStatic || SourceMeta.DeclMeta != DestinationMeta.DeclMeta))
							{
								OutError = "generic global reflection metadata mismatch for '" + Name + "': " + FirstGlobalMetadataDifference(DestinationMeta, SourceMeta);
								return false;
							}
							if (bSourceHasMeta && (!bDestinationHasMeta || (!DestinationMeta.ConstantValue && SourceMeta.ConstantValue)) && Existing->second < Result.GlobalMetadata.size())
							{
								Result.GlobalMetadata[Existing->second] = SourceMeta;
							}
							Maps.Globals[Index] = Existing->second;
							Maps.SkipGlobalInitializers[Index] = true;
							continue;
						}
						OutError = "duplicate global definition: " + Name;
						return false;
					}
					if (!SourceOwner.empty() && CurrentNewPackageOwners.find(SourceOwner) == CurrentNewPackageOwners.end())
					{
						OutError = "package component contains unexpected global: " + Name;
						return false;
					}
					if (Result.GlobalNames.size() >= InvalidIndex)
					{
						OutError = "global table overflow while linking";
						return false;
					}
					const auto NewIndex =
					    static_cast<std::uint16_t>(Result.GlobalNames.size());
					Result.GlobalNames.push_back(Name);
					Result.GlobalTypeNames.push_back(
					    Index < Source.GlobalTypeNames.size()
					        ? Source.GlobalTypeNames[Index]
					        : std::string{});
					Result.GlobalMetadata.push_back(
					    Index < Source.GlobalMetadata.size()
					        ? Source.GlobalMetadata[Index]
					        : FBytecodeGlobalMeta{});
					Result.PackageGlobalOwners.push_back(SourceOwner);
					GlobalByName.emplace(Name, NewIndex);
					Maps.Globals[Index] = NewIndex;
				}
				return true;
			}

			bool AllocateConstants(
			    const FBytecodeModule& Source,
			    FIndexMaps& Maps,
			    std::string& OutError)
			{
				for (FBytecodeConstant Constant : Source.Constants)
				{
					if (Constant.Kind == EConstantKind::Str || Constant.Kind == EConstantKind::Enum)
					{
						if (Constant.StringIndex >= Maps.Strings.size())
						{
							OutError = "invalid constant string index while linking";
							return false;
						}
						Constant.StringIndex = Maps.Strings[Constant.StringIndex];
					}
					else if (Constant.Kind == EConstantKind::WStr)
					{
						if (Constant.StringIndex >= Maps.WStrings.size())
						{
							OutError = "invalid constant wide string index while linking";
							return false;
						}
						Constant.StringIndex = Maps.WStrings[Constant.StringIndex];
					}
					const auto Existing = ConstantIndices.find(Constant);
					if (Existing != ConstantIndices.end())
					{
						Maps.Constants.push_back(Existing->second);
						continue;
					}
					if (Result.Constants.size() >= InvalidIndex)
					{
						OutError = "constant table overflow while linking";
						return false;
					}
					const auto NewIndex =
					    static_cast<std::uint16_t>(Result.Constants.size());
					Maps.Constants.push_back(NewIndex);
					Result.Constants.push_back(Constant);
					ConstantIndices.emplace(Result.Constants.back(), NewIndex);
				}
				return true;
			}

			bool RemapFields(
			    std::vector<FBytecodeFieldDesc>& Fields,
			    const FIndexMaps& Maps,
			    std::string& OutError)
			{
				for (FBytecodeFieldDesc& Field : Fields)
				{
					if (Field.bHasDefault && (Field.DefaultValue.Kind == EConstantKind::Str || Field.DefaultValue.Kind == EConstantKind::Enum))
					{
						if (Field.DefaultValue.StringIndex >= Maps.Strings.size())
						{
							OutError = "invalid field default string index while linking";
							return false;
						}
						Field.DefaultValue.StringIndex =
						    Maps.Strings[Field.DefaultValue.StringIndex];
					}
					else if (Field.bHasDefault && Field.DefaultValue.Kind == EConstantKind::WStr)
					{
						if (Field.DefaultValue.StringIndex >= Maps.WStrings.size())
						{
							OutError = "invalid field default wide string index while linking";
							return false;
						}
						Field.DefaultValue.StringIndex =
						    Maps.WStrings[Field.DefaultValue.StringIndex];
					}
					if (!RemapCode(Field.InitCode, Maps, OutError))
					{
						return false;
					}
				}
				return true;
			}

			bool CopyInterfaces(
			    const FBytecodeModule& Source,
			    const FIndexMaps& Maps,
			    std::string& OutError)
			{
				for (std::size_t Index = 0; Index < Source.Interfaces.size(); ++Index)
				{
					FBytecodeInterfaceDesc& Destination =
					    Result.Interfaces[Maps.Interfaces[Index]];
					FBytecodeInterfaceDesc Candidate = Source.Interfaces[Index];
					for (std::uint16_t& Base : Candidate.BaseInterfaceIndices)
					{
						if (Base >= Maps.Interfaces.size())
						{
							OutError = "invalid base interface index while linking";
							return false;
						}
						Base = Maps.Interfaces[Base];
					}
					if (!RemapFields(Candidate.Fields, Maps, OutError) || !RemapFields(Candidate.StaticFields, Maps, OutError))
					{
						return false;
					}
					if (!Destination.Name.empty())
					{
						if (!EquivalentInterface(Destination, Candidate))
						{
							OutError = "conflicting duplicate interface definition: " + Candidate.Name;
							return false;
						}
						continue;
					}
					Destination = std::move(Candidate);
				}
				return true;
			}

			bool CopyTypes(
			    const FBytecodeModule& Source,
			    const FIndexMaps& Maps,
			    std::string& OutError)
			{
				for (std::size_t Index = 0; Index < Source.Types.size(); ++Index)
				{
					FBytecodeTypeDesc& Destination = Result.Types[Maps.Types[Index]];
					FBytecodeTypeDesc Candidate = Source.Types[Index];
					for (std::uint16_t& Base : Candidate.BaseTypeIndices)
					{
						if (Base >= Maps.Types.size())
						{
							OutError = "invalid base type index while linking";
							return false;
						}
						Base = Maps.Types[Base];
					}
					for (std::uint16_t& Interface : Candidate.ImplementedInterfaceIndices)
					{
						if (Interface >= Maps.Interfaces.size())
						{
							OutError = "invalid implemented interface index while linking";
							return false;
						}
						Interface = Maps.Interfaces[Interface];
					}
					if (Candidate.DestructorFunctionIndex != InvalidIndex)
					{
						if (Candidate.DestructorFunctionIndex >= Maps.Functions.size())
						{
							OutError = "invalid destructor function index while linking";
							return false;
						}
						Candidate.DestructorFunctionIndex =
						    Maps.Functions[Candidate.DestructorFunctionIndex];
					}
					if (Candidate.CopyConstructorIndex != InvalidIndex)
					{
						if (Candidate.CopyConstructorIndex >= Maps.Functions.size())
						{
							OutError = "invalid copy constructor index while linking";
							return false;
						}
						Candidate.CopyConstructorIndex = Maps.Functions[Candidate.CopyConstructorIndex];
					}
					if (!RemapFields(Candidate.Fields, Maps, OutError) || !RemapFields(Candidate.StaticFields, Maps, OutError))
					{
						return false;
					}
					if (!Destination.Name.empty())
					{
						if (!EquivalentType(Destination, Candidate))
						{
							OutError = "conflicting duplicate type definition: " + Candidate.Name + " (different " + FirstTypeDifference(Destination, Candidate) + ")";
							return false;
						}
						continue;
					}
					Destination = std::move(Candidate);
				}
				return true;
			}

			bool CopyEnums(
			    const FBytecodeModule& Source,
			    std::string& OutError)
			{
				for (std::size_t Index = 0; Index < Source.Enums.size(); ++Index)
				{
					const FBytecodeEnumDesc& Enum = Source.Enums[Index];
					const std::string SourceOwner =
					    Index < Source.PackageEnumOwners.size()
					    ? Source.PackageEnumOwners[Index]
					    : std::string{};
					const auto Existing = std::find_if(
					    Result.Enums.begin(),
					    Result.Enums.end(),
					    [&](const FBytecodeEnumDesc& Candidate)
					{
						return Candidate.Name == Enum.Name;
					});
					if (Existing != Result.Enums.end())
					{
						// Implicit core standard enums are present in every compiled unit.
						// Merge only identical definitions; user enums retain normal ownership checks.
						const auto* StandardType = StandardLibrary::GetBindingCatalog().FindType(Enum.Name);
						if (StandardType && StandardType->Kind == EHostTypeKind::Enum && EquivalentEnum(*Existing, Enum))
						{
							continue;
						}
						// Host enum metadata has no source declaration scope. Independent
						// units may repeat it; source-owned enums keep their ownership checks.
						if (Existing->Scope.Id.empty() && Existing->Scope.ModulePath.empty() && Enum.Scope.Id.empty() && Enum.Scope.ModulePath.empty() && EquivalentEnum(*Existing, Enum))
						{
							continue;
						}
						const std::size_t ExistingIndex =
						    static_cast<std::size_t>(Existing - Result.Enums.begin());
						const std::string DestinationOwner =
						    ExistingIndex < Result.PackageEnumOwners.size()
						    ? Result.PackageEnumOwners[ExistingIndex]
						    : std::string{};
						if (!SourceOwner.empty() && SourceOwner == DestinationOwner)
						{
							if (!EquivalentEnum(*Existing, Enum))
							{
								OutError = "conflicting duplicate enum definition: " + Enum.Name;
								return false;
							}
							continue;
						}
						OutError = "duplicate enum definition: " + Enum.Name;
						return false;
					}
					if (!SourceOwner.empty() && CurrentNewPackageOwners.find(SourceOwner) == CurrentNewPackageOwners.end())
					{
						OutError = "package component contains unexpected enum: " + Enum.Name;
						return false;
					}
					Result.Enums.push_back(Enum);
					Result.PackageEnumOwners.push_back(SourceOwner);
				}
				return true;
			}

			bool CopyFunctions(
			    const FBytecodeModule& Source,
			    const FIndexMaps& Maps,
			    std::string& OutError)
			{
				for (std::size_t Index = 0; Index < Source.Functions.size(); ++Index)
				{
					FBytecodeFunction& Destination =
					    Result.Functions[Maps.Functions[Index]];
					FBytecodeFunction Candidate = Source.Functions[Index];
					for (auto& Initializer : Candidate.LocalStaticInitializers)
					{
						if (Initializer.GlobalSlot >= Maps.Globals.size())
						{
							OutError = "local static slot is not part of its module";
							return false;
						}
						Initializer.GlobalSlot = Maps.Globals[Initializer.GlobalSlot];
					}
					if (Candidate.bIsMethod && Candidate.OwnerTypeIndex != InvalidIndex)
					{
						if (Candidate.OwnerTypeIndex >= Maps.Types.size())
						{
							OutError = "invalid function owner type index while linking";
							return false;
						}
						Candidate.OwnerTypeIndex = Maps.Types[Candidate.OwnerTypeIndex];
					}
					for (FDebugLine& Line : Candidate.DebugLines)
					{
						if (Line.FileIndex >= Maps.DebugFiles.size())
						{
							OutError = "invalid function debug file index while linking";
							return false;
						}
						Line.FileIndex = Maps.DebugFiles[Line.FileIndex];
					}
					if (!RemapCode(Candidate.Code, Maps, OutError))
					{
						return false;
					}
					if (!Destination.Name.empty())
					{
						if (CompatibleStub(Candidate, Destination))
						{
							continue;
						}
						if (!EquivalentFunction(Destination, Candidate))
						{
							OutError = "conflicting duplicate function definition: " + Candidate.Name;
							return false;
						}
						continue;
					}
					Destination = std::move(Candidate);
				}
				return true;
			}

			bool CopyInit(
			    const FBytecodeModule& Source,
			    const FIndexMaps& Maps,
			    std::string& OutError)
			{
				std::vector<std::uint8_t> Code = Source.InitCode;
				if (!RemapCode(Code, Maps, OutError))
				{
					return false;
				}
				std::unordered_map<std::string, std::vector<std::uint8_t>>
				    SourceInitByOwner;
				std::unordered_map<std::string, std::vector<std::uint32_t>> SourceInitMarkersByOwner;
				for (const FBytecodePackageInitSegment& Segment :
				    Source.PackageInitSegments)
				{
					std::vector<std::uint8_t>& OwnedCode =
					    SourceInitByOwner[Segment.OwnerId];
					const auto Base = OwnedCode.size();
					for (auto Marker = std::lower_bound(Source.InitPauseMarkerOffsets.begin(), Source.InitPauseMarkerOffsets.end(), Segment.BeginOffset);
					    Marker != Source.InitPauseMarkerOffsets.end() && *Marker < Segment.EndOffset; ++Marker)
					{
						SourceInitMarkersByOwner[Segment.OwnerId].push_back(static_cast<std::uint32_t>(Base + *Marker - Segment.BeginOffset));
					}
					OwnedCode.insert(
					    OwnedCode.end(),
					    Code.begin() + static_cast<std::ptrdiff_t>(Segment.BeginOffset),
					    Code.begin() + static_cast<std::ptrdiff_t>(Segment.EndOffset));
				}
				for (auto& Entry : SourceInitByOwner)
				{
					const auto Existing = PackageInitCodeByOwner.find(Entry.first);
					if (CurrentNewPackageOwners.find(Entry.first) != CurrentNewPackageOwners.end())
					{
						if (Existing != PackageInitCodeByOwner.end())
						{
							OutError = "duplicate package initializer ownership: " + Entry.first;
							return false;
						}
						PackageInitCodeByOwner.emplace(
						    Entry.first, std::move(Entry.second));
						PackageInitMarkersByOwner.emplace(Entry.first, SourceInitMarkersByOwner[Entry.first]);
					}
					else if (Existing == PackageInitCodeByOwner.end() || Existing->second != Entry.second || PackageInitMarkersByOwner[Entry.first] != SourceInitMarkersByOwner[Entry.first])
					{
						OutError = "conflicting package initializer component: " + Entry.first;
						return false;
					}
				}
				for (const FBytecodeGlobalInitRange& Range : Source.GlobalInitRanges)
				{
					if (Range.GlobalSlot < Maps.SkipGlobalInitializers.size() && Maps.SkipGlobalInitializers[Range.GlobalSlot])
					{
						std::fill(
						    Code.begin() + static_cast<std::ptrdiff_t>(Range.BeginOffset),
						    Code.begin() + static_cast<std::ptrdiff_t>(Range.EndOffset),
						    static_cast<std::uint8_t>(EOpcode::Nop));
					}
				}
				struct FCopyInterval
				{
					std::size_t SourceBegin = 0;
					std::size_t SourceEnd = 0;
					std::size_t DestinationBegin = 0;
				};
				std::vector<FCopyInterval> Intervals;
				const auto CopyInterval =
				    [&](const std::size_t Begin, const std::size_t End)
				{
					if (Begin >= End)
					{
						return true;
					}
					const std::size_t DestinationBegin = Result.InitCode.size();
					Result.InitCode.insert(
					    Result.InitCode.end(),
					    Code.begin() + static_cast<std::ptrdiff_t>(Begin),
					    Code.begin() + static_cast<std::ptrdiff_t>(End));
					Intervals.push_back(
					    FCopyInterval{Begin, End, DestinationBegin});
					for (auto Marker = std::lower_bound(Source.InitPauseMarkerOffsets.begin(), Source.InitPauseMarkerOffsets.end(), Begin);
					    Marker != Source.InitPauseMarkerOffsets.end() && *Marker < End; ++Marker)
					{
						// Retained-global holes were overwritten with ordinary Nops.
						if (*Marker + 1 < Code.size() && static_cast<EOpcode>(Code[*Marker]) == EOpcode::Nop &&
						    static_cast<EOpcode>(Code[*Marker + 1]) != EOpcode::Nop)
						{
							Result.InitPauseMarkerOffsets.push_back(static_cast<std::uint32_t>(DestinationBegin + *Marker - Begin));
						}
					}

					if (!Source.InitDebugLines.empty())
					{
						Result.InitDebugLines.resize(Result.InitCode.size());
						const std::size_t DebugEnd =
						    std::min(End, Source.InitDebugLines.size());
						for (std::size_t Offset = Begin;
						    Offset < DebugEnd;
						    ++Offset)
						{
							FDebugLine Line = Source.InitDebugLines[Offset];
							if (Line.Line != 0)
							{
								if (Line.FileIndex >= Maps.DebugFiles.size())
								{
									OutError =
									    "invalid init debug file index while linking";
									return false;
								}
								Line.FileIndex = Maps.DebugFiles[Line.FileIndex];
							}
							Result.InitDebugLines[DestinationBegin + Offset - Begin] = Line;
						}
					}
					return true;
				};

				std::size_t Cursor = 0;
				for (const FBytecodePackageInitSegment& Segment :
				    Source.PackageInitSegments)
				{
					if (!CopyInterval(Cursor, Segment.BeginOffset))
					{
						return false;
					}
					if (CurrentNewPackageOwners.find(Segment.OwnerId) != CurrentNewPackageOwners.end())
					{
						const std::size_t DestinationBegin = Result.InitCode.size();
						if (!CopyInterval(Segment.BeginOffset, Segment.EndOffset))
						{
							return false;
						}
						Result.PackageInitSegments.push_back(
						    FBytecodePackageInitSegment{
						        Segment.OwnerId,
						        DestinationBegin,
						        Result.InitCode.size()});
					}
					Cursor = Segment.EndOffset;
				}
				if (!CopyInterval(Cursor, Code.size()))
				{
					return false;
				}
				for (const FBytecodeGlobalInitRange& Range : Source.GlobalInitRanges)
				{
					if (Range.GlobalSlot >= Maps.Globals.size())
					{
						OutError = "invalid global initializer slot while linking";
						return false;
					}
					if (Range.GlobalSlot < Maps.SkipGlobalInitializers.size() && Maps.SkipGlobalInitializers[Range.GlobalSlot])
					{
						continue;
					}
					const auto Interval = std::find_if(
					    Intervals.begin(),
					    Intervals.end(),
					    [&](const FCopyInterval& Candidate)
					{
						return Range.BeginOffset >= Candidate.SourceBegin && Range.EndOffset <= Candidate.SourceEnd;
					});
					if (Interval != Intervals.end())
					{
						Result.GlobalInitRanges.push_back(FBytecodeGlobalInitRange{
						    Maps.Globals[Range.GlobalSlot],
						    Interval->DestinationBegin + Range.BeginOffset - Interval->SourceBegin,
						    Interval->DestinationBegin + Range.EndOffset - Interval->SourceBegin, Range.bDemandInitialized});
					}
				}
				Result.InitLocalCount =
				    std::max(Result.InitLocalCount, Source.InitLocalCount);
				return true;
			}

			FBytecodeModule Result;
			std::unordered_map<std::string, std::uint16_t> StringPoolIndices;
			std::unordered_map<std::u16string, std::uint16_t> WStringPoolIndices;
			std::unordered_map<std::string, std::uint16_t> DebugFileIndices;
			std::unordered_map<
			    FBytecodeConstant,
			    std::uint16_t,
			    FConstantHash,
			    FConstantEqual>
			    ConstantIndices;
			std::unordered_map<std::string, std::uint16_t> TypeByName;
			std::unordered_map<std::string, std::uint16_t> InterfaceByName;
			std::unordered_map<std::string, std::uint16_t> FunctionByName;
			std::unordered_map<std::string, std::uint16_t> GlobalByName;
			std::unordered_map<std::string, std::uint64_t>
			    PackageComponentFingerprints;
			std::unordered_map<std::string, std::vector<std::uint8_t>>
			    PackageInitCodeByOwner;
			std::unordered_map<std::string, std::vector<std::uint32_t>> PackageInitMarkersByOwner;
			std::unordered_set<std::string> CurrentNewPackageOwners;
		};
	}

	bool LinkBytecodeModules(
	    const FBytecodeModule& RootModule,
	    const std::vector<const FBytecodeModule*>& Dependencies,
	    std::unique_ptr<FBytecodeModule>& OutModule,
	    std::string& OutError)
	try
	{
		OutError.clear();
		std::string ValidationError;
		// Empty functions are unresolved link stubs. Validate a temporary shape
		// with a Return opcode, then require every stub to be satisfied during merge;
		// the final linked module is validated again with no relaxation.
		FBytecodeModule ValidationApplication = RootModule;
		for (FBytecodeFunction& Function : ValidationApplication.Functions)
		{
			if (Function.Code.empty() && !Function.bIsAbstract && !Function.bDeleted)
			{
				Function.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
			}
		}
		if (!VeroScriptBytecodeIO::ValidateModule(
		        ValidationApplication, ValidationError))
		{
			OutError = "invalid root module: " + ValidationError;
			return false;
		}

		FLinker Linker;
		for (const FBytecodeModule* const Dependency : Dependencies)
		{
			if (Dependency == nullptr)
			{
				OutError = "null dependency module";
				return false;
			}
			if (Dependency->MainFunctionIndex >= 0 || !VeroScriptBytecodeIO::ValidateModule(*Dependency, ValidationError))
			{
				OutError = "invalid dependency module: " + (ValidationError.empty() ? std::string("dependency contains main") : ValidationError);
				return false;
			}
			if (!Linker.Merge(*Dependency, false, OutError))
			{
				return false;
			}
		}
		if (!Linker.Merge(RootModule, true, OutError))
		{
			return false;
		}
		std::unique_ptr<FBytecodeModule> Linked = Linker.Finish(OutError);
		if (!Linked)
		{
			return false;
		}
		Linked->PublicMacros = RootModule.PublicMacros;
		OutModule = std::move(Linked);
		return true;
	}
	catch (const std::bad_alloc&)
	{
		OutError.clear();
		OutError = "out of memory";
		return false;
	}
	catch (const std::length_error&)
	{
		OutError.clear();
		OutError = "size overflow";
		return false;
	}
}
