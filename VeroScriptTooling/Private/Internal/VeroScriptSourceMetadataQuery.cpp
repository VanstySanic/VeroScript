// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSourceMetadataQuery.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptStandardLibrary.h"
#include <algorithm>
#include <unordered_set>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		class FMetadataQueryCatalog final : public IHostBindingCatalog
		{
		public:

			//~ Begin IHostBindingCatalog Interface
			const std::vector<FNativeFunction>& GetNativeFunctions() const override
			{
				return Base->GetNativeFunctions();
			}
			const std::vector<FHostFunctionDesc>& GetFunctions() const override
			{
				return Base->GetFunctions();
			}
			const std::vector<FHostTypeDesc>& GetTypes() const override
			{
				return Base->GetTypes();
			}
			const std::vector<FHostTypeAliasDesc>& GetTypeAliases() const override
			{
				return Base->GetTypeAliases();
			}
			const std::vector<FGenericHostTypeTemplateDesc>& GetGenericTypeTemplates() const override
			{
				return Base->GetGenericTypeTemplates();
			}
			const std::vector<FHostAnnotationDesc>& GetAnnotations() const override
			{
				return Annotations;
			}
			const FHostFunctionDesc* FindFunction(std::string_view Name) const override
			{
				return Base->FindFunction(Name);
			}
			const FHostTypeDesc* FindType(std::string_view Name) const override
			{
				return Base->FindType(Name);
			}
			const FHostTypeDesc* FindType(FHostTypeId Id) const override
			{
				return Base->FindType(Id);
			}
			const FGenericHostTypeTemplateDesc* FindGenericTypeTemplate(std::string_view Name) const override
			{
				return Base->FindGenericTypeTemplate(Name);
			}
			const FHostTypeAliasDesc* FindTypeAlias(std::string_view Name) const override
			{
				return Base->FindTypeAlias(Name);
			}
			//~ End IHostBindingCatalog Interface

			FMetadataQueryCatalog(const IHostBindingCatalog* InBindings, const std::vector<FAnnotationDefinition>& Definitions)
				: Base(InBindings ? InBindings : &StandardLibrary::GetBindingCatalog()), Annotations(Base->GetAnnotations())
			{
				std::unordered_set<std::string> Names;
				for (const auto& A : Annotations)
				{
					Names.insert(A.Name);
				}

				const auto CopyArguments = [&](const auto& Self, const auto& Arguments,
											   unsigned Depth) -> std::vector<FHostAnnotationArgumentDesc>
				{
					std::vector<FHostAnnotationArgumentDesc> Out;
					if (Depth > 64) return Out;
					for (const auto& A : Arguments)
					{
						CheckAnalysisCancellation();
						FHostAnnotationArgumentDesc Desc;
						Desc.Name = A.Name;
						Desc.Detail = A.Detail;
						Desc.Documentation = A.Documentation;
						Desc.Children = Self(Self, A.Children, Depth + 1);
						Out.push_back(std::move(Desc));
					}
					return Out;
				};
				for (const auto& D : Definitions)
				{
					if (D.Name.empty() || !Names.insert(D.Name).second)
					{
						continue;
					}

					FHostAnnotationDesc Desc;
					Desc.Name = D.Name;
					Desc.Detail = D.Detail;
					Desc.Documentation = D.Documentation;
					Desc.Targets = D.Targets;
					Desc.CaseSensitive = D.CaseSensitive;
					Desc.Arguments = CopyArguments(CopyArguments, D.Arguments, 0);
					Annotations.push_back(std::move(Desc));
				}
			}

		private:
			const IHostBindingCatalog* Base;
			std::vector<FHostAnnotationDesc> Annotations;
		};
	}
	std::shared_ptr<const IHostBindingCatalog> SourceMetadataQueryBindings(
		const IHostBindingCatalog* Bindings, const std::vector<FAnnotationDefinition>& Definitions)
	{
		// A non-owning alias keeps the empty/common path allocation free. The
		// caller's prepared query or workspace retains the underlying catalog.
		const bool Covered = Bindings &&
			std::all_of(Definitions.begin(), Definitions.end(),
				[&](const auto& Definition)
				{
					const auto& Existing = Bindings->GetAnnotations();
					return std::any_of(Existing.begin(), Existing.end(),
						[&](const auto& A)
						{
							return A.Name == Definition.Name && A.CaseSensitive == Definition.CaseSensitive;
						});
				});
		if (Definitions.empty() || Covered) return std::shared_ptr<const IHostBindingCatalog>{std::shared_ptr<const IHostBindingCatalog>{}, Bindings};
		return std::make_shared<FMetadataQueryCatalog>(Bindings, Definitions);
	}
}
