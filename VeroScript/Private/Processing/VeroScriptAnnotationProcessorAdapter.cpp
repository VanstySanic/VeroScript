// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptProcessingSession.h"
#include <algorithm>
#include <stdexcept>

namespace VeroScript
{
	namespace
	{
		// The annotation convenience API submits to the same phase transactions,
		// metadata collector and package ledger as every other processor.
		class FAnnotationAdapter final : public ICompileProcessor
		{
		public:
			void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& Output) override
			{
				if (C.Event == ECompileProcessingEvent::Module)
				{
					for (auto I = PendingBodies.begin(); I != PendingBodies.end();)
					{
						if (I->Scope.ModuleId != C.ModuleId)
						{
							++I;
							continue;
						}
						const auto* Target = C.FindDeclaration(I->TargetId);
						if (!Target)
						{
							throw std::runtime_error("annotation body target no longer exists");
						}

						if (!Target->bHasBody)
						{
							I->Snapshot = C.Snapshot;
							Output.SubmitOperation(std::move(*I));
						}
						I = PendingBodies.erase(I);
					}
					return;
				}
				if (!Processor)
				{
					Processor = Description.Factory();
				}

				if (!Processor)
				{
					throw std::runtime_error("annotation processor factory returned null");
				}

				const auto& Declaration = *C.Declaration;
				const auto OwnerId = Declaration.OwnerId.empty() ? Declaration.Id : Declaration.OwnerId;
				const auto* Owner = C.FindDeclaration(OwnerId);
				FAnnotationContext Context{Declaration, *C.Annotation, Owner, C.Declarations, C.CheckCancelled, C.ReadDependency, C.ReadResource};
				FAnnotationOutput Result;
				Processor->Process(Context, Result);
				for (const auto& D : Result.GetDiagnostics())
				{
					Output.ReportDiagnostic(D);
				}

				for (const auto& Op : Result.GetOperations())
				{
					if (!(Description.Operations & static_cast<unsigned>(Op.Kind)))
					{
						throw std::runtime_error("annotation processor submitted a disallowed operation");
					}

					const auto* Target = C.FindDeclaration(Op.TargetId);
					if (!Target || Target->ModulePath != Declaration.ModulePath ||
					    (Op.Kind == EAnnotationOperation::AddMethod ? Op.TargetId != OwnerId
					                                                : Target->OwnerId != OwnerId))
					{
						throw std::runtime_error(
						    "annotation generation target is outside the annotated owner");
					}

					if (Op.Source.size() > 1024 * 1024)
					{
						throw std::runtime_error("annotation fragment budget exceeded");
					}

					if (Op.Kind == EAnnotationOperation::AddMethod)
					{
						if (!Owner ||
						    (Owner->Target != EAnnotationTarget::Class &&
						        Owner->Target != EAnnotationTarget::Struct &&
						        Owner->Target != EAnnotationTarget::Interface))
						{
							throw std::runtime_error("annotation method generation requires a type owner");
						}

						const char* Access = Op.Access == EMemberAccess::Public ? "public:"
						    : Op.Access == EMemberAccess::Protected             ? "protected:"
						    : Op.Access == EMemberAccess::Private               ? "private:"
						                                                        : nullptr;
						if (!Access)
						{
							throw std::runtime_error("invalid generated method access");
						}

						Output.AddDeclaration({Declaration.ModulePath, "", OwnerId, C.Snapshot},
						    std::string(Access) + Op.Source);
					}
					else if (C.Phase == ECompileProcessingPhase::Bodies)
					{
						Output.CompleteBody(Op.TargetId, C.Snapshot, Op.Source);
					}

					else
					{
						FCompileGenerationOperation Body;
						Body.Kind = ECompileOperation::CompleteBody;
						Body.TargetId = Op.TargetId;
						Body.Source = Op.Source;
						Body.Scope.ModuleId = Declaration.ModulePath;
						Body.Origin.Range = Declaration.Metadata.SourceRange;
						PendingBodies.push_back(std::move(Body));
					}
				}
			}

			explicit FAnnotationAdapter(FAnnotationProcessorDesc D) : Description(std::move(D))
			{
			}

		private:
			FAnnotationProcessorDesc Description;
			std::unique_ptr<IAnnotationProcessor> Processor;
			std::vector<FCompileGenerationOperation> PendingBodies;
		};
	}
	bool AttachAnnotationProcessors(FCompileProcessingSession& Session, const FAnnotationProcessingOptions& O, std::string& Error)
	{
		if (!O.Registry)
		{
			// Package replay still consumes the legacy generation budget even when
			// its original annotation processors are unavailable to the consumer.
			const FAnnotationProcessingOptions Defaults;
			if (O.MaxOperations == Defaults.MaxOperations && O.MaxSourceBytes == Defaults.MaxSourceBytes) return true;
			auto Options = Session.Options;
			const FCompileProcessingOptions GeneralDefaults;
			if (O.MaxOperations != Defaults.MaxOperations)
			{
				Options.MaxOperations =
				    !Options.Registry && Options.MaxOperations == GeneralDefaults.MaxOperations
				    ? O.MaxOperations
				    : std::min(Options.MaxOperations, O.MaxOperations);
			}

			if (O.MaxSourceBytes != Defaults.MaxSourceBytes)
			{
				Options.MaxSourceBytes =
				    !Options.Registry && Options.MaxSourceBytes == GeneralDefaults.MaxSourceBytes
				    ? O.MaxSourceBytes
				    : std::min(Options.MaxSourceBytes, O.MaxSourceBytes);
			}

			return Session.ReplaceRegistry(std::move(Options), Error);
		}
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		if (Session.Options.Registry)
		{
			for (const auto& D : Session.Options.Registry->GetProcessors())
			{
				if (!Registry->Register(D, Error)) return false;
			}
		}

		for (const auto& D : O.Registry->GetProcessors())
		{
			bool CaseSensitive = true;
			for (const auto& Definition : Session.Options.AnnotationDefinitions)
			{
				if (AnnotationNamesEqual(D.Name, Definition.Name, Definition.CaseSensitive))
				{
					CaseSensitive = Definition.CaseSensitive;
					break;
				}
			}

			std::size_t Matches = 0;
			for (const auto& Other : O.Registry->GetProcessors())
			{
				if (AnnotationNamesEqual(Other.Name, D.Name, CaseSensitive) && (Other.Targets & D.Targets))
				{
					++Matches;
				}
			}

			if (Matches > O.MaxProcessorsPerAnnotation)
			{
				Error = "annotation processor match limit exceeded";
				return false;
			}
			FCompileProcessorDesc General;
			General.ProcessorId = D.ProcessorId;
			General.Version = D.Version;
			General.ConfigurationKey = D.ConfigurationKey;
			General.Order = D.Order;
			General.Cacheable = D.Cacheable;
			const bool Members = (D.Operations & 1) != 0;
			General.Phases = Members ? CompilePhaseMask(ECompileProcessingPhase::Members) : CompilePhaseMask(ECompileProcessingPhase::Bodies);
			General.Operations = (D.Operations & 1 ? 2u : 0u) | (D.Operations & 2 ? 8u : 0u);
			General.Subscriptions.push_back(
			    {ECompileProcessingEvent::Annotation, D.Targets, D.Name, "", {}, General.Phases});
			if (Members && (D.Operations & 2))
			{
				General.Phases |= CompilePhaseMask(ECompileProcessingPhase::Bodies);
				General.Subscriptions.push_back({ECompileProcessingEvent::Module, AnnotationAllTargets, "",
				    "", {}, CompilePhaseMask(ECompileProcessingPhase::Bodies)});
			}
			General.Factory = [D]
			{
				return std::make_unique<FAnnotationAdapter>(D);
			};
			if (!Registry->Register(std::move(General), Error)) return false;
		}
		Registry->Freeze();
		auto Options = Session.Options;
		const FCompileProcessingOptions GeneralDefaults;
		if (!Options.Registry && Options.MaxOperations == GeneralDefaults.MaxOperations)
		{
			Options.MaxOperations = O.MaxOperations;
		}

		if (!Options.Registry && Options.MaxSourceBytes == GeneralDefaults.MaxSourceBytes)
		{
			Options.MaxSourceBytes = O.MaxSourceBytes;
		}

		Options.Registry = Registry;
		Options.MaxOperations = std::min(Options.MaxOperations, O.MaxOperations);
		Options.MaxSourceBytes = std::min(Options.MaxSourceBytes, O.MaxSourceBytes);
		if (!Options.ReadDependency)
		{
			Options.ReadDependency = O.ReadDependency;
		}

		const auto PreviousCheck = Options.CheckCancelled;
		Options.CheckCancelled = [PreviousCheck, Check = O.CheckCancelled]
		{
			if (PreviousCheck)
			{
				PreviousCheck();
			}
			if (Check)
			{
				Check();
			}
		};
		// Structural preparation precedes the first invocation of this adapter.
		// Keep the source processor instances that have already accumulated state.
		return Session.ReplaceRegistry(std::move(Options), Error);
	}
}
