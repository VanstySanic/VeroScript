// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStandardLibrary.h"
#include "VeroScriptMemory.h"
#include "VeroScriptI18n.h"
#include "VeroScriptStandardLibraryServices.h"
#include "VeroScriptNumericBindings.h"
#include "VeroScriptHostBinding.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace VeroScript::StandardLibrary
{
	namespace
	{
		class FCatalog final : public IHostBindingCatalog
		{
		public:
			//~ Begin IHostBindingCatalog Interface
			const std::vector<FNativeFunction>& GetNativeFunctions() const override
			{
				return Natives;
			}

			const std::vector<FHostTypeDesc>& GetTypes() const override
			{
				return Types;
			}

			const std::vector<FHostFunctionDesc>& GetFunctions() const override
			{
				return Functions;
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
				const FHostFunctionDesc* Result = nullptr;
				for (const auto& F : Functions)
				{
					if (F.Name == N)
					{
						if (Result) return nullptr;
						Result = &F;
					}
				}
				return Result;
			}

			const FHostTypeDesc* FindType(std::string_view N) const override
			{
				for (const auto& T : Types)
				{
					if (T.Name == N) return &T;
				}
				return nullptr;
			}

			const FHostTypeDesc* FindType(FHostTypeId I) const override
			{
				for (const auto& T : Types)
				{
					if (T.Id == I) return &T;
				}
				return nullptr;
			}
			//~ End IHostBindingCatalog Interface

			const IHostBindingCatalog* Source = nullptr;

			std::vector<FNativeFunction> Natives;

			std::vector<FHostTypeDesc> Types;

			std::vector<FHostFunctionDesc> Functions;
		};
		bool SameDesc(const FNativeTypeDesc& A, const FNativeTypeDesc& B)
		{
			if (A.Type != B.Type || A.HostTypeName != B.HostTypeName ||
			    A.ElementTypeName != B.ElementTypeName || A.KeyTypeName != B.KeyTypeName ||
			    A.ValueTypeName != B.ValueTypeName || A.TypeArguments.size() != B.TypeArguments.size())
			{
				return false;
			}

			for (std::size_t I = 0; I < A.TypeArguments.size(); ++I)
			{
				if (!SameDesc(A.TypeArguments[I], B.TypeArguments[I])) return false;
			}

			return true;
		}
		bool SameNative(const FNativeFunction& A, const FNativeFunction& B, bool CompareOwners = true)
		{
			if (!SameDesc(A.StructuredReturnType, B.StructuredReturnType) ||
			    A.StructuredParamTypes.size() != B.StructuredParamTypes.size())
			{
				return false;
			}
			for (std::size_t I = 0; I < A.StructuredParamTypes.size(); ++I)
			{
				if (!SameDesc(A.StructuredParamTypes[I], B.StructuredParamTypes[I])) return false;
			}

			return A.ProviderIdentity == B.ProviderIdentity && A.Origin == B.Origin && A.Name == B.Name &&
			    A.Callback == B.Callback && A.UserData == B.UserData && A.ReturnType == B.ReturnType &&
			    A.ParamTypes == B.ParamTypes && A.HostTypeName == B.HostTypeName &&
			    A.ReturnHostTypeName == B.ReturnHostTypeName &&
			    A.ParamHostTypeNames == B.ParamHostTypeNames &&
			    A.ReturnElementTypeName == B.ReturnElementTypeName &&
			    A.ParamElementTypeNames == B.ParamElementTypeNames &&
			    A.ReturnKeyTypeName == B.ReturnKeyTypeName &&
			    A.ReturnValueTypeName == B.ReturnValueTypeName &&
			    A.ParamKeyTypeNames == B.ParamKeyTypeNames &&
			    A.ParamValueTypeNames == B.ParamValueTypeNames && A.ParamFlags == B.ParamFlags &&
			    A.RuntimeReturnTypeName == B.RuntimeReturnTypeName &&
			    A.RuntimeParamTypeNames == B.RuntimeParamTypeNames && A.TypeParams == B.TypeParams &&
			    A.ScriptReturnTypeName == B.ScriptReturnTypeName &&
			    A.bHostStaticMethod == B.bHostStaticMethod &&
			    A.bGenericHostObjectCast == B.bGenericHostObjectCast &&
			    A.bHasStructuredTypeDescs == B.bHasStructuredTypeDescs &&
			    A.ReturnGenericTemplateId == B.ReturnGenericTemplateId &&
			    A.ReturnGenericTypeArguments == B.ReturnGenericTypeArguments &&
			    (!CompareOwners ||
			        (A.CallbackOwner == B.CallbackOwner && !A.CallbackOwner.owner_before(B.CallbackOwner) &&
			            !B.CallbackOwner.owner_before(A.CallbackOwner)));
		}
		bool SameType(const FHostTypeDesc& A, const FHostTypeDesc& B)
		{
			if (A.ProviderIdentity != B.ProviderIdentity || A.Origin != B.Origin ||
			    A.ModuleOwner != B.ModuleOwner || A.Id != B.Id || A.Name != B.Name || A.Kind != B.Kind ||
			    A.CloneCallback != B.CloneCallback || A.EqualsCallback != B.EqualsCallback ||
			    A.HashCallback != B.HashCallback || A.CloneUserData != B.CloneUserData ||
			    A.EqualsUserData != B.EqualsUserData || A.HashUserData != B.HashUserData ||
			    A.Members.size() != B.Members.size() || A.GenericTemplateId != B.GenericTemplateId ||
			    A.GenericTypeArguments != B.GenericTypeArguments || A.bInheritable != B.bInheritable ||
			    A.bScopedEnum != B.bScopedEnum || A.EnumUnderlyingTypeName != B.EnumUnderlyingTypeName ||
			    A.SubclassFactory != B.SubclassFactory ||
			    A.SubclassFactoryUserData != B.SubclassFactoryUserData ||
			    A.CallbackLifetimes != B.CallbackLifetimes)
			{
				return false;
			}

			for (std::size_t I = 0; I < A.Members.size(); ++I)
			{
				const auto& X = A.Members[I];
				const auto& Y = B.Members[I];
				if (X.Kind != Y.Kind || X.Name != Y.Name || X.Callback != Y.Callback ||
				    X.UserData != Y.UserData || X.ParamTypes != Y.ParamTypes ||
				    X.ParamHostTypeNames != Y.ParamHostTypeNames || X.ParamFlags != Y.ParamFlags ||
				    X.ReturnType != Y.ReturnType || X.ReturnHostTypeName != Y.ReturnHostTypeName ||
				    X.bConstMethod != Y.bConstMethod || X.bMutableThis != Y.bMutableThis ||
				    X.bOperator != Y.bOperator || X.NativeName != Y.NativeName ||
				    X.ParamElementTypeNames != Y.ParamElementTypeNames ||
				    X.ReturnElementTypeName != Y.ReturnElementTypeName || X.EnumValue != Y.EnumValue ||
				    X.ReturnKeyTypeName != Y.ReturnKeyTypeName ||
				    X.ReturnValueTypeName != Y.ReturnValueTypeName ||
				    X.ParamKeyTypeNames != Y.ParamKeyTypeNames ||
				    X.ParamValueTypeNames != Y.ParamValueTypeNames)
				{
					return false;
				}
			}
			return true;
		}
	}
	bool NativeFunctionsEquivalent(const FNativeFunction& A, const FNativeFunction& B)
	{
		return SameNative(A, B, false);
	}
	const IHostBindingCatalog& GetBindingCatalog()
	{
		static const FCatalog Catalog = []()
		{
			FCatalog R;
			for (const auto* C : {&Memory::GetBindingCatalog(), &I18n::GetBindingCatalog(), &GetServiceBindingCatalog(), &Numerics::GetBindingCatalog()})
			{
				const auto& N = C->GetNativeFunctions();
				R.Natives.insert(R.Natives.end(), N.begin(), N.end());
				const auto& T = C->GetTypes();
				R.Types.insert(R.Types.end(), T.begin(), T.end());
				const auto& F = C->GetFunctions();
				R.Functions.insert(R.Functions.end(), F.begin(), F.end());
			}
			return R;
		}();
		return Catalog;
	}
	const std::vector<FNativeFunction>* MergeNativeFunctions(const std::vector<FNativeFunction>* Natives, std::vector<FNativeFunction>& Storage)
	{
		const auto& Core = GetBindingCatalog().GetNativeFunctions();
		if (!Natives || Natives->empty()) return &Core;
		if (Natives->size() > 65536)
		{
			throw std::invalid_argument("diag.stdlib_native_limit");
		}

		static const auto Names = [&]()
		{
			std::unordered_set<std::string_view> R;
			for (const auto& N : Core)
			{
				R.insert(N.Name);
			}
			return R;
		}();
		std::size_t Prefix = 0;
		while (Prefix < Natives->size() && !Names.count((*Natives)[Prefix].Name))
		{
			++Prefix;
		}

		const auto Existing = Natives->size() - Prefix;
		// Accept an exact complete catalog, including a directly supplied feature
		// catalog, then normalize it to the same core order. Never accept fragments
		// with changed names, callbacks, ownership or type metadata.
		const auto Matches = [&](const std::vector<FNativeFunction>& Candidate)
		{
			if (Existing != Candidate.size()) return false;
			for (std::size_t I = 0; I < Existing; ++I)
			{
				if (!SameNative((*Natives)[Prefix + I], Candidate[I])) return false;
			}

			return true;
		};
		if (Existing && !Matches(Core) && !Matches(Memory::GetBindingCatalog().GetNativeFunctions()) &&
		    !Matches(I18n::GetBindingCatalog().GetNativeFunctions()))
		{
			throw std::invalid_argument("diag.stdlib_reserved_binding");
		}

		if (Existing == Core.size()) return Natives;
		if (Prefix > 65536 - Core.size())
		{
			throw std::invalid_argument("diag.stdlib_native_limit");
		}

		Storage.assign(Natives->begin(), Natives->begin() + Prefix);
		Storage.insert(Storage.end(), Core.begin(), Core.end());
		return &Storage;
	}
	std::shared_ptr<const IHostBindingCatalog> MergeBindingCatalog(const IHostBindingCatalog* Source, const std::vector<FNativeFunction>* Natives)
	{
		if (!Source && (!Natives || Natives->empty()))
		{
			static const std::shared_ptr<const IHostBindingCatalog> C(&GetBindingCatalog(),
			    [](const IHostBindingCatalog*)
			{
			});
			return C;
		}
		auto C = std::make_shared<FCatalog>();
		C->Source = Source;
		if (Source)
		{
			C->Types = Source->GetTypes();
			C->Functions = Source->GetFunctions();
		}
		for (const auto& T : GetBindingCatalog().GetTypes())
		{
			if (Source)
			{
				for (const auto& A : Source->GetTypeAliases())
				{
					if (A.Name == T.Name)
					{
						throw std::invalid_argument("diag.stdlib_reserved_binding");
					}
				}

				for (const auto& A : Source->GetGenericTypeTemplates())
				{
					if (A.Name == T.Name)
					{
						throw std::invalid_argument("diag.stdlib_reserved_binding");
					}
				}

				if (const auto* Old = Source->FindType(T.Name))
				{
					if (!SameType(*Old, T))
					{
						throw std::invalid_argument("diag.stdlib_reserved_binding");
					}
					continue;
				}
				if (Source->FindType(T.Id))
				{
					throw std::invalid_argument("diag.stdlib_reserved_binding");
				}
			}
			C->Types.push_back(T);
		}
		for (const auto& F : GetBindingCatalog().GetFunctions())
		{
			if (!C->FindFunction(F.Name))
			{
				C->Functions.push_back(F);
			}
		}

		std::vector<FNativeFunction> Work;
		const auto* All = MergeNativeFunctions(Natives ? Natives : Source ? &Source->GetNativeFunctions()
		                                                                  : nullptr,
		    Work);
		C->Natives = All == &Work ? std::move(Work) : *All;
		return C;
	}
}
