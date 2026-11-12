// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHostBinding.h"
#include <functional>

namespace VeroScript
{
	// Keeps stable native/type IDs. Only name lookup is scoped; authoritative
	// metadata remains available to ordinary signature/ABI validation.
	class FScopedBindingCatalog final : public IHostBindingCatalog
	{
	public:
		//~ Begin IHostBindingCatalog Interface
		const std::vector<FNativeFunction>& GetNativeFunctions() const override
		{
			return Source->GetNativeFunctions();
		}
		const std::vector<FHostFunctionDesc>& GetFunctions() const override
		{
			return Source->GetFunctions();
		}
		const std::vector<FHostTypeDesc>& GetTypes() const override
		{
			return Source->GetTypes();
		}
		const std::vector<FHostTypeAliasDesc>& GetTypeAliases() const override
		{
			return Source->GetTypeAliases();
		}
		const std::vector<FGenericHostTypeTemplateDesc>& GetGenericTypeTemplates() const override
		{
			return Source->GetGenericTypeTemplates();
		}
		const std::vector<FHostAnnotationDesc>& GetAnnotations() const override
		{
			return Source->GetAnnotations();
		}
		const FHostFunctionDesc* FindFunction(std::string_view N) const override
		{
			const auto* D = Source->FindFunction(N);
			return D && Check(D->Origin, {}) ? D : nullptr;
		}
		const FHostTypeDesc* FindType(std::string_view N) const override
		{
			const auto* D = Source->FindType(N);
			return D && Check(D->Origin, D->Name) ? D : nullptr;
		}
		const FHostTypeDesc* FindType(FHostTypeId N) const override
		{
			const auto* D = Source->FindType(N);
			return D && Check(D->Origin, D->Name) ? D : nullptr;
		}
		const FGenericHostTypeTemplateDesc* FindGenericTypeTemplate(std::string_view N) const override
		{
			const auto* D = Source->FindGenericTypeTemplate(N);
			return D && Check(D->Origin, D->Name) ? D : nullptr;
		}
		const FHostTypeAliasDesc* FindTypeAlias(std::string_view N) const override
		{
			const auto* D = Source->FindTypeAlias(N);
			return D && Check(D->Origin, D->Name) ? D : nullptr;
		}
		//~ End IHostBindingCatalog Interface

		FScopedBindingCatalog(std::shared_ptr<const IHostBindingCatalog> S,
		    std::function<bool(const FBindingOrigin&, const std::string&)> C) : Source(std::move(S)), Check(std::move(C))
		{
		}

	private:
		std::shared_ptr<const IHostBindingCatalog> Source;
		std::function<bool(const FBindingOrigin&, const std::string&)> Check;
	};
}
