// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptProcessingSession.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptPath.h"
#include "VeroScriptLexer.h"
#include "VeroScriptAst.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptSourceMetadataIO.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <tuple>

namespace VeroScript
{
	namespace
	{
		void Append(std::string& R, std::string_view S)
		{
			R += std::to_string(S.size()) + ":";
			R.append(S);
		}
		std::string DependencyKey(const std::string& S)
		{
			return IsResourceDependencyId(S) ? S : PathUtil::IdentityKey(S);
		}
		FSourceLocation Location(const FSourceRange& R)
		{
			return {R.SourceId, R.Position.Begin.Line, R.Position.Begin.Column};
		}
		bool UTF8Boundary(std::string_view S, std::size_t P)
		{
			return P <= S.size() && (P == S.size() || (static_cast<unsigned char>(S[P]) & 0xc0) != 0x80);
		}
	}
	bool NormalizeActiveAnnotationDefinitions(
		std::vector<FAnnotationDefinition>& Definitions, std::string& Error)
	{
		std::map<std::string, std::vector<std::uint8_t>> Names, Identities;
		std::map<std::string, std::vector<const FAnnotationDefinition*>> FoldedNames;
		std::vector<FAnnotationDefinition> Accepted;
		for (const auto& Definition : Definitions)
		{
			auto Folded = Definition.Name;
			for (char& C : Folded)
			{
				if (C >= 'A' && C <= 'Z')
				{
					C += 'a' - 'A';
				}
			}

			auto& Group = FoldedNames[Folded];
			if (!Group.empty() && (!Definition.CaseSensitive || !Group.front()->CaseSensitive))
			{
				for (const auto* Other : Group)
				{
					if (Other->Name != Definition.Name)
					{
						Error =
							"overlapping annotation name policies: " + Other->Name + " / " + Definition.Name;
						return false;
					}
				}
			}

			FSourceMetadataSnapshot Snapshot;
			Snapshot.Definitions = {Definition};
			std::vector<std::uint8_t> Original;
			if (!EncodeSourceMetadata(Snapshot, Original, Error)) return false;
			const auto Identity = Identities.emplace(Definition.Id, Original);
			if (!Identity.second && Identity.first->second != Original)
			{
				Error = "annotation description identity has conflicting contents: " + Definition.Id;
				return false;
			}
			Snapshot.Definitions[0].Id = "description";
			Snapshot.Definitions[0].ProviderId = "provider";
			std::vector<std::uint8_t> Content;
			if (!EncodeSourceMetadata(Snapshot, Content, Error)) return false;
			const auto Name = Names.emplace(Definition.Name, std::move(Content));
			if (!Name.second)
			{
				std::vector<std::uint8_t> Comparison;
				if (!EncodeSourceMetadata(Snapshot, Comparison, Error)) return false;
				if (Name.first->second != Comparison)
				{
					Error = "conflicting active annotation descriptions: " + Definition.Name;
					return false;
				}
				continue;
			}
			Accepted.push_back(Definition);
			Group.push_back(&Definition);
		}
		Definitions = std::move(Accepted);
		return true;
	}
	FCompileProcessingSession::FCompileProcessingSession(FCompileProcessingOptions O) : Options(std::move(O))
	{
		std::string DefinitionError;
		if (!NormalizeActiveAnnotationDefinitions(Options.AnnotationDefinitions, DefinitionError))
		{
			FScriptError E;
			E.Message = std::move(DefinitionError);
			Diagnostics.push_back(std::move(E));
		}
		if (Options.Registry && !Options.Registry->IsFrozen())
		{
			FScriptError E;
			E.Message = "compile processor registry must be frozen";
			Diagnostics.push_back(std::move(E));
		}
		if (static_cast<unsigned>(Options.CommentRetention) > 2)
		{
			FScriptError E;
			E.Message = "invalid comment retention policy";
			Diagnostics.push_back(std::move(E));
		}
		Metadata->Retention = Options.CommentRetention;
		Metadata->Definitions = Options.AnnotationDefinitions;
		Cacheable = Options.bCacheable;
		Dependencies = Options.InitialDependencies;
		std::set<std::string> DependencyIdentities;
		for (const auto& D : Dependencies)
		{
			if (D.Path.empty() || !DependencyIdentities.insert(DependencyKey(D.Path)).second ||
				D.Content.size() > Options.MaxInputBytes - std::min(DependencyBytes, Options.MaxInputBytes))
			{
				FScriptError E;
				E.Message = "invalid initial compile dependencies or exceeded byte budget";
				Diagnostics.push_back(std::move(E));
				break;
			}
			DependencyBytes += D.Content.size();
		}
		if (Dependencies.size() > Options.MaxDependencies)
		{
			FScriptError E;
			E.Message = "initial compile dependency count exceeded";
			Diagnostics.push_back(std::move(E));
		}
		std::size_t DescriptionNodes = 0;
		std::set<std::string> DefinitionIds, DefinitionNames;
		std::function<bool(const std::vector<FAnnotationArgumentDefinition>&, std::size_t)> Validate =
			[&](const auto& Args, std::size_t Depth)
		{
			if (Depth > 64 || Args.size() > 65536 - DescriptionNodes) return false;
			DescriptionNodes += Args.size();
			std::set<std::string> Names;
			for (const auto& A : Args)
			{
				if (A.Name.empty() || !Names.insert(A.Name).second || !Validate(A.Children, Depth + 1))
				{
					return false;
				}
			}
			return true;
		};
		for (const auto& D : Options.AnnotationDefinitions)
		{
			if (D.Id.empty() || D.Name.empty() || !D.Targets || D.Targets > AnnotationAllTargets ||
				!DefinitionIds.insert(D.Id).second ||
				!DefinitionNames.insert(D.ProviderId + "\x1f" + D.Name).second || !Validate(D.Arguments, 0))
			{
				FScriptError E;
				E.Message = "invalid or duplicate annotation description";
				Diagnostics.push_back(std::move(E));
				break;
			}
		}

		if (Options.Registry)
		{
			Instances.resize(Options.Registry->GetProcessors().size());
		}
	}
	void FCompileProcessingSession::Check() const
	{
		CheckAnalysisCancellation();
		if (Options.CheckCancelled)
		{
			Options.CheckCancelled();
		}
	}
	bool FCompileProcessingSession::HasErrors() const
	{
		return std::any_of(Diagnostics.begin(), Diagnostics.end(),
			[](const auto& D)
			{
				return D.Severity == ECompileDiagnosticSeverity::Error;
			});
	}
	std::string FCompileProcessingSession::Fingerprint() const
	{
		if (HasErrors()) return {};
		std::string R = Options.Registry ? Options.Registry->GetFingerprint() : "";
		Append(R, Options.ImplementationFingerprint);
		Append(R, Options.bCacheable ? "1" : "0");
		Append(R, std::to_string(static_cast<unsigned>(Options.CommentRetention)));
		for (const auto& M : Options.WritableModules)
		{
			Append(R, PathUtil::Normalize(M));
		}

		Append(R, std::to_string(Options.MaxOperations));
		Append(R, std::to_string(Options.MaxSourceBytes));
		Append(R, std::to_string(Options.MaxInputBytes));
		Append(R, std::to_string(Options.MaxEvents));
		Append(R, std::to_string(Options.MaxDependencies));
		std::function<void(const std::vector<FAnnotationArgumentDefinition>&)> Arguments = [&](const auto& Ds)
		{
			Append(R, std::to_string(Ds.size()));
			for (const auto& D : Ds)
			{
				Append(R, D.Name);
				Append(R, D.Detail);
				Append(R, D.Documentation);
				Arguments(D.Children);
			}
		};
		for (const auto& D : Options.AnnotationDefinitions)
		{
			Append(R, D.Id);
			Append(R, D.ProviderId);
			Append(R, D.Name);
			Append(R, D.Detail);
			Append(R, D.Documentation);
			Append(R, std::to_string(D.Targets));
			Append(R, D.CaseSensitive ? "1" : "0");
			Arguments(D.Arguments);
		}
		return R;
	}
	bool FCompileProcessingSession::ReplaceRegistry(FCompileProcessingOptions NewOptions, std::string& Error)
	{
		FCompileProcessingSession Next(std::move(NewOptions));
		if (Next.HasErrors())
		{
			Error = Next.Diagnostics.front().Message;
			return false;
		}
		if (Active)
		{
			Error = "cannot replace an active processor registry";
			return false;
		}
		if (Options.Registry)
		{
			for (std::size_t I = 0; I < Instances.size(); ++I)
			{
				if (Instances[I])
				{
					const auto& Id = Options.Registry->GetProcessors()[I].ProcessorId;
					for (std::size_t J = 0; J < Next.Instances.size(); ++J)
					{
						if (Next.Options.Registry->GetProcessors()[J].ProcessorId == Id)
						{
							Next.Instances[J] = std::move(Instances[I]);
							break;
						}
					}
				}
			}
		}

		Options = std::move(Next.Options);
		Instances = std::move(Next.Instances);
		return true;
	}
	bool FCompileProcessingSession::ProcessSource(
		ECompileProcessingPhase Phase, const std::string& Module, std::string& Text)
	{
		Check();
		if (HasErrors()) return false;
		if (!ProcessedSources.emplace(Module, Phase).second) return !HasErrors();
		if (Phase == ECompileProcessingPhase::RawSource)
		{
			if (Text.size() > Options.MaxInputBytes - std::min(InputBytes, Options.MaxInputBytes))
			{
				throw std::runtime_error("compile source input budget exceeded");
			}

			InputBytes += Text.size();
		}
		FSourceDocument D;
		D.Id = Module;
		D.ModuleId = Module;
		D.Text = Text;
		std::vector<FSourceRange> Protected;
		if (Phase == ECompileProcessingPhase::RawSource && Options.PrepareSource)
		{
			Protected = Options.PrepareSource(Module, Text);
			if (D.Text != Text)
			{
				auto Original = D;
				Original.Id = Module + ":compiler-input";
				Metadata->Documents.push_back(std::move(Original));
				D.Text = Text;
			}
		}
		if (Phase == ECompileProcessingPhase::ActiveSource)
		{
			if (const auto* Input = Metadata->FindDocument(Module + ":active-input"))
			{
				D.Mappings = Input->Mappings;
			}
		}

		D.Comments = CollectSourceComments(Text, Module,
			Phase == ECompileProcessingPhase::RawSource ? ESourceActivity::Unknown : ESourceActivity::Active);
		const std::vector<FAnnotationDeclarationView> Empty;
		auto Edits = Dispatch(Phase, Empty, &D);
		if (HasErrors()) return false;
		std::sort(Edits.begin(), Edits.end(),
			[](const auto& A, const auto& B)
			{
				return std::tie(A.EditRange.Begin, A.EditRange.End) <
					std::tie(B.EditRange.Begin, B.EditRange.End);
			});
		std::size_t End = 0;
		bool Previous = false;
		std::size_t PreviousBegin = 0;
		for (const auto& E : Edits)
		{
			for (const auto& R : Protected)
			{
				if ((E.EditRange.Begin < R.End && E.EditRange.End > R.Begin) ||
					(E.EditRange.Begin == E.EditRange.End && E.EditRange.Begin >= R.Begin &&
						E.EditRange.Begin < R.End))
				{
					throw std::runtime_error("compile processing cannot edit compiler import directives");
				}
			}

			if (E.EditRange.SourceId != Module || E.EditRange.Begin > E.EditRange.End ||
				!UTF8Boundary(Text, E.EditRange.Begin) || !UTF8Boundary(Text, E.EditRange.End) ||
				(Previous && (E.EditRange.Begin < End || E.EditRange.Begin == PreviousBegin)))
			{
				throw std::runtime_error("invalid or conflicting source edits");
			}

			Previous = true;
			PreviousBegin = E.EditRange.Begin;
			End = E.EditRange.End;
		}
		std::string Output;
		std::size_t Cursor = 0;
		FSourceDocument Accepted;
		Accepted.Id = Module + ":accepted";
		Accepted.ModuleId = Module;
		std::vector<std::size_t> OriginalLines{0};
		for (std::size_t I = 0; I < Text.size(); ++I)
		{
			if (Text[I] == '\n')
			{
				OriginalLines.push_back(I + 1);
			}
		}

		auto OriginalRange = [&](std::size_t B, std::size_t E)
		{
			FSourceRange R;
			R.SourceId = Module + (Phase == ECompileProcessingPhase::RawSource ? ":raw" : ":active-input");
			R.Begin = B;
			R.End = E;
			auto Position = [&](std::size_t O)
			{
				const auto It = std::upper_bound(OriginalLines.begin(), OriginalLines.end(), O);
				const auto I = static_cast<std::size_t>(It - OriginalLines.begin() - 1);
				return FAnnotationSourcePosition{Module, static_cast<std::uint32_t>(I + 1),
					static_cast<std::uint32_t>(O - OriginalLines[I] + 1)};
			};
			R.Position.Begin = Position(B);
			R.Position.End = Position(E);
			return R;
		};

		auto Preserve = [&](std::size_t From, std::size_t To, std::size_t OutputBegin)
		{
			if (From == To) return;
			if (Phase == ECompileProcessingPhase::RawSource || D.Mappings.empty())
			{
				Accepted.Mappings.push_back(
					{OutputBegin, OutputBegin + To - From, {OriginalRange(From, To), {}, {}}});
				return;
			}
			for (const auto& M : D.Mappings)
			{
				const auto B = std::max(From, M.Begin), E = std::min(To, M.End);
				if (B >= E)
				{
					continue;
				}

				auto Origin = M.Origin;
				if (Origin.GenerationId.empty() && Origin.Range.End - Origin.Range.Begin == M.End - M.Begin)
				{
					Origin.Range.Begin += B - M.Begin;
					Origin.Range.End = Origin.Range.Begin + E - B;
				}
				Accepted.Mappings.push_back(
					{OutputBegin + B - From, OutputBegin + E - From, std::move(Origin)});
			}
		};
		auto ResolveOrigin = [&](FSourceOrigin Origin)
		{
			if (Origin.Range.SourceId == Module)
			{
				if (Origin.Range.Begin > Origin.Range.End || !UTF8Boundary(D.Text, Origin.Range.Begin) ||
					!UTF8Boundary(D.Text, Origin.Range.End))
				{
					throw std::runtime_error("source edit origin is outside its input");
				}

				Origin.Range = OriginalRange(Origin.Range.Begin, Origin.Range.End);
			}
			else
			{
				const auto* Input = Metadata->FindDocument(Origin.Range.SourceId);
				if (!Input || Origin.Range.Begin > Origin.Range.End ||
					!UTF8Boundary(Input->Text, Origin.Range.Begin) ||
					!UTF8Boundary(Input->Text, Origin.Range.End))
				{
					throw std::runtime_error("source edit origin has no valid owned input range");
				}
			}
			return Origin;
		};
		for (auto& E : Edits)
		{
			const auto Before = Output.size();
			Output.append(Text, Cursor, E.EditRange.Begin - Cursor);
			Preserve(Cursor, E.EditRange.Begin, Before);
			const auto Begin = Output.size();
			Output += E.Source;
			E.Origin = ResolveOrigin(E.Origin);
			std::size_t MappingEnd = 0;
			for (auto& M : E.SourceMappings)
			{
				if (M.Begin < MappingEnd || M.Begin >= M.End || !UTF8Boundary(E.Source, M.Begin) ||
					!UTF8Boundary(E.Source, M.End))
				{
					throw std::runtime_error("invalid or overlapping source edit mappings");
				}

				if (MappingEnd < M.Begin)
				{
					Accepted.Mappings.push_back({Begin + MappingEnd, Begin + M.Begin, E.Origin});
				}

				M.Origin = ResolveOrigin(M.Origin);
				if (M.Origin.GenerationId.empty() &&
					M.End - M.Begin != M.Origin.Range.End - M.Origin.Range.Begin)
				{
					throw std::runtime_error("precise source mapping lengths disagree");
				}

				Accepted.Mappings.push_back({Begin + M.Begin, Begin + M.End, M.Origin});
				MappingEnd = M.End;
			}
			if (MappingEnd < E.Source.size())
			{
				Accepted.Mappings.push_back({Begin + MappingEnd, Output.size(), E.Origin});
			}

			Cursor = E.EditRange.End;
		}
		const auto Tail = Output.size();
		Output.append(Text, Cursor, Text.size() - Cursor);
		Preserve(Cursor, Text.size(), Tail);
		if (Output.size() > Options.MaxInputBytes)
		{
			throw std::runtime_error("processed source budget exceeded");
		}

		if (Phase == ECompileProcessingPhase::ActiveSource && !Edits.empty())
		{
			VeroScriptLexer Lexer(Output, Module);
			const auto& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				throw std::runtime_error("invalid ActiveSource output: " + Lexer.GetErrorMessage());
			}

			for (const auto& T : Tokens)
			{
				if (T.Kind == ETokenKind::Symbol && T.Lexeme == "#")
				{
					throw std::runtime_error("ActiveSource cannot introduce preprocessor directives");
				}
			}
		}
		Text = std::move(Output);
		for (auto& E : Edits)
		{
			Operations.push_back(std::move(E));
		}

		if (Phase == ECompileProcessingPhase::ActiveSource)
		{
			D.Text = Text;
			D.Comments = CollectSourceComments(Text, Module, ESourceActivity::Active);
			D.Mappings = std::move(Accepted.Mappings);
			auto Existing = std::find_if(Metadata->Documents.begin(), Metadata->Documents.end(),
				[&](const auto& X)
				{
					return X.Id == Module;
				});
			if (Existing == Metadata->Documents.end())
			{
				Metadata->Documents.push_back(std::move(D));
			}
			else
			{
				*Existing = std::move(D);
			}

		}
		else
		{
			D.Id = Module + ":raw";
			for (auto& C : D.Comments)
			{
				C.Range.SourceId = D.Id;
			}

			Accepted.Text = Text;
			auto IndexLines = [&](const std::string& Id, const std::string& S)
			{
				auto& Lines = SourceLines[Id];
				Lines = {0};
				for (std::size_t I = 0; I < S.size(); ++I)
				{
					if (S[I] == '\n')
					{
						Lines.push_back(I + 1);
					}
				}
			};
			IndexLines(D.Id, D.Text);
			IndexLines(Accepted.Id, Accepted.Text);
			Metadata->Documents.push_back(std::move(D));
			Metadata->Documents.push_back(std::move(Accepted));
		}
		return true;
	}
	void FCompileProcessingSession::RecordPreprocessedSource(
		const std::string& Module, const std::string& Text, const FPreprocessLineMap& Map)
	{
		FSourceDocument Input;
		Input.Id = Module + ":active-input";
		Input.ModuleId = Module;
		Input.Text = Text;
		const auto* Raw = Metadata->FindDocument(Module + ":raw");
		const auto* Accepted = Metadata->FindDocument(Module + ":accepted");
		const auto Lines = SourceLines.find(Module + ":raw");
		if (Raw && Accepted && Lines != SourceLines.end())
		{
			for (std::size_t I = 0; I < Map.OutByteToPhysical.size() && I < Text.size(); ++I)
			{
				Check();
				const auto P = Map.OutByteToPhysical[I];
				if (!P.Line || P.Line > Lines->second.size())
				{
					continue;
				}

				const auto Offset =
					std::min(Raw->Text.size(), Lines->second[P.Line - 1] + (P.Column ? P.Column - 1 : 0));
				if (!Input.Mappings.empty())
				{
					auto& Last = Input.Mappings.back();
					if (Last.End == I && Last.Origin.GenerationId.empty() && Last.Origin.Range.End == Offset)
					{
						++Last.End;
						Last.Origin.Range.End = std::min(Raw->Text.size(), Offset + 1);
						continue;
					}
					if (Last.End == I && Last.Origin.Range.Begin == Offset &&
						Last.Origin.Range.End <= Offset + 1)
					{
						++Last.End;
						Last.Origin.ProcessorId = "core.preprocessor";
						Last.Origin.GenerationId = "macro:" + std::to_string(Offset);
						continue;
					}
				}
				FSourceOrigin Origin;
				Origin.Range.SourceId = Raw->Id;
				Origin.Range.Begin = Offset;
				Origin.Range.End = std::min(Raw->Text.size(), Offset + 1);
				Origin.Range.Position.Begin = Origin.Range.Position.End = {Module, P.Line, P.Column};
				Input.Mappings.push_back({I, I + 1, std::move(Origin)});
			}
			// Only unchanged source intervals can prove whether an original comment
			// participated in a condition branch. Converted regions keep Unknown.
			for (auto& D : Metadata->Documents)
			{
				if (D.Id == Raw->Id)
				{
					for (auto& C : D.Comments)
					{
						C.Activity = ESourceActivity::Removed;
						for (const auto& M : Accepted->Mappings)
						{
							if (M.Origin.Range.SourceId == Raw->Id && M.Origin.Range.Begin <= C.Range.Begin &&
								M.Origin.Range.End >= C.Range.End)
							{
								C.Activity = ESourceActivity::Unknown;
								if (M.Origin.GenerationId.empty())
								{
									const auto Offset = M.Begin + C.Range.Begin - M.Origin.Range.Begin;
									const auto AcceptedLines = SourceLines.find(Accepted->Id);
									if (AcceptedLines != SourceLines.end())
									{
										const auto Line = static_cast<std::size_t>(
											std::upper_bound(AcceptedLines->second.begin(),
												AcceptedLines->second.end(), Offset) -
											AcceptedLines->second.begin());
										if (Line < Map.PhysicalLineActiveCode.size())
										{
											C.Activity = Map.PhysicalLineActiveCode[Line]
												? ESourceActivity::Active
												: ESourceActivity::Inactive;
										}
									}
								}
								break;
							}
						}
					}
				}
			}
		}
		auto& InputLines = SourceLines[Input.Id];
		InputLines = {0};
		for (std::size_t I = 0; I < Input.Text.size(); ++I)
		{
			if (Input.Text[I] == '\n')
			{
				InputLines.push_back(I + 1);
			}
		}

		Input.LineOffsets = InputLines;
		Metadata->Documents.push_back(std::move(Input));
	}
	FPreprocessSourcePos FCompileProcessingSession::MapSourcePosition(
		const std::string& Module, FPreprocessSourcePos P) const
	{
		const auto* D = Metadata->FindDocument(Module + ":accepted");
		const auto Lines = SourceLines.find(Module + ":accepted");
		if (!D || Lines == SourceLines.end() || !P.Line || P.Line > Lines->second.size()) return P;
		const auto Offset =
			std::min(D->Text.size(), Lines->second[P.Line - 1] + (P.Column ? P.Column - 1 : 0));
		const auto It = std::upper_bound(D->Mappings.begin(), D->Mappings.end(), Offset,
			[](auto O, const auto& M)
			{
				return O < M.Begin;
			});
		if (It == D->Mappings.begin()) return P;
		const auto& M = *std::prev(It);
		if (Offset >= M.End) return P;
		auto Original = M.Origin.Range.Begin;
		if (M.Origin.GenerationId.empty())
		{
			Original += Offset - M.Begin;
		}

		const auto OriginalLines = SourceLines.find(M.Origin.Range.SourceId);
		if (OriginalLines == SourceLines.end()) return P;
		const auto L = std::upper_bound(OriginalLines->second.begin(), OriginalLines->second.end(), Original);
		const auto Index = static_cast<std::size_t>(L - OriginalLines->second.begin() - 1);
		return {static_cast<std::uint32_t>(Index + 1),
			static_cast<std::uint32_t>(Original - OriginalLines->second[Index] + 1)};
	}
	std::vector<FCompileGenerationOperation> FCompileProcessingSession::Dispatch(
		ECompileProcessingPhase Phase, const std::vector<FAnnotationDeclarationView>& Decls,
		const FSourceDocument* Source)
	{
		Check();
		++Snapshot;
		std::vector<FCompileGenerationOperation> Result;
		if (Phase == ECompileProcessingPhase::Types)
		{
			for (const auto& D : Decls)
			{
				if (!D.bFromPackage && !D.bGenerated)
				{
					OriginalDeclarations.insert(D.Id);
					for (const auto& A : D.Metadata.Annotations)
					{
						OriginalAnnotations.insert(D.Id + ":" + A.Name + ":" + std::to_string(A.Ordinal));
					}
				}
			}

			for (const auto& D : Metadata->Documents)
			{
				if (D.Id == D.ModuleId)
				{
					for (const auto& C : D.Comments)
					{
						OriginalComments.insert(C.Id);
					}
				}
			}
		}
		if (!Options.Registry || !Options.Registry->HasPhase(Phase)) return Result;
		const auto Blocks = Program ? GetCompileBlockViews(*Program) : std::vector<FCompileBlockView>{};
		if (Active)
		{
			throw std::runtime_error("compile processing cannot be reentered");
		}

		struct FGuard
		{
			FGuard(bool& B) : V(B)
			{
				V = true;
			}

			~FGuard()
			{
				V = false;
			}

			bool& V;
		} Guard(Active);
		struct FEvent
		{
			ECompileProcessingEvent Kind;
			std::string Id, Module;
			std::size_t Ordinal = 0;
			const FAnnotationDeclarationView* Decl = nullptr;
			const FDeclarationAnnotation* Ann = nullptr;
			const FSourceComment* Comment = nullptr;
			const FSourceNodeMetadata* Node = nullptr;
		};
		std::vector<FEvent> Events;
		if (Source)
		{
			Events.push_back({ECompileProcessingEvent::Module, Source->Id, Source->ModuleId});
			for (const auto& C : Source->Comments)
			{
				Events.push_back({ECompileProcessingEvent::Comment, C.Id, C.ModuleId, C.Range.Begin, nullptr,
					nullptr, &C});
			}

		}
		else
		{
			std::set<std::string> Modules;
			for (const auto& D : Decls)
			{
				if (D.bFromPackage || D.bGenerated)
				{
					continue;
				}

				Modules.insert(D.ModulePath);
				if (Phase == ECompileProcessingPhase::Types)
				{
					OriginalDeclarations.insert(D.Id);
				}

				if (!OriginalDeclarations.count(D.Id))
				{
					continue;
				}

				Events.push_back({ECompileProcessingEvent::Declaration, D.Id, D.ModulePath,
					D.Metadata.SourceRange.Begin, &D});
				for (const auto& A : D.Metadata.Annotations)
				{
					if (OriginalAnnotations.count(D.Id + ":" + A.Name + ":" + std::to_string(A.Ordinal)))
					{
						std::size_t Offset = D.Metadata.SourceRange.Begin;
						if (const auto* Doc = Metadata->FindDocument(A.NamePosition.FileLabel);
							Doc && A.NamePosition.Line && A.NamePosition.Line <= Doc->LineOffsets.size())
						{
							Offset = Doc->LineOffsets[A.NamePosition.Line - 1] + A.NamePosition.Column - 1;
						}

						Events.push_back({ECompileProcessingEvent::Annotation,
							D.Id + ":annotation:" + std::to_string(A.Ordinal), D.ModulePath, Offset, &D, &A});
					}
				}
			}
			for (const auto& D : Metadata->Documents)
			{
				if (D.Id == D.ModuleId)
				{
					const bool Package = Program &&
						std::any_of(Program->PackageSourceModules.begin(),
							Program->PackageSourceModules.end(),
							[&](const auto& M)
							{
								return M.bFromPackage && M.ModulePath == D.ModuleId;
							});
					if (!Package)
					{
						Modules.insert(D.ModuleId);
					}
				}
			}

			for (const auto& M : Modules)
			{
				Events.push_back({ECompileProcessingEvent::Module, M, M});
			}

			for (const auto& D : Metadata->Documents)
			// Historical source documents are queryable, but only the current
			// active document supplies declaration-phase comment events.
			{
				if (D.Id == D.ModuleId && Modules.count(D.ModuleId))
				{
					for (const auto& C : D.Comments)
					{
						if (C.Activity == ESourceActivity::Active && OriginalComments.count(C.Id))
						{
							Events.push_back({ECompileProcessingEvent::Comment, C.Id, C.ModuleId,
								C.Range.Begin, nullptr, nullptr, &C});
						}
					}
				}
			}

			if (Phase == ECompileProcessingPhase::Bodies)
			{
				for (const auto& N : Metadata->Nodes)
				{
					if (Modules.count(N.ModuleId) &&
						(N.Kind.rfind("statement:", 0) == 0 || N.Kind.rfind("expression:", 0) == 0) &&
						N.Range.SourceId.rfind("veroscript-generated:", 0) != 0)
					{
						Events.push_back({ECompileProcessingEvent::Syntax, N.Id, N.ModuleId, N.Range.Begin,
							nullptr, nullptr, nullptr, &N});
					}
				}
			}
		}
		std::sort(Events.begin(), Events.end(),
			[](const auto& A, const auto& B)
			{
				return std::tie(A.Module, A.Ordinal, A.Kind, A.Id) <
					std::tie(B.Module, B.Ordinal, B.Kind, B.Id);
			});
		const auto& Descs = Options.Registry->GetProcessors();
		std::map<std::string, std::string> SourceOwners;
		// Keep the producer owner captured by package/overlay preparation. A
		// physical-path lookup alone can assign a linked source to its consumer.
		if (Program && Options.ModuleEnvironment && std::any_of(Descs.begin(), Descs.end(), [](const auto& Desc)
		{
			return !Desc.Origin.ProviderId.empty();
		}))
		{
			for (const auto& Packaged : Program->PackageSourceModules)
			{
				if (Packaged.bFromPackage && !Packaged.SourceOwnerId.empty())
				{
					SourceOwners.emplace(Packaged.ModulePath, Packaged.SourceOwnerId);
				}
			}
		}
		const auto SourceOwner = [&](const std::string& Module)
		{
			const auto Cached = SourceOwners.find(Module);
			if (Cached != SourceOwners.end()) return Cached->second;
			if (Program)
			{
				const auto Captured = Program->ModuleSourceOwners.find(Module);
				if (Captured != Program->ModuleSourceOwners.end()) return SourceOwners.emplace(Module, Captured->second).first->second;
			}
			return SourceOwners.emplace(Module, Options.ModuleEnvironment->GetSourceOwner(Module)).first->second;
		};
		for (const auto& E : Events)
		{
			if (ReadOnlyModules.count(E.Module) &&
				std::find(Options.WritableModules.begin(), Options.WritableModules.end(), E.Module) ==
					Options.WritableModules.end())
			{
				continue;
			}

			Check();
			if (++EventCount > Options.MaxEvents)
			{
				throw std::runtime_error("compile event budget exceeded");
			}

			const auto* AnnotationDefinition = E.Ann && !E.Ann->DefinitionId.empty()
				? Metadata->FindDefinition(E.Ann->DefinitionId)
				: nullptr;
			const bool CaseSensitive = !AnnotationDefinition || AnnotationDefinition->CaseSensitive;
			for (std::size_t I = 0; I < Descs.size(); ++I)
			{
				const auto& D = Descs[I];
				if (!(D.Phases & CompilePhaseMask(Phase)))
				{
					continue;
				}

				bool Match = false;
				for (const auto& S : D.Subscriptions)
				{
					if (S.Event != E.Kind || (S.Phases && !(S.Phases & CompilePhaseMask(Phase))))
					{
						continue;
					}

					if (E.Decl && !(S.Targets & AnnotationTargetMask(E.Decl->Target)))
					{
						continue;
					}

					if (E.Ann && !AnnotationNamesEqual(E.Ann->Name, S.AnnotationName, CaseSensitive))
					{
						continue;
					}

					if (E.Comment && S.CommentKind && *S.CommentKind != E.Comment->Kind)
					{
						continue;
					}

					if (E.Node && !S.SyntaxKind.empty() && S.SyntaxKind != E.Node->Kind)
					{
						continue;
					}

					Match = true;
					break;
				}
				if (!Match)
				{
					continue;
				}
				if (Options.ModuleEnvironment && !D.Origin.ProviderId.empty())
				{
					const auto Owner = SourceOwner(E.Module);
					const bool ModuleVisible = Program && !D.Origin.ModulePath.empty() &&
						ModuleVisibility::CanAccessSymbol(*Program, D.Origin.ModulePath, D.Origin.bExported, E.Module);
					if ((!D.Origin.bExported && Owner != D.Origin.ProviderId) ||
						(!ModuleVisible && !Options.ModuleEnvironment->CanAccessOwner(Owner, D.Origin.ProviderId)))
					{
						continue;
					}
				}

				const auto EventLocation = E.Ann ? E.Ann->NamePosition
					: E.Decl					 ? E.Decl->Location
					: E.Comment					 ? E.Comment->Range.Position.Begin
					: E.Node					 ? E.Node->Range.Position.Begin
												 : FAnnotationSourcePosition{E.Module, 1, 1};
				FCompileProcessingOutput Out;
				try
				{
					if (!Instances[I])
					{
						Instances[I] = D.Factory();
					}

					if (!Instances[I])
					{
						throw std::runtime_error("compile processor factory returned null");
					}

					const auto ResourceFrom = D.Origin.ProviderId.empty() ? E.Module : D.Origin.ProviderId;
					auto Read = [&](std::string_view Path, std::string& Content)
					{
						Check();
						Content.clear();
						std::string Resolved(Path);
						const bool Resource = IsResourceDependencyId(Resolved);
						if (!Resource && !PathUtil::FromUtf8(Resolved).is_absolute() && !E.Module.empty() &&
							E.Module.front() != '<')
						{
							Resolved = PathUtil::ToUtf8(
								PathUtil::FromUtf8(E.Module).parent_path() / PathUtil::FromUtf8(Resolved));
						}

						if (!Resource)
						{
							Resolved = PathUtil::Normalize(Resolved);
						}

						if (Options.ModuleEnvironment && !Resource)
						{
							std::string Error;
							if (!D.Origin.ProviderId.empty())
							{
								if (!Options.ModuleEnvironment->CanAccessOwner(D.Origin.ProviderId,
										Options.ModuleEnvironment->GetSourceOwner(Resolved)))
								{
									throw std::runtime_error(
										"compile dependency is outside the processor scope: " + Resolved);
								}

							}
							else if (!Options.ModuleEnvironment->AuthorizeSource(Resolved, E.Module, Error))
							{
								throw std::runtime_error(Error);
							}
						}
						if (Resource)
						{
							FResourceRequest R;
							std::string From;
							if (!ParseResourceDependencyId(Resolved, R, From) || From != ResourceFrom)
							{
								throw std::runtime_error(
									"resource dependency has a different requesting module");
							}
						}
						for (const auto& Dependency : Dependencies)
						{
							if (DependencyKey(Dependency.Path) == DependencyKey(Resolved))
							{
								Content = Dependency.Content;
								return Dependency.bExists;
							}
						}

						const bool Exists =
							Options.ReadDependency && Options.ReadDependency(Resolved, Content);
						if (Dependencies.size() >= Options.MaxDependencies ||
							Content.size() >
								Options.MaxInputBytes - std::min(DependencyBytes, Options.MaxInputBytes))
						{
							throw std::runtime_error("compile dependency budget exceeded");
						}

						DependencyBytes += Content.size();
						Dependencies.push_back({Resolved, Content, Exists});
						return Exists;
					};
					FCompileProcessingContext C{Phase, E.Kind, Snapshot, E.Module, E.Id, E.Decl, E.Ann,
						E.Comment, E.Node, Source ? Source : Metadata->FindDocument(E.Module), Decls,
						*Metadata,
						[&]
						{
							Check();
						},
						Read};
					C.ResourceRequester = ResourceFrom;
					C.Blocks = &Blocks;
					C.ReadResource = [&](const FResourceRequest& R, FResourceData& Data)
					{
						Check();
						if (!Options.ModuleEnvironment)
						{
							throw std::runtime_error("no module resource environment is configured");
						}

						std::string Failure;
						if (!Options.MaxInputBytes)
						{
							throw std::runtime_error("compile dependency budget exceeded");
						}

						if (!Options.ModuleEnvironment->ReadResource(
								R, ResourceFrom, {C.CheckCancelled, Options.MaxInputBytes}, Data, Failure))
						{
							throw std::runtime_error(Failure);
						}

						const auto Id = MakeResourceDependencyId(R, ResourceFrom);
						const auto Content = ResourceDependencyContent(Data);
						for (const auto& Dependency : Dependencies)
						{
							if (Dependency.Path == Id)
							{
								if (Dependency.bExists != Data.bExists || Dependency.Content != Content)
								{
									throw std::runtime_error("resource changed during compilation");
								}

								return Data.bExists;
							}
						}

						if (Dependencies.size() >= Options.MaxDependencies ||
							Content.size() >
								Options.MaxInputBytes - std::min(DependencyBytes, Options.MaxInputBytes))
						{
							throw std::runtime_error("compile dependency budget exceeded");
						}

						DependencyBytes += Content.size();
						Dependencies.push_back({Id, Content, Data.bExists});
						return Data.bExists;
					};
					C.Program = Source ? nullptr : Program;
					Instances[I]->Process(C, Out);
					Check();
				}
				catch (const FAnalysisCancelled&)
				{
					throw;
				}
				catch (const std::exception& X)
				{
					FScriptError Error;
					Error.Message = "compile processor " + D.ProcessorId + " failed: " + X.what();
					Error.Location =
						FSourceLocation{EventLocation.FileLabel, EventLocation.Line, EventLocation.Column};
					Diagnostics.push_back(std::move(Error));
					return {};
				}
				catch (...)
				{
					FScriptError Error;
					Error.Message = "compile processor " + D.ProcessorId + " failed";
					Error.Location =
						FSourceLocation{EventLocation.FileLabel, EventLocation.Line, EventLocation.Column};
					Diagnostics.push_back(std::move(Error));
					return {};
				}
				for (auto Error : Out.GetDiagnostics())
				{
					if (Error.Location.FileLabel.empty())
					{
						Error.Location = FSourceLocation{
							EventLocation.FileLabel, EventLocation.Line, EventLocation.Column};
					}
					Diagnostics.push_back(std::move(Error));
				}
				if (HasErrors()) return {};
				Cacheable = Cacheable && D.Cacheable;
				std::size_t Ordinal = 0;
				for (auto O : Out.GetOperations())
				{
					// Descriptor permissions are masks; a submitted operation is one
					// kind. Combined values must not reach the declaration fallback.
					switch (O.Kind)
					{
					case ECompileOperation::EditSource:
					case ECompileOperation::AddDeclaration:
					case ECompileOperation::ReplaceDeclaration:
					case ECompileOperation::CompleteBody:
					case ECompileOperation::ReplaceBody:
					case ECompileOperation::SetMetadata:
						{

							break;
						}
					default:
						{
							FScriptError Error;
							Error.Message = "invalid compile operation kind from processor " + D.ProcessorId;
							Error.Location = FSourceLocation{
								EventLocation.FileLabel, EventLocation.Line, EventLocation.Column};
							Diagnostics.push_back(std::move(Error));
							return {};
						}
					}
					if (O.Kind == ECompileOperation::AddDeclaration && !O.TargetId.empty())
					{
						FScriptError Error;
						Error.Message =
							"declaration addition cannot carry a replacement target from processor " +
							D.ProcessorId;
						Error.Location = FSourceLocation{
							EventLocation.FileLabel, EventLocation.Line, EventLocation.Column};
						Diagnostics.push_back(std::move(Error));
						return {};
					}
					if (!(D.Operations & CompileOperationMask(O.Kind)))
					{
						throw std::runtime_error("processor submitted a disallowed operation");
					}

					FSourceMetadataSnapshot Payload;
					FSourceNodeMetadata Root;
					Root.Id = "payload";
					if (O.Metadata.Annotations)
					{
						Root.Annotations = *O.Metadata.Annotations;
					}
					if (O.Metadata.Comments)
					{
						Root.Comments = *O.Metadata.Comments;
					}
					Payload.Nodes.push_back(std::move(Root));
					for (const auto& Child : O.Metadata.Children)
					{
						auto N = Child.Metadata;
						N.Id = "child:" + std::to_string(Payload.Nodes.size());
						Payload.Nodes.push_back(std::move(N));
					}
					std::vector<std::uint8_t> PayloadBytes;
					std::string PayloadError;
					if (!EncodeSourceMetadata(Payload, PayloadBytes, PayloadError))
					{
						throw std::runtime_error("invalid processing metadata: " + PayloadError);
					}

					if (PayloadBytes.size() >
						Options.MaxInputBytes - std::min(GeneratedMetadataBytes, Options.MaxInputBytes))
					{
						throw std::runtime_error("generated metadata budget exceeded");
					}

					GeneratedMetadataBytes += PayloadBytes.size();
					if (!O.Metadata.Children.empty() &&
						(O.Kind == ECompileOperation::SetMetadata || O.Kind == ECompileOperation::EditSource))
					{
						throw std::runtime_error("child metadata requires a generated syntax fragment");
					}

					for (const auto& C : O.Metadata.Children)
					{
						if (C.Begin > C.End || C.End > O.Source.size() || C.Kind.empty())
						{
							throw std::runtime_error("fragment metadata range is outside its source");
						}
					}

					const bool IsSource = Phase == ECompileProcessingPhase::RawSource ||
						Phase == ECompileProcessingPhase::ActiveSource;
					const auto Allowed =
						IsSource ? 1u : (Phase == ECompileProcessingPhase::Bodies ? 56u : 38u);
					if (!(Allowed & CompileOperationMask(O.Kind)))
					{
						throw std::runtime_error("operation is not allowed in this phase");
					}

					if ((O.Snapshot && O.Snapshot != Snapshot) ||
						(O.Scope.Snapshot && O.Scope.Snapshot != Snapshot))
					{
						throw std::runtime_error("stale compile processing target");
					}

					if (O.Kind == ECompileOperation::AddDeclaration && !O.Scope.OwnerId.empty() &&
						O.Scope.Snapshot != Snapshot)
					{
						throw std::runtime_error("member generation requires the current scope snapshot");
					}

					if (O.Kind != ECompileOperation::EditSource &&
						O.Kind != ECompileOperation::AddDeclaration && O.Snapshot != Snapshot)
					{
						throw std::runtime_error("compile processing target requires the current snapshot");
					}

					if (Operations.size() + ReservedReplayOperations + Result.size() >=
							Options.MaxOperations ||
						O.Source.size() >
							Options.MaxSourceBytes - std::min(SourceBytes, Options.MaxSourceBytes))
					{
						throw std::runtime_error("compile generation budget exceeded");
					}

					SourceBytes += O.Source.size();
					O.Phase = Phase;
					O.ProcessorId = D.ProcessorId;
					O.Snapshot = Snapshot;
					O.GenerationId = D.ProcessorId + ":" + std::to_string(static_cast<unsigned>(Phase)) +
						":" + E.Id + ":" + std::to_string(Ordinal++);
					if (O.Scope.ModuleId.empty())
					{
						O.Scope.ModuleId = E.Module;
					}

					if (ReadOnlyModules.count(O.Scope.ModuleId) &&
						std::find(Options.WritableModules.begin(), Options.WritableModules.end(),
							O.Scope.ModuleId) == Options.WritableModules.end())
					{
						throw std::runtime_error("compile operation cannot modify a read-only module");
					}

					if (O.Scope.ModuleId != E.Module &&
						std::find(Options.WritableModules.begin(), Options.WritableModules.end(),
							O.Scope.ModuleId) == Options.WritableModules.end())
					{
						throw std::runtime_error("compile operation is outside the writable module set");
					}

					if (O.Origin.Range.SourceId.empty())
					{
						if (O.Kind == ECompileOperation::EditSource)
						{
							O.Origin.Range = O.EditRange;
						}

						else if (E.Comment)
						{
							O.Origin.Range = E.Comment->Range;
						}

						else if (E.Node)
						{
							O.Origin.Range = E.Node->Range;
						}

						else if (E.Decl)
						{
							O.Origin.Range = E.Decl->Metadata.SourceRange;
						}

						else if (const auto* Input = Source ? Source : Metadata->FindDocument(E.Module))
						{
							O.Origin.Range = {
								Input->Id, 0, Input->Text.size(), {{Input->Id, 1, 1}, {Input->Id, 1, 1}}};
						}
					}
					O.Origin.ProcessorId = D.ProcessorId;
					O.Origin.GenerationId = O.GenerationId;
					Result.push_back(std::move(O));
				}
			}
		}
		return Result;
	}

}
