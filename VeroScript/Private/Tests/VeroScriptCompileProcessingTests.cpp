// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptAst.h"
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptPackage.h"
#include "VeroScriptHostBindingCatalog.h"
#include "../Processing/VeroScriptDeclarationRangeIndex.h"
#include "../Processing/VeroScriptSourceMetadataIO.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>

using namespace VeroScript;
namespace
{
	int Failures = 0;
	void Expect(bool V, const std::string& Name)
	{
		if (!V)
		{
			++Failures;
			std::cerr << "FAIL: " << Name << '\n';
		}
	}

	void TestDeclarationRangeIndex()
	{
		std::vector<FSourceRange> Ranges;
		FDeclarationRangeIndex Index;
		for (std::size_t I = 0; I < 128; ++I)
		{
			FSourceRange Range;
			Range.SourceId = I % 3 == 0 ? "first" : "second";
			Range.Begin = (I * 19) % 80;
			Range.End = Range.Begin + (I * 7) % 40;
			Index.Add(Range, Ranges.size());
			Ranges.push_back(Range);
			if (I % 11 == 0)
			{
				Index.Add(Range, Ranges.size());
				Ranges.push_back(Range);
			}
		}
		Index.Build();
		bool Equal = true;
		for (const auto* Document : {"first", "second", "missing"})
		{
			for (std::size_t Begin = 0; Begin < 120; ++Begin)
			{
				for (std::size_t End = Begin; End < 120; ++End)
				{
					std::optional<std::size_t> Reference;
					std::size_t Width = std::numeric_limits<std::size_t>::max();
					for (std::size_t I = 0; I < Ranges.size(); ++I)
					{
						const auto& Range = Ranges[I];
						if (Range.SourceId == Document && Range.Begin <= Begin && Range.End >= End &&
						    Range.End - Range.Begin < Width)
						{
							Reference = I;
							Width = Range.End - Range.Begin;
						}
					}
					FSourceRange Query;
					Query.SourceId = Document;
					Query.Begin = Begin;
					Query.End = End;
					Equal = Equal && Index.FindContaining(Query) == Reference;
				}
			}
		}
		Expect(Equal, "declaration range index preserves containment and source-order ties");
		for (int Stage = 0; Stage < 3; ++Stage)
		{
			bool Cancelled = false;
			FAnalysisControlScope Control([]()
			{
				return true;
			});
			try
			{
				if (Stage == 0)
				{
					Index.Add(Ranges.front(), 0);
				}
				else if (Stage == 1)
				{
					Index.Build();
				}
				else
				{
					(void)Index.FindContaining(Ranges.front());
				}
			}
			catch (const FAnalysisCancelled&)
			{
				Cancelled = true;
			}
			Expect(Cancelled, "declaration range index respects analysis cancellation");
		}
	}

	struct FMetadataSnapshotObserver : ICompileProcessor
	{
		std::function<void(const FCompileProcessingContext&)> Observe;
		void Process(const FCompileProcessingContext& Context, FCompileProcessingOutput&) override
		{
			Observe(Context);
		}
	};

	void TestDocumentlessMetadataOwners()
	{
		const std::string Source =
		    "int First(int value=11){\n"
		    "return value+1;\n"
		    "}\n"
		    "int Second(int value=21){\n"
		    "int local=value+2;\n"
		    "return local;\n"
		    "}\n"
		    "class Alpha{public:\n"
		    "int alphaValue=31;\n"
		    "Alpha(){}\n"
		    "int ReadAlpha(int extra=3){\n"
		    "return alphaValue+extra;\n"
		    "}\n"
		    "};\n"
		    "struct Beta{\n"
		    "int betaValue=41;\n"
		    "int ReadBeta(int extra=4){\n"
		    "return betaValue+extra;\n"
		    "}\n"
		    "};\n"
		    "void main(){Alpha a=Alpha();Beta b=Beta();print(First());print(Second());"
		    "print(a.ReadAlpha());print(b.ReadBeta());}\n";
		const std::string Label = "metadata-owners.vs";
		const auto Parse = [&](const bool WithSource)
		{
			VeroScriptLexer Lexer(Source, Label);
			const auto Tokens = Lexer.Tokenize();
			VeroScriptParser Parser(Tokens, Label);
			if (WithSource)
			{
				Parser.SetSourceText(Source);
			}
			auto Program = Parser.ParseProgram();
			Expect(Program && !Parser.HasError(), "parse direct metadata owner fixture");
			if (Program && WithSource)
			{
				auto Metadata = std::make_shared<FSourceMetadataSnapshot>();
				FSourceDocument Document;
				Document.Id = Document.ModuleId = Label;
				Document.Text = Source;
				Metadata->Documents.push_back(std::move(Document));
				Program->SourceMetadata = std::move(Metadata);
			}
			return Program;
		};
		const auto Signature = [](const FSourceMetadataSnapshot& Metadata)
		{
			std::vector<std::string> Result;
			for (const auto& Node : Metadata.Nodes)
			{
				Result.push_back(Node.Id + "|" + Node.OwnerId + "|" + Node.ParentId + "|" + Node.ModuleId + "|" + Node.Range.SourceId + "|" + std::to_string(Node.Range.Begin) + "|" + std::to_string(Node.Range.End));
			}
			std::sort(Result.begin(), Result.end());
			return Result;
		};
		const auto Observe = [&](std::function<void(const FCompileProcessingContext&)> Callback)
		{
			auto Registry = std::make_shared<FCompileProcessorRegistry>();
			FCompileProcessorDesc Description;
			Description.ProcessorId = "test.metadata-owner-observer";
			Description.Version = "1";
			Description.Phases = CompilePhaseMask(ECompileProcessingPhase::Bodies);
			Description.Subscriptions.push_back({ECompileProcessingEvent::Module});
			Description.Factory = [Callback = std::move(Callback)]
			{
				auto Observer = std::make_unique<FMetadataSnapshotObserver>();
				Observer->Observe = Callback;
				return Observer;
			};
			std::string Error;
			Expect(Registry->Register(std::move(Description), Error), Error);
			Registry->Freeze();
			FCompileProcessingOptions Options;
			Options.Registry = std::move(Registry);
			return Options;
		};
		const auto CheckOwners = [&](const FProgram& Program, const FSourceMetadataSnapshot& Metadata, const bool WithSource)
		{
			const auto CheckNode = [&](const FDeclMeta& Syntax, const std::string& Owner)
			{
				const auto* Node = Metadata.FindNode(Syntax.StableId);
				Expect(Node && Node->OwnerId == Owner, "syntax metadata retains its structural declaration owner");
				if (Node && !WithSource)
				{
					Expect(Node->Range.Begin == 0 && Node->Range.End == 0,
					    "documentless offsets remain unknown instead of fabricated");
				}
			};
			std::unordered_map<std::uint32_t, std::string> LineOwners;
			const auto Callable = [&](const auto& Declaration)
			{
				if (Declaration.Body)
				{
					CheckNode(Declaration.Body->DeclMeta, Declaration.DeclMeta.StableId);
				}
				for (const auto& Parameter : Declaration.Parameters)
				{
					if (Parameter.DefaultValue)
					{
						CheckNode(Parameter.DefaultValue->DeclMeta, Parameter.DeclMeta.StableId);
					}
				}
			};
			for (const auto& Function : Program.Functions)
			{
				Callable(Function);
				if (Function.Name == "First")
				{
					LineOwners.emplace(2, Function.DeclMeta.StableId);
				}
				if (Function.Name == "Second")
				{
					LineOwners.emplace(5, Function.DeclMeta.StableId);
					LineOwners.emplace(6, Function.DeclMeta.StableId);
				}
			}
			const auto Types = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					for (const auto& Field : Declaration.Fields)
					{
						if (Field.DefaultValue)
						{
							CheckNode(Field.DefaultValue->DeclMeta, Field.DeclMeta.StableId);
						}
					}
					for (const auto& Method : Declaration.Methods)
					{
						Callable(Method);
						if (Method.Name == "ReadAlpha")
						{
							LineOwners.emplace(12, Method.DeclMeta.StableId);
						}
						if (Method.Name == "ReadBeta")
						{
							LineOwners.emplace(18, Method.DeclMeta.StableId);
						}
					}
					for (const auto& Constructor : Declaration.Constructors)
					{
						Callable(Constructor);
					}
				}
			};
			Types(Program.Classes);
			Types(Program.Structs);
			std::size_t Checked = 0;
			for (const auto& Node : Metadata.Nodes)
			{
				if (Node.Kind == "declaration" || Node.Kind == "module")
				{
					continue;
				}
				if (const auto Owner = LineOwners.find(Node.Range.Position.Begin.Line); Owner != LineOwners.end())
				{
					++Checked;
					Expect(Node.OwnerId == Owner->second, "nested syntax metadata belongs to the correct body");
					if (const auto* Parent = Metadata.FindNode(Node.ParentId))
					{
						Expect(Parent->Id == Owner->second || Parent->OwnerId == Owner->second,
						    "syntax parent stays inside the corresponding declaration");
					}
				}
			}
			Expect(Checked >= 5, "documentless owner fixture observes all separate callable bodies");
		};
		for (const bool WithSource : {false, true})
		{
			auto Program = Parse(WithSource);
			if (!Program)
			{
				continue;
			}
			std::size_t Observations = 0;
			auto Processing = Observe([&](const FCompileProcessingContext& Context)
			{
				++Observations;
				Expect(Context.Program != nullptr, "body observer receives the owned program");
				if (Context.Program)
				{
					CheckOwners(*Context.Program, Context.SourceMetadata, WithSource);
				}
			});
			auto Prepared = VeroScriptProgramPreparation::PrepareWithProcessing(std::move(Program), Processing);
			Expect(Prepared.Succeeded() && Prepared.Diagnostics.empty(), "prepare direct parser metadata fixture");
			Expect(Observations > 0, "observe direct parser metadata before final retention trimming");
			if (!Prepared.Succeeded())
			{
				continue;
			}
			Program = Parse(WithSource);
			if (!Program)
			{
				continue;
			}
			VeroScriptCompiler Compiler;
			auto First = Compiler.Compile(*Program);
			Expect(bool(First), "compile direct parser metadata fixture: " + Compiler.GetError().Message);
			auto Second = Compiler.Compile(*Program);
			Expect(bool(Second), "repeat direct parser compilation: " + Compiler.GetError().Message);
			if (First && Second)
			{
				Expect(First->SourceMetadata && Second->SourceMetadata && Signature(*First->SourceMetadata) == Signature(*Second->SourceMetadata),
				    "repeat compilation keeps metadata identity and ownership stable");
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Expect(Vm.Execute(*First, ERunMode::Script) && Output.str() == "12\n23\n34\n45\n",
				    "direct parser metadata fixture retains execution behavior");
			}
		}
		std::size_t FullObservations = 0;
		FCompileOptions FullOptions = VeroScript::Test::MakeCompileOptions();
		FullOptions.CompileProcessing = Observe([&](const FCompileProcessingContext& Context)
		{
			++FullObservations;
			if (Context.Program)
			{
				CheckOwners(*Context.Program, Context.SourceMetadata, true);
			}
		});
		const auto Full = VeroScriptCompile::CompileSource(Source, Label, FullOptions);
		Expect(Full.bSuccess, "full source metadata fixture: " + Full.Error.Message);
		Expect(FullObservations > 0, "observe full source metadata before final retention trimming");
		{
			const std::string Text = "int Anchor(){return 1;}2+3;";
			VeroScriptLexer Lexer(Text, Label);
			const auto Tokens = Lexer.Tokenize();
			VeroScriptParser Parser(Tokens, Label);
			auto Program = Parser.ParseProgram();
			Expect(Program && !Parser.HasError(), "parse unowned documentless syntax fixture");
			if (Program)
			{
				std::size_t Observations = 0;
				auto Processing = Observe([&](const FCompileProcessingContext& Context)
				{
					++Observations;
					if (!Context.Program) return;
					const auto& P = *Context.Program;
					Expect(!P.TopLevelStatements.empty(), "unowned syntax fixture contains a top-level statement");
					if (!P.TopLevelStatements.empty())
					{
						const auto* Statement = Context.SourceMetadata.FindNode(
						    P.TopLevelStatements.front()->DeclMeta.StableId);
						Expect(Statement && (Statement->OwnerId.empty() || Statement->OwnerId.rfind("module:", 0) == 0),
						    "unknown documentless owner never guesses an unrelated declaration");
					}
				});
				auto Prepared = VeroScriptProgramPreparation::PrepareWithProcessing(std::move(Program), Processing);
				Expect(Prepared.Succeeded(), "prepare unowned documentless syntax fixture");
				Expect(Observations > 0, "observe unowned syntax before final trimming");
			}
		}
		{
			const std::string Text = "int global=3;void main(){}";
			VeroScriptLexer Lexer(Text, "");
			const auto Tokens = Lexer.Tokenize();
			VeroScriptParser Parser(Tokens, "");
			auto Program = Parser.ParseProgram();
			Expect(Program && !Parser.HasError(), "parse documentless global with empty module label");
			if (Program)
			{
				auto Prepared = VeroScriptProgramPreparation::Prepare(std::move(Program));
				Expect(Prepared.Succeeded(), "prepare documentless global with empty module label");
				if (Prepared.Succeeded())
				{
					const auto& P = Prepared.Prepared->GetProgram();
					Expect(!P.TopLevelStatements.empty(), "empty-label fixture retains its global");
					if (!P.TopLevelStatements.empty())
					{
						const auto* Global = Prepared.Prepared->GetSourceMetadata().FindNode(
						    P.TopLevelStatements.front()->DeclMeta.StableId);
						Expect(Global && Global->Kind == "declaration" && Global->OwnerId != Global->Id && Global->ParentId != Global->Id,
						    "global declaration is not overwritten as its own syntax child");
					}
				}
			}
		}
		// A generated point is an origin anchor, not a declaration containment
		// interval. Preserve it while deriving ownership from the owned method body.
		{
			auto Program = Parse(true);
			if (!Program || Program->Functions.empty() || !Program->Functions.front().Body) return;
			auto& Anchor = Program->Functions.front().Body->SourceRange;
			Anchor.SourceId = "veroscript-generated:point-anchor";
			Anchor.Begin = Anchor.End = 0;
			Anchor.Position.Begin.FileLabel = Anchor.Position.End.FileLabel = Anchor.SourceId;
			const auto Position = Anchor.Position;
			std::size_t Observations = 0;
			auto Processing = Observe([&](const FCompileProcessingContext& Context)
			{
				++Observations;
				if (!Context.Program) return;
				const auto& Function = Context.Program->Functions.front();
				const auto* Body = Context.SourceMetadata.FindNode(Function.Body->DeclMeta.StableId);
				Expect(Body && Body->OwnerId == Function.DeclMeta.StableId && Body->ParentId == Function.DeclMeta.StableId && Body->Range.SourceId == "veroscript-generated:point-anchor" && Body->Range.Begin == 0 && Body->Range.End == 0 && Body->Range.Position.Begin.Line == Position.Begin.Line && Body->Range.Position.Begin.Column == Position.Begin.Column,
				    "generated point anchor retains location and structural owner");
			});
			auto Prepared = VeroScriptProgramPreparation::PrepareWithProcessing(std::move(Program), Processing);
			Expect(Prepared.Succeeded(), "prepare generated point-anchor fixture");
			Expect(Observations > 0, "observe generated point anchor before final trimming");
		}
		// Scale semantics and diagnostic count without a timing assertion or
		// production instrumentation. The source guards prove no empty-interval
		// containment scan is attempted for any of these declarations.
		std::size_t PreviousNodeCount = 0;
		for (const int Count : {100, 1000})
		{
			std::string Text;
			for (int Index = 0; Index < Count; ++Index)
			{
				Text += "int F" + std::to_string(Index) + "(int value){return value;}\n";
			}
			VeroScriptLexer Lexer(Text, Label);
			const auto Tokens = Lexer.Tokenize();
			VeroScriptParser Parser(Tokens, Label);
			auto Program = Parser.ParseProgram();
			Expect(Program && !Parser.HasError(), "parse scaled documentless metadata fixture");
			if (!Program)
			{
				continue;
			}
			std::size_t Observations = 0;
			auto Processing = Observe([&](const FCompileProcessingContext& Context)
			{
				++Observations;
				if (!Context.Program) return;
				std::unordered_map<std::string, const FSourceNodeMetadata*> Nodes;
				for (const auto& Node : Context.SourceMetadata.Nodes)
				{
					Nodes.emplace(Node.Id, &Node);
				}
				for (const auto& Function : Context.Program->Functions)
				{
					const auto Body = Nodes.find(Function.Body->DeclMeta.StableId);
					Expect(Body != Nodes.end() && Body->second->OwnerId == Function.DeclMeta.StableId,
					    "scaled documentless bodies keep distinct owners");
				}
				const auto Size = Context.SourceMetadata.Nodes.size();
				Expect(!PreviousNodeCount || Size <= PreviousNodeCount * 10 + 10,
				    "documentless metadata output grows with declarations");
				PreviousNodeCount = Size;
			});
			auto Prepared = VeroScriptProgramPreparation::PrepareWithProcessing(std::move(Program), Processing);
			Expect(Prepared.Succeeded() && Prepared.Diagnostics.empty(), "scaled documentless metadata has zero diagnostics");
			Expect(Observations > 0, "observe scaled metadata before final trimming");
		}
	}

	void TestImmediateSourceOrigins()
	{
		FSourceMetadataSnapshot Snapshot;
		for (const auto* Name : {"generated", "input", "raw"})
		{
			FSourceDocument Document;
			Document.Id = Document.ModuleId = Name;
			Document.LineOffsets = {0, 2, 6, 9};
			Snapshot.Documents.push_back(std::move(Document));
		}
		FSourceOrigin Origin;
		Origin.Range.SourceId = "input";
		Origin.Range.End = 10;
		Snapshot.Documents[0].Mappings.push_back({10, 20, Origin});
		Origin.Range.SourceId = "raw";
		Snapshot.Documents[1].Mappings.push_back({0, 10, Origin});
		FSourceRange Query;
		Query.SourceId = "generated";
		Query.Begin = 12;
		Query.End = 15;
		FSourceOrigin First;
		const auto Full = ResolveSourceOrigins(Snapshot, Query);
		Expect(TryResolveFirstSourceOrigin(Snapshot, Query, First) && Full.size() == 2 &&
		        First.Range.SourceId == "input" && First.Range.Begin == 2 && First.Range.End == 5 &&
		        First.Range.Position.Begin.Line == 2 && First.Range.Position.Begin.Column == 1 &&
		        First.Range.Position.End.Line == 2 && First.Range.Position.End.Column == 4 &&
		        Full.back().Range.SourceId == "raw" && Full.back().Range.Begin == 2,
		    "immediate source origin retains affine offsets and positions");
		Snapshot.Documents[0].Mappings[0].Origin.GenerationId = "processor-generation";
		Expect(TryResolveFirstSourceOrigin(Snapshot, Query, First) && First.Range.Begin == 0 &&
		        First.Range.End == 10 && First.GenerationId == "processor-generation",
		    "generated origin retains the producer's coarse source range");
		Query.Begin = Query.End = 20;
		First.Range.SourceId = "unchanged";
		Expect(!TryResolveFirstSourceOrigin(Snapshot, Query, First) && First.Range.SourceId == "unchanged",
		    "missing source mapping leaves the destination unchanged");
		Query.Begin = 12;
		Query.End = 11;
		Expect(!TryResolveFirstSourceOrigin(Snapshot, Query, First) && First.Range.SourceId == "unchanged" &&
		        ResolveSourceOrigins(Snapshot, Query).empty(),
		    "inverted source query is rejected");
		Query.End = 15;
		Snapshot.Documents[0].Mappings[0].Origin.Range.Begin = 11;
		Expect(!TryResolveFirstSourceOrigin(Snapshot, Query, First) && First.Range.SourceId == "unchanged" &&
		        ResolveSourceOrigins(Snapshot, Query).empty(),
		    "inverted source origin is rejected");
		Snapshot.Documents[0].Mappings[0].Origin.Range.Begin = 0;
		Snapshot.Documents[1].LineOffsets.front() = 1;
		Expect(!TryResolveFirstSourceOrigin(Snapshot, Query, First) && First.Range.SourceId == "unchanged" &&
		        ResolveSourceOrigins(Snapshot, Query).empty(),
		    "invalid source line origin cannot underflow the line index");
	}
	struct FGenerator : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase == ECompileProcessingPhase::Types)
			{
				O.AddDeclaration(
				    {C.ModuleId, "", "", C.Snapshot}, "class Generated { public: int value = 42; };");
			}

			else if (C.Phase == ECompileProcessingPhase::Members)
			{
				for (const auto& D : C.Declarations)
				{
					if (D.Name == "Original" && D.Target == EAnnotationTarget::Class)
					{
						O.AddDeclaration({C.ModuleId, "", D.Id, C.Snapshot},
						    "public: static Generated Create(){return Generated();}");
					}
				}
			}
			else if (C.Phase == ECompileProcessingPhase::Bodies)
			{
				for (const auto& D : C.Declarations)
				{
					if (D.Name == "Missing" && !D.bHasBody)
					{
						O.CompleteBody(D.Id, C.Snapshot, "{return 13;}");
					}
				}
			}
		}
	};
	struct FMemberAppendGenerator : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase != ECompileProcessingPhase::Members) return;
			for (const auto& D : C.Declarations)
			{
				std::vector<std::string> Sources;
				if (D.Target == EAnnotationTarget::Class && D.Name == "AppendAlpha")
				{
					Sources = {"public:int Offset=1;", "public:AppendAlpha(int seed){Seed=seed;}",
					    "public:virtual int Read(){return Seed+Offset;}"};
				}
				else if (D.Target == EAnnotationTarget::Struct && D.Name == "AppendBox")
				{
					Sources = {"T Seed;", "int Offset=1;", "T Read(){return Seed+Offset;}"};
				}
				else if (D.Target == EAnnotationTarget::Interface && D.Name == "AppendContract")
				{
					Sources = {"virtual int Read()=0;"};
				}
				for (auto& Source : Sources)
				{
					O.AddDeclaration({C.ModuleId, "", D.Id, C.Snapshot}, std::move(Source));
				}
			}
		}
	};
	struct FInterleavedAppendGenerator : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase != ECompileProcessingPhase::Members) return;
			std::string ClassId, StructId, InterfaceId;
			for (const auto& D : C.Declarations)
			{
				if (D.Target == EAnnotationTarget::Class && D.Name == "MixedAlpha")
				{
					ClassId = D.Id;
				}
				else if (D.Target == EAnnotationTarget::Struct && D.Name == "MixedBox")
				{
					StructId = D.Id;
				}
				else if (D.Target == EAnnotationTarget::Interface && D.Name == "MixedContract")
				{
					InterfaceId = D.Id;
				}
			}
			Expect(!ClassId.empty() && !StructId.empty() && !InterfaceId.empty(),
			    "mixed append processor receives all original type owners");
			for (int I = 0; I < 32; ++I)
			{
				const auto Suffix = std::to_string(I);
				// Interleave namespace helpers, local types, and members of each original owner kind.
				O.AddDeclaration({C.ModuleId, "MixedHelpers", "", C.Snapshot},
				    "int Helper" + Suffix + "(){interface LocalContract{virtual int Read()=0;};"
				                            "class LocalValue:LocalContract{public:virtual int Read(){return " +
				        Suffix + ";}};"
				                 "struct LocalBox{int Offset=1;};LocalValue value=LocalValue();"
				                 "LocalContract contract=value;LocalBox box=LocalBox();return contract.Read()+box.Offset;}");
				O.AddDeclaration({C.ModuleId, "", ClassId, C.Snapshot},
				    "public:virtual int Read" + Suffix + "(){return Seed+MixedHelpers::Helper" + Suffix + "();}");
				O.AddDeclaration({C.ModuleId, "", StructId, C.Snapshot},
				    "T Read" + Suffix + "(){return Seed+MixedHelpers::Helper" + Suffix + "();}");
				O.AddDeclaration({C.ModuleId, "", InterfaceId, C.Snapshot},
				    "virtual int Read" + Suffix + "()=0;");
			}
		}
	};
	struct FTemplateGenerator : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase == ECompileProcessingPhase::Types)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot}, "struct Generated{int value=9;};");
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot},
				    "template<typename T,int N> struct GeneratedBuffer{T values[N];};");
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot}, "enum class GeneratedPolicy{Ready=3};");
				for (const auto& D : C.Declarations)
				{
					if (D.Name == "Source" && D.Target == EAnnotationTarget::Class)
					{
						O.AddDeclaration({C.ModuleId, "", D.Id, C.Snapshot}, "public:struct Inner{T data[N];};");
					}
				}
			}
			else if (C.Phase == ECompileProcessingPhase::Members)
			{
				for (const auto& D : C.Declarations)
				{
					if (D.Name == "Source" && D.Target == EAnnotationTarget::Class)
					{
						Expect(D.TemplateParameters.size() == 2 && D.TemplateParameters[1].bValue, "processor sees mixed owner schema");

						O.AddDeclaration({C.ModuleId, "", D.Id, C.Snapshot},
						    "public:template<typename U,int M=N> U echo(U value){GeneratedBuffer<U,M> data=GeneratedBuffer<U,M>();data.values[0]=value;return data.values[0];}");
					}
					if (D.Name == "original")
					{
						Expect(D.TemplateParameters.size() == 3 && D.TemplateParameters[0].bEnclosing && D.TemplateParameters[1].bValue && !D.TemplateParameters[2].bEnclosing, "method view includes enclosing and own parameters");
					}
				}
			}
		}
	};
	struct FSourceConverter : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase == ECompileProcessingPhase::RawSource)
			{
				++SourceCalls;
				const auto P = C.Source->Text.find("CUSTOM");
				if (P != std::string::npos)
				{
					FSourceRange R;
					R.SourceId = C.Source->Id;
					R.Begin = P;
					R.End = P + 6;
					O.EditSource(R, "class");
				}
			}
			else
			{
				if (SourceCalls != 1)
				{
					throw std::runtime_error("source instance state lost");
				}

				for (const auto& D : C.Declarations)
				{
					if (D.Name == "Pending" && !D.bHasBody)
					{
						O.CompleteBody(D.Id, C.Snapshot, "{return 9;}");
					}
				}
			}
		}

		std::size_t SourceCalls = 0;
	};
	struct FReplace : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			for (const auto& D : C.Declarations)
			{
				if (D.Name == "Compute" && D.Target == EAnnotationTarget::Function)
				{
					O.ReplaceBody(D.Id, C.Snapshot, Body);
				}
			}
		}

		std::string Body = "{return 81;}";
	};
	struct FReplaceBodyComments : FReplace
	{

		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			for (const auto& D : C.Declarations)
			{
				if (D.Name == "Compute" && D.bHasBody &&
				    (D.Target == EAnnotationTarget::Function || D.Target == EAnnotationTarget::Method))
				{
					O.ReplaceBody(D.Id, C.Snapshot, Body);
				}
			}
		}

		FReplaceBodyComments()
		{
			Body = "{ // new body\nreturn 81;}";
		}
	};
	struct FInvalidOperation : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			FCompileGenerationOperation Op;
			if (C.Phase == ECompileProcessingPhase::RawSource)
			{
				Op.Kind = static_cast<ECompileOperation>(3);
				Op.EditRange.SourceId = C.Source->Id;
				Op.Source = "// invalid combined operation\n";
			}
			else
			{
				Op.Kind = static_cast<ECompileOperation>(6);
				for (const auto& D : C.Declarations)
				{
					if (D.Name == "Compute")
					{
						Op.TargetId = D.Id;
					}
				}

				Op.Snapshot = C.Snapshot;
				Op.Source = "int Compute(){return 99;}";
			}
			O.SubmitOperation(std::move(Op));
		}
	};
	struct FCommentProcessor : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (Calls)
			{
				++(*Calls)[static_cast<std::size_t>(C.Phase)];
			}

			if (Generate)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot}, "int GeneratedComment(){return 7;}");
			}
		}

		std::shared_ptr<std::array<std::size_t, 5>> Calls;

		bool Generate = false;
	};
	struct FGenericGenerator : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase == ECompileProcessingPhase::Types)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot},
				    "export class Generated<T>{public:T value;T get(){return value;}};");
			}

			else
			{
				for (const auto& D : C.Declarations)
				{
					if (D.ModulePath == C.ModuleId && D.Name == "Build")
					{
						for (const auto& N : C.SourceMetadata.Nodes)
						{
							if (N.OwnerId == D.Id &&
							    N.Kind ==
							        "statement:" + std::to_string(static_cast<unsigned>(EStmtKind::Return)))
							{
								FCompileMetadata M;
								FDeclarationAnnotation A;
								A.Name = "@PackagedBody";
								M.Annotations = std::vector<FDeclarationAnnotation>{A};
								O.SetMetadata(N.Id, C.Snapshot, std::move(M));
							}
						}
					}
				}
			}
		}
	};
	struct FLineMapper : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			FSourceRange R;
			R.SourceId = C.Source->Id;
			O.EditSource(R, "\n// inserted\n");
		}
	};
	struct FClearComments : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			for (const auto& D : C.Declarations)
			{
				if (D.Name == "Commented" && D.Target == EAnnotationTarget::Class)
				{
					FCompileMetadata M;
					M.Comments = std::vector<FSourceComment>{};
					O.SetMetadata(D.Id, C.Snapshot, std::move(M));
				}
			}
		}
	};
	struct FReplaceType : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			for (const auto& D : C.Declarations)
			{
				if (D.Name == "Replacement" && D.Target == EAnnotationTarget::Struct)
				{
					O.ReplaceDeclaration(D.Id, C.Snapshot, Source);
				}
			}
		}

		std::string Source = "struct Replacement {int value=19;};";
	};
	struct FReplaceDocumentedType : FReplaceType
	{
		FReplaceDocumentedType()
		{
			Source = "/// Replacement docs\n@Fresh(mark=10) struct Replacement {int value=19;};";
		}
	};
	struct FReplaceFullGenericType : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			for (const auto& D : C.Declarations)
			{
				if (D.Name == "Work" && D.Target == EAnnotationTarget::Class)
				{
					O.ReplaceDeclaration(D.Id, C.Snapshot,
					    "export class Work<T>{public:T value;int New(int arg){ // new body\nreturn arg+1;}};");
				}
			}
		}
	};
	struct FAdditionWithTarget : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			for (const auto& D : C.Declarations)
			{
				if (D.Name == "Work" && D.Target == EAnnotationTarget::Class)
				{
					FCompileGenerationOperation Op;
					Op.Kind = ECompileOperation::AddDeclaration;
					Op.TargetId = D.Id;
					Op.Source = "export class Work<T>{public:T value;};";
					O.SubmitOperation(std::move(Op));
				}
			}
		}
	};
	struct FGeneratedComments : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase == ECompileProcessingPhase::Types)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot},
				    "/// Generated type docs\nstruct GeneratedValue{int value;};");
			}

			else
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot},
				    "/// Generated function docs\nint GeneratedRead(){ // Body note\nreturn 5;}");
			}
		}
	};
	struct FDocumentedGeneric : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase == ECompileProcessingPhase::Types)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot},
				    "/// Generated generic docs\nexport class Generated<T>{public:T value;};");
			}

			else
			{
				for (const auto& D : C.Declarations)
				{
					if (D.Name == "Generated" && D.Target == EAnnotationTarget::Class)
					{
						O.AddDeclaration({C.ModuleId, "", D.Id, C.Snapshot},
						    "/// Generated method docs\npublic:T get(){return value;}");
					}
				}
			}
		}
	};
	struct FReplaceOtherDeclarations : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			for (const auto& D : C.Declarations)
			{
				std::string Source;
				if (D.Target == EAnnotationTarget::Field && D.Name == "field")
				{
					Source = "public:int field=11;";
				}

				else if (D.Target == EAnnotationTarget::Method && D.Name == "member")
				{
					Source = "/// Replacement method docs\npublic:int member(){ // new body\nreturn 12;}";
				}

				else if (D.Target == EAnnotationTarget::Destructor)
				{
					Source = "~Subject(){}";
				}

				else if (D.Target == EAnnotationTarget::Function && D.Name == "Free")
				{
					Source = "int Free(){return 14;}";
				}

				else if (D.Target == EAnnotationTarget::GlobalVariable && D.Name == "global")
				{
					Source = "int global=15;";
				}

				if (!Source.empty())
				{
					O.ReplaceDeclaration(D.Id, C.Snapshot, std::move(Source));
				}
			}
		}
	};
	struct FMemberGenerator : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			O.AddDeclaration({C.ModuleId, "", "", C.Snapshot}, "int GeneratedFunction(){return 31;}");
		}
	};
	struct FChildMetadata : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			for (const auto& D : C.Declarations)
			{
				if (D.Name == "Compute" && D.Target == EAnnotationTarget::Function)
				{
					FCompileMetadata M;
					FCompileMetadata::FChild Child;
					Child.Begin = 1;
					Child.End = 10;
					Child.Kind = "statement:" + std::to_string(static_cast<unsigned>(EStmtKind::Return));
					FDeclarationAnnotation Annotation;
					Annotation.Name = "@GeneratedReturn";
					Child.Metadata.Annotations.push_back(Annotation);
					M.Children.push_back(std::move(Child));
					O.ReplaceBody(D.Id, C.Snapshot, "{return 7;}", std::move(M));
				}
			}
		}
	};
	struct FCompleteTypes : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			const FCompileScope Scope{C.ModuleId, "GeneratedNS", "", C.Snapshot};
			O.AddDeclaration(Scope, "interface Readable {virtual int read()=0;};");
			O.AddDeclaration(Scope, "enum Choice { First=2, Second=5 };");
			O.AddDeclaration(Scope, "using Number=int;");
			O.AddDeclaration(Scope, "class Left {public: Right make(){return Right();}};");
			const std::string Source = "class Right:public Readable {public:int read(){return 7;}};";
			FCompileMetadata M;
			FCompileMetadata::FChild Child;
			Child.Begin = Source.find('7');
			Child.End = Child.Begin + 1;
			Child.Kind = "expression:" + std::to_string(static_cast<unsigned>(EExprKind::IntegerLiteral));
			FDeclarationAnnotation A;
			A.Name = "@Literal";
			Child.Metadata.Annotations.push_back(A);
			M.Children.push_back(std::move(Child));
			O.AddDeclaration(Scope, Source, std::move(M));
		}
	};
	struct FVirtualHelpers : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase == ECompileProcessingPhase::Members)
			{
				for (const auto& D : C.Declarations)
				{
					if (D.ModulePath == C.ModuleId && D.Name == "Base" &&
					    D.Target == EAnnotationTarget::Class)
					{
						O.AddDeclaration({C.ModuleId, "", D.Id, C.Snapshot}, "public:virtual int Helper();");
					}
				}
			}
			else
			{
				for (const auto& D : C.Declarations)
				{
					if (D.ModulePath == C.ModuleId && D.Name == "Helper" && !D.bHasBody)
					{
						O.CompleteBody(D.Id, C.Snapshot, "{return 3;}");
					}
				}
			}
		}
	};
	struct FActiveInvalidImport : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			FSourceRange R;
			R.SourceId = C.Source->Id;
			O.EditSource(R, "import \"unexpected.vs\";");
		}
	};
	struct FReadDependency : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput&) override
		{
			std::string Content;
			C.ReadDependency("settings.txt", Content);
		}
	};
	struct FSourceProbe : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput&) override
		{
			const auto* Raw = C.SourceMetadata.FindDocument(C.ModuleId + ":raw");
			Expect(Raw != nullptr, "original source remains queryable during processing");
			if (Raw)
			{
				for (const auto& Comment : Raw->Comments)
				{
					if (Comment.Text.find("inactive") != std::string::npos)
					{
						Expect(Comment.Activity == ESourceActivity::Inactive,
						    "inactive branch comment is classified");
					}

					if (Comment.Text.find("active") != std::string::npos &&
					    Comment.Text.find("inactive") == std::string::npos)
					{
						Expect(Comment.Activity == ESourceActivity::Active,
						    "active branch comment is classified");
					}
				}
			}

			for (const auto& D : C.Declarations)
			{
				if (D.Name == "Visible")
				{
					const auto Origins = ResolveSourceOrigins(C.SourceMetadata, D.Metadata.SourceRange);
					Expect(!Origins.empty() && Origins.back().Range.SourceId == C.ModuleId + ":raw",
					    "source mapping reaches original owned document");
				}
			}
		}
	};
	struct FModifierProbe : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput&) override
		{
			for (const auto& D : C.Declarations)
			{
				if (D.Name == "Locked")
				{
					Expect(D.bFinal, "final type modifier is retained in declaration views");
				}

				if (D.Target == EAnnotationTarget::Destructor)
				{
					Expect(D.bVirtual, "virtual destructor modifier is retained");
				}

				if (D.Target == EAnnotationTarget::Parameter && D.Name == "value")
				{
					Expect(D.bIsConst && D.bIsRef, "parameter declaration views retain const and ref");
				}

				if (D.Target == EAnnotationTarget::Parameter && D.Name == "value")
				{
					Expect(!D.Location.FileLabel.empty() &&
					        D.Location.FileLabel == D.Metadata.SourceRange.Position.Begin.FileLabel &&
					        D.Location.Line == D.Metadata.SourceRange.Position.Begin.Line &&
					        D.Location.Column == D.Metadata.SourceRange.Position.Begin.Column,
					    "parameter declaration views retain their owned source position");
				}
			}
		}
	};
	struct FReferenceReturnObserver final : ICompileProcessor
	{
		void Process(const FCompileProcessingContext&, FCompileProcessingOutput&) override
		{
		}
	};
	template <class T>
	FCompileProcessingOptions Options(std::uint32_t Phases, std::uint32_t Operations)
	{
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc Desc;
		Desc.ProcessorId = "test.processor";
		Desc.Version = "1";
		Desc.Phases = Phases;
		Desc.Operations = Operations;
		Desc.Subscriptions.push_back({ECompileProcessingEvent::Module});
		Desc.Factory = []
		{
			return std::make_unique<T>();
		};
		std::string Error;
		Expect(Registry->Register(std::move(Desc), Error), Error);
		Registry->Freeze();
		FCompileProcessingOptions R;
		R.Registry = Registry;
		return R;
	}
	void Run(const std::string& Source, FCompileProcessingOptions P, const std::string& Expected, bool Ledger = true)
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = std::move(P);
		auto C = VeroScriptCompile::CompileSource(Source, "general-processing.vs", O);
		Expect(C.bSuccess, "compile: " + C.Error.Message + " at " + C.Error.Location.FileLabel + ":" + std::to_string(C.Error.Location.Line) + ":" + std::to_string(C.Error.Location.Column));
		if (!C.bSuccess) return;
		for (auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed})
		{
			std::ostringstream Out, Err;
			std::istringstream In;
			VeroScriptVm Vm(Out, In, Err);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			const bool Executed = Vm.Execute(*C.Module, ERunMode::Script);
			Expect(Executed, "execute: " + Vm.GetError().Message);
			Expect(Out.str() == Expected, "output: " + Out.str());
		}
		Expect(bool(C.PreparedProgram), "immutable prepared result");
		if (Ledger)
		{
			Expect(C.PreparedProgram && !C.PreparedProgram->GetProgram().CompileGeneratedOperations.empty(),
			    "generation ledger");
		}
	}
	struct FAnnotationNameProbe : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput&) override
		{
			++*Calls;
			Expect(C.Annotation && !C.Annotation->DefinitionId.empty(),
			    "matched annotations have a resolved identity");
			if (C.Annotation)
			{
				const auto* D = C.SourceMetadata.FindDefinition(C.Annotation->DefinitionId);
				Expect(D && D->Name == "@GuiWidget" && !D->CaseSensitive,
				    "processor uses the canonical annotation definition");
			}
		}

		explicit FAnnotationNameProbe(std::shared_ptr<unsigned> C) : Calls(std::move(C))
		{
		}

		std::shared_ptr<unsigned> Calls;
	};
	struct FGeneratedAnnotationMetadata : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			if (C.Phase == ECompileProcessingPhase::Types)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot},
				    "export class MetadataGenerated<T>{public:@gUiWiDgEt T value;};");
				return;
			}
			for (const auto& N : C.SourceMetadata.Nodes)
			{
				if (N.ModuleId == C.ModuleId &&
				    (N.Kind == "module" ||
				        N.Kind == "statement:" + std::to_string(static_cast<unsigned>(EStmtKind::Return))))
				{
					FCompileMetadata M;
					FDeclarationAnnotation A;
					A.Name = "@gUiWiDgEt";
					M.Annotations = std::vector<FDeclarationAnnotation>{A};
					O.SetMetadata(N.Id, C.Snapshot, std::move(M));
				}
			}
		}
	};
	void TestGeneratedAnnotationNamePolicy()
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FGeneratedAnnotationMetadata>(4 | 16, 2 | 32);
		FAnnotationDefinition D;
		D.Id = "test.generated-annotation";
		D.Name = "@GuiWidget";
		D.CaseSensitive = false;
		O.CompileProcessing.AnnotationDefinitions = {D};
		const std::string Declaration = "/// @file Module metadata\nexport T Build<T>(T value){return value;}";
		auto C = VeroScriptCompile::CompileSource(Declaration + "void main(){module_info info=module_info();assert(len(info.find_annotations(\"@GUIWIDGET\"))==1);MetadataGenerated<int> value=MetadataGenerated<int>();type<MetadataGenerated<int>> t=type<MetadataGenerated<int>>();field_info<MetadataGenerated<int>> f(t.qualified_name,0);assert(len(f.find_annotations(\"@GUIWIDGET\"))==1);}", "generated-annotation-names.vs", O);
		Expect(C.bSuccess, "compile generated annotation metadata: " + C.Error.Message);
		if (!C.bSuccess) return;
		auto Check = [&](const FBytecodeModule& Module, unsigned Unknown = 0)
		{
			unsigned Known = 0, Unregistered = 0;
			for (const auto& N : Module.SourceMetadata->Nodes)
			{
				for (const auto& A : N.Annotations)
				{
					if (A.DefinitionId.empty())
					{
						++Unregistered;
						Expect(!Module.SourceMetadata->MatchesAnnotationName(A, "@GUIWIDGET"),
						    "unregistered producer annotations do not acquire consumer or dependency policies");
					}
					else
					{
						++Known;
						Expect(
						    A.DefinitionId == D.Id, "generated annotations retain the producer definition");
						Expect(Module.SourceMetadata->MatchesAnnotationName(A, "@GUIWIDGET"),
						    "generated annotations follow the registered name policy");
						if (N.Kind == "module")
						{
							Expect(N.Comments.size() == 1,
							    "module annotation updates retain source comments exactly once");
						}
					}
				}
			}

			Expect(Known == 3 && Unregistered == Unknown,
			    "generated declaration, syntax and module annotations survive compilation and replay");
		};
		Check(*C.Module);
		std::ostringstream Out;
		VeroScriptVm Vm(Out);
		VeroScript::Test::Configure(Vm);
		Expect(Vm.Execute(*C.Module, ERunMode::Script), "generated metadata reflection: " + Vm.GetError().Message);
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		Expect(VeroScriptBytecodeIO::SerializeBytecode(*C.Module, 0, Bytes, Error), "serialize generated annotation policy: " + Error);
		auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), std::nullopt, Error);
		Expect(bool(Loaded), "deserialize generated annotation policy: " + Error);
		if (Loaded)
		{
			Check(*Loaded);
			std::ostringstream Output;
			VeroScriptVm Machine(Output);
			VeroScript::Test::Configure(Machine);
			Expect(Machine.Execute(*Loaded, ERunMode::Script),
			    "execute generated metadata bytecode: " + Machine.GetError().Message);
		}
		auto Library = VeroScriptCompile::CompileSource(Declaration, "generated-annotation-library.vs", O);
		Expect(Library.bSuccess, "compile generated annotation library: " + Library.Error.Message);
		if (!Library.bSuccess) return;
		FPackageBuildOptions Package;
		Package.Kind = EPackageKind::Library;
		Package.PackageName = "GeneratedMetadata";
		Package.bStripDebugInfo = true;
		const auto Dir = std::filesystem::temp_directory_path() / ("vero-generated-annotation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directory(Dir);
		const auto File = Dir / "generated.vslib", UnknownFile = Dir / "unregistered.vslib";
		auto Save = [&](const FBytecodeModule& Module, const std::filesystem::path& Path)
		{
			if (!BuildPackage(Module, Package, Bytes, Error))
			{
				Expect(false, "build generated annotation library: " + Error);
				return false;
			}
			std::ofstream Output(Path, std::ios::binary);
			Output.write(
			    reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
			return bool(Output);
		};
		if (Save(*Library.Module, File))
		{
			FHostBindingRegistry Bindings;
			Expect(Bindings.RegisterAnnotation("@GuiWidget", "", Error), Error);
			FCompileOptions ConsumerOptions = VeroScript::Test::MakeCompileOptions();
			ConsumerOptions.Bindings = &Bindings;
			auto Consumer = VeroScriptCompile::CompileSource("import \"" + File.generic_string() + "\";void main(){assert(Build<int>(7)==7);type<MetadataGenerated<int>> t=type<MetadataGenerated<int>>();field_info<MetadataGenerated<int>> f(t.qualified_name,0);assert(f.has_annotation(\"GUIWIDGET\"));}", "generated-annotation-consumer.vs", ConsumerOptions);
			Expect(Consumer.bSuccess, "compile generated metadata consumer: " + Consumer.Error.Message);
			if (Consumer.bSuccess)
			{
				Check(*Consumer.Module);
				std::ostringstream Output;
				VeroScriptVm Machine(Output);
				VeroScript::Test::Configure(Machine);
				Expect(Machine.Execute(*Consumer.Module, ERunMode::Script),
				    "execute generated metadata consumer: " + Machine.GetError().Message);
			}
			FCompileOptions UnknownOptions = VeroScript::Test::MakeCompileOptions();
			UnknownOptions.CompileProcessing = Options<FGeneratedAnnotationMetadata>(16, 32);
			auto UnknownLibrary = VeroScriptCompile::CompileSource("import \"" + File.generic_string() + "\";export T UnknownBuild<T>(T value){return value;}", "unregistered-annotation-library.vs", UnknownOptions);
			Expect(UnknownLibrary.bSuccess, "compile unregistered annotations with an annotated dependency: " + UnknownLibrary.Error.Message);
			Package.PackageName = "UnregisteredMetadata";
			if (UnknownLibrary.bSuccess && Save(*UnknownLibrary.Module, UnknownFile))
			{
				Check(*UnknownLibrary.Module, 2);
				FHostBindingRegistry ConsumerBindings;
				Expect(ConsumerBindings.RegisterAnnotation("@GuiWidget", "", Error, "", {}, AnnotationAllTargets, false), Error);
				ConsumerOptions.Bindings = &ConsumerBindings;
				auto UnknownConsumer = VeroScriptCompile::CompileSource("import \"" + UnknownFile.generic_string() + "\";void main(){assert(UnknownBuild<int>(9)==9);}", "unregistered-annotation-consumer.vs", ConsumerOptions);
				Expect(UnknownConsumer.bSuccess,
				    "compile consumer of unregistered generated annotations: " +
				        UnknownConsumer.Error.Message);
				if (UnknownConsumer.bSuccess)
				{
					Check(*UnknownConsumer.Module, 2);
					std::ostringstream Output;
					VeroScriptVm Machine(Output);
					VeroScript::Test::Configure(Machine);
					Expect(Machine.Execute(*UnknownConsumer.Module, ERunMode::Script),
					    "execute unregistered metadata consumer: " + Machine.GetError().Message);
				}
			}
		}
		std::filesystem::remove(UnknownFile);
		std::filesystem::remove(File);
		std::filesystem::remove(Dir);
	}
	void TestAnnotationNamePolicy()
	{
		std::string Error, Json;
		FHostBindingRegistry Bindings;
		Expect(Bindings.RegisterAnnotation("@GuiWidget", "Widget", Error, "Widget docs", {}, AnnotationAllTargets, false), Error);
		Expect(!Bindings.RegisterAnnotation("@guiwidget", "", Error), "insensitive registration rejects a sensitive spelling collision");
		FHostBindingRegistry Reverse;
		Expect(Reverse.RegisterAnnotation("@guiwidget", "", Error), Error);
		Expect(!Reverse.RegisterAnnotation("@GuiWidget", "", Error, "", {}, AnnotationAllTargets, false), "insensitive registration rejects an existing spelling collision");
		Expect(Reverse.RegisterAnnotation("@GuiWidget", "", Error), "distinct case-sensitive spellings remain legal");
		Expect(Bindings.RegisterAnnotation("@Strict", "", Error), Error);
		Expect(ExportBindingCatalogJson(Bindings, Json), "export annotation name policy");
		FHostBindingRegistry Restored;
		Expect(ImportBindingCatalogJson(Json, Restored, Error), "import annotation name policy: " + Error);
		Expect(Restored.GetAnnotations().size() == 2 && !Restored.GetAnnotations()[0].CaseSensitive && Restored.GetAnnotations()[1].CaseSensitive, "catalog preserves exact and insensitive policies");
		Expect(Restored.RegisterAnnotation("GuiMeta", "", Error, "", {}, AnnotationAllTargets, false), Error);
		auto Calls = std::make_shared<unsigned>(0);
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc Processor;
		Processor.ProcessorId = "test.annotation-names";
		Processor.Version = "0.1.1";
		Processor.Phases = CompilePhaseMask(ECompileProcessingPhase::Members);
		Processor.Subscriptions = {{ECompileProcessingEvent::Annotation, AnnotationAllTargets, "@GUIWIDGET"}};
		Processor.Factory = [Calls]
		{
			return std::make_unique<FAnnotationNameProbe>(Calls);
		};
		Expect(Registry->Register(std::move(Processor), Error), Error);
		Registry->Freeze();
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.Bindings = &Restored;
		O.CompileProcessing.Registry = Registry;
		const std::string Source = R"vs(
@GuiWidget(Name=3) export class First {public:@gUIwIDGET int value; @GUIwidget int read(){return value;}};
@guiWidget export class Second{};
@guiwidget export class Third{};
@GUIWIDGET @Strict @Unknown export class Fourth{};
@gUiWiDgEt export class Generic<T>{public:T value;};
@GUIwidget export int global_value=7;
@guiWIDGET export int read_value(){return global_value;}
guimeta(Flag) class MacroStyle{};
void main(){
 annotation_info invalid=annotation_info();assert(!invalid.matches_name(""));
 type<First> a=type<First>();type<Fourth> d=type<Fourth>();
 assert(a.has_annotation("@GUIWIDGET") && a.has_annotation("guiwidget"));
 assert(type<Second>().has_annotation("@GuiWidget") && type<Third>().has_annotation("@GuiWidget"));
 assert(d.has_annotation("GuiWidget") && d.has_annotation("Strict") && !d.has_annotation("strict"));
 assert(d.has_annotation("Unknown") && !d.has_annotation("unknown"));
 assert(len(a.find_annotations("@guiwidget"))==1 && len(a.find_annotations("guiwidget"))==0);
 assert(a.annotation(0).name()=="@GuiWidget" && a.annotation(0).matches_name("@gUIwIDGET"));
 assert(a.annotation(0).definition().name()=="@GuiWidget" && !a.annotation(0).definition().case_sensitive());
 assert(d.annotation(0).name()=="@GUIWIDGET" && d.metadata("__annot")=="@GUIWIDGET");
 assert(d.annotation(1).definition().case_sensitive() && len(d.find_annotations("@strict"))==0);
 assert(len(a.annotation(0).find_arguments("Name"))==1 && len(a.annotation(0).find_arguments("name"))==0);
 assert(a.metadata("Name")=="3" && !a.has_metadata("name"));
 assert(type<MacroStyle>().has_annotation("GUIMETA") && len(type<MacroStyle>().find_annotations("GUIMETA"))==1);
 assert(type<Generic<int>>().has_annotation("guiwidget"));
 field_info<First> f(a.qualified_name,0);assert(f.has_annotation("GUIWIDGET"));
 for(int i=0;i<a.method_count();++i){method_info<First> m(a.qualified_name,i);if(m.name()=="read")assert(m.has_annotation("GuiWidget"));}
})vs";
		auto C = VeroScriptCompile::CompileSource(Source, "annotation-names.vs", O);
		Expect(C.bSuccess, "compile annotation name policies: " + C.Error.Message);
		Expect(*Calls == 9, "all annotation spellings and declaration targets dispatch exactly once");
		if (!C.bSuccess) return;
		std::vector<std::uint8_t> Bytes;
		Expect(VeroScriptBytecodeIO::SerializeBytecode(*C.Module, 0, Bytes, Error), "serialize name policies: " + Error);
		auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), std::nullopt, Error);
		Expect(bool(Loaded), "deserialize name policies: " + Error);
		for (const auto* Module : {C.Module.get(), Loaded.get()})
		{
			if (Module)
			{
				for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed})
				{
					std::ostringstream Out;
					VeroScriptVm Vm(Out);
					VeroScript::Test::Configure(Vm);
					Vm.SetExecutionBackend(Backend);
					Expect(Vm.Execute(*Module, ERunMode::Script),
					    "name policy reflection execution: " + Vm.GetError().Message);
				}
			}
		}

		auto Policies = GetAnnotationDefinitions(Restored);
		FCompileProcessingOptions Same = O.CompileProcessing;
		Same.AnnotationDefinitions = Policies;
		Expect(C.PreparedProgram->MatchesProcessing(Same), "matching name policy accepts prepared result");
		Same.AnnotationDefinitions[0].CaseSensitive = true;
		Expect(!C.PreparedProgram->MatchesProcessing(Same), "policy changes invalidate prepared result even when definition ID is unchanged");
		Same.AnnotationDefinitions.push_back(Policies[0]);
		Same.AnnotationDefinitions.back().Id += "-collision";
		Same.AnnotationDefinitions.back().Name = "@guiwidget";
		FCompileOptions Conflict = VeroScript::Test::MakeCompileOptions();
		Conflict.CompileProcessing = Same;
		const auto Invalid = VeroScriptCompile::CompileSource("void main(){}", "annotation-collision.vs", Conflict);
		Expect(!Invalid.bSuccess, "overlapping descriptions are rejected before compilation");
		FPackageBuildOptions Package;
		Package.Kind = EPackageKind::Library;
		Package.PackageName = "AnnotationNames";
		Package.bStripDebugInfo = true;
		auto Library = VeroScriptCompile::CompileSource(Source.substr(0, Source.find("void main()")), "annotation-library.vs", O);
		Expect(Library.bSuccess, "compile annotation policy library: " + Library.Error.Message);
		if (!Library.bSuccess || !BuildPackage(*Library.Module, Package, Bytes, Error))
		{
			Expect(false, "build annotation policy library: " + Error);
			return;
		}
		const auto Dir = std::filesystem::temp_directory_path() /
		    ("vero-annotation-names-" +
		        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directory(Dir);
		const auto File = Dir / "names.vslib";
		{
			std::ofstream Out(File, std::ios::binary);
			Out.write(
			    reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
		}
		FHostBindingRegistry ConsumerBindings;
		Expect(ConsumerBindings.RegisterAnnotation("@GuiWidget", "", Error), Error);
		for (const IHostBindingCatalog* Catalog : {static_cast<const IHostBindingCatalog*>(nullptr),
		         static_cast<const IHostBindingCatalog*>(&ConsumerBindings)})
		{
			FCompileOptions ConsumerOptions = VeroScript::Test::MakeCompileOptions();
			ConsumerOptions.Bindings = Catalog;
			const auto Consumer = VeroScriptCompile::CompileSource("import \"" + File.generic_string() + "\";void main(){type<Fourth> t=type<Fourth>();assert(t.has_annotation(\"guiwidget\"));assert(t.annotation(0).name()==\"@GUIWIDGET\");assert(t.annotation(0).definition().name()==\"@GuiWidget\");assert(type<Generic<int>>().has_annotation(\"GUIWIDGET\"));}", "annotation-consumer.vs", ConsumerOptions);
			Expect(Consumer.bSuccess,
			    "compile library consumer without producer registrations: " + Consumer.Error.Message);
			if (Consumer.bSuccess)
			{
				std::ostringstream Out;
				VeroScriptVm Vm(Out);
				VeroScript::Test::Configure(Vm);
				Expect(Vm.Execute(*Consumer.Module, ERunMode::Script),
				    "library retains annotation name policy: " + Vm.GetError().Message);
			}
		}
		std::filesystem::remove(File);
		std::filesystem::remove(Dir);
	}
}
int main()
{
	TestDeclarationRangeIndex();
	TestDocumentlessMetadataOwners();
	TestImmediateSourceOrigins();
	Run("class Counter<T>{public:T value;T& write(){return value;}const T& read()const{return value;}};"
	    "int total=4;int& current(){return total;}void main(){Counter<int> c=Counter<int>();"
	    "c.write()=8;current()+=1;print(c.read());print(total);}",
	    Options<FReferenceReturnObserver>(CompilePhaseMask(ECompileProcessingPhase::Types), 0), "8\n5\n", false);
	Run("int total=4;int& current(){return total;}fn()->int& make(){return current;}"
	    "void update(fn()->int& callback){callback()=8;}void main(){update(make());print(total);}",
	    Options<FReferenceReturnObserver>(CompilePhaseMask(ECompileProcessingPhase::Types), 0), "8\n", false);
	Run("int total=4;const int& read(){return total;}class Reader<T>{public:fn()->const T& callback;};"
	    "void main(){Reader<int> r=Reader<int>();r.callback=read;auto callback=r.callback;print(callback());}",
	    Options<FReferenceReturnObserver>(CompilePhaseMask(ECompileProcessingPhase::Types), 0), "4\n", false);
	Run("void use(array<fn()->int&> callbacks){}void main(){print(1);}",
	    Options<FReferenceReturnObserver>(CompilePhaseMask(ECompileProcessingPhase::Types), 0), "1\n", false);
	{
		FCompileOptions ReadOnlyOptions = VeroScript::Test::MakeCompileOptions();
		ReadOnlyOptions.CompileProcessing = Options<FReferenceReturnObserver>(CompilePhaseMask(ECompileProcessingPhase::Types), 0);
		const auto ReadOnly = VeroScriptCompile::CompileSource(
		    "int total=4;const int& read(){return total;}void write(fn()->const int& f){f()=8;}void main(){}",
		    "const-reference-processing.vs", ReadOnlyOptions);
		Expect(!ReadOnly.bSuccess, "canonical callable return retains const reference write protection");
		const auto InvalidSource = VeroScriptCompile::CompileSource(
		    "int total=4;ref int read(){return total;}void main(){}",
		    "source-reference-grammar.vs", ReadOnlyOptions);
		Expect(!InvalidSource.bSuccess, "canonical reference spelling stays confined to declaration type fragments");
	}
	Run("interface AppendContract{};class AppendAlpha:AppendContract{public:int Seed=0;};"
	    "template<typename T>struct AppendBox{};void main(){AppendAlpha a=AppendAlpha(40);a.Offset=2;"
	    "AppendContract contract=a;print(contract.Read());AppendBox<int> b=AppendBox<int>();b.Seed=7;print(b.Read());}",
	    Options<FMemberAppendGenerator>(CompilePhaseMask(ECompileProcessingPhase::Members),
	        CompileOperationMask(ECompileOperation::AddDeclaration)),
	    "42\n8\n");
	Run("interface MixedContract{};class MixedAlpha:MixedContract{public:int Seed=10;};"
	    "template<typename T>struct MixedBox{T Seed;};void main(){MixedAlpha a=MixedAlpha();"
	    "print(a.Read0());print(a.Read31());MixedContract contract=a;print(contract.Read31());"
	    "MixedBox<int> b=MixedBox<int>();b.Seed=20;print(b.Read31());"
	    "print(MixedHelpers::Helper0());print(MixedHelpers::Helper31());}",
	    Options<FInterleavedAppendGenerator>(CompilePhaseMask(ECompileProcessingPhase::Members),
	        CompileOperationMask(ECompileOperation::AddDeclaration)),
	    "11\n42\n42\n52\n1\n32\n");
	Run("template<typename T=Generated,int N=3> class Source{public:template<typename U> U original(U value){return value;}};"
	    "template<GeneratedPolicy Mode=GeneratedPolicy::Ready> struct PolicyBox{int read(){return cast<int>(Mode);}};"
	    "void main(){Source<> source=Source<>();Generated value=Generated();print(source.echo(value).value);print(source.original(7));"
	    "Source<int,4>::Inner inner=Source<int,4>::Inner();inner.data[0]=12;print(inner.data[0]);PolicyBox<> policy=PolicyBox<>();print(policy.read());}",
	    Options<FTemplateGenerator>(4 | 8, 2), "9\n7\n12\n3\n");
	TestAnnotationNamePolicy();
	TestGeneratedAnnotationNamePolicy();
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FAdditionWithTarget>(4, 2);
		auto C = VeroScriptCompile::CompileSource("export class Work<T>{public:T old;};", "invalid-addition.vs", O);
		Expect(!C.bSuccess && C.Error.Message.find("declaration addition cannot carry a replacement target") != std::string::npos && C.Error.Location.FileLabel.find("invalid-addition.vs") != std::string::npos, "addition cannot bypass replacement permissions and target proof");
	}
	{
		for (const bool Raw : {true, false})
		{
			FCompileOptions O = VeroScript::Test::MakeCompileOptions();
			O.CompileProcessing = Options<FInvalidOperation>(Raw ? 1u : 8u, Raw ? 1u : 2u);
			auto C = VeroScriptCompile::CompileSource("int Compute(){return 1;}void main(){print(Compute());}", "invalid-operation.vs", O);
			Expect(!C.bSuccess && C.Error.Message.find("invalid compile operation kind") != std::string::npos && C.Error.Location.FileLabel.find("invalid-operation.vs") != std::string::npos,
			    "combined operation kind is rejected before source or declaration mutation");
		}
	}
	{
		for (const bool Generate : {false, true})
		{
			auto Calls = std::make_shared<std::array<std::size_t, 5>>();
			auto Registry = std::make_shared<FCompileProcessorRegistry>();
			std::string Error;
			FCompileProcessorDesc D;
			D.ProcessorId = "test.comments";
			D.Version = "1";
			D.Phases = Generate ? 8u : 31u;
			D.Operations = Generate ? 2u : 0u;
			D.Subscriptions = {{ECompileProcessingEvent::Comment}};
			D.Factory = [Calls, Generate]
			{
				auto P = std::make_unique<FCommentProcessor>();
				P->Calls = Calls;
				P->Generate = Generate;
				return P;
			};
			Expect(Registry->Register(std::move(D), Error), Error);
			Registry->Freeze();
			FCompileProcessingOptions Processing;
			Processing.Registry = Registry;
			if (Generate)
			{
				Run("// generate\nvoid main(){print(GeneratedComment());}", Processing, "7\n");
			}

			else
			{
				Run("// kept\n#if 0\n// inactive\n#endif\nvoid main(){}", Processing, "", false);
				Expect(*Calls == std::array<std::size_t, 5>{2, 1, 1, 1, 1}, "each phase dispatches one event per comment in its current source view");
			}
		}
	}
	Run("class Original{}; int Missing(); void main(){Generated g=Original::Create();print(g.value);print(Missing());}",
	    Options<FGenerator>(28, 2 | 8), "42\n13\n");
	Run("struct Generic<T>{T value;T operator T()const{return value;}};"
	    "void main(){Generic<int> object=Generic<int>();object.value=7;int value=object;print(value);}",
	    Options<FGenerator>(28, 2 | 8), "7\n");
	Run("CUSTOM Item{};int Pending();void main(){print(Pending());}",
	    Options<FSourceConverter>(1 | 16, 1 | 8), "9\n");
	Run("int Compute(){return 1;}void main(){print(Compute());}", Options<FReplace>(16, 16), "81\n");
	Run("class Base{public:int Read(){return Helper();}};class Derived:public Base{public:int Helper() override{return 9;}};void main(){Base b=Derived();print(b.Read());}", Options<FVirtualHelpers>(8 | 16, 2 | 8), "9\n");
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FVirtualHelpers>(8 | 16, 2 | 8);
		O.SourceOverlays = {{"generated-base.vs", "export class Base{public:int Read(){return Helper();}};"},
		    {"generated-implementation.vs", "import \"generated-base.vs\";int Base::Helper(){return 12;}"}};
		auto C = VeroScriptCompile::CompileSource("import \"generated-base.vs\";import \"generated-implementation.vs\";void main(){Base b=Base();print(b.Read());}", "cross-generated.vs", O);
		Expect(C.bSuccess, "user implements a generated prototype across files: " + C.Error.Message);
		if (C.bSuccess)
		{
			std::ostringstream Out;
			VeroScriptVm Vm(Out);
			VeroScript::Test::Configure(Vm);
			Expect(Vm.Execute(*C.Module, ERunMode::Script) && Out.str() == "12\n",
			    "cross-file user implementation precedes generated default");
		}
	}
	Run("void main(){print(__LINE__);}", Options<FLineMapper>(1, 1), "1\n");
	Run("#if 0\n// inactive\nint Hidden=1;\n#endif\n// active\nclass Visible{};void main(){}", Options<FSourceProbe>(16, 0), "", false);
	Run("struct Locked final{};class Lifetime{public:virtual ~Lifetime(){}};int Echo(const int& value){return value;}void main(){int n=4;print(Echo(&n));}", Options<FModifierProbe>(16, 0), "4\n", false);
	Run("/// Clear me\nclass Commented{};void main(){type<Commented> t=type<Commented>();print(t.comment_count());}", Options<FClearComments>(16, 32), "0\n");
	Run("struct Replacement{int value=1;};void main(){Replacement r=Replacement();print(r.value);}", Options<FReplaceType>(4, 4), "19\n");
	Run("/// Original docs\n@Tag(mark=9) struct Replacement{int value=1;};"
	    "void main(){type<Replacement> t=type<Replacement>();print(t.annotation_count());print(t.has_annotation(\"@Tag\"));print(t.metadata(\"mark\"));print(t.documentation());print(len(module_info().documentation()));}",
	    Options<FReplaceType>(4, 4), "1\ntrue\n9\nOriginal docs\n0\n");
	Run("/// Original docs\n@Tag(mark=9) struct Replacement{int value=1;};"
	    "void main(){type<Replacement> t=type<Replacement>();print(t.annotation_count());print(t.has_annotation(\"@Tag\"));print(t.has_annotation(\"@Fresh\"));print(t.metadata(\"mark\"));print(t.documentation());print(len(module_info().documentation()));}",
	    Options<FReplaceDocumentedType>(4, 4), "1\nfalse\ntrue\n10\nReplacement docs\n0\n");
	Run("class A{};void main(){source_node_info n=source_node_info();print(n.is_valid());print(n.parameter_count());print(len(n.source_metadata()));}", {}, "false\n0\n0\n", false);
	Run("/// Template docs\ntemplate<T> class Typed{public:T value;};void main(){print(type<Typed<int>>().documentation());}", {}, "Template docs\n", false);
	Run("/// Template docs\ntemplate/* > */</* < > */ T> class Typed{public:T value;};void main(){print(type<Typed<int>>().documentation());}", {}, "Template docs\n", false);
	Run("/// Type docs\npublic/* access note */static class Math{int answer(){return 42;}};void main(){print(type<Math>().documentation());}", {}, "Type docs\n", false);
	Run("struct Subject{/// Field docs\npublic /* access note */:int field;};void main(){print(field_info<Subject>(type<Subject>().qualified_name,0).documentation());}", {}, "Field docs\n", false);
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FReplaceBodyComments>(16, 16);
		auto C = VeroScriptCompile::CompileSource("/// Function docs\nint Compute(){ // old body\nreturn 1;}void main(){print(Compute());}", "replacement-body-comments.vs", O);
		Expect(C.bSuccess, "replace body comments compile: " + C.Error.Message);
		if (C.bSuccess)
		{
			for (const auto& F : C.PreparedProgram->GetProgram().Functions)
			{
				if (F.Name == "Compute")
				{
					Expect(F.DeclMeta.Documentation == "Function docs" && F.DeclMeta.Comments.size() == 2,
					    "body replacement preserves leading documentation and replaces interior comments");
					Expect(std::none_of(F.DeclMeta.Comments.begin(), F.DeclMeta.Comments.end(),
					           [](const auto& C)
					{
						return C.Text.find("old body") != std::string::npos;
					}),
					    "old body comments disappear from declaration metadata");
				}
			}

			for (const auto& D : C.PreparedProgram->GetSourceMetadata().Documents)
			{
				if (D.Id == D.ModuleId)
				{
					for (const auto& Comment : D.Comments)
					{
						if (Comment.Text.find("old body") != std::string::npos)
						{
							Expect(Comment.Activity == ESourceActivity::Removed,
							    "old body comment remains historical and removed");
						}
					}
				}
			}

			std::vector<std::uint8_t> Bytes;
			std::string Error;
			Expect(VeroScriptBytecodeIO::SerializeBytecode(*C.Module, 0, Bytes, Error), "serialize replaced body comments: " + Error);
			auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), std::nullopt, Error);
			Expect(bool(Loaded), "deserialize replaced body comments: " + Error);
			if (Loaded)
			{
				for (const auto& N : Loaded->SourceMetadata->Nodes)
				{
					if (N.Name == "Compute")
					{
						Expect(N.Documentation == "Function docs" && N.Comments.size() == 2,
						    "VSBC retains only current declaration/body comments");
					}
				}

				std::ostringstream Out;
				VeroScriptVm Vm(Out);
				VeroScript::Test::Configure(Vm);
				Expect(Vm.Execute(*Loaded, ERunMode::Script) && Out.str() == "81\n",
				    "serialized replaced body executes");
			}
		}
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FReplaceOtherDeclarations>(8, 4);
		auto C = VeroScriptCompile::CompileSource("struct Subject{public:\n/// Field docs\n@Tag(mark=1)int field=1;"
		                                          "\n/// Method docs\n@Tag(mark=2)int member(){ // old body\nreturn 2;}"
		                                          "\n/// Destructor docs\n@Tag(mark=3)~Subject(){}};"
		                                          "\n/// Function docs\n@Tag(mark=4)int Free(){return 4;}"
		                                          "\n/// Global docs\n@Tag(mark=5)int global=5;void main(){Subject s=Subject();print(s.field);print(s.member());print(Free());print(global);}",
		    "replacement-members.vs", O);
		Expect(C.bSuccess, "replace member/function/global/destructor: " + C.Error.Message);
		if (C.bSuccess)
		{
			std::ostringstream Out;
			VeroScriptVm Vm(Out);
			VeroScript::Test::Configure(Vm);
			Expect(Vm.Execute(*C.Module, ERunMode::Script) && Out.str() == "11\n12\n14\n15\n",
			    "replacement declarations execute normally");
			const auto& P = C.PreparedProgram->GetProgram();
			const auto Check = [](const FDeclMeta& M, const char* Docs)
			{
				return M.Annotations.size() == 1 && M.Documentation == Docs &&
				    std::find(M.begin(), M.end(),
				        std::make_pair(std::string("__annot"), std::string("@Tag"))) != M.end();
			};
			const auto& T = P.Structs.front();
			Expect(Check(T.Fields.front().DeclMeta, "Field docs") &&
			        Check(T.Methods.front().DeclMeta, "Replacement method docs") && T.Destructor &&
			        Check(T.Destructor->DeclMeta, "Destructor docs"),
			    "member replacements preserve metadata and accept new source documentation");
			for (const auto& F : P.Functions)
			{
				if (F.Name == "Free")
				{
					Expect(
					    Check(F.DeclMeta, "Function docs"), "free function replacement preserves metadata");
				}
			}

			for (const auto& S : P.TopLevelStatements)
			{
				if (S->Kind == EStmtKind::VarDecl && static_cast<const FVarDeclStmt&>(*S).Name == "global")
				{
					Expect(Check(S->DeclMeta, "Global docs"), "global replacement preserves metadata");
				}
			}

			Expect(T.Methods.front().DeclMeta.Comments.size() == 2,
			    "replacement removes old body comments and associates new ones");
		}
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FGeneratedComments>(4 | 8, 2);
		auto C = VeroScriptCompile::CompileSource("void main(){print(type<GeneratedValue>().documentation());print(GeneratedRead());}", "generated-comments.vs", O);
		Expect(C.bSuccess, "generated comments compile: " + C.Error.Message);
		if (C.bSuccess)
		{
			std::ostringstream Out;
			VeroScriptVm Vm(Out);
			VeroScript::Test::Configure(Vm);
			Expect(Vm.Execute(*C.Module, ERunMode::Script) && Out.str() == "Generated type docs\n5\n",
			    "generated type comments participate in reflection");
			for (const auto& F : C.PreparedProgram->GetProgram().Functions)
			{
				if (F.Name == "GeneratedRead")
				{
					Expect(F.DeclMeta.Documentation == "Generated function docs" &&
					        F.DeclMeta.Comments.size() == 2,
					    "generated function leading and body comments retain their owner");
				}
			}
		}
	}
	Run("void main(){print(GeneratedFunction());}", Options<FMemberGenerator>(8, 2), "31\n");
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FCompleteTypes>(4, 2);
		auto C = VeroScriptCompile::CompileSource("void main(){GeneratedNS::Left x=GeneratedNS::Left();GeneratedNS::Right y=x.make();GeneratedNS::Number n=y.read();print(n);print(cast<int>(GeneratedNS::Choice::Second));}", "complete-types.vs", O);
		Expect(C.bSuccess, "complete types and mutual references: " + C.Error.Message);
		if (C.bSuccess)
		{
			bool Found = false;
			for (const auto& N : C.Module->SourceMetadata->Nodes)
			{
				for (const auto& A : N.Annotations)
				{
					if (A.Name == "@Literal")
					{
						const auto* D = C.Module->SourceMetadata->FindDocument(N.Range.SourceId);
						Found = D && N.Range.End <= D->Text.size() &&
						    D->Text.substr(N.Range.Begin, N.Range.End - N.Range.Begin) == "7";
						// Runtime snapshots omit source text; the prepared generated view owns it.
						if (!Found)
						{
							for (const auto& G : C.PreparedProgram->GetGeneratedDocuments())
							{
								if (G.FileLabel == N.Range.SourceId)
								{
									Found =
									    G.Source.substr(N.Range.Begin, N.Range.End - N.Range.Begin) == "7";
								}
							}
						}
					}
				}
			}
			Expect(Found, "generated type descendant range uses document offsets");
		}
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FActiveInvalidImport>(2, 1);
		auto C = VeroScriptCompile::CompileSource("void main(){}", "active-import.vs", O);
		Expect(!C.bSuccess, "ActiveSource cannot change the resolved import graph");
		O.CompileProcessing = Options<FGenerator>(28, 2 | 8);
		O.CompileProcessing.MaxOperations = 0;
		C = VeroScriptCompile::CompileSource("class Original{};void main(){}", "budget.vs", O);
		Expect(!C.bSuccess, "generation operation budget is enforced");
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FReadDependency>(16, 0);
		std::string Content = "first";
		O.CompileProcessing.ReadDependency = [&](std::string_view Path, std::string& Out)
		{
			Expect(Path.find("settings.txt") != std::string_view::npos, "dependency resolves against module");
			Out = Content;
			return true;
		};
		auto C = VeroScriptCompile::CompileSource("void main(){}", "dependency.vs", O);
		Expect(C.bSuccess, "dependency-only processing: " + C.Error.Message);
		if (C.bSuccess)
		{
			Expect(C.PreparedProgram->MatchesDependencies(O.CompileProcessing),
			    "matching processing dependency");
			Content = "changed";
			Expect(!C.PreparedProgram->MatchesDependencies(O.CompileProcessing),
			    "changed processing dependency invalidates preparation");
		}
	}
	{
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		std::string Error;
		for (const auto* Id : {"first", "second"})
		{
			FCompileProcessorDesc D;
			D.ProcessorId = Id;
			D.Version = "1";
			D.Phases = 16;
			D.Operations = 16;
			D.Subscriptions = {{ECompileProcessingEvent::Module}};
			D.Factory = []
			{
				return std::make_unique<FReplace>();
			};
			Expect(Registry->Register(std::move(D), Error), Error);
		}
		Registry->Freeze();
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing.Registry = Registry;
		auto C = VeroScriptCompile::CompileSource("int Compute(){return 1;}void main(){}", "conflict.vs", O);
		Expect(!C.bSuccess && C.Error.Message.find("conflicting compile operations") != std::string::npos, "same-target processor conflict is diagnosed");
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FChildMetadata>(16, 16);
		auto C = VeroScriptCompile::CompileSource("int Compute(){return 1;}void main(){print(Compute());}", "child-metadata.vs", O);
		Expect(C.bSuccess, "fragment child metadata: " + C.Error.Message);
		if (C.bSuccess)
		{
			bool Found = false;
			for (const auto& N : C.Module->SourceMetadata->Nodes)
			{
				for (const auto& A : N.Annotations)
				{
					if (A.Name == "@GeneratedReturn")
					{
						Found = true;
					}
				}
			}
			Expect(Found, "fragment child metadata survives final snapshot");
		}
	}
	Run("/// Type docs\n@Param(meta=(size=4, enabled=true))\nclass Value { public: /// Field docs\n@Field int item; @Method int f(int x){ // Body note\n return x;} };void main(){type<Value> t=type<Value>();print(t.annotation_count());print(t.annotation(0).name());print(t.annotation(0).argument(0).argument(0).int64_value());print(t.documentation());field_info<Value> field(t.qualified_name,0);print(field.annotation(0).name());for(int i=0;i<t.method_count();++i){method_info<Value> m(t.qualified_name,i);if(m.name()==\"f\"){print(m.annotation(0).name());print(m.declaration().parameter_count());print(len(m.declaration().source_metadata()));}}}", {}, "1\n@Param\n4\nType docs\n@Field\n@Method\n1\n1\n", false);
	Run("/// Class docs\nclass Scoped { // Interior note\n\n int field;};void main(){type<Scoped> t=type<Scoped>();print(t.comment_count());print(t.documentation());}", {}, "2\nClass docs\n", false);
	auto Comments = CollectSourceComments("string a=\"// no\"; /* yes */\n/// docs\nchar c='/';", "comment.vs");
	Expect(Comments.size() == 2, "literal-aware comment collection");
	Comments = CollectSourceComments("/**/ int x; // after\n", "empty-doc.vs");
	Expect(Comments.size() == 2 && Comments[0].RawText == "/**/" && Comments[0].Text.empty() && Comments[1].Text == " after", "empty documentation block preserves following source and comments");
	Run("class Scoped { /// Interior docs\n\n int field;};void main(){type<Scoped> t=type<Scoped>();print(t.comment_count());print(t.comment(0).association());print(t.documentation());}", {}, "1\n1\n\n", false);
	{
		std::string Source;
		for (int N = 0; N < 96; ++N)
		{
			Source += "/// docs " + std::to_string(N) + "\nint f" + std::to_string(N) +
			    "(){ // body\nreturn 1;} // trailing\n";
		}

		Source += "void main(){}";
		auto C = VeroScriptCompile::CompileSource(Source, "comment-association.vs");
		Expect(C.bSuccess, "multiple commented declarations: " + C.Error.Message);
		if (C.bSuccess)
		{
			for (int N = 0; N < 96; ++N)
			{
				const auto& Nodes = C.Module->SourceMetadata->Nodes;
				const auto I = std::find_if(Nodes.begin(), Nodes.end(),
				    [&](const auto& V)
				{
					return V.Kind == "declaration" && V.Name == "f" + std::to_string(N);
				});
				Expect(I != Nodes.end() && I->Documentation == "docs " + std::to_string(N) &&
				        I->Comments.size() == 3,
				    "leading documentation, body and trailing comments retain their declaration owner");
			}
		}
	}
	Run("@Values(a=7i64,b=18_446_744_073_709_551_615u64,c=0xA_B,d=0b1_0,e=1_000UL) class Value{};"
	    "void main(){annotation_info a=type<Value>().annotation(0);print(a.argument(0).int64_value());print(a.argument(1).uint64_value());"
	    "print(a.argument(2).int64_value());print(a.argument(3).int64_value());print(a.argument(4).uint64_value());}",
	    {}, "7\n18446744073709551615\n171\n2\n1000\n", false);
	FAnnotationArgument A;
	A.Kind = EAnnotationArgumentKind::Scalar;
	A.ScalarKind = EAnnotationScalarKind::Integer;
	A.Value = "-9223372036854775808";
	std::int64_t I = 0;
	std::uint64_t U = 0;
	Expect(AnnotationArgumentInt64(A, I) && I == INT64_MIN, "signed boundary");
	Expect(!AnnotationArgumentUInt64(A, U), "negative unsigned rejected");
	A.Value = "18446744073709551615";
	Expect(AnnotationArgumentUInt64(A, U) && U == UINT64_MAX, "unsigned boundary");
	Expect(!AnnotationArgumentInt64(A, I), "signed overflow rejected");
	for (const char* Value : {"1uu", "1LL", "1i64u", "1ulul"})
	{
		A.Value = Value;
		Expect(!AnnotationArgumentInt64(A, I) && !AnnotationArgumentUInt64(A, U),
		    "invalid metadata integer suffix is rejected");
	}
	{
		auto C = VeroScriptCompile::CompileSource("void main(){}", "metadata-chain.vs");
		Expect(C.bSuccess, "compile metadata chain base");
		if (C.bSuccess)
		{
			auto Snapshot = std::make_shared<FSourceMetadataSnapshot>(*C.Module->SourceMetadata);
			const auto Begin = Snapshot->Nodes.size();
			for (std::size_t N = 0; N < 10000; ++N)
			{
				FSourceNodeMetadata Node;
				Node.Id = "test-chain:" + std::to_string(N);
				if (N)
				{
					Node.ParentId = "test-chain:" + std::to_string(N - 1);
				}
				Snapshot->Nodes.push_back(std::move(Node));
			}
			C.Module->SourceMetadata = Snapshot;
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			Expect(VeroScriptBytecodeIO::SerializeBytecode(*C.Module, 0, Bytes, Error), "serialize long metadata chain: " + Error);
			auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), std::nullopt, Error);
			Expect(Loaded && Loaded->SourceMetadata->Nodes.size() == Snapshot->Nodes.size(), "deserialize long metadata chain: " + Error);
			Snapshot->Nodes[Begin].ParentId = Snapshot->Nodes.back().Id;
			Expect(!VeroScriptBytecodeIO::ValidateModule(*C.Module, Error) && Error.find("cyclic") != std::string::npos, "metadata parent cycle is rejected");
		}
	}
	{
		FCompileProcessingOptions Retention;
		Retention.CommentRetention = ECommentRetention::Documentation;
		Run("// Fallback docs\nclass Scoped { /// Interior docs\n\n int field;};"
		    "void main(){type<Scoped> t=type<Scoped>();print(t.documentation());print(t.comment_count());print(t.comment(0).association());}",
		    Retention, "Fallback docs\n1\n1\n", false);
		auto C = VeroScriptCompile::CompileSource(
		    "// Fallback docs\nclass Scoped { /// Interior docs\n\n int field;};void main(){}",
		    "documentation.vs",
		    [&]
		{
			FCompileOptions O = VeroScript::Test::MakeCompileOptions();
			O.CompileProcessing = Retention;
			return O;
		}());
		Expect(C.bSuccess &&
		        C.PreparedProgram->GetProgram().Classes.front().DeclMeta.Documentation == "Fallback docs",
		    "prepared declaration preserves Documentation fallback");
	}
	{
		FCompileProcessingOptions Retention;
		Retention.CommentRetention = ECommentRetention::NoComments;
		Run("/// Hidden docs\n@Tag class Hidden{};void main(){type<Hidden> t=type<Hidden>();print(t.annotation_count());print(t.comment_count());print(t.comments_available());print(t.comments_retention());}", Retention, "1\n0\nfalse\n2\n", false);
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.CompileProcessing = Retention;
		auto C = VeroScriptCompile::CompileSource("/// Hidden\nclass Hidden{};void main(){}", "retention.vs", Options);
		Expect(C.bSuccess && C.PreparedProgram->GetProgram().Classes.front().DeclMeta.Comments.empty() && C.Module->Types.front().DeclMeta.Comments.empty(), "NoComments trims AST and bytecode declaration metadata");
	}
	{
		auto First = VeroScriptCompile::CompileSource("void main(){print(1);}", "fingerprint.vs");
		auto Second = VeroScriptCompile::CompileSource("void main(){print(2);}", "fingerprint.vs");
		Expect(First.bSuccess && Second.bSuccess && First.PreparedProgram->GetFingerprint() != Second.PreparedProgram->GetFingerprint(), "prepared identity includes original body content");
	}
	{
		FHostBindingRegistry Bindings;
		std::string Error;
		FHostAnnotationArgumentDesc Size;
		Size.Name = "size";
		Size.Documentation = "Size docs";
		FHostAnnotationArgumentDesc Group;
		Group.Name = "meta";
		Group.Children.push_back(Size);
		Expect(Bindings.RegisterAnnotation("@Meta", "Metadata", Error, "Annotation docs", {Group}), "description registration: " + Error);
		std::string Json;
		Expect(ExportBindingCatalogJson(Bindings, Json) && Json.find("children") != std::string::npos, "nested description catalog export");
		FHostBindingRegistry Restored;
		Expect(ImportBindingCatalogJson(Json, Restored, Error), "nested description catalog import: " + Error);
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.Bindings = &Bindings;
		auto C = VeroScriptCompile::CompileSource("@Meta(meta=(size=3)) class MetaValue{};void main(){type<MetaValue> t=type<MetaValue>();print(t.annotation(0).definition().documentation());print(t.annotation(0).argument(0).name());print(t.annotation(0).argument(0).argument(0).definition().documentation());}", "descriptions.vs", Options);
		Expect(C.bSuccess, "description-only metadata compile: " + C.Error.Message);
		if (C.bSuccess)
		{
			std::ostringstream Out, Err;
			std::istringstream In;
			VeroScriptVm Vm(Out, In, Err);
			VeroScript::Test::Configure(Vm);
			const auto Executed = Vm.Execute(*C.Module, ERunMode::Script);
			Expect(Executed, "description execution: " + Vm.GetError().Message);
			Expect(Out.str() == "Annotation docs\nmeta\nSize docs\n", "description path mapping: " + Out.str());
			Expect(C.PreparedProgram->MatchesProcessing({}, &Bindings), "effective processing configuration matches binding descriptions");
		}
		C = VeroScriptCompile::CompileSource("/// Type docs\n@Meta(meta=(size=3,unknown=4),extra=5) @Unknown class MetaValue{};"
		                                     "void main(){type<MetaValue> t=type<MetaValue>();annotation_definition_info d=t.annotation(0).definition();"
		                                     "print(d.argument_count());print(len(d.arguments()));print(d.argument(0).name());print(d.argument(0).argument_count());"
		                                     "print(d.argument(0).argument(0).name());print(d.argument(0).argument(0).argument_count());"
		                                     "annotation_definition_info missing=t.annotation(0).argument(0).argument(1).definition();print(missing.is_valid());"
		                                     "print(len(missing.name()));print(len(missing.documentation()));print(len(missing.detail()));print(len(missing.provider()));"
		                                     "print(missing.argument_count());print(len(missing.arguments()));"
		                                     "annotation_definition_info absent=t.annotation(1).definition();print(absent.is_valid());"
		                                     "print(len(absent.name()));print(len(absent.documentation()));print(absent.argument_count());}",
		    "description-schema.vs", Options);
		Expect(C.bSuccess, "description schema differs from instance arguments: " + C.Error.Message);
		if (C.bSuccess)
		{
			std::vector<std::uint8_t> Bytes;
			std::string CodecError;
			Expect(VeroScriptBytecodeIO::SerializeBytecode(*C.Module, 0, Bytes, CodecError), "serialize description schema: " + CodecError);
			auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), std::nullopt, CodecError);
			Expect(bool(Loaded), "deserialize description schema: " + CodecError);
			for (const auto* Module : {C.Module.get(), Loaded.get()})
			{
				if (Module)
				{
					std::ostringstream Out;
					VeroScriptVm Vm(Out);
					VeroScript::Test::Configure(Vm);
					Expect(Vm.Execute(*Module, ERunMode::Script),
					    "description schema execution: " + Vm.GetError().Message);
					Expect(Out.str() == "1\n1\nmeta\n1\nsize\n0\nfalse\n0\n0\n0\n0\n0\n0\nfalse\n0\n0\n0\n",
					    "schema counts and missing descriptions stay independent: " + Out.str());
				}
			}
		}
	}
	for (const bool Documented : {false, true})
	{
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.CompileProcessing = Documented ? ::Options<FDocumentedGeneric>(4 | 8, 2) : ::Options<FGenericGenerator>(4 | 16, 2 | 32);
		auto Library = VeroScriptCompile::CompileSource("export T Build<T>(T x){Generated<T> g=Generated<T>();g.value=x;return g.get();}", "generic-processing.vs", Options);
		Expect(Library.bSuccess, "generic producer: " + Library.Error.Message);
		if (Library.bSuccess)
		{
			FPackageBuildOptions Package;
			Package.Kind = EPackageKind::Library;
			Package.PackageName = "GeneralProcessing";
			Package.bStripDebugInfo = true;
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			const bool Built = BuildPackage(*Library.Module, Package, Bytes, Error);
			Expect(Built, "generic processing package: " + Error);
			if (Built)
			{
				const auto Directory = std::filesystem::temp_directory_path() /
				    ("vero-general-processing-" +
				        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
				std::filesystem::create_directory(Directory);
				const auto File = Directory / "GeneralProcessing.vslib";
				{
					std::ofstream Out(File, std::ios::binary);
					Out.write(reinterpret_cast<const char*>(Bytes.data()),
					    static_cast<std::streamsize>(Bytes.size()));
				}
				Run("import \"" + File.generic_string() + "\";void main(){print(Build<int>(23));}", {},
				    "23\n", false);
				if (Documented)
				{
					Run("import \"" + File.generic_string() +
					        "\";void main(){print(Build<int>(23));print(type<Generated<int>>().documentation());}",
					    {}, "23\nGenerated generic docs\n", false);
				}

				std::filesystem::remove(File);
				std::filesystem::remove(Directory);
			}
		}
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FReplaceBodyComments>(16, 16);
		auto Library = VeroScriptCompile::CompileSource("export class Work<T>{public:T value;\n/// Method docs\nint Compute(){ // old body\nreturn 1;}};", "body-replay.vs", O);
		Expect(Library.bSuccess, "body replacement package producer: " + Library.Error.Message);
		if (Library.bSuccess)
		{
			FPackageBuildOptions Package;
			Package.Kind = EPackageKind::Library;
			Package.PackageName = "BodyReplay";
			Package.bStripDebugInfo = true;
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			const bool Built = BuildPackage(*Library.Module, Package, Bytes, Error);
			Expect(Built, "body replacement package build: " + Error);
			if (Built)
			{
				const auto Directory = std::filesystem::temp_directory_path() /
				    ("vero-body-replay-" +
				        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
				std::filesystem::create_directory(Directory);
				const auto File = Directory / "BodyReplay.vslib";
				{
					std::ofstream Out(File, std::ios::binary);
					Out.write(reinterpret_cast<const char*>(Bytes.data()),
					    static_cast<std::streamsize>(Bytes.size()));
				}
				const std::string Source = "import \"" + File.generic_string() +
				    "\";void main(){Work<int> w=Work<int>();print(w.Compute());"
				    "type<Work<int>> t=type<Work<int>>();for(int i=0;i<t.method_count();++i){method_info<Work<int>> m(t.qualified_name,i);"
				    "if(m.name()==\"Compute\"){print(m.documentation());print(m.comment_count());array<source_node_info> nodes=m.declaration().source_metadata();"
				    "for(int j=0;j<len(nodes);++j)if(nodes[j].kind()==\"comment\")print(nodes[j].comment(0).text());}}}";
				Run(Source, {}, "81\nMethod docs\n2\n new body\n", false);
				auto Consumer = VeroScriptCompile::CompileSource(Source, "body-replay-consumer.vs");
				Expect(Consumer.bSuccess, "body replacement consumer snapshot: " + Consumer.Error.Message);
				if (Consumer.bSuccess)
				{
					for (const auto& N : Consumer.PreparedProgram->GetSourceMetadata().Nodes)
					{
						for (const auto& Comment : N.Comments)
						{
							Expect(Comment.Text.find("old body") == std::string::npos,
							    "package replay discards stale body comment nodes");
						}
					}
				}

				std::filesystem::remove(File);
				std::filesystem::remove(Directory);
			}
		}
	}
	{
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.CompileProcessing = Options<FReplaceFullGenericType>(4, 4);
		const std::string Original =
		    "/// Work docs\nexport class Work<T>{public:\n/// Old field docs\nT old;"
		    "\n/// Old method docs\nint Old(int former){ // old body\nreturn former;}};";
		const auto Check = [](const FCompileResult& C)
		{
			if (!C.bSuccess) return;
			for (const auto& N : C.PreparedProgram->GetSourceMetadata().Nodes)
			{
				Expect(N.Name != "old" && N.Name != "Old" && N.Name != "former",
				    "type replacement snapshot contains only current children");
				for (const auto& Comment : N.Comments)
				{
					Expect(Comment.Text.find("old body") == std::string::npos &&
					        Comment.Text.find("Old field docs") == std::string::npos &&
					        Comment.Text.find("Old method docs") == std::string::npos,
					    "old child comments do not become module documentation");
				}

				if (N.Name == "Work")
				{
					Expect(
					    N.Documentation == "Work docs", "type replacement preserves exterior documentation");
				}
			}
			for (const auto& D : C.PreparedProgram->GetSourceMetadata().Documents)
			{
				if (D.Id == D.ModuleId)
				{
					for (const auto& Comment : D.Comments)
					{
						if (Comment.Text.find("old body") != std::string::npos ||
						    Comment.Text.find("Old field docs") != std::string::npos ||
						    Comment.Text.find("Old method docs") != std::string::npos)
						{
							Expect(Comment.Activity == ESourceActivity::Removed,
							    "old type child comments retain removed historical status");
						}
					}
				}
			}
		};
		auto Library = VeroScriptCompile::CompileSource(Original, "type-replay.vs", O);
		Expect(Library.bSuccess, "full type replacement producer: " + Library.Error.Message);
		Check(Library);
		if (Library.bSuccess)
		{
			FPackageBuildOptions Package;
			Package.Kind = EPackageKind::Library;
			Package.PackageName = "TypeReplay";
			Package.bStripDebugInfo = true;
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			const bool Built = BuildPackage(*Library.Module, Package, Bytes, Error);
			Expect(Built, "full type replacement package: " + Error);
			if (Built)
			{
				const auto Directory = std::filesystem::temp_directory_path() /
				    ("vero-type-replay-" +
				        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
				std::filesystem::create_directory(Directory);
				const auto File = Directory / "TypeReplay.vslib";
				{
					std::ofstream Out(File, std::ios::binary);
					Out.write(reinterpret_cast<const char*>(Bytes.data()),
					    static_cast<std::streamsize>(Bytes.size()));
				}
				const std::string Source = "import \"" + File.generic_string() +
				    "\";void main(){Work<int> w=Work<int>();w.value=7;print(w.value);print(w.New(8));print(type<Work<int>>().documentation());}";
				Run(Source, {}, "7\n9\nWork docs\n", false);
				auto Consumer = VeroScriptCompile::CompileSource(Source, "type-replay-consumer.vs");
				Expect(Consumer.bSuccess, "full type replacement consumer: " + Consumer.Error.Message);
				Check(Consumer);
				if (Consumer.bSuccess)
				{
					Expect(VeroScriptBytecodeIO::SerializeBytecode(*Consumer.Module, 0, Bytes, Error),
					    "serialize replaced type: " + Error);
					auto Loaded =
					    VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), std::nullopt, Error);
					Expect(bool(Loaded), "deserialize replaced type: " + Error);
					if (Loaded)
					{
						for (const auto& N : Loaded->SourceMetadata->Nodes)
						{
							Expect(N.Name != "old" && N.Name != "Old" && N.Name != "former",
							    "VSBC excludes replaced type children");
						}

						std::ostringstream Out;
						VeroScriptVm Vm(Out);
						VeroScript::Test::Configure(Vm);
						Expect(Vm.Execute(*Loaded, ERunMode::Script) && Out.str() == "7\n9\nWork docs\n",
						    "serialized replaced type executes");
					}
				}
				std::filesystem::remove(File);
				std::filesystem::remove(Directory);
			}
		}
	}
	return Failures ? 1 : 0;
}
