// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCallHelp.h"

#include "Internal/VeroScriptSignatureHelp.h"
#include "Internal/VeroScriptSourceMetadataQuery.h"
#include "Internal/VeroScriptStandardNamespaceMetadata.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptNative.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPath.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace VeroScript::Tooling
{
	namespace
	{
		void ActivateSignature(FSignatureHelpResult& Result)
		{
			if (!Result.bActive && !Result.Signatures.empty())
			{
				Result.bActive = true;
				Result.ActiveSignature =
					static_cast<std::uint32_t>(Result.Signatures.size() - 1);
			}
		}

		bool HasSignatureLabel(
			const FSignatureHelpResult& Result,
			const std::string_view Label)
		{
			for (const FSignatureInformation& Existing : Result.Signatures)
			{
				if (Existing.Label == Label)
				{
					return true;
				}
			}
			return false;
		}

		void ApplyNativeParamQualifier(
			const std::vector<std::uint8_t>& ParamFlags,
			const std::size_t Index,
			std::string& InOutLabel)
		{
			const std::uint8_t Flag =
				Index < ParamFlags.size() ? ParamFlags[Index] : std::uint8_t{0};
			if (!VeroScript::NativeParamFlagIsRef(Flag))
			{
				return;
			}
			if ((Flag & VeroScript::NativeParamFlagConstRef) != 0)
			{
				InOutLabel.insert(0, "const ");
			}
			InOutLabel.push_back('&');
		}

		void AppendHostFunctionSignature(
			const VeroScript::FHostFunctionDesc& Function,
			FSignatureHelpResult& Result)
		{
			const std::string Full = VeroScript::FormatHostFunctionSignature(Function);
			FSignatureInformation Signature;
			const std::size_t Paren = Full.find('(');
			Signature.Label = Paren == std::string::npos
				? Full
				: Function.Name + Full.substr(Paren);
			if (HasSignatureLabel(Result, Signature.Label))
			{
				return;
			}
			for (std::size_t Index = 0; Index < Function.ParamTypes.size(); ++Index)
			{
				const std::string HostName =
					Index < Function.ParamHostTypeNames.size()
					? Function.ParamHostTypeNames[Index]
					: std::string{};
				const std::string ElementName =
					Index < Function.ParamElementTypeNames.size()
					? Function.ParamElementTypeNames[Index]
					: std::string{};
				std::string Label;
				if (Function.ParamTypes[Index] == VeroScript::ENativeType::HostObject
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::Enum
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::Function)
				{
					Label = HostName.empty()
						? std::string(VeroScript::NativeTypeName(Function.ParamTypes[Index]))
						: HostName;
				}
				else if (Function.ParamTypes[Index] == VeroScript::ENativeType::Array
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::Set
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::MultiSet
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::OrderedMultiSet
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::Queue
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::PriorityQueue
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::Stack
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::Deque
					|| Function.ParamTypes[Index] == VeroScript::ENativeType::Channel)
				{
					Label = ElementName.empty()
						? std::string(VeroScript::NativeTypeName(Function.ParamTypes[Index]))
						: VeroScript::MakeNativeUnaryContainerTypeName(
							VeroScript::NativeTypeName(Function.ParamTypes[Index]),
							VeroScript::CanonicalizeNativeElementTypeName(ElementName));
				}
				else if (VeroScript::NativeTypeIsBinaryMap(Function.ParamTypes[Index]))
				{
					const std::string KeyName =
						Index < Function.ParamKeyTypeNames.size()
						? Function.ParamKeyTypeNames[Index]
						: std::string{};
					const std::string ValueName =
						Index < Function.ParamValueTypeNames.size()
						? Function.ParamValueTypeNames[Index]
						: std::string{};
					Label = KeyName.empty() || ValueName.empty()
						? std::string(VeroScript::NativeTypeName(Function.ParamTypes[Index]))
						: VeroScript::MakeNativeBinaryContainerTypeName(
							VeroScript::NativeTypeName(Function.ParamTypes[Index]),
							VeroScript::CanonicalizeNativeElementTypeName(KeyName),
							VeroScript::CanonicalizeNativeElementTypeName(ValueName));
				}
				else
				{
					Label = VeroScript::NativeTypeName(Function.ParamTypes[Index]);
				}
				ApplyNativeParamQualifier(Function.ParamFlags, Index, Label);
				if (Index < Function.ParamNames.size()
					&& !Function.ParamNames[Index].empty())
				{
					Label.push_back(' ');
					Label += Function.ParamNames[Index];
				}
				Signature.Parameters.push_back(FSignatureParameter{std::move(Label)});
			}
			Result.Signatures.push_back(std::move(Signature));
			ActivateSignature(Result);
		}

		void AppendHostMemberSignature(
			const VeroScript::FHostMemberDesc& Member,
			const std::string_view OwnerTypeName,
			const std::string_view DisplayName,
			FSignatureHelpResult& Result)
		{
			const std::string Full =
				VeroScript::FormatHostMemberSignature(Member, OwnerTypeName);
			FSignatureInformation Signature;
			const std::size_t Paren = Full.find('(');
			Signature.Label = Paren == std::string::npos
				? (std::string(DisplayName) + ' ' + Full)
				: (std::string(DisplayName) + Full.substr(Paren));
			if (HasSignatureLabel(Result, Signature.Label))
			{
				return;
			}
			for (std::size_t Index = 0; Index < Member.ParamTypes.size(); ++Index)
			{
				const std::string HostName =
					Index < Member.ParamHostTypeNames.size()
					? Member.ParamHostTypeNames[Index]
					: std::string{};
				const std::string ElementName =
					Index < Member.ParamElementTypeNames.size()
					? Member.ParamElementTypeNames[Index]
					: std::string{};
				std::string Label;
				if (Member.ParamTypes[Index] == VeroScript::ENativeType::HostObject
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::Enum
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::Function)
				{
					Label = HostName.empty()
						? (Member.ParamTypes[Index] == VeroScript::ENativeType::HostObject
							? std::string(OwnerTypeName)
							: std::string(VeroScript::NativeTypeName(Member.ParamTypes[Index])))
						: HostName;
				}
				else if (Member.ParamTypes[Index] == VeroScript::ENativeType::Array
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::Set
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::MultiSet
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::OrderedMultiSet
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::Queue
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::PriorityQueue
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::Stack
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::Deque
					|| Member.ParamTypes[Index] == VeroScript::ENativeType::Channel)
				{
					Label = ElementName.empty()
						? std::string(VeroScript::NativeTypeName(Member.ParamTypes[Index]))
						: VeroScript::MakeNativeUnaryContainerTypeName(
							VeroScript::NativeTypeName(Member.ParamTypes[Index]),
							VeroScript::CanonicalizeNativeElementTypeName(ElementName));
				}
				else if (VeroScript::NativeTypeIsBinaryMap(Member.ParamTypes[Index]))
				{
					const std::string KeyName =
						Index < Member.ParamKeyTypeNames.size()
						? Member.ParamKeyTypeNames[Index]
						: std::string{};
					const std::string ValueName =
						Index < Member.ParamValueTypeNames.size()
						? Member.ParamValueTypeNames[Index]
						: std::string{};
					Label = KeyName.empty() || ValueName.empty()
						? std::string(VeroScript::NativeTypeName(Member.ParamTypes[Index]))
						: VeroScript::MakeNativeBinaryContainerTypeName(
							VeroScript::NativeTypeName(Member.ParamTypes[Index]),
							VeroScript::CanonicalizeNativeElementTypeName(KeyName),
							VeroScript::CanonicalizeNativeElementTypeName(ValueName));
				}
				else
				{
					Label = VeroScript::NativeTypeName(Member.ParamTypes[Index]);
				}
				ApplyNativeParamQualifier(Member.ParamFlags, Index, Label);
				if (Index < Member.ParamNames.size()
					&& !Member.ParamNames[Index].empty())
				{
					Label.push_back(' ');
					Label += Member.ParamNames[Index];
				}
				Signature.Parameters.push_back(FSignatureParameter{std::move(Label)});
			}
			Result.Signatures.push_back(std::move(Signature));
			ActivateSignature(Result);
		}

		void AppendAnnotationSignature(
			const VeroScript::IHostBindingCatalog* Bindings,
			const std::string_view CalleeName,
			FSignatureHelpResult& Result)
		{
			if (!Bindings) return;
			for (const VeroScript::FHostAnnotationDesc& Annotation : Bindings->GetAnnotations())
			{
				if (!VeroScript::AnnotationNamesEqual(Annotation.Name, CalleeName, Annotation.CaseSensitive))
				{
					continue;
				}
				const auto Engine = Detail::MakeAnnotationSignature(Annotation);
				FSignatureInformation Info;
				Info.Label = Engine.Label;
				Info.Documentation = Engine.Documentation;
				for (const auto& Parameter : Engine.Parameters)
				{
					Info.Parameters.push_back({Parameter.Label});
				}

				Result.Signatures.push_back(std::move(Info));
				Result.bActive = true;
				break;
			}
		}

		void AppendHostBindingSignatures(
			const VeroScript::IHostBindingCatalog* Bindings,
			const std::string_view CalleeName,
			const std::string_view ReceiverHint,
			FSignatureHelpResult& Result)
		{
			if (!Bindings || CalleeName.empty())
			{
				return;
			}
			for (const VeroScript::FHostFunctionDesc& Function : Bindings->GetFunctions())
			{
				if (Function.Name == CalleeName)
				{
					AppendHostFunctionSignature(Function, Result);
				}
			}

			AppendAnnotationSignature(Bindings, CalleeName, Result);

			if (const VeroScript::FHostTypeDesc* const Type = Bindings->FindType(CalleeName))
			{
				for (const VeroScript::FHostMemberDesc& Member : Type->Members)
				{
					if (Member.Kind == VeroScript::EHostMemberKind::Constructor)
					{
						AppendHostMemberSignature(Member, Type->Name, Type->Name, Result);
					}
				}
			}

			const auto AppendMethodsOnType =
				[&](const VeroScript::FHostTypeDesc& Type)
				{
					for (const VeroScript::FHostMemberDesc& Member : Type.Members)
					{
						if (Member.Name != CalleeName)
						{
							continue;
						}
						if (Member.Kind != VeroScript::EHostMemberKind::Method
								&& Member.Kind != VeroScript::EHostMemberKind::StaticMethod)
						{
							continue;
						}
						AppendHostMemberSignature(Member, Type.Name, Member.Name, Result);
					}
				};

			if (!ReceiverHint.empty())
			{
				if (const VeroScript::FHostTypeDesc* const ReceiverType =
					Bindings->FindType(ReceiverHint))
				{
					AppendMethodsOnType(*ReceiverType);
					return;
				}
			}
			// No type-name receiver (or variable like `a.attach`) — match by method name.
			for (const VeroScript::FHostTypeDesc& Type : Bindings->GetTypes())
			{
				AppendMethodsOnType(Type);
			}
		}

		FSignatureHelpResult FromEngine(
			const Detail::FSignatureHelpResult& In,
			const std::string_view FallbackFileLabel)
		{
			FSignatureHelpResult Result;
			Result.bSuccess = In.bSuccess;
			if (!In.bSuccess)
			{
				AppendToolingDiagnostics(
					Result.Diagnostics,
					In.Error,
					In.Diagnostics,
					FallbackFileLabel,
					"signature help failed");
				return Result;
			}

			Result.bActive = In.bActive;
			Result.ActiveSignature = In.ActiveSignature;
			Result.ActiveParameter = In.ActiveParameter;
			Result.Signatures.reserve(In.Signatures.size());
			for (const Detail::FSignatureInformation& Signature : In.Signatures)
			{
				FSignatureInformation Mapped;
				Mapped.Label = Signature.Label;
				Mapped.Documentation = Signature.Documentation;
				Mapped.Parameters.reserve(Signature.Parameters.size());
				for (const Detail::FSignatureParameter& Parameter : Signature.Parameters)
				{
					Mapped.Parameters.push_back(FSignatureParameter{Detail::FormatStandardSourceSpelling(Parameter.Label)});
				}
				Result.Signatures.push_back(std::move(Mapped));
			}

			return Result;
		}

		Detail::FSignatureHelpOptions ToEngineOptions(const FSignatureHelpOptions& Options)
		{
			Detail::FSignatureHelpOptions Out;
			Out.ImportPaths = Options.ImportPaths;
			Out.StdlibSearchPaths = Options.StdlibSearchPaths;
			Out.ImplicitImportPaths = Options.ImplicitImportPaths;
			Out.bAllowCircularImports = Options.bAllowCircularImports;
			Out.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			Out.Defines = Options.Defines;
			Out.PredefinedMacros = Options.PredefinedMacros;
			Out.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
			Out.Bindings = Options.Bindings;
			Out.AnnotationProcessing = Options.AnnotationProcessing;
			Out.CompileProcessing = Options.CompileProcessing;
			Out.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			Out.ModuleEnvironment = Options.ModuleEnvironment;
			return Out;
		}

		Detail::FTextPosition ToEnginePosition(const FTextPosition Position)
		{
			return Detail::FTextPosition{Position.Line, Position.Column};
		}

		struct FGuessedCallee
		{
			std::string Name;
			// Type or receiver identifier before `.` when present (`Actor.attach` / `a.attach`).
			std::string ReceiverHint;
			std::uint32_t ActiveParameter = 0;
		};

		// Extract callee (+ optional receiver) immediately before an active call
		// delimiter when the engine has no script signature.
		FGuessedCallee GuessCalleeBeforeCallDelimiter(
			const std::string_view Source,
			const FTextPosition Position)
		{
			FGuessedCallee Out;
			std::size_t Offset = 0;
			std::uint32_t Line = 1;
			std::uint32_t Column = 1;
			while (Offset < Source.size() && (Line < Position.Line
						|| (Line == Position.Line && Column < Position.Column)))
			{
				if (Source[Offset] == '\n')
				{
					++Line;
					Column = 1;
				}
				else
				{
					++Column;
				}
				++Offset;
			}
			if (Offset == 0)
			{
				return Out;
			}

			enum class ELexState : std::uint8_t
			{
				Normal,
				LineComment,
				BlockComment,
				String,
				Character,
			};
			ELexState State = ELexState::Normal;
			bool bEscape = false;
			struct FOpenDelimiter
			{
				char Value = '\0';
				std::size_t Offset = 0;
				std::uint32_t Commas = 0;
			};
			std::vector<FOpenDelimiter> OpenDelimiters;
			for (std::size_t Index = 0; Index < Offset; ++Index)
			{
				const char Ch = Source[Index];
				const char Next = Index + 1 < Offset ? Source[Index + 1] : '\0';
				switch (State)
				{
				case ELexState::LineComment:
					{
						if (Ch == '\n')
						{
							State = ELexState::Normal;
						}
						continue;
					}
				case ELexState::BlockComment:
					{
						if (Ch == '*' && Next == '/')
						{
							State = ELexState::Normal;
							++Index;
						}
						continue;
					}
				case ELexState::String:
				case ELexState::Character:
					{
						if (bEscape)
						{
							bEscape = false;
						}
						else if (Ch == '\\')
						{
							bEscape = true;
						}
						else if ((State == ELexState::String && Ch == '"')
							|| (State == ELexState::Character && Ch == '\''))
						{
							State = ELexState::Normal;
						}
						continue;
					}
				case ELexState::Normal:
					{
						break;
					}
				default:
					{
						break;
					}
				}

				if (Ch == '/' && Next == '/')
				{
					State = ELexState::LineComment;
					++Index;
				}
				else if (Ch == '/' && Next == '*')
				{
					State = ELexState::BlockComment;
					++Index;
				}
				else if (Ch == '"')
				{
					State = ELexState::String;
				}
				else if (Ch == '\'')
				{
					State = ELexState::Character;
				}
				else if (Ch == '(' || Ch == '[' || Ch == '{')
				{
					OpenDelimiters.push_back(FOpenDelimiter{Ch, Index, 0});
				}
				else if (Ch == ')' || Ch == ']' || Ch == '}')
				{
					if (!OpenDelimiters.empty())
					{
						const char Expected =
							Ch == ')' ? '(' : (Ch == ']' ? '[' : '{');
						if (OpenDelimiters.back().Value == Expected)
						{
							OpenDelimiters.pop_back();
						}
					}
				}
				else if (Ch == ',' && !OpenDelimiters.empty()
					&& (OpenDelimiters.back().Value == '('
						|| OpenDelimiters.back().Value == '{'))
				{
					++OpenDelimiters.back().Commas;
				}
			}
			const auto OpenCall = std::find_if(
				OpenDelimiters.rbegin(),
				OpenDelimiters.rend(),
				[&](const FOpenDelimiter& Delimiter)
				{
					if (Delimiter.Value == '(')
					{
						return true;
					}
					if (Delimiter.Value != '{')
					{
						return false;
					}
					std::size_t Probe = Delimiter.Offset;
					while (Probe > 0
							&& (Source[Probe - 1] == ' ' || Source[Probe - 1] == '\t'))
					{
						--Probe;
					}
					if (Probe == 0 || Source[Probe - 1] == '>')
					{
						return Probe > 0;
					}
					if (std::isalnum(static_cast<unsigned char>(Source[Probe - 1])) == 0
							&& Source[Probe - 1] != '_')
					{
						return false;
					}
					std::size_t NameBegin = Probe;
					while (NameBegin > 0
							&& (std::isalnum(static_cast<unsigned char>(Source[NameBegin - 1])) != 0
								|| Source[NameBegin - 1] == '_'))
					{
						--NameBegin;
					}
					std::size_t KeywordEnd = NameBegin;
					while (KeywordEnd > 0
							&& (Source[KeywordEnd - 1] == ' ' || Source[KeywordEnd - 1] == '\t'))
					{
						--KeywordEnd;
					}
					std::size_t KeywordBegin = KeywordEnd;
					while (KeywordBegin > 0
							&& std::isalpha(
								static_cast<unsigned char>(Source[KeywordBegin - 1])) != 0)
					{
						--KeywordBegin;
					}
					const std::string_view Keyword =
							Source.substr(KeywordBegin, KeywordEnd - KeywordBegin);
					return Keyword != "struct"
							&& Keyword != "class"
							&& Keyword != "interface"
							&& Keyword != "enum"
							&& Keyword != "namespace";
				});
			if (OpenCall == OpenDelimiters.rend())
			{
				return Out;
			}

			Out.ActiveParameter = OpenCall->Commas;
			std::size_t End = OpenCall->Offset;
			while (End > 0 && (Source[End - 1] == ' ' || Source[End - 1] == '\t'))
			{
				--End;
			}
			std::size_t Begin = End;
			while (Begin > 0)
			{
				const char Ch = Source[Begin - 1];
				if ((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z')
					|| (Ch >= '0' && Ch <= '9') || Ch == '_')
				{
					--Begin;
					continue;
				}
				break;
			}
			Out.Name = std::string(Source.substr(Begin, End - Begin));
			if (Begin > 0 && Source[Begin - 1] == '@')
			{
				Out.Name.insert(Out.Name.begin(), '@');
			}
			bool bDirectInitializer = false;
			std::size_t TypeEnd = Begin;
			while (TypeEnd > 0
				&& (Source[TypeEnd - 1] == ' ' || Source[TypeEnd - 1] == '\t'))
			{
				--TypeEnd;
			}
			if (TypeEnd > 0 && Source[TypeEnd - 1] == '>')
			{
				int AngleDepth = 1;
				std::size_t Probe = TypeEnd - 1;
				while (Probe > 0 && AngleDepth > 0)
				{
					--Probe;
					if (Source[Probe] == '>')
					{
						++AngleDepth;
					}
					else if (Source[Probe] == '<')
					{
						--AngleDepth;
					}
				}
				if (AngleDepth == 0)
				{
					TypeEnd = Probe;
					while (TypeEnd > 0
						&& (Source[TypeEnd - 1] == ' ' || Source[TypeEnd - 1] == '\t'))
					{
						--TypeEnd;
					}
				}
			}
			std::size_t TypeBegin = TypeEnd;
			while (TypeBegin > 0)
			{
				const char Ch = Source[TypeBegin - 1];
				if (std::isalnum(static_cast<unsigned char>(Ch)) != 0 || Ch == '_')
				{
					--TypeBegin;
					continue;
				}
				break;
			}
			if (TypeBegin < TypeEnd)
			{
				const std::string_view Candidate = Source.substr(TypeBegin, TypeEnd - TypeBegin);
				const bool bControlKeyword =
					Candidate == "return"
					|| Candidate == "throw"
					|| Candidate == "case"
					|| Candidate == "new"
					|| Candidate == "delete";
				if (!bControlKeyword)
				{
					Out.Name.assign(Candidate);
					bDirectInitializer = true;
				}
			}
			std::size_t Dot = Begin;
			while (Dot > 0 && (Source[Dot - 1] == ' ' || Source[Dot - 1] == '\t'))
			{
				--Dot;
			}
			// `.` and `::` are equivalent member / type separators.
			bool bMemberAccess = false;
			if (Dot > 0 && Source[Dot - 1] == '.')
			{
				--Dot;
				bMemberAccess = true;
			}
			else if (Dot > 1 && Source[Dot - 1] == ':' && Source[Dot - 2] == ':')
			{
				Dot -= 2;
				bMemberAccess = true;
			}
			if (bMemberAccess && !bDirectInitializer)
			{
				while (Dot > 0 && (Source[Dot - 1] == ' ' || Source[Dot - 1] == '\t'))
				{
					--Dot;
				}
				std::size_t RecvEnd = Dot;
				std::size_t RecvBegin = RecvEnd;
				while (RecvBegin > 0)
				{
					const char Ch = Source[RecvBegin - 1];
					if ((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z')
						|| (Ch >= '0' && Ch <= '9') || Ch == '_')
					{
						--RecvBegin;
						continue;
					}
					break;
				}
				Out.ReceiverHint = std::string(Source.substr(RecvBegin, RecvEnd - RecvBegin));
			}
			return Out;
		}
	}

	FSignatureHelpResult VeroScriptCallHelp::HelpAtSource(
		const std::string_view Source,
		const FTextPosition Position,
		const std::string_view FileLabel,
		const FSignatureHelpOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FSignatureHelpResult>(Failure);
			}

			return HelpAtSource(Source, Position, FileLabel, Effective);
		}

		FSignatureHelpResult Result = FromEngine(
			Detail::VeroScriptSignatureHelp::HelpAtSource(
				Source,
				ToEnginePosition(Position),
				FileLabel,
				ToEngineOptions(Options)),
			FileLabel);
		if ((Options.Bindings || !Options.CompileProcessing.AnnotationDefinitions.empty())
			&& !Options.ModuleEnvironment && Result.Signatures.empty())
		{
			const FGuessedCallee Guessed = GuessCalleeBeforeCallDelimiter(Source, Position);
			const std::size_t SignatureCount = Result.Signatures.size();
			AppendHostBindingSignatures(
				Options.Bindings,
				Guessed.Name,
				Guessed.ReceiverHint,
				Result);
			if (Result.Signatures.empty() && Guessed.ReceiverHint.empty())
			{
				// Known annotation descriptions remain usable when unrelated syntax
				// errors prevent a program snapshot from being loaded.
				const auto Descriptions = Detail::SourceMetadataQueryBindings(
					Options.Bindings, Options.CompileProcessing.AnnotationDefinitions);
				AppendAnnotationSignature(Descriptions.get(), Guessed.Name, Result);
			}
			if (Result.Signatures.size() > SignatureCount)
			{
				// A host binding is authoritative even when the incomplete editor
				// buffer could not be loaded by the script parser.
				Result.bSuccess = true;
				Result.ActiveParameter = Guessed.ActiveParameter;
				std::size_t BestParameterCount = 0;
				for (std::size_t Index = 0; Index < Result.Signatures.size(); ++Index)
				{
					const std::size_t ParameterCount =
						Result.Signatures[Index].Parameters.size();
					if (ParameterCount > BestParameterCount)
					{
						BestParameterCount = ParameterCount;
						Result.ActiveSignature = static_cast<std::uint32_t>(Index);
					}
					if (ParameterCount > Guessed.ActiveParameter)
					{
						Result.ActiveSignature = static_cast<std::uint32_t>(Index);
						break;
					}
				}
				Result.Diagnostics.clear();
			}
		}
		return Result;
	}

	FSignatureHelpResult VeroScriptCallHelp::HelpAtFile(
		const std::string_view FilePath,
		const FTextPosition Position,
		const FSignatureHelpOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FSignatureHelpResult>(Failure);
			}

			return HelpAtFile(FilePath, Position, Effective);
		}

		std::ifstream Input(PathUtil::FromUtf8(FilePath), std::ios::binary);
		if (!Input)
		{
			return FromEngine(
				Detail::VeroScriptSignatureHelp::HelpAtFile(
					FilePath,
					ToEnginePosition(Position),
					ToEngineOptions(Options)),
					FilePath);
		}
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		return HelpAtSource(Buffer.str(), Position, FilePath, Options);
	}
}
