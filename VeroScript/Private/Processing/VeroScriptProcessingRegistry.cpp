// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompileProcessing.h"
#include <algorithm>
#include <tuple>

namespace VeroScript
{
	const FAnnotationDeclarationView* FCompileProcessingContext::FindDeclaration(std::string_view Id) const
	{
		for (const auto& D : Declarations)
		{
			if (D.Id == Id) return &D;
		}
		return nullptr;
	}
	void FCompileProcessingOutput::EditSource(
		FSourceRange R, std::string S, FSourceOrigin O, std::vector<FSourceDocument::FMapping> Mappings)
	{
		FCompileGenerationOperation P;
		P.Kind = ECompileOperation::EditSource;
		P.EditRange = std::move(R);
		P.Source = std::move(S);
		P.Origin = std::move(O);
		P.SourceMappings = std::move(Mappings);
		Operations.push_back(std::move(P));
	}
	void FCompileProcessingOutput::AddDeclaration(FCompileScope Scope, std::string S, FCompileMetadata M)
	{
		FCompileGenerationOperation P;
		P.Kind = ECompileOperation::AddDeclaration;
		P.Scope = std::move(Scope);
		P.Source = std::move(S);
		P.Metadata = std::move(M);
		Operations.push_back(std::move(P));
	}
	void FCompileProcessingOutput::ReplaceDeclaration(
		std::string Id, std::uint64_t V, std::string S, FCompileMetadata M)
	{
		FCompileGenerationOperation P;
		P.Kind = ECompileOperation::ReplaceDeclaration;
		P.TargetId = std::move(Id);
		P.Snapshot = V;
		P.Source = std::move(S);
		P.Metadata = std::move(M);
		Operations.push_back(std::move(P));
	}
	void FCompileProcessingOutput::CompleteBody(
		std::string Id, std::uint64_t V, std::string S, FCompileMetadata M)
	{
		FCompileGenerationOperation P;
		P.Kind = ECompileOperation::CompleteBody;
		P.TargetId = std::move(Id);
		P.Snapshot = V;
		P.Source = std::move(S);
		P.Metadata = std::move(M);
		Operations.push_back(std::move(P));
	}
	void FCompileProcessingOutput::ReplaceBody(
		std::string Id, std::uint64_t V, std::string S, FCompileMetadata M)
	{
		FCompileGenerationOperation P;
		P.Kind = ECompileOperation::ReplaceBody;
		P.TargetId = std::move(Id);
		P.Snapshot = V;
		P.Source = std::move(S);
		P.Metadata = std::move(M);
		Operations.push_back(std::move(P));
	}
	void FCompileProcessingOutput::SetMetadata(std::string Id, std::uint64_t V, FCompileMetadata M)
	{
		FCompileGenerationOperation P;
		P.Kind = ECompileOperation::SetMetadata;
		P.TargetId = std::move(Id);
		P.Snapshot = V;
		P.Metadata = std::move(M);
		Operations.push_back(std::move(P));
	}
	void FCompileProcessingOutput::SubmitOperation(FCompileGenerationOperation O)
	{
		Operations.push_back(std::move(O));
	}
	void FCompileProcessingOutput::ReportDiagnostic(FScriptError E)
	{
		Diagnostics.push_back(std::move(E));
	}
	bool FCompileProcessorRegistry::Register(FCompileProcessorDesc D, std::string& Error)
	{
		Error.clear();
		auto Fail = [&](const char* S)
		{
			Error = S;
			return false;
		};
		if (bFrozen) return Fail("compile processor registry is frozen");
		if (D.ProcessorId.empty() || D.Version.empty() || !D.Factory || !D.Phases || (D.Phases & ~31u) ||
			(D.Operations & ~63u) || D.Subscriptions.empty())
		{
			return Fail("invalid compile processor descriptor");
		}

		for (const auto& P : Processors)
		{
			if (P.ProcessorId == D.ProcessorId) return Fail("duplicate compile processor identity");
		}

		for (const auto& S : D.Subscriptions)
		{
			if (static_cast<unsigned>(S.Event) > 4 || (S.Targets & ~AnnotationAllTargets) ||
				(S.CommentKind && static_cast<unsigned>(*S.CommentKind) > 3))
			{
				return Fail("invalid compile processor subscription");
			}

			if (S.Event == ECompileProcessingEvent::Annotation && S.AnnotationName.empty())
			{
				return Fail("annotation subscription requires a name");
			}
			if ((S.Event==ECompileProcessingEvent::Annotation || S.Event==ECompileProcessingEvent::Declaration) && !S.Targets)return Fail("declaration subscription requires a target");
			const auto Available = (S.Event == ECompileProcessingEvent::Module || S.Event == ECompileProcessingEvent::Comment)
				? 31u : (S.Event == ECompileProcessingEvent::Syntax ? 16u : 28u);
			if (S.Phases ? ((S.Phases & ~Available) || (S.Phases & ~D.Phases)) : !(Available & D.Phases))
			{
				return Fail("subscription does not match processor phases");
			}
		}
		std::uint32_t Allowed = 0;
		if (D.Phases & 3u)
		{
			Allowed |= CompileOperationMask(ECompileOperation::EditSource);
		}

		if (D.Phases & 12u)
		{
			Allowed |= 2u | 4u | 32u;
		}

		if (D.Phases & 16u)
		{
			Allowed |= 8u | 16u | 32u;
		}

		if (D.Operations & ~Allowed) return Fail("compile operations do not match processor phases");
		if (Processors.size() >= 4096) return Fail("compile processor registry limit exceeded");
		Processors.push_back(std::move(D));
		return true;
	}
	void FCompileProcessorRegistry::Freeze()
	{
		std::sort(Processors.begin(), Processors.end(),
			[](const auto& A, const auto& B)
			{
				return std::tie(A.Order, A.ProcessorId) < std::tie(B.Order, B.ProcessorId);
			});
		bFrozen = true;
	}
	std::string FCompileProcessorRegistry::GetFingerprint() const
	{
		std::string R;
		auto Add = [&](std::string_view S)
		{
			R += std::to_string(S.size()) + ":";
			R.append(S);
		};
		Add(std::to_string(Processors.size()));
		for (const auto& D : Processors)
		{
			Add(D.ProcessorId);
			Add(D.Version);
			Add(D.ConfigurationKey);
			Add(D.Origin.ProviderId);
			Add(D.Origin.ModulePath);
			Add(D.Origin.bExported ? "1" : "0");
			Add(std::to_string(D.Phases));
			Add(std::to_string(D.Operations));
			Add(std::to_string(D.Order));
			Add(D.Cacheable ? "1" : "0");
			Add(std::to_string(D.Subscriptions.size()));
			for (const auto& S : D.Subscriptions)
			{
				Add(std::to_string(static_cast<unsigned>(S.Event)));
				Add(std::to_string(S.Targets));
				Add(S.AnnotationName);
				Add(S.SyntaxKind);
				Add(S.CommentKind ? std::to_string(static_cast<unsigned>(*S.CommentKind)) : "-");
				Add(std::to_string(S.Phases));
			}
		}
		return R;
	}
	bool FCompileProcessorRegistry::HasPhase(ECompileProcessingPhase P) const
	{
		for (const auto& D : Processors)
		{
			if (D.Phases & CompilePhaseMask(P)) return true;
		}
		return false;
	}
}
