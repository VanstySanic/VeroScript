// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAnalysisControl.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptScriptExtensionsInternal.h"
#include "VeroScriptSourceMetadataIO.h"
#include <algorithm>
#include <stdexcept>

namespace VeroScript::ScriptExtensions
{
	namespace
	{
		constexpr auto Void = ENativeType::Void, String = ENativeType::String, Int = ENativeType::Int,
		               Bool = ENativeType::Bool, Wide = ENativeType::Int64;
		FNativeTypeDesc S(ENativeType T)
		{
			return FNativeTypeDesc::Scalar(T);
		}
		FNativeTypeDesc H(std::string N)
		{
			return FNativeTypeDesc::Host("std.compiler." + N);
		}
		FNativeTypeDesc A(std::string N)
		{
			return FNativeTypeDesc::ArrayOf(N == "string" ? N : "std.compiler." + N);
		}
		FNativeTypeDesc E(std::string N)
		{
			return FNativeTypeDesc::Enum("std.compiler." + N);
		}
		FValue Enum(std::string N, std::int64_t V)
		{
			return FEnumValue{"std.compiler." + N, V};
		}
		const FValue& Arg(const FNativeCallContext& C, std::size_t I)
		{
			const auto J = I + (C.HostThis ? 1 : 0);
			if (J >= C.ArgCount)
			{
				throw std::runtime_error("missing compilation service argument");
			}

			return C.Args[J];
		}
		FBridgeValue& This(const FNativeCallContext& C)
		{
			if (!C.HostThis)
			{
				throw std::runtime_error("missing compilation service receiver");
			}

			return *static_cast<FBridgeValue*>(C.HostThis);
		}
		bool Call(const FNativeCallContext& C, FValue& R, std::string& Error)
		{
			try
			{
				R = static_cast<FCall*>(C.UserData)->Invoke(C);
				return true;
			}
			catch (const FAnalysisCancelled&)
			{
				throw;
			}
			catch (const std::exception& X)
			{
				Error = X.what();
				return false;
			}
		}
		std::string Name(const FNativeTypeDesc& D)
		{
			if (D.Type == ENativeType::HostObject || D.Type == ENativeType::Enum ||
			    D.Type == ENativeType::Function)
			{
				return D.HostTypeName;
			}

			return {};
		}
		void Member(FTask& T, std::string Type, std::string NameValue, FNativeTypeDesc Result,
		    std::vector<FNativeTypeDesc> Params, std::function<FValue(const FNativeCallContext&)> Fn,
		    EHostMemberKind Kind = EHostMemberKind::Method)
		{
			auto F = std::make_shared<FCall>();
			F->Invoke = std::move(Fn);
			T.Calls.push_back(F);
			const auto Label = Type + "." + NameValue;
			FHostMemberDesc M;
			M.Name = std::move(NameValue);
			M.Kind = Kind;
			M.ReturnType = Result.Type;
			M.ReturnHostTypeName = Name(Result);
			M.ReturnElementTypeName = Result.ElementTypeName;
			M.Callback = Call;
			M.UserData = F.get();
			for (const auto& P : Params)
			{
				M.ParamTypes.push_back(P.Type);
				M.ParamHostTypeNames.push_back(Name(P));
				M.ParamElementTypeNames.push_back(P.ElementTypeName);
			}
			std::string Error;
			if (!T.Bindings.RegisterMember(
			        T.Bindings.FindType("std.compiler." + Type)->Id, std::move(M), Error))
			{
				throw std::runtime_error("compilation member registration " + Label + ": " + Error);
			}

			T.Bindings.RetainNativeCallback(F.get(), F);
		}
		void Free(FTask& T, std::string N, FNativeTypeDesc R, std::vector<FNativeTypeDesc> Ps,
		    std::function<FValue(const FNativeCallContext&)> Fn)
		{
			auto F = std::make_shared<FCall>();
			F->Invoke = std::move(Fn);
			T.Calls.push_back(F);
			std::vector<FNativeParamDesc> Params;
			for (auto& P : Ps)
			{
				FNativeParamDesc D;
				D.Type = std::move(P);
				Params.push_back(std::move(D));
			}
			std::string Error;
			if (!T.Bindings.RegisterNative("std.compiler." + N, R, std::move(Params), Call, F.get(), Error))
			{
				throw std::runtime_error(Error);
			}

			T.Bindings.RetainNativeCallback(F.get(), F);
		}
		struct FField
		{
			std::string Name;
			FNativeTypeDesc Type;
			FValue Default;
		};
		FField Str(std::string N)
		{
			return {std::move(N), S(String), std::string{}};
		}
		FField Flag(std::string N, bool V = false)
		{
			return {std::move(N), S(Bool), V};
		}
		FField Num(std::string N, std::int64_t V = 0)
		{
			return {std::move(N), S(Wide), V};
		}
		FField List(std::string N, std::string Element, std::vector<FValue> V = {})
		{
			return {std::move(N), A(Element),
			    Array(Element == "string" ? Element : "std.compiler." + Element, std::move(V))};
		}
		void Fields(FTask& T, std::string Type, std::vector<FField> Fs, bool Writable)
		{
			if (Writable)
			{
				Member(
				    T, Type, Type, H(Type), {},
				    [&T, Type, Fs](const auto&)
				{
					std::map<std::string, FValue> F;
					for (const auto& V : Fs)
					{
						auto D = V.Default;
						if (auto A = std::get_if<FDynamicArrayHandle>(&D))
						{
							D = Array((*A)->ElementTypeName, (*A)->Elements);
						}

						F[V.Name] = std::move(D);
					}
					return T.Make(Type, std::move(F));
				},
				    EHostMemberKind::Constructor);
			}

			for (const auto& F : Fs)
			{
				Member(
				    T, Type, F.Name, F.Type, {},
				    [N = F.Name, Writable](const auto& C)
				{
					auto& V = This(C);
					auto I = V.Fields.find(N);
					if (I == V.Fields.end())
					{
						throw std::runtime_error("unavailable compilation query: " + N);
					}

					auto R = I->second;
					if (!Writable)
					{
						if (auto A = std::get_if<FDynamicArrayHandle>(&R); A && *A)
						{
							R = Array((*A)->ElementTypeName, (*A)->Elements);
						}
					}

					return R;
				},
				    Writable ? EHostMemberKind::PropertyGetter : EHostMemberKind::Method);
				if (Writable)
				{
					Member(
					    T, Type, F.Name, S(Void), {F.Type},
					    [N = F.Name](const auto& C)
					{
						This(C).Fields[N] = Arg(C, 0);
						return std::int32_t{0};
					},
					    EHostMemberKind::PropertySetter);
				}
			}
		}
		std::uint32_t DescriptorMask(const FValue& V, std::uint32_t Allowed, bool Nonempty = false)
		{
			std::uint32_t R = 0;
			const auto& Values = Elements(V);
			if (Nonempty && Values.empty())
			{
				throw std::runtime_error("empty compilation descriptor mask");
			}

			for (const auto& F : Values)
			{
				auto I = Integer(F);
				if (I <= 0 || I > UINT32_MAX || (static_cast<std::uint32_t>(I) & ~Allowed))
				{
					throw std::runtime_error("invalid compilation descriptor enum");
				}

				R |= static_cast<std::uint32_t>(I);
			}
			return R;
		}
		FSourceRange Range(FTask& T, const FValue& V)
		{
			auto& F = T.Value(V, "source_range");
			auto B = Integer(Get(F, "begin")), E = Integer(Get(F, "end"));
			if (B < 0 || E < B || static_cast<std::uint64_t>(E) > SIZE_MAX)
			{
				throw std::runtime_error("invalid compilation source range");
			}

			FSourceRange R;
			R.SourceId = Text(Get(F, "source_id"));
			R.Begin = static_cast<std::size_t>(B);
			R.End = static_cast<std::size_t>(E);
			auto Position = [&](std::string_view Line, std::string_view Column)
			{
				auto L = Integer(Get(F, Line)), C = Integer(Get(F, Column));
				if (L <= 0 || C <= 0 || L > UINT32_MAX || C > UINT32_MAX)
				{
					throw std::runtime_error("invalid source position");
				}

				return FAnnotationSourcePosition{
				    R.SourceId, static_cast<std::uint32_t>(L), static_cast<std::uint32_t>(C)};
			};
			R.Position.Begin = Position("begin_line", "begin_column");
			R.Position.End = Position("end_line", "end_column");
			return R;
		}
		FCompileScope Scope(FTask& T, const FValue& V)
		{
			auto& F = T.Value(V, "scope");
			T.Validate(F, "scope");
			FCompileScope S;
			S.ModuleId = Text(Get(F, "module_id"));
			S.NamespacePath = Text(Get(F, "namespace_path"));
			S.OwnerId = Text(Get(F, "owner_id"));
			S.ScopeId = Text(Get(F, "scope_id"));
			S.InsertionBeforeId = Text(Get(F, "insertion_before_id"));
			S.Snapshot = static_cast<std::uint64_t>(Integer(Get(F, "snapshot")));
			return S;
		}
		FSourceOrigin Origin(FTask& T, const FValue& V)
		{
			FSourceOrigin O;
			if (auto P = std::get_if<FHostObjectHandle>(&V); !P || !*P)
			{
				return O;
			}

			auto& F = T.Value(V, "source_origin");
			if (auto P = std::get_if<FHostObjectHandle>(&F.Fields.at("range")); P && *P)
			{
				O.Range = Range(T, F.Fields.at("range"));
			}

			return O;
		}
		std::vector<FAnnotationArgument> AnnotationArguments(FTask& T, const FValue& V, std::size_t Depth = 0)
		{
			if (Depth > 64)
			{
				throw std::runtime_error("annotation argument depth exceeded");
			}

			std::vector<FAnnotationArgument> Out;
			for (const auto& Item : Elements(V))
			{
				auto& F = T.Value(Item, "annotation_argument");
				FAnnotationArgument A;
				A.Name = Text(Get(F, "name"));
				A.RawText = Text(Get(F, "raw_text"));
				A.Value = Text(Get(F, "value"));
				auto K = Integer(Get(F, "kind")), S = Integer(Get(F, "scalar_kind"));
				if (K < 0 || K > 2 || S < 0 || S > 4)
				{
					throw std::runtime_error("invalid annotation argument kind");
				}

				A.Kind = static_cast<EAnnotationArgumentKind>(K);
				A.ScalarKind = static_cast<EAnnotationScalarKind>(S);
				A.Children = AnnotationArguments(T, Get(F, "children"), Depth + 1);
				Out.push_back(std::move(A));
			}
			return Out;
		}
		std::vector<FDeclarationAnnotation> Annotations(FTask& T, const FValue& V)
		{
			std::vector<FDeclarationAnnotation> Out;
			for (const auto& Item : Elements(V))
			{
				auto& F = T.Value(Item, "annotation");
				FDeclarationAnnotation A;
				A.Name = Text(Get(F, "name"));
				A.RawText = Text(Get(F, "raw_text"));
				A.Arguments = AnnotationArguments(T, Get(F, "arguments"));
				A.Ordinal = Out.size();
				Out.push_back(std::move(A));
			}
			return Out;
		}
		std::vector<FSourceComment> Comments(FTask& T, const FValue& V)
		{
			std::vector<FSourceComment> Out;
			for (const auto& Item : Elements(V))
			{
				auto& F = T.Value(Item, "comment");
				FSourceComment C;
				C.Id = Text(Get(F, "id"));
				C.Text = Text(Get(F, "text"));
				C.RawText = Text(Get(F, "raw_text"));
				auto K = Integer(Get(F, "kind"));
				if (K < 0 || K > 3)
				{
					throw std::runtime_error("invalid compilation comment kind");
				}

				C.Kind = static_cast<ESourceCommentKind>(K);
				C.Activity = ESourceActivity::Active;
				C.Association = ECommentAssociation::Explicit;
				auto R = Get(F, "range");
				if (auto P = std::get_if<FHostObjectHandle>(&R); P && *P)
				{
					C.Range = Range(T, R);
				}

				Out.push_back(std::move(C));
			}
			return Out;
		}
		FCompileMetadata Metadata(FTask& T, const FValue& V)
		{
			FCompileMetadata Out;
			if (auto P = std::get_if<FHostObjectHandle>(&V); !P || !*P)
			{
				return Out;
			}

			auto& F = T.Value(V, "metadata");
			if (std::get<bool>(Get(F, "annotations_present")))
			{
				Out.Annotations = Annotations(T, Get(F, "annotations"));
			}

			if (std::get<bool>(Get(F, "comments_present")))
			{
				Out.Comments = Comments(T, Get(F, "comments"));
			}

			for (const auto& Item : Elements(Get(F, "children")))
			{
				auto& C = T.Value(Item, "metadata_child");
				auto B = Integer(Get(C, "begin")), E = Integer(Get(C, "end"));
				if (B < 0 || E < B)
				{
					throw std::runtime_error("invalid child metadata range");
				}

				FCompileMetadata::FChild D;
				D.Begin = static_cast<std::size_t>(B);
				D.End = static_cast<std::size_t>(E);
				D.Kind = Text(Get(C, "kind"));
				D.Metadata.Documentation = Text(Get(C, "documentation"));
				D.Metadata.Annotations = Annotations(T, Get(C, "annotations"));
				D.Metadata.Comments = Comments(T, Get(C, "comments"));
				Out.Children.push_back(std::move(D));
			}
			return Out;
		}
		std::vector<FAnnotationArgumentDefinition> Definitions(FTask& T, const FValue& V, std::size_t Depth = 0)
		{
			if (Depth > 64)
			{
				throw std::runtime_error("annotation description depth exceeded");
			}

			std::vector<FAnnotationArgumentDefinition> Out;
			for (const auto& A : Elements(V))
			{
				auto& F = T.Value(A, "annotation_argument_definition");
				FAnnotationArgumentDefinition D;
				D.Name = Text(Get(F, "name"));
				D.Detail = Text(Get(F, "detail"));
				D.Documentation = Text(Get(F, "documentation"));
				D.Children = Definitions(T, Get(F, "children"), Depth + 1);
				Out.push_back(std::move(D));
			}
			return Out;
		}
		FHostObjectHandle AnnotationArgument(FTask& T, const FAnnotationArgument& A)
		{
			std::vector<FValue> Children;
			for (const auto& C : A.Children)
			{
				Children.push_back(AnnotationArgument(T, C));
			}

			return T.Make("annotation_argument_info",
			    {{"name", A.Name}, {"raw_text", A.RawText}, {"value", A.Value},
			        {"kind", static_cast<std::int32_t>(A.Kind)},
			        {"scalar_kind", static_cast<std::int32_t>(A.ScalarKind)},
			        {"arguments", Array("std.compiler.annotation_argument_info", std::move(Children))},
			        {"argument_count", static_cast<std::int32_t>(A.Children.size())}, {"is_valid", true}});
		}
		FHostObjectHandle SourceRange(FTask&, const FSourceRange&);
		FHostObjectHandle SourceOrigin(FTask&, const FSourceOrigin&);
		FHostObjectHandle Annotation(FTask& T, const FDeclarationAnnotation* A)
		{
			std::vector<FValue> Args;
			if (A)
			{
				for (const auto& C : A->Arguments)
				{
					Args.push_back(AnnotationArgument(T, C));
				}
			}

			auto Result = T.Make("annotation_info",
			    {{"name", A ? A->Name : ""}, {"raw_text", A ? A->RawText : ""},
			        {"definition_id", A ? A->DefinitionId : ""},
			        {"arguments", Array("std.compiler.annotation_argument_info", std::move(Args))},
			        {"argument_count", static_cast<std::int32_t>(A ? A->Arguments.size() : 0)},
			        {"is_valid", A != nullptr}});
			if (A)
			{
				const auto* Definition =
				    T.Context ? T.Context->SourceMetadata.FindDefinition(A->DefinitionId) : nullptr;
				if (!Definition)
				{
					for (const auto& D : T.Processing.AnnotationDefinitions)
					{
						if (D.Id == A->DefinitionId)
						{
							Definition = &D;
							break;
						}
					}
				}

				if (Definition)
				{
					T.Value(FValue(Result)).AnnotationDefinition = *Definition;
				}
			}
			return Result;
		}
		FHostObjectHandle Comment(FTask& T, const FSourceComment* C)
		{
			return T.Make("comment_info",
			    {{"id", C ? C->Id : ""},
			        {"text", C ? C->Text : ""},
			        {"raw_text", C ? C->RawText : ""},
			        {"owner_id", C ? C->OwnerId : ""},
			        {"module_id", C ? C->ModuleId : ""},
			        {"source_id", C ? C->Range.SourceId : ""},
			        {"begin", static_cast<std::int64_t>(C ? C->Range.Begin : 0)},
			        {"end", static_cast<std::int64_t>(C ? C->Range.End : 0)},
			        {"source_range", SourceRange(T, C ? C->Range : FSourceRange{})},
			        {"origin", SourceOrigin(T, C ? C->Origin : FSourceOrigin{})},
			        {"association", static_cast<std::int32_t>(C ? C->Association : ECommentAssociation::Module)},
			        {"kind", static_cast<std::int32_t>(C ? C->Kind : ESourceCommentKind::Line)},
			        {"activity", static_cast<std::int32_t>(C ? C->Activity : ESourceActivity::Unknown)},
			        {"is_valid", C != nullptr}});
		}
		FHostObjectHandle Parameter(FTask& T, const FAnnotationParameterView& P)
		{
			std::vector<FValue> Annotations, Comments;
			for (const auto& A : P.Metadata.Annotations)
			{
				Annotations.push_back(Annotation(T, &A));
			}

			for (const auto& C : P.Metadata.Comments)
			{
				Comments.push_back(Comment(T, &C));
			}

			return T.Make("parameter_info",
			    {{"id", P.Id}, {"name", P.Name}, {"type_name", P.TypeName},
			        {"canonical_type_name", P.CanonicalTypeName}, {"is_ref", P.bIsRef},
			        {"is_const", P.bIsConst}, {"is_variadic", P.bIsVariadic}, {"has_default", P.bHasDefault},
			        {"default_source", P.DefaultValueSource},
			        {"source_range", SourceRange(T, P.Metadata.SourceRange)},
			        {"documentation", P.Metadata.Documentation},
			        {"annotations", Array("std.compiler.annotation_info", std::move(Annotations))},
			        {"comments", Array("std.compiler.comment_info", std::move(Comments))}});
		}
		FHostObjectHandle DescriptionArgument(FTask& T, const FAnnotationArgumentDefinition& A)
		{
			std::vector<FValue> Children;
			for (const auto& C : A.Children)
			{
				Children.push_back(DescriptionArgument(T, C));
			}

			return T.Make("annotation_definition_info",
			    {{"is_valid", true}, {"id", std::string{}}, {"name", A.Name}, {"detail", A.Detail},
			        {"documentation", A.Documentation}, {"provider", std::string{}},
			        {"target_mask", std::int32_t{0}}, {"case_sensitive", true},
			        {"argument_count", static_cast<std::int32_t>(A.Children.size())},
			        {"arguments", Array("std.compiler.annotation_definition_info", std::move(Children))}});
		}
		FHostObjectHandle Description(FTask& T, const FAnnotationDefinition* D)
		{
			std::vector<FValue> Children;
			if (D)
			{
				for (const auto& A : D->Arguments)
				{
					Children.push_back(DescriptionArgument(T, A));
				}
			}

			return T.Make("annotation_definition_info",
			    {{"is_valid", D != nullptr}, {"id", D ? D->Id : ""}, {"name", D ? D->Name : ""},
			        {"detail", D ? D->Detail : ""}, {"documentation", D ? D->Documentation : ""},
			        {"provider", D ? D->ProviderId : ""},
			        {"target_mask", static_cast<std::int32_t>(D ? D->Targets : 0)},
			        {"case_sensitive", !D || D->CaseSensitive},
			        {"argument_count", static_cast<std::int32_t>(D ? D->Arguments.size() : 0)},
			        {"arguments", Array("std.compiler.annotation_definition_info", std::move(Children))}});
		}
		FHostObjectHandle SourceRange(FTask& T, const FSourceRange& R)
		{
			return T.Make("source_range", {{"source_id", R.SourceId}, {"begin", static_cast<std::int64_t>(R.Begin)}, {"end", static_cast<std::int64_t>(R.End)}, {"begin_line", static_cast<std::int64_t>(R.Position.Begin.Line)}, {"begin_column", static_cast<std::int64_t>(R.Position.Begin.Column)}, {"end_line", static_cast<std::int64_t>(R.Position.End.Line)}, {"end_column", static_cast<std::int64_t>(R.Position.End.Column)}});
		}
		FHostObjectHandle SourceOrigin(FTask& T, const FSourceOrigin& O)
		{
			return T.Make("source_origin_info", {{"range", SourceRange(T, O.Range)}, {"processor_id", O.ProcessorId}, {"generation_id", O.GenerationId}});
		}
		std::string TargetId(FTask& T, const FValue& V)
		{
			if (auto S = std::get_if<std::string>(&V))
			{
				return *S;
			}

			return Text(Get(T.Value(V, "declaration_info"), "id"));
		}
		void ActiveOutput(FTask& T, const FNativeCallContext& C)
		{
			T.Validate(This(C), "output");
			if (!T.Output || !T.Context)
			{
				throw std::runtime_error("output outside compiler callback");
			}
		}
		class FAdapter final : public ICompileProcessor
		{
		public:
			void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
			{
				auto T = Task.lock();
				if (!T)
				{
					throw std::runtime_error("expired script compilation processor");
				}

				T->Check();
				auto P = std::find_if(T->Processors.begin(), T->Processors.end(),
				    [&](const auto& P)
				{
					return P.Description.ProcessorId == Id;
				});
				if (P == T->Processors.end())
				{
					throw std::runtime_error("missing script compilation processor");
				}

				T->Executing = P->Module;
				T->Context = &C;
				T->Output = &O;
				++T->Epoch;
				try
				{
					T->Invoke(*P->Module, P->Callback, {T->Make("context", {}, true), T->Make("output", {}, true)});
					T->Context = nullptr;
					T->Output = nullptr;
					T->Executing.reset();
					++T->Epoch;
				}
				catch (...)
				{
					T->Context = nullptr;
					T->Output = nullptr;
					T->Executing.reset();
					++T->Epoch;
					throw;
				}
			}

			FAdapter(std::weak_ptr<FTask> T, std::string I) : Task(std::move(T)), Id(std::move(I))
			{
			}

		private:
			std::weak_ptr<FTask> Task;
			std::string Id;
		};
	} // namespace
	void FTask::RegisterAnnotation(const FValue& V)
	{
		Check();
		if (!Registering || Frozen)
		{
			throw std::runtime_error("annotation registration is frozen");
		}

		auto& F = Value(V, "annotation_definition");
		FAnnotationDefinition D;
		D.Name = Text(Get(F, "name"));
		D.CaseSensitive = std::get<bool>(Get(F, "case_sensitive"));
		D.Detail = Text(Get(F, "detail"));
		D.Documentation = Text(Get(F, "documentation"));
		D.Targets = DescriptorMask(Get(F, "targets"), AnnotationAllTargets, true);
		D.Arguments = Definitions(*this, Get(F, "arguments"));
		D.ProviderId =
		    "script-extension:" + std::to_string(Runtime::Fnv1a64(14695981039346656037ull, Executing->Identity));
		FSourceMetadataSnapshot M;
		D.Id = "pending";
		M.Definitions.push_back(D);
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		// Canonical descriptions are validated and fingerprinted by the same serializer.
		if (D.Name.empty())
		{
			throw std::runtime_error("annotation name is empty");
		}

		std::string Identity = D.Name + "\x1f" + D.Detail + "\x1f" + D.Documentation + "\x1f" +
		    std::to_string(D.Targets) +
		    (D.CaseSensitive ? "\x1f"
		                       "1"
		                     : "\x1f"
		                       "0");
		std::function<void(const std::vector<FAnnotationArgumentDefinition>&)> Add = [&](const auto& As)
		{
			Identity += std::to_string(As.size()) + ":";
			for (const auto& A : As)
			{
				Identity += std::to_string(A.Name.size()) + ":" + A.Name + std::to_string(A.Detail.size()) + ":" +
				    A.Detail + std::to_string(A.Documentation.size()) + ":" + A.Documentation;
				Add(A.Children);
			}
		};
		Add(D.Arguments);
		D.Id =
		    "description:" + D.ProviderId + ":" + std::to_string(Runtime::Fnv1a64(14695981039346656037ull, Identity));
		for (const auto& Old : Processing.AnnotationDefinitions)
		{
			if (AnnotationNamesEqual(Old.Name, D.Name, Old.CaseSensitive && D.CaseSensitive))
			{
				auto A = Old;
				A.Id = D.Id;
				A.ProviderId = D.ProviderId;
				FSourceMetadataSnapshot L, R;
				L.Definitions = {A};
				R.Definitions = {D};
				std::vector<std::uint8_t> LB, RB;
				EncodeSourceMetadata(L, LB, Error);
				EncodeSourceMetadata(R, RB, Error);
				if (LB != RB)
				{
					throw std::runtime_error("conflicting annotation descriptions: " + D.Name);
				}

				return;
			}
		}

		if (Processing.AnnotationDefinitions.size() + Processors.size() >=
		    Inputs.ScriptCompileExtensions.MaxRegistrations)
		{
			throw std::runtime_error("compiler registration budget exceeded");
		}

		if (!EncodeSourceMetadata(M, Bytes, Error))
		{
			throw std::runtime_error(Error);
		}

		Processing.AnnotationDefinitions.push_back(std::move(D));
	}
	void FTask::RegisterProcessor(const FValue& V, const FValue& Factory)
	{
		Check();
		if (!Registering || Frozen)
		{
			throw std::runtime_error("processor registration is frozen");
		}

		if (Processors.size() + Processing.AnnotationDefinitions.size() >=
		    Inputs.ScriptCompileExtensions.MaxRegistrations)
		{
			throw std::runtime_error("compiler registration budget exceeded");
		}

		auto& F = Value(V, "processor_definition");
		auto P = std::get_if<FFunctionHandle>(&Factory);
		if (!P || !*P)
		{
			throw std::runtime_error("invalid compiler processor factory");
		}

		FProcessor R;
		R.Module = Executing;
		if (Inputs.ModuleEnvironment)
		{
			R.Description.Origin.ProviderId = Inputs.ModuleEnvironment->GetSourceOwner(Executing->Path);
			R.Description.Origin.ModulePath = Executing->Path;
		}
		R.Factory = *P;
		R.Description.ProcessorId = Text(Get(F, "id"));
		R.Description.Version = Text(Get(F, "version"));
		R.Description.ConfigurationKey = Text(Get(F, "configuration_key"));
		auto Order = Integer(Get(F, "order"));
		if (Order < INT32_MIN || Order > INT32_MAX)
		{
			throw std::runtime_error("compiler processor order out of range");
		}

		R.Description.Order = static_cast<std::int32_t>(Order);
		R.Description.Cacheable = std::get<bool>(Get(F, "cacheable"));
		R.Description.Phases = DescriptorMask(Get(F, "phases"), 31, true);
		R.Description.Operations = DescriptorMask(Get(F, "operations"), 63);
		for (const auto& SubscriptionValue : Elements(Get(F, "subscriptions")))
		{
			auto& S = Value(SubscriptionValue, "subscription");
			FCompileSubscription D;
			auto Kind = Integer(Get(S, "event"));
			if (Kind < 0 || Kind > 4)
			{
				throw std::runtime_error("invalid compiler event");
			}

			D.Event = static_cast<ECompileProcessingEvent>(Kind);
			D.Targets = DescriptorMask(Get(S, "targets"), AnnotationAllTargets, true);
			D.Phases = DescriptorMask(Get(S, "phases"), 31);
			D.AnnotationName = Text(Get(S, "annotation_name"));
			D.SyntaxKind = Text(Get(S, "syntax_kind"));
			auto CommentKind = Integer(Get(S, "comment_kind"));
			if (CommentKind < -1 || CommentKind > 3)
			{
				throw std::runtime_error("invalid comment subscription kind");
			}

			if (CommentKind >= 0)
			{
				D.CommentKind = static_cast<ESourceCommentKind>(CommentKind);
			}

			R.Description.Subscriptions.push_back(std::move(D));
		}
		const auto Id = R.Description.ProcessorId;
		R.Description.Factory = [W = weak_from_this(), Id]
		{
			return std::make_unique<FAdapter>(W, Id);
		};
		FCompileProcessorRegistry Validate;
		std::string Error;
		if (!Validate.Register(R.Description, Error))
		{
			throw std::runtime_error(Error);
		}

		for (const auto& Registered : Processors)
		{
			if (Registered.Description.ProcessorId == Id)
			{
				throw std::runtime_error("duplicate compiler processor: " + Id);
			}
		}

		if ((*P)->ProgramOwner.get() != Executing->Code.get() ||
		    (*P)->ProgramGeneration != Executing->Generation ||
		    (*P)->TypeName != "fn()->fn(std.compiler.context,std.compiler.output)->void")
		{
			throw std::runtime_error("compiler factory has a foreign module or invalid signature");
		}

		Executing->Callables.push_back(R.Factory);
		Processors.push_back(std::move(R));
	}
	void FTask::InitializeBindings()
	{
		if (const auto Catalog = Inputs.ScriptCompileExtensions.Bindings)
		{
			std::string Error;
			std::map<std::uint32_t, FGenericHostTypeTemplateId> Templates;
			for (const auto& T : Catalog->GetGenericTypeTemplates())
			{
				if (T.Name.rfind("std.compiler.", 0) == 0)
				{
					throw std::runtime_error("custom compilation templates cannot replace core services");
				}

				FGenericHostTypeTemplateId Id;
				if (!Bindings.RegisterGenericHostTypeTemplate(
				        T.Name, T.Arity, T.Instantiate, T.UserData, Id, Error, T.Documentation))
				{
					throw std::runtime_error(Error);
				}

				Templates.emplace(T.Id.Value, Id);
			}
			for (const auto& T : Catalog->GetTypes())
			{
				if (T.Name.rfind("std.compiler.", 0) == 0)
				{
					throw std::runtime_error("custom compilation bindings cannot replace core services");
				}

				FHostTypeId Id;
				if (!Bindings.RegisterType(T.Name, T.Kind, Id, Error, T.Annotations))
				{
					throw std::runtime_error(Error);
				}

				if (T.Kind == EHostTypeKind::Enum &&
				    (!Bindings.SetEnumUnderlyingType(Id, T.EnumUnderlyingTypeName, Error) ||
				        !Bindings.SetEnumScoped(Id, T.bScopedEnum, Error)))
				{
					throw std::runtime_error(Error);
				}

				for (const auto& M : T.Members)
				{
					if (!Bindings.RegisterMember(Id, M, Error))
					{
						throw std::runtime_error(Error);
					}
				}

				if (T.CloneCallback &&
				    !Bindings.RegisterTypeClone(Id, T.CloneCallback, T.CloneUserData, Error))
				{
					throw std::runtime_error(Error);
				}

				if (T.EqualsCallback &&
				    !Bindings.RegisterTypeEquals(Id, T.EqualsCallback, T.EqualsUserData, Error))
				{
					throw std::runtime_error(Error);
				}

				if (T.HashCallback && !Bindings.RegisterTypeHash(Id, T.HashCallback, T.HashUserData, Error))
				{
					throw std::runtime_error(Error);
				}

				if (T.bInheritable && !Bindings.SetTypeInheritable(Id, true, Error))
				{
					throw std::runtime_error(Error);
				}

				if (T.SubclassFactory &&
				    !Bindings.RegisterTypeSubclassFactory(
				        Id, T.SubclassFactory, T.SubclassFactoryUserData, Error))
				{
					throw std::runtime_error(Error);
				}

				if (!T.Documentation.empty() && !Bindings.SetTypeDocumentation(Id, T.Documentation, Error))
				{
					throw std::runtime_error(Error);
				}

				if (T.GenericTemplateId.Value)
				{
					auto Template = Templates.find(T.GenericTemplateId.Value);
					if (Template == Templates.end() ||
					    !Bindings.SetTypeGenericInstanceInfo(
					        Id, Template->second, T.GenericTypeArguments, Error))
					{
						throw std::runtime_error("invalid compilation generic host identity: " + Error);
					}
				}
				for (const auto& Lifetime : T.CallbackLifetimes)
				{
					Bindings.RetainNativeCallback(Lifetime.first, Lifetime.second);
				}
			}
			for (const auto& Alias : Catalog->GetTypeAliases())
			{
				if (!Bindings.RegisterTypeAlias(
				        Alias.Name, Alias.TypeParams, Alias.TargetTypeName, Error, Alias.Documentation))
				{
					throw std::runtime_error(Error);
				}
			}

			for (const auto& F : Catalog->GetFunctions())
			{
				FNativeFunction Description;
				Description.HostTypeName = F.ReturnHostTypeName;
				Description.ParamTypes = F.ParamTypes;
				Description.ParamHostTypeNames = F.ParamHostTypeNames;
				Description.ParamElementTypeNames = F.ParamElementTypeNames;
				Description.ParamKeyTypeNames = F.ParamKeyTypeNames;
				Description.ParamValueTypeNames = F.ParamValueTypeNames;
				Description.ParamFlags = F.ParamFlags;
				for (std::size_t I = 0; I < Description.ParamTypes.size(); ++I)
				{
					std::string Type;
					if (!ResolveHostRuntimeTypeName(*Catalog, ResolveNativeParamTypeName(Description, I), Type, Error))
					{
						throw std::runtime_error(Error);
					}
					Description.RuntimeParamTypeNames.push_back(std::move(Type));
				}
				const auto Signature = NativeParameterSignature(Description);
				bool Exists = false;
				for (const auto& N : Bindings.GetNativeFunctions())
				{
					if (N.Name == F.Name && NativeParameterSignature(N) == Signature)
					{
						Exists = true;
						break;
					}
				}

				if (Exists)
				{
					continue;
				}

				if (F.Name.rfind("std.compiler.", 0) == 0)
				{
					throw std::runtime_error("custom compilation bindings cannot replace core services");
				}

				const FNativeFunction* Native = nullptr;
				for (const auto& N : Catalog->GetNativeFunctions())
				{
					if (N.Name == F.Name && NativeParameterSignature(N) == Signature)
					{
						Native = &N;
						break;
					}
				}

				if (!Native || !Bindings.RegisterNative(F, Native->Callback, Native->UserData, Error))
				{
					throw std::runtime_error("invalid compilation binding: " + F.Name + ": " + Error);
				}
			}
			for (const auto& N : Catalog->GetNativeFunctions())
			{
				if (N.CallbackOwner)
				{
					Bindings.RetainNativeCallback(N.UserData, N.CallbackOwner);
				}
			}
		}
		std::string Error;
		const std::vector<std::string> Types = {"annotation_definition",
		    "annotation_argument_definition",
		    "processor_definition",
		    "subscription",
		    "registration",
		    "context",
		    "output",
		    "scope",
		    "block_info",
		    "statement_anchor_info",
		    "source_range",
		    "declaration_info",
		    "parameter_info",
		    "source_document_info",
		    "annotation_info",
		    "annotation_argument_info",
		    "comment_info",
		    "syntax_info",
		    "dependency",
		    "configuration_value",
		    "metadata",
		    "annotation",
		    "annotation_argument",
		    "comment",
		    "source_origin",
		    "source_mapping",
		    "metadata_child",
		    "generation_operation",
		    "annotation_definition_info",
		    "processor_info",
		    "type_bound_info",
		    "template_parameter_info",
		    "source_origin_info",
		    "source_mapping_info"};
		const std::set<std::string> ValueTypes = {"annotation_definition",
		    "annotation_argument_definition",
		    "processor_definition",
		    "subscription",
		    "source_range",
		    "metadata",
		    "annotation",
		    "annotation_argument",
		    "comment",
		    "source_origin",
		    "source_mapping",
		    "metadata_child",
		    "generation_operation"};
		for (const auto& T : Types)
		{
			FHostTypeId Id;
			if (!Bindings.RegisterType("std.compiler." + T,
			        ValueTypes.count(T) ? EHostTypeKind::Struct : EHostTypeKind::Class, Id, Error))
			{
				throw std::runtime_error(Error);
			}

			if (ValueTypes.count(T))
			{
				auto Fn = std::make_shared<FCall>();
				Fn->Invoke = [this, T](const auto& C) -> FValue
				{
					if (!C.ArgCount)
					{
						throw std::runtime_error("missing clone source");
					}

					return CloneValue(Value(C.Args[0], T));
				};
				Calls.push_back(Fn);
				if (!Bindings.RegisterTypeClone(Id, Call, Fn.get(), Error))
				{
					throw std::runtime_error(Error);
				}

				Bindings.RetainNativeCallback(Fn.get(), Fn);
			}
		}
		const std::map<std::string, std::vector<std::pair<std::string, int>>> Enums = {
		    {"phase", {{"raw_source", 1}, {"active_source", 2}, {"types", 4}, {"members", 8}, {"bodies", 16}}},
		    {"event", {{"module", 0}, {"declaration", 1}, {"annotation", 2}, {"comment", 3}, {"syntax", 4}}},
		    {"operation",
		        {{"edit_source", 1},
		            {"add_declaration", 2},
		            {"replace_declaration", 4},
		            {"complete_body", 8},
		            {"replace_body", 16},
		            {"set_metadata", 32}}},
		    {"target",
		        {{"struct_type", 1},
		            {"class_type", 2},
		            {"interface_type", 4},
		            {"enum_type", 8},
		            {"field", 16},
		            {"static_field", 32},
		            {"method", 64},
		            {"static_method", 128},
		            {"constructor", 256},
		            {"destructor", 512},
		            {"function", 1024},
		            {"global_variable", 2048},
		            {"enum_member", 4096},
		            {"type_alias", 8192},
		            {"namespace_scope", 16384},
		            {"parameter", 32768},
		            {"module", 65536},
		            {"all", 131071}}}};
		for (const auto& Pair : Enums)
		{
			FHostTypeId Id;
			if (!Bindings.RegisterType("std.compiler." + Pair.first, EHostTypeKind::Enum, Id, Error) ||
			    !Bindings.SetEnumScoped(Id, true, Error))
			{
				throw std::runtime_error(Error);
			}

			for (const auto& V : Pair.second)
			{
				FHostMemberDesc D;
				D.Kind = EHostMemberKind::EnumMember;
				D.Name = V.first;
				D.EnumValue = V.second;
				if (!Bindings.RegisterMember(Id, D, Error))
				{
					throw std::runtime_error(Error);
				}
			}
		}
		Fields(*this, "annotation_definition",
		    {Str("name"), Str("detail"), Str("documentation"), Flag("case_sensitive", true),
		        List("targets", "target", {Enum("target", AnnotationAllTargets)}),
		        List("arguments", "annotation_argument_definition")},
		    true);
		Fields(*this, "annotation_argument_definition",
		    {Str("name"), Str("detail"), Str("documentation"), List("children", "annotation_argument_definition")},
		    true);
		Fields(*this, "processor_definition",
		    {Str("id"), Str("version"), Str("configuration_key"), Num("order"), Flag("cacheable", true),
		        List("phases", "phase"), List("operations", "operation"), List("subscriptions", "subscription")},
		    true);
		Fields(*this, "subscription",
		    {{"event", E("event"), Enum("event", 1)},
		        List("targets", "target", {Enum("target", AnnotationAllTargets)}),
		        List("phases", "phase"),
		        Str("annotation_name"),
		        Str("syntax_kind"),
		        Num("comment_kind", -1)},
		    true);
		Fields(*this, "source_range",
		    {Str("source_id"), Num("begin"), Num("end"), Num("begin_line", 1), Num("begin_column", 1),
		        Num("end_line", 1), Num("end_column", 1)},
		    true);
		Fields(*this, "metadata",
		    {Flag("annotations_present"), Flag("comments_present"), List("annotations", "annotation"),
		        List("comments", "comment"), List("children", "metadata_child")},
		    true);
		Fields(*this, "annotation", {Str("name"), Str("raw_text"), List("arguments", "annotation_argument")}, true);
		Fields(*this, "annotation_argument",
		    {Str("name"),
		        Str("raw_text"),
		        Str("value"),
		        {"kind", S(Int), std::int32_t{0}},
		        {"scalar_kind", S(Int), std::int32_t{0}},
		        List("children", "annotation_argument")},
		    true);
		Fields(*this, "comment",
		    {Str("id"),
		        Str("text"),
		        Str("raw_text"),
		        {"kind", S(Int), std::int32_t{0}},
		        {"range", H("source_range"), FHostObjectHandle{}}},
		    true);
		Fields(*this, "source_origin", {{"range", H("source_range"), FHostObjectHandle{}}}, true);
		Fields(*this, "source_mapping", {Num("begin"), Num("end"), {"origin", H("source_origin"), FHostObjectHandle{}}},
		    true);
		Fields(*this, "metadata_child",
		    {Num("begin"), Num("end"), Str("kind"), Str("documentation"), List("annotations", "annotation"),
		        List("comments", "comment")},
		    true);
		Fields(*this, "generation_operation",
		    {{"kind", E("operation"), Enum("operation", 2)},
		        {"scope", H("scope"), FHostObjectHandle{}},
		        Str("target_id"),
		        Str("source"),
		        {"edit_range", H("source_range"), FHostObjectHandle{}},
		        {"origin", H("source_origin"), FHostObjectHandle{}},
		        {"metadata", H("metadata"), FHostObjectHandle{}},
		        List("source_mappings", "source_mapping")},
		    true);
		Fields(*this, "scope", {Str("module_id"), Str("namespace_path"), Str("owner_id"), Num("snapshot"), Str("scope_id"), Str("insertion_before_id")}, false);
		Fields(*this, "dependency", {Str("path"), Str("content"), Flag("exists")}, false);
		Fields(*this, "configuration_value", {Str("value"), Flag("exists")}, false);
		Fields(*this, "source_document_info", {Str("id"), Str("module_id"), Str("text"), Flag("is_valid")}, false);
		Fields(*this, "statement_anchor_info", {Str("id"), {"range", H("source_range"), FHostObjectHandle{}}}, false);
		Fields(*this, "block_info", {Str("module_id"), Str("scope_id"), Str("parent_scope_id"), Str("owner_id"), Str("end_anchor_id"), List("statements", "statement_anchor_info")}, false);
		Fields(*this, "parameter_info",
		    {Str("id"), Str("name"), Str("type_name"), Str("canonical_type_name"), Flag("is_ref"), Flag("is_const"),
		        Flag("is_variadic"), Flag("has_default"), Str("default_source")},
		    false);
		Fields(*this, "template_parameter_info",
		    {Str("id"), Str("name"), Str("value_type_name"), Str("default_source"),
		        Flag("is_value"), Flag("has_default"), Flag("is_enclosing")},
		    false);
		Fields(*this, "declaration_info",
		    {Str("id"),
		        Str("scope_id"), Str("parent_scope_id"), Flag("is_nested"), Flag("is_local"),
		        Str("owner_id"),
		        Str("name"),
		        Str("module_id"),
		        Str("type_name"),
		        Str("canonical_type_name"),
		        Str("body_source"),
		        Str("body_module_id"),
		        Str("default_source"),
		        {"target", E("target"), Enum("target", 0)},
		        {"access", S(Int), std::int32_t{0}},
		        Flag("is_valid"),
		        Flag("is_exported"),
		        Flag("has_body"),
		        Flag("is_virtual"),
		        Flag("is_override"),
		        Flag("is_final"),
		        Flag("is_const"),
		        Flag("is_constexpr"),
		        Flag("is_ref"),
		        Flag("is_static"),
		        Flag("is_deleted"),
		        Flag("is_pure_virtual"),
		        Flag("is_generated"),
		        Flag("is_from_package"),
		        List("parameters", "parameter_info"),
		        List("type_parameters", "string"),
		        List("template_parameters", "template_parameter_info"),
		        List("base_types", "string"),
		        List("annotations", "annotation_info"),
		        List("comments", "comment_info")},
		    false);
		Fields(*this, "annotation_info",
		    {Str("name"),
		        Str("raw_text"),
		        Str("definition_id"),
		        List("arguments", "annotation_argument_info"),
		        {"argument_count", S(Int), std::int32_t{0}},
		        Flag("is_valid")},
		    false);
		Fields(*this, "annotation_argument_info",
		    {Str("name"),
		        Str("raw_text"),
		        Str("value"),
		        {"kind", S(Int), std::int32_t{0}},
		        {"scalar_kind", S(Int), std::int32_t{0}},
		        List("arguments", "annotation_argument_info"),
		        {"argument_count", S(Int), std::int32_t{0}},
		        Flag("is_valid")},
		    false);
		Fields(*this, "comment_info",
		    {Str("id"),
		        Str("text"),
		        Str("raw_text"),
		        Str("owner_id"),
		        Str("source_id"),
		        Num("begin"),
		        Num("end"),
		        {"kind", S(Int), std::int32_t{0}},
		        {"activity", S(Int), std::int32_t{0}},
		        Flag("is_valid")},
		    false);
		Fields(*this, "syntax_info",
		    {Str("id"), Str("owner_id"), Str("kind"), Str("name"), Str("source_id"), Num("begin"), Num("end"),
		        Flag("is_valid")},
		    false);
		Fields(*this, "annotation_definition_info",
		    {Str("id"),
		        Str("name"),
		        Str("detail"),
		        Str("documentation"),
		        Str("provider"),
		        Flag("case_sensitive", true),
		        Flag("is_valid"),
		        {"target_mask", S(Int), std::int32_t{0}},
		        {"argument_count", S(Int), std::int32_t{0}},
		        List("arguments", "annotation_definition_info")},
		    false);
		Fields(*this, "processor_info",
		    {Str("id"),
		        Str("version"),
		        Str("configuration_key"),
		        Num("order"),
		        Flag("cacheable"),
		        {"phase_mask", S(Int), std::int32_t{0}},
		        {"operation_mask", S(Int), std::int32_t{0}}},
		    false);
		Fields(*this, "type_bound_info", {Str("parameter"), Str("type_name"), Str("canonical_type_name")}, false);
		Fields(*this, "declaration_info",
		    {List("type_parameter_defaults", "string"),
		        List("canonical_base_types", "string"),
		        {"base_access", FNativeTypeDesc::ArrayOf("int"), Array("int")},
		        List("type_bounds", "type_bound_info"),
		        Str("documentation"),
		        {"source_range", H("source_range"), FHostObjectHandle{}},
		        Flag("has_default"),
		        Flag("is_mutable"),
		        Flag("is_explicit"),
		        Flag("is_defaulted")},
		    false);
		Fields(*this, "parameter_info",
		    {{"source_range", H("source_range"), FHostObjectHandle{}},
		        Str("documentation"),
		        List("annotations", "annotation_info"),
		        List("comments", "comment_info")},
		    false);
		Fields(*this, "comment_info",
		    {Str("module_id"),
		        Num("association"),
		        {"source_range", H("source_range"), FHostObjectHandle{}},
		        {"origin", H("source_origin_info"), FHostObjectHandle{}}},
		    false);
		Fields(*this, "source_document_info",
		    {Num("byte_count"),
		        {"line_offsets", FNativeTypeDesc::ArrayOf("uint64"), Array("uint64")},
		        List("mappings", "source_mapping_info"),
		        List("comments", "comment_info")},
		    false);
		Fields(*this, "source_origin_info",
		    {{"range", H("source_range"), FHostObjectHandle{}}, Str("processor_id"), Str("generation_id")}, false);
		Fields(*this, "source_mapping_info",
		    {Num("begin"), Num("end"), {"origin", H("source_origin_info"), FHostObjectHandle{}}}, false);
		Fields(*this, "syntax_info",
		    {Str("parent_id"),
		        Str("type_name"),
		        Str("documentation"),
		        {"target", E("target"), Enum("target", 0)},
		        {"source_range", H("source_range"), FHostObjectHandle{}},
		        {"origin", H("source_origin_info"), FHostObjectHandle{}},
		        List("annotations", "annotation_info"),
		        List("comments", "comment_info")},
		    false);
		for (const auto& Name : {"signed_value", "unsigned_value", "bool_value"})
		{
			Member(*this, "annotation_argument_info", Name,
			    S(Name == std::string("bool_value")             ? Bool
			            : Name == std::string("unsigned_value") ? ENativeType::UInt64
			                                                    : Wide),
			    {},
			    [Name](const auto& C) -> FValue
			{
				auto& F = This(C);
				FAnnotationArgument A;
				A.Kind = static_cast<EAnnotationArgumentKind>(Integer(Get(F, "kind")));
				A.ScalarKind = static_cast<EAnnotationScalarKind>(Integer(Get(F, "scalar_kind")));
				A.Value = Text(Get(F, "value"));
				if (Name == std::string("signed_value"))
				{
					std::int64_t V;
					if (!AnnotationArgumentInt64(A, V))
					{
						throw std::runtime_error("annotation value is not int64");
					}

					return V;
				}
				if (Name == std::string("unsigned_value"))
				{
					std::uint64_t V;
					if (!AnnotationArgumentUInt64(A, V))
					{
						throw std::runtime_error("annotation value is not uint64");
					}

					return V;
				}
				if (A.Kind != EAnnotationArgumentKind::Scalar ||
				    A.ScalarKind != EAnnotationScalarKind::Boolean ||
				    (A.Value != "true" && A.Value != "false"))
				{
					throw std::runtime_error("annotation value is not boolean");
				}

				return A.Value == "true";
			});
		}

		for (const auto& Receiver : {"registration", "context"})
		{
			Member(*this, Receiver, "annotations", A("annotation_definition_info"), {},
			    [this, Receiver](const auto& C) -> FValue
			{
				Validate(This(C), Receiver);
				std::vector<FValue> V;
				for (const auto& D : Processing.AnnotationDefinitions)
				{
					V.push_back(Description(*this, &D));
				}

				return Array("std.compiler.annotation_definition_info", std::move(V));
			});
			Member(*this, Receiver, "find_annotation", H("annotation_definition_info"), {S(String)},
			    [this, Receiver](const auto& C) -> FValue
			{
				Validate(This(C), Receiver);
				auto N = Text(Arg(C, 0));
				for (const auto& D : Processing.AnnotationDefinitions)
				{
					if (AnnotationNamesEqual(D.Name, N, D.CaseSensitive))
					{
						return Description(*this, &D);
					}
				}

				return Description(*this, nullptr);
			});
			Member(*this, Receiver, "processors", A("processor_info"), {},
			    [this, Receiver](const auto& C) -> FValue
			{
				Validate(This(C), Receiver);
				std::vector<FValue> V;
				auto Add = [&](const FCompileProcessorDesc& D)
				{
					V.push_back(Make("processor_info",
					    {{"id", D.ProcessorId}, {"version", D.Version},
					        {"configuration_key", D.ConfigurationKey},
					        {"order", static_cast<std::int64_t>(D.Order)}, {"cacheable", D.Cacheable},
					        {"phase_mask", static_cast<std::int32_t>(D.Phases)},
					        {"operation_mask", static_cast<std::int32_t>(D.Operations)}}));
				};
				if (Inputs.CompileProcessing.Registry)
				{
					for (const auto& D : Inputs.CompileProcessing.Registry->GetProcessors())
					{
						Add(D);
					}
				}

				for (const auto& P : Processors)
				{
					Add(P.Description);
				}

				return Array("std.compiler.processor_info", std::move(V));
			});
		}
		Member(*this, "annotation_info", "definition", H("annotation_definition_info"), {},
		    [this](const auto& C) -> FValue
		{
			auto& Value = This(C);
			return Description(
			    *this, Value.AnnotationDefinition ? &*Value.AnnotationDefinition : nullptr);
		});
		Member(*this, "annotation_info", "matches_name", S(Bool), {S(String)},
		    [this](const auto& C) -> FValue
		{
			auto& V = This(C);
			if (!std::get<bool>(Get(V, "is_valid"))) return false;
			return AnnotationNamesEqual(Text(Get(V, "name")), Text(Arg(C, 0)),
			    !V.AnnotationDefinition || V.AnnotationDefinition->CaseSensitive);
		});
		Free(*this, "escape_literal", S(String), {S(String)},
		    [](const auto& C) -> FValue
		{
			auto V = Text(Arg(C, 0));
			std::string R = "\"";
			for (unsigned char B : V)
			{
				switch (B)
				{
				case '"':
					{

						R += "\\\"";
						break;
					}
				case '\\':
					{

						R += "\\\\";
						break;
					}
				case '\n':
					{

						R += "\\n";
						break;
					}
				case '\r':
					{

						R += "\\r";
						break;
					}
				case '\t':
					{

						R += "\\t";
						break;
					}
				case '\0':
					{

						R += "\\0";
						break;
					}
				default:
					{

						R += static_cast<char>(B);
					}
				}
			}
			return R + "\"";
		});
		Free(*this, "source_name", S(String), {H("declaration_info")},
		    [this](const auto& C) -> FValue
		{
			auto N = Text(Get(Value(Arg(C, 0), "declaration_info"), "name"));
			auto P = N.find('\x1f');
			if (P != std::string::npos)
			{
				N = N.substr(P + 1);
			}

			std::string Result;
			for (char Ch : N)
			{
				Result += Ch == '.' ? "::" : std::string(1, Ch);
			}

			return Result;
		});
		Member(*this, "registration", "register_annotation", S(Void), {H("annotation_definition")},
		    [this](const auto& C)
		{
			Validate(This(C), "registration");
			RegisterAnnotation(Arg(C, 0));
			return std::int32_t{0};
		});
		Member(*this, "registration", "register_processor", S(Void),
		    {H("processor_definition"),
		        FNativeTypeDesc::Function("fn()->fn(std.compiler.context,std.compiler.output)->void")},
		    [this](const auto& C)
		{
			Validate(This(C), "registration");
			RegisterProcessor(Arg(C, 0), Arg(C, 1));
			return std::int32_t{0};
		});
		auto Configuration = [this](const auto& C) -> FValue
		{
			Check();
			if (!Executing)
			{
				throw std::runtime_error("configuration outside extension execution");
			}

			auto Key = Text(Arg(C, 0));
			auto I = Inputs.ScriptCompileExtensions.Configuration.find(Key);
			return Make(
			    "configuration_value",
			    {{"exists", I != Inputs.ScriptCompileExtensions.Configuration.end()},
			        {"value", I == Inputs.ScriptCompileExtensions.Configuration.end() ? std::string{} : I->second}});
		};
		Free(*this, "configuration", H("configuration_value"), {S(String)}, Configuration);
		Free(*this, "read_dependency", H("dependency"), {S(String)},
		    [this](const auto& C)
		{
			return Read(Text(Arg(C, 0)));
		});
		const auto ReadResource = [this](const auto& C)
		{
			return this->ReadResource({Text(Arg(C, 0)), Text(Arg(C, 1)), Text(Arg(C, 2)), Text(Arg(C, 3))});
		};
		Free(*this, "read_resource", H("dependency"), {S(String), S(String), S(String), S(String)}, ReadResource);
		Member(*this, "context", "read_resource", H("dependency"),
		    {S(String), S(String), S(String), S(String)},
		    [this, ReadResource](const auto& C)
		{
			Validate(This(C), "context");
			return ReadResource(C);
		});
		Free(*this, "check_cancelled", S(Void), {},
		    [this](const auto&)
		{
			Check();
			return std::int32_t{0};
		});
		for (const auto& N : {"configuration", "read_dependency"})
		{
			Member(*this, "context", N,
			    H(N == std::string("configuration") ? "configuration_value" : "dependency"), {S(String)},
			    [this, N, Configuration](const auto& C) -> FValue
			{
				Validate(This(C), "context");
				return N == std::string("configuration") ? Configuration(C) : Read(Text(Arg(C, 0)));
			});
		}

		Member(*this, "context", "check_cancelled", S(Void), {},
		    [this](const auto& C)
		{
			Validate(This(C), "context");
			Check();
			return std::int32_t{0};
		});
		Member(*this, "context", "phase", E("phase"), {},
		    [this](const auto& C)
		{
			Validate(This(C), "context");
			return Enum("phase", CompilePhaseMask(Context->Phase));
		});
		Member(*this, "context", "event", E("event"), {},
		    [this](const auto& C)
		{
			Validate(This(C), "context");
			return Enum("event", static_cast<int>(Context->Event));
		});
		for (const auto& N : {"module_id", "event_id"})
		{
			Member(*this, "context", N, S(String), {},
			    [this, N](const auto& C) -> FValue
			{
				Validate(This(C), "context");
				return N == std::string("module_id") ? Context->ModuleId : Context->EventId;
			});
		}

		Member(*this, "context", "snapshot", S(Wide), {},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			return static_cast<std::int64_t>(Context->Snapshot);
		});
		Member(*this, "context", "source", H("source_document_info"), {},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			return Document(Context->Source);
		});
		Member(*this, "context", "find_source_document", H("source_document_info"), {S(String)},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			return Document(Context->SourceMetadata.FindDocument(Text(Arg(C, 0))));
		});
		Member(*this, "context", "declaration", H("declaration_info"), {},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			return Declaration(Context->Declaration);
		});
		Member(*this, "context", "owner", H("declaration_info"), {},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			const auto Id = Context->Declaration ? Context->Declaration->OwnerId
			    : Context->Comment               ? Context->Comment->OwnerId
			    : Context->Syntax                ? Context->Syntax->OwnerId
			                                     : std::string{};
			return Declaration(Context->FindDeclaration(Id));
		});
		Member(*this, "context", "find_declaration", H("declaration_info"), {S(String)},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			return Declaration(Context->FindDeclaration(Text(Arg(C, 0))));
		});
		Member(*this, "context", "declarations", A("declaration_info"), {},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			std::vector<FValue> V;
			for (const auto& D : Context->Declarations)
			{
				V.push_back(Declaration(&D));
			}

			return Array("std.compiler.declaration_info", std::move(V));
		});
		Member(*this, "context", "blocks", A("block_info"), {},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			std::vector<FValue> Blocks;
			if (Context->Blocks)
			{
				for (const auto& Block : *Context->Blocks)
				{
					std::vector<FValue> Statements;
					for (const auto& Statement : Block.Statements)
					{
						Statements.push_back(Make("statement_anchor_info",
						    {{"id", Statement.Id}, {"range", SourceRange(*this, Statement.Range)}}));
					}
					Blocks.push_back(Make("block_info", {{"module_id", Block.ModuleId}, {"scope_id", Block.ScopeId}, {"parent_scope_id", Block.ParentScopeId}, {"owner_id", Block.OwnerId}, {"end_anchor_id", Block.EndAnchorId}, {"statements", Array("std.compiler.statement_anchor_info", std::move(Statements))}}));
				}
			}
			return Array("std.compiler.block_info", std::move(Blocks));
		});
		Member(*this, "context", "block_scope", H("scope"), {S(String), S(String)},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			const auto Id = Text(Arg(C, 0)), Before = Text(Arg(C, 1));
			if (Context->Blocks)
			{
				for (const auto& Block : *Context->Blocks)
				{
					if (Block.ScopeId == Id && (Block.EndAnchorId == Before || std::any_of(Block.Statements.begin(), Block.Statements.end(), [&](const auto& Statement)
					{
						return Statement.Id == Before;
					})))
					{
						return Make("scope", {{"module_id", Block.ModuleId}, {"namespace_path", std::string{}}, {"owner_id", std::string{}}, {"scope_id", Id}, {"insertion_before_id", Before}, {"snapshot", static_cast<std::int64_t>(Context->Snapshot)}}, true);
					}
				}
			}
			throw std::runtime_error("unknown compilation block scope or statement boundary");
		});
		Member(*this, "context", "annotation", H("annotation_info"), {},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			return Annotation(*this, Context->Annotation);
		});
		Member(*this, "context", "comment", H("comment_info"), {},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			return Comment(*this, Context->Comment);
		});
		Member(*this, "context", "syntax", H("syntax_info"), {},
		    [this](const auto& C) -> FValue
		{
			Validate(This(C), "context");
			return MetadataNode(Context->Syntax);
		});
		for (bool M : {false, true})
		{
			Member(*this, "context", M ? "member_scope" : "top_scope", H("scope"), {},
			    [this, M](const auto& C) -> FValue
			{
				Validate(This(C), "context");
				return Scope(M);
			});
		}

		Member(*this, "output", "add_declaration", S(Void), {H("scope"), S(String)},
		    [this](const auto& C) -> FValue
		{
			ActiveOutput(*this, C);
			Output->AddDeclaration(ScriptExtensions::Scope(*this, Arg(C, 0)), Text(Arg(C, 1)));
			return std::int32_t{0};
		});
		Member(*this, "output", "edit_source", S(Void), {H("source_range"), S(String)},
		    [this](const auto& C) -> FValue
		{
			ActiveOutput(*this, C);
			Output->EditSource(Range(*this, Arg(C, 0)), Text(Arg(C, 1)));
			return std::int32_t{0};
		});
		Member(*this, "output", "submit_operation", S(Void), {H("generation_operation")},
		    [this](const auto& C) -> FValue
		{
			ActiveOutput(*this, C);
			auto& F = Value(Arg(C, 0), "generation_operation");
			FCompileGenerationOperation O;
			auto K = Integer(Get(F, "kind"));
			if (K <= 0 || K > 32)
			{
				throw std::runtime_error("invalid compilation operation kind");
			}

			O.Kind = static_cast<ECompileOperation>(K);
			O.TargetId = Text(Get(F, "target_id"));
			O.Source = Text(Get(F, "source"));
			O.Snapshot = Context->Snapshot;
			auto ScopeValue = Get(F, "scope");
			if (auto P = std::get_if<FHostObjectHandle>(&ScopeValue); P && *P)
			{
				O.Scope = ScriptExtensions::Scope(*this, ScopeValue);
			}

			if (O.Kind == ECompileOperation::EditSource)
			{
				O.EditRange = Range(*this, Get(F, "edit_range"));
			}

			O.Origin = Origin(*this, Get(F, "origin"));
			O.Metadata = Metadata(*this, Get(F, "metadata"));
			for (const auto& V : Elements(Get(F, "source_mappings")))
			{
				auto& M = Value(V, "source_mapping");
				auto B = Integer(Get(M, "begin")), E = Integer(Get(M, "end"));
				if (B < 0 || E < B)
				{
					throw std::runtime_error("invalid source mapping range");
				}

				O.SourceMappings.push_back({static_cast<std::size_t>(B), static_cast<std::size_t>(E),
				    Origin(*this, Get(M, "origin"))});
			}
			Output->SubmitOperation(std::move(O));
			return std::int32_t{0};
		});
		Member(*this, "output", "set_metadata", S(Void), {S(String), H("metadata")},
		    [this](const auto& C) -> FValue
		{
			ActiveOutput(*this, C);
			Output->SetMetadata(
			    TargetId(*this, Arg(C, 0)), Context->Snapshot, Metadata(*this, Arg(C, 1)));
			return std::int32_t{0};
		});
		for (const auto& N : {"replace_declaration", "complete_body", "replace_body"})
		{
			Member(*this, "output", N, S(Void), {S(String), S(String)},
			    [this, N](const auto& C) -> FValue
			{
				ActiveOutput(*this, C);
				auto Id = TargetId(*this, Arg(C, 0));
				auto Source = Text(Arg(C, 1));
				if (N == std::string("replace_declaration"))
				{
					Output->ReplaceDeclaration(Id, Context->Snapshot, Source);
				}

				else if (N == std::string("complete_body"))
				{
					Output->CompleteBody(Id, Context->Snapshot, Source);
				}

				else
				{
					Output->ReplaceBody(Id, Context->Snapshot, Source);
				}

				return std::int32_t{0};
			});
		}

		Member(*this, "output", "report_diagnostic", S(Void), {S(String), S(Int)},
		    [this](const auto& C) -> FValue
		{
			ActiveOutput(*this, C);
			FScriptError D;
			D.Message = Text(Arg(C, 0));
			auto Severity = Integer(Arg(C, 1));
			if (Severity < 0 || Severity > 3)
			{
				throw std::runtime_error("invalid compilation diagnostic severity");
			}

			D.Severity = static_cast<ECompileDiagnosticSeverity>(Severity);
			if (Context->Declaration)
			{
				D.Location = FSourceLocation{Context->Declaration->Location.FileLabel,
				    Context->Declaration->Location.Line, Context->Declaration->Location.Column};
			}

			Output->ReportDiagnostic(std::move(D));
			return std::int32_t{0};
		});
	}
	FHostObjectHandle FTask::Declaration(const FAnnotationDeclarationView* D)
	{
		std::vector<FValue> Ps, Ts, Bs, As, Cs, Templates;
		if (D)
		{
			for (const auto& P : D->Parameters)
			{
				Ps.push_back(Parameter(*this, P));
			}

			for (const auto& Parameter : D->TemplateParameters)
			{
				Templates.push_back(Make("template_parameter_info", {{"id", Parameter.Id}, {"name", Parameter.Name}, {"value_type_name", Parameter.ValueTypeName}, {"default_source", Parameter.DefaultSource}, {"is_value", Parameter.bValue}, {"has_default", Parameter.bHasDefault}, {"is_enclosing", Parameter.bEnclosing}}));
			}
			for (const auto& T : D->TypeParameters)
			{
				Ts.push_back(T);
			}

			for (const auto& B : D->BaseTypes)
			{
				Bs.push_back(B);
			}

			for (const auto& A : D->Metadata.Annotations)
			{
				As.push_back(Annotation(*this, &A));
			}
		}
		if (D && Context)
		{
			if (auto N = Context->SourceMetadata.FindNode(D->Id))
			{
				for (const auto& C : N->Comments)
				{
					Cs.push_back(Comment(*this, &C));
				}
			}
		}

		auto R = Make("declaration_info",
		    {{"id", D ? D->Id : ""}, {"owner_id", D ? D->OwnerId : ""}, {"name", D ? D->Name : ""},
		        {"scope_id", D ? D->ScopeId : ""}, {"parent_scope_id", D ? D->ParentScopeId : ""},
		        {"is_nested", D && D->bNested}, {"is_local", D && D->bLocal},
		        {"module_id", D ? D->ModulePath : ""}, {"type_name", D ? D->TypeName : ""},
		        {"canonical_type_name", D ? D->CanonicalTypeName : ""},
		        {"body_source", D ? D->BodySource : ""}, {"body_module_id", D ? D->BodyModulePath : ""},
		        {"default_source", D ? D->DefaultValueSource : ""},
		        {"target", Enum("target", D ? static_cast<std::uint32_t>(D->Target) : 0)},
		        {"access", static_cast<std::int32_t>(D ? D->Access : EMemberAccess::Private)},
		        {"is_valid", D != nullptr}, {"is_exported", D && D->bExported},
		        {"has_body", D && D->bHasBody}, {"is_virtual", D && D->bVirtual},
		        {"is_override", D && D->bOverride}, {"is_final", D && D->bFinal},
		        {"is_const", D && D->bIsConst}, {"is_constexpr", D && D->bConstexpr}, {"is_ref", D && D->bIsRef}, {"is_static", D && D->bIsStatic},
		        {"is_deleted", D && D->bDeleted}, {"is_pure_virtual", D && D->bPureVirtual},
		        {"is_generated", D && D->bGenerated}, {"is_from_package", D && D->bFromPackage},
		        {"parameters", Array("std.compiler.parameter_info", std::move(Ps))},
		        {"type_parameters", Array("string", std::move(Ts))},
		        {"template_parameters", Array("std.compiler.template_parameter_info", std::move(Templates))},
		        {"base_types", Array("string", std::move(Bs))},
		        {"annotations", Array("std.compiler.annotation_info", std::move(As))},
		        {"comments", Array("std.compiler.comment_info", std::move(Cs))}});
		auto& F = Value(FValue(R));
		std::vector<FValue> Defaults, CanonicalBases, Access, Bounds;
		if (D)
		{
			for (const auto& V : D->TypeParameterDefaults)
			{
				Defaults.push_back(V);
			}

			for (const auto& V : D->CanonicalBaseTypes)
			{
				CanonicalBases.push_back(V);
			}

			for (const auto V : D->BaseAccess)
			{
				Access.push_back(static_cast<std::int32_t>(V));
			}

			for (const auto& V : D->TypeBounds)
			{
				Bounds.push_back(Make("type_bound_info",
				    {{"parameter", V.TypeParameter}, {"type_name", V.InterfaceTypeName},
				        {"canonical_type_name", V.CanonicalInterfaceTypeName}}));
			}
		}
		F.Fields["type_parameter_defaults"] = Array("string", std::move(Defaults));
		F.Fields["canonical_base_types"] = Array("string", std::move(CanonicalBases));
		F.Fields["base_access"] = Array("int", std::move(Access));
		F.Fields["type_bounds"] = Array("std.compiler.type_bound_info", std::move(Bounds));
		const auto Node = D && Context ? Context->SourceMetadata.FindNode(D->Id) : nullptr;
		F.Fields["documentation"] = Node ? Node->Documentation : std::string{};
		F.Fields["source_range"] = SourceRange(*this, Node ? Node->Range : FSourceRange{});
		F.Fields["has_default"] = D && D->bHasDefault;
		F.Fields["is_mutable"] = D && D->bIsMutable;
		F.Fields["is_explicit"] = D && D->bExplicit;
		F.Fields["is_defaulted"] = D && D->bDefaulted;
		return R;
	}
	FHostObjectHandle FTask::Document(const FSourceDocument* D)
	{
		std::vector<FValue> Lines, Mappings, Comments;
		if (D)
		{
			if (D->LineOffsets.empty())
			{
				Lines.push_back(std::uint64_t{0});
				for (std::size_t I = 0; I < D->Text.size(); ++I)
				{
					if (D->Text[I] == '\n')
					{
						Lines.push_back(static_cast<std::uint64_t>(I + 1));
					}
				}
			}
			else
			{
				for (const auto Offset : D->LineOffsets)
				{
					Lines.push_back(static_cast<std::uint64_t>(Offset));
				}
			}

			for (const auto& M : D->Mappings)
			{
				Mappings.push_back(Make("source_mapping_info",
				    {{"begin", static_cast<std::int64_t>(M.Begin)}, {"end", static_cast<std::int64_t>(M.End)},
				        {"origin", SourceOrigin(*this, M.Origin)}}));
			}

			for (const auto& C : D->Comments)
			{
				Comments.push_back(Comment(*this, &C));
			}
		}
		return Make("source_document_info",
		    {{"is_valid", D != nullptr},
		        {"id", D ? D->Id : ""},
		        {"module_id", D ? D->ModuleId : ""},
		        {"text", D ? D->Text : ""},
		        {"byte_count", static_cast<std::int64_t>(D ? D->Text.size() : 0)},
		        {"line_offsets", Array("uint64", std::move(Lines))},
		        {"mappings", Array("std.compiler.source_mapping_info", std::move(Mappings))},
		        {"comments", Array("std.compiler.comment_info", std::move(Comments))}});
	}
	FHostObjectHandle FTask::Scope(bool Member)
	{
		if (!Context)
		{
			throw std::runtime_error("scope outside compilation callback");
		}

		const auto* D = Context->Declaration;
		if (!D && Context->Syntax)
		{
			D = Context->FindDeclaration(Context->Syntax->OwnerId);
		}

		if (!D && Context->Comment)
		{
			D = Context->FindDeclaration(Context->Comment->OwnerId);
		}

		if (Member)
		{
			while (D && !(static_cast<std::uint32_t>(D->Target) & 7))
			{
				D = Context->FindDeclaration(D->OwnerId);
			}

			if (!D)
			{
				throw std::runtime_error("member scope requires an owning type declaration");
			}
		}
		const auto* Top = D;
		while (Top && !Top->OwnerId.empty())
		{
			const auto* Parent = Context->FindDeclaration(Top->OwnerId);
			if (!Parent)
			{
				break;
			}

			Top = Parent;
		}
		std::string Namespace;
		if (Top)
		{
			auto Name = Top->Name;
			auto ModuleSeparator = Name.find('\x1f');
			if (ModuleSeparator != Name.npos)
			{
				Name = Name.substr(ModuleSeparator + 1);
			}

			if (Top->Target == EAnnotationTarget::Namespace)
			{
				Namespace = Name;
			}

			else
			{
				auto I = Name.rfind('.');
				if (I != std::string::npos)
				{
					Namespace = Name.substr(0, I);
				}
			}
		}
		return Make("scope",
		    {{"module_id", Context->ModuleId},
		        {"namespace_path", Namespace},
		        {"owner_id", Member ? D->Id : ""},
		        {"scope_id", std::string{}},
		        {"insertion_before_id", std::string{}},
		        {"snapshot", static_cast<std::int64_t>(Context->Snapshot)}},
		    true);
	}
	FHostObjectHandle FTask::MetadataNode(const FSourceNodeMetadata* D)
	{
		std::vector<FValue> Annotations, Comments;
		if (D)
		{
			for (const auto& A : D->Annotations)
			{
				Annotations.push_back(Annotation(*this, &A));
			}

			for (const auto& C : D->Comments)
			{
				Comments.push_back(Comment(*this, &C));
			}
		}
		return Make("syntax_info", {{"is_valid", D != nullptr}, {"id", D ? D->Id : ""}, {"owner_id", D ? D->OwnerId : ""}, {"parent_id", D ? D->ParentId : ""}, {"kind", D ? D->Kind : ""}, {"name", D ? D->Name : ""}, {"type_name", D ? D->TypeName : ""}, {"documentation", D ? D->Documentation : ""}, {"target", Enum("target", D ? D->Target : 0)}, {"source_id", D ? D->Range.SourceId : ""}, {"begin", static_cast<std::int64_t>(D ? D->Range.Begin : 0)}, {"end", static_cast<std::int64_t>(D ? D->Range.End : 0)}, {"source_range", SourceRange(*this, D ? D->Range : FSourceRange{})}, {"origin", SourceOrigin(*this, D ? D->Origin : FSourceOrigin{})}, {"annotations", Array("std.compiler.annotation_info", std::move(Annotations))}, {"comments", Array("std.compiler.comment_info", std::move(Comments))}});
	}
	FHostObjectHandle FTask::CloneValue(const FBridgeValue& Value)
	{
		std::function<FValue(const FValue&, std::size_t)> Clone = [&](const FValue& V,
		                                                              std::size_t Depth) -> FValue
		{
			Check();
			if (Depth > 64)
			{
				throw std::runtime_error("compilation value clone depth exceeded");
			}

			if (auto A = std::get_if<FDynamicArrayHandle>(&V); A && *A)
			{
				if ((*A)->Elements.size() > Inputs.ScriptCompileExtensions.MaxBridgeValues -
				        std::min(BridgeValues, Inputs.ScriptCompileExtensions.MaxBridgeValues))
				{
					throw std::runtime_error("compilation value clone budget exceeded");
				}

				BridgeValues += (*A)->Elements.size();
				std::vector<FValue> Values;
				Values.reserve((*A)->Elements.size());
				for (const auto& Element : (*A)->Elements)
				{
					Values.push_back(Clone(Element, Depth + 1));
				}

				return Array((*A)->ElementTypeName, std::move(Values));
			}
			if (auto H = std::get_if<FHostObjectHandle>(&V); H && *H && (*H)->TypeName.rfind("std.compiler.", 0) == 0)
			{
				const auto Desc = Bindings.FindType((*H)->TypeName);
				if (Desc && Desc->Kind == EHostTypeKind::Struct)
				{
					const auto& Source = this->Value(V);
					std::map<std::string, FValue> Fields;
					for (const auto& F : Source.Fields)
					{
						Fields.emplace(F.first, Clone(F.second, Depth + 1));
					}

					return Make(Source.Type, std::move(Fields));
				}
			}
			return V;
		};
		std::map<std::string, FValue> Fields;
		for (const auto& F : Value.Fields)
		{
			Fields.emplace(F.first, Clone(F.second, 0));
		}

		return Make(Value.Type, std::move(Fields));
	}
} // namespace VeroScript::ScriptExtensions
