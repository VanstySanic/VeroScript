// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSourceMetadataIO.h"
#include "VeroScriptBytecode.h"
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

namespace VeroScript
{
	namespace
	{
		constexpr std::size_t MaxBytes=64u*1024u*1024u,MaxNodes=262144,MaxDepth=64;
		constexpr std::uint64_t TextReferenceBit = std::uint64_t{1} << 63;
		// Snapshots own their decoded strings, including repeated references.
		constexpr std::size_t MaxExpandedTextBytes = 8u * MaxBytes;
		bool ValidUTF8(std::string_view S)
		{
			for (std::size_t I = 0; I < S.size();)
			{
				const auto C = static_cast<unsigned char>(S[I++]);
				if (C < 0x80)
				{
					continue;
				}

				unsigned Count = 0;
				std::uint32_t Value = 0, Minimum = 0;
				if (C >= 0xc2 && C <= 0xdf)
				{
					Count = 1;
					Value = C & 31;
					Minimum = 0x80;
				}
				else if (C >= 0xe0 && C <= 0xef)
				{
					Count = 2;
					Value = C & 15;
					Minimum = 0x800;
				}
				else if (C >= 0xf0 && C <= 0xf4)
				{
					Count = 3;
					Value = C & 7;
					Minimum = 0x10000;
				}
				else
				{
					return false;
				}

				if (Count > S.size() - I) return false;
				while (Count--)
				{
					const auto Next = static_cast<unsigned char>(S[I++]);
					if ((Next & 0xc0) != 0x80) return false;
					Value = (Value << 6) | (Next & 63);
				}
				if (Value < Minimum || Value > 0x10ffff || (Value >= 0xd800 && Value <= 0xdfff)) return false;
			}
			return true;
		}
		struct FWriter
		{
			void U(std::uint64_t V)
			{
				Bytecode::WriteU64LE(B, V);
				Check();
			}

			void Check()
			{
				if (B.size() > MaxBytes)
				{
					throw std::runtime_error("source metadata byte budget exceeded");
				}
			}

			void Text(const std::string& S)
			{
				if (bStringReferences)
				{
					const auto Existing = Strings.find(S);
					if (Existing != Strings.end())
					{
						U(TextReferenceBit | Existing->second);
						return;
					}
				}
				if (S.size() > MaxBytes)
				{
					throw std::runtime_error("source metadata string too long");
				}
				if (!ValidUTF8(S))
				{
					throw std::runtime_error("invalid UTF-8 in source metadata");
				}
				U(S.size());
				B.insert(B.end(), S.begin(), S.end());
				Check();
				if (bStringReferences)
				{
					Strings.emplace(S, Strings.size());
				}
			}

			void Position(const FAnnotationSourcePosition& P)
			{
				if (!P.Line || !P.Column)
				{
					throw std::runtime_error("invalid metadata position");
				}
				Text(P.FileLabel);
				U(P.Line);
				U(P.Column);
			}

			void Range(const FSourceRange& R)
			{
				if (R.Begin > R.End || R.End > MaxBytes)
				{
					throw std::runtime_error("invalid source metadata range");
				}
				Text(R.SourceId);
				U(R.Begin);
				U(R.End);
				Position(R.Position.Begin);
				Position(R.Position.End);
			}

			void Origin(const FSourceOrigin& O)
			{
				Range(O.Range);
				Text(O.ProcessorId);
				Text(O.GenerationId);
			}

			template <class T, class F> void List(const std::vector<T>& V, F Fn)
			{
				if (V.size() > MaxNodes - Nodes)
				{
					throw std::runtime_error("source metadata node budget exceeded");
				}
				Nodes += V.size();
				U(V.size());
				for (const auto& X : V)
				{
					Fn(X);
				}
			}

			void Arguments(const std::vector<FAnnotationArgument>& V, std::size_t Depth = 0)
			{
				if (Depth > MaxDepth)
				{
					throw std::runtime_error("source annotation depth exceeded");
				}

				List(V,
					[&](const auto& A)
					{
						if (static_cast<unsigned>(A.Kind) > 2 || static_cast<unsigned>(A.ScalarKind) > 4 ||
							A.Name.empty() ||
							(A.Kind != EAnnotationArgumentKind::Group && !A.Children.empty()) ||
							(A.Kind != EAnnotationArgumentKind::Scalar && !A.Value.empty()))
						{
							throw std::runtime_error("invalid annotation argument");
						}

						Text(A.Name);
						U(static_cast<unsigned>(A.Kind));
						U(static_cast<unsigned>(A.ScalarKind));
						Text(A.RawText);
						Text(A.Value);
						Position(A.Range.Begin);
						Position(A.Range.End);
						Arguments(A.Children, Depth + 1);
					});
			}

			void Annotations(const std::vector<FDeclarationAnnotation>& V)
			{
				List(V,
					[&](const auto& A)
					{
						if (A.Name.empty())
						{
							throw std::runtime_error("empty annotation name");
						}
						Text(A.Name);
						Text(A.RawText);
						Text(A.DefinitionId);
						U(A.Ordinal);
						Position(A.Range.Begin);
						Position(A.Range.End);
						Position(A.NamePosition);
						Arguments(A.Arguments);
					});
			}

			void Comments(const std::vector<FSourceComment>& V)
			{
				List(V,
					[&](const auto& C)
					{
						if (static_cast<unsigned>(C.Kind) > 3 || static_cast<unsigned>(C.Activity) > 3 ||
							static_cast<unsigned>(C.Association) > 5)
						{
							throw std::runtime_error("invalid source comment");
						}

						Text(C.Id);
						Text(C.ModuleId);
						Text(C.OwnerId);
						Text(C.RawText);
						Text(C.Text);
						U(static_cast<unsigned>(C.Kind));
						U(static_cast<unsigned>(C.Activity));
						U(static_cast<unsigned>(C.Association));
						Range(C.Range);
						Origin(C.Origin);
					});
			}

			void Definitions(const std::vector<FAnnotationArgumentDefinition>& V, std::size_t Depth = 0)
			{
				if (Depth > MaxDepth)
				{
					throw std::runtime_error("annotation description depth exceeded");
				}
				List(V,
					[&](const auto& D)
					{
						Text(D.Name);
						Text(D.Detail);
						Text(D.Documentation);
						Definitions(D.Children, Depth + 1);
					});
			}

			std::vector<std::uint8_t>& B;

			std::size_t Nodes = 0;
			bool bStringReferences = false;
			std::unordered_map<std::string_view, std::size_t> Strings{};
		};
		struct FReader
		{
			std::string CopyText(std::string_view S)
			{
				if (S.size() > MaxExpandedTextBytes - TextBytes)
				{
					throw std::runtime_error("source metadata expanded text budget exceeded");
				}
				TextBytes += S.size();
				return std::string(S);
			}

			std::uint64_t U()
			{
				if (Offset > Size || Size - Offset < 8)
				{
					throw std::runtime_error("truncated source metadata");
				}
				const auto V = Bytecode::ReadU64LE(B + Offset);
				Offset += 8;
				return V;
			}

			std::string Text()
			{
				const auto N = U();
				if (bStringReferences && (N & TextReferenceBit))
				{
					const auto Index = N & ~TextReferenceBit;
					if (Index >= Strings.size())
					{
						throw std::runtime_error("invalid source metadata string reference");
					}
					return CopyText(Strings[static_cast<std::size_t>(Index)]);
				}
				if (N > Size - Offset)
				{
					throw std::runtime_error("source metadata string exceeds data");
				}
				const std::string_view S(reinterpret_cast<const char*>(B + Offset), static_cast<std::size_t>(N));
				if (bStringReferences)
				{
					Strings.emplace_back(reinterpret_cast<const char*>(B + Offset), static_cast<std::size_t>(N));
				}
				Offset += static_cast<std::size_t>(N);
				if (!ValidUTF8(S))
				{
					throw std::runtime_error("invalid UTF-8 in source metadata");
				}
				return CopyText(S);
			}

			FAnnotationSourcePosition Position()
			{
				FAnnotationSourcePosition P;
				P.FileLabel = Text();
				auto L = U(), C = U();
				if (!L || !C || L > UINT32_MAX || C > UINT32_MAX)
				{
					throw std::runtime_error("invalid metadata position");
				}
				P.Line = static_cast<std::uint32_t>(L);
				P.Column = static_cast<std::uint32_t>(C);
				return P;
			}

			FSourceRange Range()
			{
				FSourceRange R;
				R.SourceId = Text();
				const auto Begin = U(), E = U();
				if (Begin > E || E > MaxBytes)
				{
					throw std::runtime_error("invalid source metadata offsets");
				}
				R.Begin = static_cast<std::size_t>(Begin);
				R.End = static_cast<std::size_t>(E);
				R.Position.Begin = Position();
				R.Position.End = Position();
				return R;
			}

			FSourceOrigin Origin()
			{
				FSourceOrigin O;
				O.Range = Range();
				O.ProcessorId = Text();
				O.GenerationId = Text();
				return O;
			}

			template <class T, class F> std::vector<T> List(F Fn)
			{
				const auto N = U();
				if (N > MaxNodes - Nodes || N > (Size - Offset) / 8)
				{
					throw std::runtime_error("source metadata count exceeds budget");
				}
				Nodes += static_cast<std::size_t>(N);
				std::vector<T> V;
				V.reserve(static_cast<std::size_t>(N));
				for (std::uint64_t I = 0; I < N; ++I)
				{
					V.push_back(Fn());
				}
				return V;
			}

			std::vector<FAnnotationArgument> Arguments(std::size_t Depth = 0)
			{
				if (Depth > MaxDepth)
				{
					throw std::runtime_error("source annotation depth exceeded");
				}

				return List<FAnnotationArgument>(
					[&]
					{
						FAnnotationArgument A;
						A.Name = Text();
						auto K = U(), S = U();
						if (K > 2 || S > 4 || A.Name.empty())
						{
							throw std::runtime_error("invalid annotation argument");
						}
						A.Kind = static_cast<EAnnotationArgumentKind>(K);
						A.ScalarKind = static_cast<EAnnotationScalarKind>(S);
						A.RawText = Text();
						A.Value = Text();
						A.Range.Begin = Position();
						A.Range.End = Position();
						A.Children = Arguments(Depth + 1);
						if ((A.Kind != EAnnotationArgumentKind::Group && !A.Children.empty()) ||
							(A.Kind != EAnnotationArgumentKind::Scalar && !A.Value.empty()))
						{
							throw std::runtime_error("invalid annotation tree");
						}

						return A;
					});
			}

			std::vector<FDeclarationAnnotation> Annotations()
			{
				return List<FDeclarationAnnotation>(
					[&]
					{
						FDeclarationAnnotation A;
						A.Name = Text();
						A.RawText = Text();
						A.DefinitionId = Text();
						A.Ordinal = U();
						if (A.Name.empty())
						{
							throw std::runtime_error("invalid annotation");
						}
						A.Range.Begin = Position();
						A.Range.End = Position();
						A.NamePosition = Position();
						A.Arguments = Arguments();
						return A;
					});
			}

			std::vector<FSourceComment> Comments()
			{
				return List<FSourceComment>(
					[&]
					{
						FSourceComment C;
						C.Id = Text();
						C.ModuleId = Text();
						C.OwnerId = Text();
						C.RawText = Text();
						C.Text = Text();
						const auto K = U(), A = U(), S = U();
						if (K > 3 || A > 3 || S > 5)
						{
							throw std::runtime_error("invalid source comment");
						}

						C.Kind = static_cast<ESourceCommentKind>(K);
						C.Activity = static_cast<ESourceActivity>(A);
						C.Association = static_cast<ECommentAssociation>(S);
						C.Range = Range();
						C.Origin = Origin();
						return C;
					});
			}

			std::vector<FAnnotationArgumentDefinition> Definitions(std::size_t Depth = 0)
			{
				if (Depth > MaxDepth)
				{
					throw std::runtime_error("annotation description depth exceeded");
				}
				return List<FAnnotationArgumentDefinition>(
					[&]
					{
						FAnnotationArgumentDefinition D;
						D.Name = Text();
						D.Detail = Text();
						D.Documentation = Text();
						D.Children = Definitions(Depth + 1);
						return D;
					});
			}

			const std::uint8_t* B;

			std::size_t Size, Offset = 0, Nodes = 0;
			std::size_t TextBytes = 0;
			bool bStringReferences = false;
			// Literal views borrow the input buffer, never the returned string values.
			std::vector<std::string_view> Strings{};
		};
	}
	bool EncodeSourceMetadata(const FSourceMetadataSnapshot& S,std::vector<std::uint8_t>& B,std::string& Error)
	try
	{
		Error.clear();
		B.clear();
		FWriter W{B};
		// Metadata identities and source labels repeat across syntax nodes. Keep
		// their complete values while writing each distinct string only once.
		W.bStringReferences = true;
		W.U(0x3454454d43534256ull);
		W.U(static_cast<unsigned>(S.Retention));
		if (static_cast<unsigned>(S.Retention) > 3)
		{
			throw std::runtime_error("invalid metadata retention policy");
		}

		W.List(S.Documents,
			[&](const auto& D)
			{
				W.Text(D.Id);
				W.Text(D.ModuleId);
				W.Text(D.Text);
				W.List(D.LineOffsets,
					[&](auto Offset)
					{
						W.U(Offset);
					});
				W.Comments(D.Comments);
				W.List(D.Mappings,
					[&](const auto& M)
					{
						W.U(M.Begin);
						W.U(M.End);
						W.Origin(M.Origin);
					});
			});
		W.List(S.Nodes,
			[&](const auto& N)
			{
				W.Text(N.Id);
				W.Text(N.OwnerId);
				W.Text(N.ParentId);
				W.Text(N.Kind);
				W.Text(N.Name);
				W.Text(N.ModuleId);
				W.Text(N.TypeName);
				W.U(N.Target);
				W.U(static_cast<unsigned>(N.CommentRetention));
				W.Range(N.Range);
				W.Origin(N.Origin);
				W.Annotations(N.Annotations);
				W.Comments(N.Comments);
				W.Text(N.Documentation);
			});
		W.List(S.Definitions,
			[&](const auto& D)
			{
				W.Text(D.Id);
				W.Text(D.ProviderId);
				W.Text(D.Name);
				W.Text(D.Detail);
				W.Text(D.Documentation);
				W.Definitions(D.Arguments);
				W.U(D.Targets);
				W.U(D.CaseSensitive ? 1 : 0);
			});
		if (!DecodeSourceMetadata(B.data(), B.size(), Error))
		{
			B.clear();
			return false;
		}
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		B.clear();
		return false;
	}
	bool EncodeCompileOperations(
		const std::vector<FCompileGenerationOperation>& Ops, std::vector<std::uint8_t>& B, std::string& Error)
	try
	{
		Error.clear();
		B.clear();
		FWriter W{B};
		W.U(0x3253504f43534256ull);
		W.List(Ops,
			[&](const auto& O)
			{
				W.U(static_cast<unsigned>(O.Kind));
				W.U(static_cast<unsigned>(O.Phase));
				W.Text(O.Scope.ModuleId);
				W.Text(O.Scope.NamespacePath);
				W.Text(O.Scope.OwnerId);
				W.Text(O.Scope.ScopeId);
				W.Text(O.Scope.InsertionBeforeId);
				W.Text(O.TargetId);
				W.Text(O.Source);
				W.Text(O.ProcessorId);
				W.Text(O.GenerationId);
				W.Text(O.InputDigest);
				W.Range(O.EditRange);
				W.Origin(O.Origin);
				W.List(O.SourceMappings,
					[&](const auto& M)
					{
						W.U(M.Begin);
						W.U(M.End);
						W.Origin(M.Origin);
					});
				W.U(O.Metadata.Annotations.has_value() ? 1 : 0);
				if (O.Metadata.Annotations)
				{
					W.Annotations(*O.Metadata.Annotations);
				}

				W.U(O.Metadata.Comments.has_value() ? 1 : 0);
				if (O.Metadata.Comments)
				{
					W.Comments(*O.Metadata.Comments);
				}

				W.List(O.Metadata.Children,
					[&](const auto& C)
					{
						W.U(C.Begin);
						W.U(C.End);
						W.Text(C.Kind);
						W.Annotations(C.Metadata.Annotations);
						W.Comments(C.Metadata.Comments);
						W.Text(C.Metadata.Documentation);
					});
			});
		std::vector<FCompileGenerationOperation> Checked;
		if (!DecodeCompileOperations(B.data(), B.size(), Checked, Error))
		{
			B.clear();
			return false;
		}
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		B.clear();
		return false;
	}
	bool DecodeCompileOperations(const std::uint8_t* Bytes, std::size_t Size,
		std::vector<FCompileGenerationOperation>& Ops, std::string& Error)
	try
	{
		Error.clear();
		Ops.clear();
		if (!Bytes || Size > MaxBytes)
		{
			throw std::runtime_error("invalid processing operation data");
		}

		FReader R{Bytes, Size};
		if (R.U() != 0x3253504f43534256ull)
		{
			throw std::runtime_error("processing operation contract mismatch");
		}

		Ops = R.List<FCompileGenerationOperation>(
			[&]
			{
				FCompileGenerationOperation O;
				const auto K = R.U(), P = R.U();
				if ((K != 1 && K != 2 && K != 4 && K != 8 && K != 16 && K != 32) || P > 4)
				{
					throw std::runtime_error("invalid processing operation kind or phase");
				}

				O.Kind = static_cast<ECompileOperation>(K);
				O.Phase = static_cast<ECompileProcessingPhase>(P);
				const auto Allowed = P < 2 ? 1u : P == 4 ? 56u : 38u;
				if (!(K & Allowed))
				{
					throw std::runtime_error("processing operation phase mismatch");
				}

				O.Scope.ModuleId = R.Text();
				O.Scope.NamespacePath = R.Text();
				O.Scope.OwnerId = R.Text();
				O.Scope.ScopeId = R.Text();
				O.Scope.InsertionBeforeId = R.Text();
				O.TargetId = R.Text();
				O.Source = R.Text();
				O.ProcessorId = R.Text();
				O.GenerationId = R.Text();
				O.InputDigest = R.Text();
				O.EditRange = R.Range();
				O.Origin = R.Origin();
				if (O.Kind == ECompileOperation::AddDeclaration && !O.TargetId.empty())
				{
					throw std::runtime_error("declaration addition cannot carry a replacement target");
				}

				O.SourceMappings = R.List<FSourceDocument::FMapping>(
					[&]
					{
						FSourceDocument::FMapping M;
						const auto B = R.U(), E = R.U();
						if (B >= E || E > MaxBytes)
						{
							throw std::runtime_error("invalid source operation mapping offsets");
						}

						M.Begin = static_cast<std::size_t>(B);
						M.End = static_cast<std::size_t>(E);
						M.Origin = R.Origin();
						return M;
					});
				const auto A = R.U();
				if (A > 1)
				{
					throw std::runtime_error("invalid annotation payload flag");
				}
				if (A)
				{
					O.Metadata.Annotations = R.Annotations();
				}

				const auto C = R.U();
				if (C > 1)
				{
					throw std::runtime_error("invalid comment payload flag");
				}
				if (C)
				{
					O.Metadata.Comments = R.Comments();
				}

				O.Metadata.Children = R.List<FCompileMetadata::FChild>(
					[&]
					{
						FCompileMetadata::FChild X;
						const auto B = R.U(), E = R.U();
						if (B > E || E > MaxBytes)
						{
							throw std::runtime_error("invalid fragment metadata target");
						}
						X.Begin = static_cast<std::size_t>(B);
						X.End = static_cast<std::size_t>(E);
						X.Kind = R.Text();
						X.Metadata.Annotations = R.Annotations();
						X.Metadata.Comments = R.Comments();
						X.Metadata.Documentation = R.Text();
						return X;
					});
				if (O.GenerationId.empty() || O.ProcessorId.empty() || O.Scope.ModuleId.empty())
				{
					throw std::runtime_error("processing operation has no identity or module");
				}

				if (O.Kind != ECompileOperation::AddDeclaration && O.Kind != ECompileOperation::EditSource &&
					(O.TargetId.empty() || O.InputDigest.empty()))
				{
					throw std::runtime_error("processing operation has no target proof");
				}

				if ((O.Kind == ECompileOperation::SetMetadata || O.Kind == ECompileOperation::EditSource) &&
					!O.Metadata.Children.empty())
				{
					throw std::runtime_error("operation cannot carry fragment metadata");
				}

				for (const auto& X : O.Metadata.Children)
				{
					if (X.End > O.Source.size() || X.Kind.empty())
					{
						throw std::runtime_error("fragment metadata exceeds its source");
					}
				}

				std::size_t MappingEnd = 0;
				for (const auto& M : O.SourceMappings)
				{
					const bool SupportsMappings=O.Kind==ECompileOperation::EditSource || O.Kind==ECompileOperation::AddDeclaration ||
						O.Kind==ECompileOperation::ReplaceDeclaration || O.Kind==ECompileOperation::CompleteBody || O.Kind==ECompileOperation::ReplaceBody;
					if (!SupportsMappings || M.Begin >= M.End || M.Begin < MappingEnd ||
						M.End > O.Source.size())
					{
						throw std::runtime_error("invalid source operation mapping");
					}
					MappingEnd = M.End;
				}
				return O;
			});
		if (R.Offset != Size)
		{
			throw std::runtime_error("trailing processing operation bytes");
		}

		std::unordered_set<std::string> Ids;
		for (const auto& O : Ops)
		{
			if (!Ids.insert(O.GenerationId).second)
			{
				throw std::runtime_error("duplicate processing operation identity");
			}
		}

		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		Ops.clear();
		return false;
	}
	std::shared_ptr<const FSourceMetadataSnapshot> DecodeSourceMetadata(
		const std::uint8_t* Bytes, std::size_t Size, std::string& Error)
	try
	{
		Error.clear();
		if (!Bytes || Size > MaxBytes)
		{
			throw std::runtime_error("invalid source metadata data");
		}

		FReader R{Bytes, Size};
		const auto Format = R.U();
		if (Format != 0x3454454d43534256ull && Format != 0x3354454d43534256ull)
		{
			throw std::runtime_error("source metadata contract mismatch");
		}
		R.bStringReferences = Format == 0x3454454d43534256ull;

		auto S = std::make_shared<FSourceMetadataSnapshot>();
		const auto Policy = R.U();
		if (Policy > 3)
		{
			throw std::runtime_error("invalid retention policy");
		}
		S->Retention = static_cast<ECommentRetention>(Policy);
		S->Documents = R.List<FSourceDocument>(
			[&]
			{
				FSourceDocument D;
				D.Id = R.Text();
				D.ModuleId = R.Text();
				D.Text = R.Text();
				D.LineOffsets = R.List<std::size_t>(
					[&]
					{
						const auto Offset = R.U();
						if (Offset > MaxBytes)
						{
							throw std::runtime_error("invalid source line offset");
						}
						return static_cast<std::size_t>(Offset);
					});
				D.Comments = R.Comments();
				D.Mappings = R.List<FSourceDocument::FMapping>(
					[&]
					{
						FSourceDocument::FMapping M;
						const auto B = R.U(), E = R.U();
						if (B > E || E > MaxBytes)
						{
							throw std::runtime_error("invalid source mapping");
						}
						M.Begin = static_cast<std::size_t>(B);
						M.End = static_cast<std::size_t>(E);
						M.Origin = R.Origin();
						return M;
					});
				return D;
			});
		S->Nodes = R.List<FSourceNodeMetadata>(
			[&]
			{
				FSourceNodeMetadata N;
				N.Id = R.Text();
				N.OwnerId = R.Text();
				N.ParentId = R.Text();
				N.Kind = R.Text();
				N.Name = R.Text();
				N.ModuleId = R.Text();
				N.TypeName = R.Text();
				const auto T = R.U();
				if (T > AnnotationAllTargets)
				{
					throw std::runtime_error("invalid metadata target");
				}
				N.Target = static_cast<std::uint32_t>(T);
				const auto Policy = R.U();
				if (Policy > 3)
				{
					throw std::runtime_error("invalid node comment policy");
				}
				N.CommentRetention = static_cast<ECommentRetention>(Policy);
				N.Range = R.Range();
				N.Origin = R.Origin();
				N.Annotations = R.Annotations();
				N.Comments = R.Comments();
				N.Documentation = R.Text();
				return N;
			});
		S->Definitions = R.List<FAnnotationDefinition>(
			[&]
			{
				FAnnotationDefinition D;
				D.Id = R.Text();
				D.ProviderId = R.Text();
				D.Name = R.Text();
				D.Detail = R.Text();
				D.Documentation = R.Text();
				D.Arguments = R.Definitions();
				const auto T = R.U();
				if (!T || T > AnnotationAllTargets)
				{
					throw std::runtime_error("invalid annotation targets");
				}
				D.Targets = static_cast<std::uint32_t>(T);
				const auto C = R.U();
				if (C > 1)
				{
					throw std::runtime_error("invalid annotation name policy");
				}
				D.CaseSensitive = C != 0;
				return D;
			});
		if (R.Offset != Size)
		{
			throw std::runtime_error("trailing source metadata bytes");
		}

		std::unordered_set<std::string> Ids;
		for (const auto& D : S->Documents)
		{
			if (D.Id.empty() || !Ids.insert(D.Id).second)
			{
				throw std::runtime_error("duplicate source document identity");
			}
		}

		Ids.clear();
		for (const auto& N : S->Nodes)
		{
			if (N.Id.empty() || !Ids.insert(N.Id).second)
			{
				throw std::runtime_error("duplicate source node identity");
			}
		}

		Ids.clear();
		for (const auto& D : S->Definitions)
		{
			if (D.Id.empty() || !Ids.insert(D.Id).second || D.Name.empty())
			{
				throw std::runtime_error("invalid annotation description identity");
			}
		}

		std::function<void(const std::vector<FAnnotationArgumentDefinition>&)> CheckDefinitions =
			[&](const auto& Values)
		{
			std::unordered_set<std::string> Names;
			for (const auto& D : Values)
			{
				if (D.Name.empty() || !Names.insert(D.Name).second)
				{
					throw std::runtime_error("invalid annotation argument description");
				}
				CheckDefinitions(D.Children);
			}
		};
		for (const auto& D : S->Definitions)
		{
			CheckDefinitions(D.Arguments);
		}

		for (const auto& D : S->Documents)
		{
			if (!D.LineOffsets.empty() && D.LineOffsets.front() != 0)
			{
				throw std::runtime_error("invalid source line origin");
			}

			for (std::size_t I = 0; I < D.LineOffsets.size(); ++I)
			{
				if ((I && D.LineOffsets[I] <= D.LineOffsets[I - 1]) ||
					(!D.Text.empty() && D.LineOffsets[I] > D.Text.size()))
				{
					throw std::runtime_error("invalid source line ordering");
				}
			}

			std::size_t PreviousEnd = 0;
			bool First = true;
			for (const auto& M : D.Mappings)
			{
				if (M.Begin == M.End || (!First && M.Begin < PreviousEnd) ||
					(!D.Text.empty() && M.End > D.Text.size()))
				{
					throw std::runtime_error("overlapping or invalid source mappings");
				}

				PreviousEnd = M.End;
				First = false;
			}
		}
		std::unordered_map<std::string, const FSourceDocument*> Documents;
		for (const auto& D : S->Documents)
		{
			Documents.emplace(D.Id, &D);
		}

		auto CheckRange = [&](const FSourceRange& V)
		{
			const auto D = Documents.find(V.SourceId);
			if (D != Documents.end() && !D->second->Text.empty() && V.End > D->second->Text.size())
			{
				throw std::runtime_error("source metadata range exceeds document");
			}
		};
		for (const auto& D : S->Documents)
		{
			for (const auto& C : D.Comments)
			{
				CheckRange(C.Range);
				CheckRange(C.Origin.Range);
			}
			for (const auto& M : D.Mappings)
			{
				CheckRange(M.Origin.Range);
			}
		}
		std::unordered_map<std::string, std::size_t> Nodes;
		for (std::size_t I = 0; I < S->Nodes.size(); ++I)
		{
			Nodes.emplace(S->Nodes[I].Id, I);
		}

		for (const auto& N : S->Nodes)
		{
			CheckRange(N.Range);
			CheckRange(N.Origin.Range);
			for (const auto& C : N.Comments)
			{
				CheckRange(C.Range);
				CheckRange(C.Origin.Range);
			}
		}
		// Validate each edge once. Restarting the complete parent walk for every
		// node makes a valid long chain quadratic and stalls bytecode loading.
		std::vector<std::uint8_t> State(S->Nodes.size(),0);
		std::vector<std::size_t> Path;
		for (std::size_t I = 0; I < S->Nodes.size(); ++I)
		{
			Path.clear();
			auto Current = I;
			while (Current < S->Nodes.size() && State[Current] != 2)
			{
				if (State[Current] == 1)
				{
					throw std::runtime_error("cyclic source node ownership");
				}

				State[Current] = 1;
				Path.push_back(Current);
				const auto Parent=Nodes.find(S->Nodes[Current].ParentId);
				Current=Parent==Nodes.end()?S->Nodes.size():Parent->second;
			}
			for (const auto Index : Path)
			{
				State[Index] = 2;
			}
		}
		return S;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return nullptr;
	}
}
