// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPackage.h"

#include "VeroScriptBytecode.h"
#include "VeroScriptScriptCompileExtensions.h"
#include "VeroScriptMethodKey.h"

#include <algorithm>
#include <charconv>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript
{
	namespace
	{
		constexpr std::uint32_t kMaxMetadataEntries = 4096;
		constexpr std::uint32_t kMaxMetadataKeyBytes = 255;
		constexpr std::uint64_t kMaxMetadataValueBytes = 64ull * 1024ull * 1024ull;
		constexpr std::uint64_t kMaxTotalMetadataBytes = 128ull * 1024ull * 1024ull;
		constexpr std::uint32_t kMaxPackageNameBytes = 255;
		constexpr std::size_t kPackageHeaderBytes = 28;

		bool ValidateCompileExtensionEntry(const FBytecodeModule& Module, const FCompileExtensionManifest& Manifest)
		{
			std::size_t Matches = 0;
			for (const auto& Function : Module.Functions)
			{
				const auto Begin = FindLinkageIdentityStart(Function.Name);
				const auto Marker = FindFunctionSignatureMarker(Function.Name);
				const auto End = Marker == std::string_view::npos ? Function.Name.size() : Marker;
				if (End < Begin ||
				    std::string_view(Function.Name).substr(Begin, End - Begin) !=
				        Manifest.RegistrationEntry ||
				    Function.ModulePath != Manifest.ModuleId)
				{
					continue;
				}

				if (!Function.bPackageExported || Function.bIsMethod || Function.bDeleted ||
				    Function.ParamCount != 1 || !Function.bReturnsVoid ||
				    BytecodeFunctionHasRefParams(Function) ||
				    Function.ClosureTypeName != "fn(std.compiler.registration)->void")
				{
					return false;
				}

				++Matches;
			}
			return Matches == 1;
		}

		bool IsMetadataKeyValid(const std::string& Key)
		{
			if (Key.empty() || Key.size() > kMaxMetadataKeyBytes)
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Key.size(); ++Index)
			{
				const unsigned char Character =
				    static_cast<unsigned char>(Key[Index]);
				const bool bAlpha =
				    (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z');
				const bool bDigit = Character >= '0' && Character <= '9';
				const bool bSeparator =
				    Character == '.' || Character == '_' || Character == '-';
				if (!(bAlpha || bDigit || (Index != 0 && bSeparator)))
				{
					return false;
				}
			}
			return true;
		}

		bool IsPackageNameValid(const std::string& Name)
		{
			if (Name.empty() || Name.size() > kMaxPackageNameBytes)
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Name.size(); ++Index)
			{
				const unsigned char Character =
				    static_cast<unsigned char>(Name[Index]);
				const bool bAlpha =
				    (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z');
				const bool bDigit = Character >= '0' && Character <= '9';
				const bool bSeparator =
				    Character == '.' || Character == '_' || Character == '-';
				if (!(bAlpha || (Index != 0 && (bDigit || bSeparator))))
				{
					return false;
				}
			}
			return true;
		}

		bool ValidatePackageMacroOrigins(
		    const FBytecodeModule& Module, EPackageKind Kind,
		    const std::string& PackageName, std::uint64_t PackageVersion,
		    std::string& OutError)
		{
			if (Module.PublicMacros.empty())
			{
				return true;
			}

			if (Kind == EPackageKind::Application)
			{
				OutError = "application package contains a public macro interface";
				return false;
			}
			std::unordered_set<std::string> MacroOwners;
			for (const auto& Component : Module.PackageComponents)
			{
				MacroOwners.insert(Component.Id);
			}

			const auto HeaderOwner = PackageName + "@" + std::to_string(PackageVersion);
			if (!MacroOwners.count(HeaderOwner))
			{
				OutError = "library macro interface does not match the package header owner";
				return false;
			}
			for (const auto& M : Module.PublicMacros)
			{
				const auto Colon = M.OriginModule.find(':', 7);
				if (M.OriginModule.rfind("<vslib:", 0) != 0 || Colon == std::string::npos ||
				    M.OriginModule.back() != '>')
				{
					OutError = "library public macro has a nonportable origin";
					return false;
				}
				const auto Owner = M.OriginModule.substr(7, Colon - 7);
				const auto At = Owner.find('@');
				std::uint64_t Version = 0;
				const auto VersionText = At == std::string::npos ? std::string{} : Owner.substr(At + 1);
				const auto ParsedVersion = std::from_chars(
				    VersionText.data(), VersionText.data() + VersionText.size(), Version);
				const auto Suffix = M.OriginModule.substr(Colon);
				if (At == std::string::npos || !IsPackageNameValid(Owner.substr(0, At)) ||
				    ParsedVersion.ec != std::errc{} ||
				    ParsedVersion.ptr != VersionText.data() + VersionText.size() ||
				    VersionText != std::to_string(Version) || Suffix.size() != 24 ||
				    Suffix.compare(0, 7, ":macro-") != 0 ||
				    !std::all_of(Suffix.begin() + 7, Suffix.end() - 1,
				        [](char C)
				{
					return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
				}))
				{
					OutError = "library public macro has an invalid portable origin";
					return false;
				}
				if (!MacroOwners.count(Owner))
				{
					OutError = "library public macro owner is not a linked package component";
					return false;
				}
			}
			return true;
		}

		bool CanAppend(
		    const std::vector<std::uint8_t>& Buffer,
		    const std::uint64_t Additional)
		{
			return Additional <= VEROSCRIPT_PACKAGE_MAX_BYTES && Buffer.size() <= VEROSCRIPT_PACKAGE_MAX_BYTES - Additional;
		}

		bool ReadU32(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    std::uint32_t& Out)
		{
			if (Offset > Size || Size - Offset < 4)
			{
				return false;
			}
			Out = Bytecode::ReadU32LE(Data + Offset);
			Offset += 4;
			return true;
		}

		bool ReadU64(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    std::uint64_t& Out)
		{
			if (Offset > Size || Size - Offset < 8)
			{
				return false;
			}
			Out = Bytecode::ReadU64LE(Data + Offset);
			Offset += 8;
			return true;
		}

		bool FitsRemaining(
		    const std::size_t Size,
		    const std::size_t Offset,
		    const std::uint64_t Length)
		{
			return Offset <= Size && Length <= static_cast<std::uint64_t>(Size - Offset) && Length <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
		}

		void ReplaceAll(
		    std::string& Value,
		    const std::string& Needle,
		    const std::string& Replacement)
		{
			for (std::size_t Offset = Value.find(Needle);
			    Offset != std::string::npos;
			    Offset = Value.find(Needle, Offset + Replacement.size()))
			{
				Value.replace(Offset, Needle.size(), Replacement);
			}
		}

		std::string StableModuleId(const std::string_view Identity)
		{
			std::uint64_t Hash = 14695981039346656037ull;
			for (const unsigned char Byte : Identity)
			{
				Hash ^= Byte;
				Hash *= 1099511628211ull;
			}
			char Hex[17];
			for (std::size_t Index = 0; Index < 16; ++Index)
			{
				const std::uint8_t Nibble = static_cast<std::uint8_t>(Hash & 0x0fu);
				Hex[15 - Index] = static_cast<char>(
				    Nibble < 10 ? '0' + Nibble : 'a' + Nibble - 10);
				Hash >>= 4;
			}
			Hex[16] = '\0';
			return Hex;
		}

		std::uint64_t FingerprintBytes(const std::vector<std::uint8_t>& Bytes)
		{
			std::uint64_t Hash = 14695981039346656037ull;
			for (const std::uint8_t Byte : Bytes)
			{
				Hash ^= Byte;
				Hash *= 1099511628211ull;
			}
			return Hash;
		}

		void FillPackageOwners(
		    std::vector<std::string>& Owners,
		    const std::size_t Count,
		    const std::string& OwnerId)
		{
			Owners.resize(Count);
			for (std::string& Owner : Owners)
			{
				if (Owner.empty())
				{
					Owner = OwnerId;
				}
			}
		}

		void FillPackageInitOwnership(
		    FBytecodeModule& Module,
		    const std::string& OwnerId)
		{
			std::vector<FBytecodePackageInitSegment> Segments;
			Segments.reserve(Module.PackageInitSegments.size() + 1);
			std::size_t Cursor = 0;
			for (const FBytecodePackageInitSegment& Segment :
			    Module.PackageInitSegments)
			{
				if (Cursor < Segment.BeginOffset)
				{
					Segments.push_back(FBytecodePackageInitSegment{
					    OwnerId, Cursor, Segment.BeginOffset});
				}
				Segments.push_back(Segment);
				Cursor = Segment.EndOffset;
			}
			if (Cursor < Module.InitCode.size())
			{
				Segments.push_back(FBytecodePackageInitSegment{
				    OwnerId, Cursor, Module.InitCode.size()});
			}
			Module.PackageInitSegments = std::move(Segments);
		}

		bool PreparePackageModule(
		    FBytecodeModule& Module,
		    const std::string& PackageName,
		    const bool bStripDebugInfo,
		    std::string& OutError)
		{
			std::vector<std::string> ModulePrefixes;
			const auto CollectPrefix =
			    [&](const std::string& LinkageName)
			{
				const std::size_t Separator = LinkageName.find('\x1F');
				if (Separator == std::string::npos)
				{
					return;
				}
				const std::string Prefix = LinkageName.substr(0, Separator);
				// Already-packaged dependency-private identities are portable and
				// must remain stable when this module is wrapped by another library.
				if (Prefix.rfind("<vslib:", 0) == 0)
				{
					return;
				}
				if (std::find(
				        ModulePrefixes.begin(),
				        ModulePrefixes.end(),
				        Prefix) == ModulePrefixes.end())
				{
					ModulePrefixes.push_back(Prefix);
				}
			};
			for (const FBytecodeTypeDesc& Type : Module.Types)
			{
				CollectPrefix(Type.Name);
			}
			for (const FBytecodeInterfaceDesc& Interface : Module.Interfaces)
			{
				CollectPrefix(Interface.Name);
			}
			for (const FBytecodeEnumDesc& Enum : Module.Enums)
			{
				CollectPrefix(Enum.Name);
			}
			for (const FBytecodeFunction& Function : Module.Functions)
			{
				CollectPrefix(Function.Name);
			}
			for (const std::string& Global : Module.GlobalNames)
			{
				CollectPrefix(Global);
			}

			std::unordered_map<std::string, std::string> Replacements;
			for (const std::string& Prefix : ModulePrefixes)
			{
				Replacements.emplace(
				    Prefix + '\x1F',
				    "<vslib:" + PackageName + ":module-" + StableModuleId(Prefix) + ">\x1F");
			}
			std::unordered_map<std::string, std::string> TemplatePathReplacements;
			for (const FBytecodePackageTemplateModule& Template :
			    Module.PackageTemplateModules)
			{
				if (Template.bFromPackage || Template.ModulePath.empty())
				{
					continue;
				}
				const std::string VirtualPath =
				    "<vslib:" + PackageName + ":module-" + StableModuleId(Template.ModulePath) + ">";
				TemplatePathReplacements.emplace(
				    Template.ModulePath, VirtualPath);
				Replacements.emplace(
				    Template.ModulePath + '\x1F', VirtualPath + '\x1F');
			}
			const auto ReplacePrivatePrefixes =
			    [&](const std::string_view Text)
			{
				std::string Result(Text);
				for (const auto& Replacement : Replacements)
				{
					ReplaceAll(Result, Replacement.first, Replacement.second);
				}
				return Result;
			};
			// Private-prefix replacement changes the byte length of generic
			// arguments. Rebuild each length prefix from the rewritten payload.
			std::function<std::string(std::string_view)> RewriteEncodedName;
			RewriteEncodedName = [&](const std::string_view Source) -> std::string
			{
				FTemplateArgument Argument;
				if (Source.substr(0, 7) == "@value:" && DecodeTemplateArgument(Source.substr(7), Argument))
				{
					if (Argument.Value.Type.bEnum)
					{
						Argument.Value.Type.Name = RewriteEncodedName(Argument.Value.Type.Name);
					}
					return "@value:" + EncodeTemplateArgument(Argument);
				}
				std::string Result;
				std::size_t Cursor = 0;
				std::size_t Search = 0;
				while (Search < Source.size())
				{
					const std::size_t Marker = Source.find('#', Search);
					if (Marker == std::string_view::npos)
					{
						break;
					}
					std::size_t Position = Marker + 1;
					std::string Arguments;
					bool bDecoded = false;
					while (Position < Source.size() && Source[Position] >= '0' && Source[Position] <= '9')
					{
						std::size_t Length = 0;
						const std::size_t DigitsStart = Position;
						while (Position < Source.size() && Source[Position] >= '0' && Source[Position] <= '9')
						{
							const std::size_t Digit =
							    static_cast<std::size_t>(Source[Position] - '0');
							if (Length > (std::numeric_limits<std::size_t>::max() - Digit) / 10u)
							{
								break;
							}
							Length = Length * 10u + Digit;
							++Position;
						}
						if (Position == DigitsStart || Position >= Source.size() || Source[Position++] != ':' || Length > Source.size() - Position)
						{
							bDecoded = false;
							break;
						}
						const std::string_view Original = Source.substr(Position, Length);
						const std::string Rewritten = RewriteEncodedName(Original);
						if (Rewritten == Original)
						{
							Arguments.append(Source.substr(
							    DigitsStart, Position + Length - DigitsStart));
						}
						else
						{
							Arguments += std::to_string(Rewritten.size());
							Arguments.push_back(':');
							Arguments += Rewritten;
						}
						Position += Length;
						bDecoded = true;
					}
					if (bDecoded)
					{
						Result += ReplacePrivatePrefixes(
						    Source.substr(Cursor, Marker - Cursor));
						Result.push_back('#');
						Result += Arguments;
						Cursor = Position;
						Search = Position;
					}
					else
					{
						Search = Marker + 1;
					}
				}
				Result += ReplacePrivatePrefixes(Source.substr(Cursor));
				return Result;
			};
			const auto HasPrivatePrefix = [&](const std::string& Value)
			{
				return std::any_of(
				    Replacements.begin(), Replacements.end(),
				    [&](const auto& Replacement)
				{
					return Value.find(Replacement.first) != std::string::npos;
				});
			};
			const auto Sanitize =
			    [&](std::string& Value)
			{
				if (!HasPrivatePrefix(Value))
				{
					return;
				}
				Value = RewriteEncodedName(Value);
			};
			const auto SanitizeModulePath =
			    [&](std::string& Value)
			{
				if (Value.empty())
				{
					return;
				}
				const auto Replacement = TemplatePathReplacements.find(Value);
				if (Replacement != TemplatePathReplacements.end())
				{
					Value = Replacement->second;
					return;
				}
				// A stripped package must not retain a physical source path even
				// when the module has no generic-template facade entry.
				if (bStripDebugInfo && Value.rfind("<vslib:", 0) != 0)
				{
					Value = "<vslib:" + PackageName + ":module-" + StableModuleId(Value) + ">";
				}
			};
			auto MetadataModules = TemplatePathReplacements;
			auto AddMetadataModules = [&](const FSourceMetadataSnapshot& Snapshot)
			{
				for (const auto& D : Snapshot.Documents)
				{
					if (!D.ModuleId.empty() && !MetadataModules.count(D.ModuleId))
					{
						auto Path = D.ModuleId;
						SanitizeModulePath(Path);
						MetadataModules.emplace(D.ModuleId, std::move(Path));
					}
				}

				for (const auto& N : Snapshot.Nodes)
				{
					if (!N.ModuleId.empty() && !MetadataModules.count(N.ModuleId))
					{
						auto Path = N.ModuleId;
						SanitizeModulePath(Path);
						MetadataModules.emplace(N.ModuleId, std::move(Path));
					}
				}
			};
			if (Module.SourceMetadata)
			{
				AddMetadataModules(*Module.SourceMetadata);
			}

			for (const auto& T : Module.PackageTemplateModules)
			{
				if (T.InitialSourceMetadata)
				{
					AddMetadataModules(*T.InitialSourceMetadata);
				}
			}

			std::vector<std::pair<std::string, std::string>> MetadataReplacements;
			for (const auto& Entry : MetadataModules)
			{
				if (!Entry.first.empty() && Entry.first != Entry.second)
				{
					MetadataReplacements.push_back(Entry);
				}
			}

			std::sort(MetadataReplacements.begin(), MetadataReplacements.end(),
			    [](const auto& L, const auto& R)
			{
				return L.first.size() > R.first.size();
			});
			const auto RewriteMetadataId = [&](std::string& Id)
			{
				std::string Output;
				for (std::size_t I = 0; I < Id.size();)
				{
					bool Found = false;
					for (const auto& Entry : MetadataReplacements)
					{
						if (Id.compare(I, Entry.first.size(), Entry.first) == 0)
						{
							Output += Entry.second;
							I += Entry.first.size();
							Found = true;
							break;
						}
					}

					if (!Found)
					{
						Output += Id[I++];
					}
				}
				Id = std::move(Output);
			};
			const auto TemplateParameters = [&](FTemplateParameterList& List)
			{
				auto Parameters = List.GetParameters();
				for (auto& Parameter : Parameters)
				{
					RewriteMetadataId(Parameter.Id);
					Sanitize(Parameter.Namespace);
					Sanitize(Parameter.ValueTypeName);
					Sanitize(Parameter.DefaultTypeName);
					Sanitize(Parameter.DefaultSource);
				}
				List.Assign(std::move(Parameters), List.GetEnclosingCount());
			};
			const auto TemplateInstance = [&](FTemplateInstanceInfo& Instance)
			{
				Sanitize(Instance.DeclarationId);
				// Declaration IDs also contain metadata paths when every symbol is exported.
				RewriteMetadataId(Instance.DeclarationId);
				for (auto* Arguments : {&Instance.Arguments, &Instance.EnclosingArguments, &Instance.ValueTypeNames})
				{
					for (auto& Argument : *Arguments)
					{
						Sanitize(Argument);
					}
				}
			};
			for (auto& Declaration : Module.TemplateDeclarations)
			{
				Sanitize(Declaration.Id);
				RewriteMetadataId(Declaration.Id);
				Sanitize(Declaration.Owner);
				RewriteMetadataId(Declaration.Owner);
				Sanitize(Declaration.Name);
				Sanitize(Declaration.ReturnType);
				SanitizeModulePath(Declaration.ModulePath);
				if (bStripDebugInfo)
				{
					Declaration.SourceFile.clear();
				}
				TemplateParameters(Declaration.Parameters);
				TemplateParameters(Declaration.EnclosingParameters);
				for (auto& Parameter : Declaration.ParameterTypeNames)
				{
					Sanitize(Parameter);
				}
			}
			for (auto& Type : Module.Types)
			{
				TemplateInstance(Type.TemplateInstance);
			}
			for (auto& Type : Module.Interfaces)
			{
				TemplateInstance(Type.TemplateInstance);
			}
			for (auto& Type : Module.Enums)
			{
				TemplateInstance(Type.TemplateInstance);
			}
			for (auto& Function : Module.Functions)
			{
				TemplateInstance(Function.TemplateInstance);
			}

			const auto SanitizeSourceIdentity = [&](std::string& Id)
			{
				if (Id.empty()) return;
				if (Id.rfind("veroscript-generated:", 0) == 0)
				{
					RewriteMetadataId(Id);
					return;
				}
				for (const auto* Suffix : {":raw", ":accepted", ":active-input"})
				{
					if (Id.size() > std::char_traits<char>::length(Suffix) &&
					    Id.compare(Id.size() - std::char_traits<char>::length(Suffix),
					        std::char_traits<char>::length(Suffix), Suffix) == 0)
					{
						auto Base = Id.substr(0, Id.size() - std::char_traits<char>::length(Suffix));
						SanitizeModulePath(Base);
						Id = Base + Suffix;
						return;
					}
				}

				SanitizeModulePath(Id);
			};
			const auto Position = [&](FAnnotationSourcePosition& P)
			{
				SanitizeSourceIdentity(P.FileLabel);
				if (bStripDebugInfo)
				{
					P.Line = 1;
					P.Column = 1;
				}
			};
			const auto Range = [&](FSourceRange& R)
			{
				SanitizeSourceIdentity(R.SourceId);
				Position(R.Position.Begin);
				Position(R.Position.End);
			};
			const auto Origin = [&](FSourceOrigin& O)
			{
				Range(O.Range);
				RewriteMetadataId(O.GenerationId);
			};
			std::function<void(std::vector<FDeclarationAnnotation>&)> Annotations = [&](auto& As)
			{
				std::function<void(std::vector<FAnnotationArgument>&)> Args = [&](auto& Values)
				{
					for (auto& A : Values)
					{
						Position(A.Range.Begin);
						Position(A.Range.End);
						Args(A.Children);
					}
				};
				for (auto& A : As)
				{
					Position(A.Range.Begin);
					Position(A.Range.End);
					Position(A.NamePosition);
					Args(A.Arguments);
				}
			};
			const auto Comments = [&](std::vector<FSourceComment>& Cs)
			{
				for (auto& C : Cs)
				{
					RewriteMetadataId(C.Id);
					RewriteMetadataId(C.OwnerId);
					SanitizeModulePath(C.ModuleId);
					Range(C.Range);
					Origin(C.Origin);
				}
			};
			const auto SanitizeMeta = [&](FDeclMeta& M)
			{
				Annotations(M.Annotations);
				Comments(M.Comments);
				RewriteMetadataId(M.StableId);
				Range(M.SourceRange);
			};
			const auto Snapshot = [&](const std::shared_ptr<const FSourceMetadataSnapshot>& Input)
			    -> std::shared_ptr<const FSourceMetadataSnapshot>
			{
				if (!Input) return {};
				auto S = std::make_shared<FSourceMetadataSnapshot>(*Input);
				for (auto& D : S->Documents)
				{
					SanitizeSourceIdentity(D.Id);
					SanitizeModulePath(D.ModuleId);
					Comments(D.Comments);
					for (auto& M : D.Mappings)
					{
						Origin(M.Origin);
					}
				}
				for (auto& N : S->Nodes)
				{
					RewriteMetadataId(N.Id);
					RewriteMetadataId(N.OwnerId);
					RewriteMetadataId(N.ParentId);
					SanitizeModulePath(N.ModuleId);
					if (N.Kind == "module")
					{
						N.Name = N.ModuleId;
					}

					Range(N.Range);
					Origin(N.Origin);
					Annotations(N.Annotations);
					Comments(N.Comments);
				}
				return S;
			};
			Module.SourceMetadata = Snapshot(Module.SourceMetadata);
			const auto RewriteGenerationKey = [&](const std::string& Key)
			{
				const auto Separator = Key.find('\x1E');
				if (Separator == std::string::npos) return ReplacePrivatePrefixes(Key);
				auto Result = ReplacePrivatePrefixes(std::string_view(Key).substr(0, Separator + 1));
				std::size_t Cursor = Separator + 1;
				while (Cursor < Key.size())
				{
					const auto Begin = Cursor;
					std::size_t Length = 0;
					while (Cursor < Key.size() && Key[Cursor] >= '0' && Key[Cursor] <= '9')
					{
						const auto Digit = std::size_t(Key[Cursor] - '0');
						if (Length > (std::numeric_limits<std::size_t>::max() - Digit) / 10)
						{
							break;
						}
						Length = Length * 10 + Digit;
						++Cursor;
					}
					if (Cursor == Begin || Cursor >= Key.size() || Key[Cursor] != ':' ||
					    Length > Key.size() - Cursor - 1)
					{
						Result += ReplacePrivatePrefixes(std::string_view(Key).substr(Begin));
						break;
					}
					++Cursor;
					const auto Value = RewriteEncodedName(std::string_view(Key).substr(Cursor, Length));
					Result += std::to_string(Value.size()) + ":" + Value;
					Cursor += Length;
				}
				return Result;
			};

			const auto SanitizeScope = [&](FTypeDeclarationScope& Scope)
			{
				TemplateParameters(Scope.EnclosingTemplateParameters);
				for (auto* Value : {&Scope.Id, &Scope.ScopeId, &Scope.ParentScopeId, &Scope.OwnerFunctionName, &Scope.DisplayName})
				{
					RewriteMetadataId(*Value);
				}
				SanitizeModulePath(Scope.ModulePath);
				SanitizeModulePath(Scope.AccessModulePath);
				Sanitize(Scope.OwnerTypeName);
				Sanitize(Scope.OwnerFunctionName);
				Sanitize(Scope.QualifiedName);
				Sanitize(Scope.AccessTypeName);
				for (auto& Argument : Scope.ConcreteTypeArguments)
				{
					Sanitize(Argument);
				}
			};
			const auto SanitizeTypeScope = [&](auto& Type)
			{
				SanitizeScope(Type.Scope);
				for (auto& Child : Type.NestedTypes)
				{
					SanitizeScope(Child.Scope);
					RewriteMetadataId(Child.DeclarationId);
				}
			};
			const auto SanitizeFields =
			    [&](std::vector<FBytecodeFieldDesc>& Fields)
			{
				for (FBytecodeFieldDesc& Field : Fields)
				{
					Sanitize(Field.TypeName);
					Sanitize(Field.DeclaredTypeName);
					SanitizeMeta(Field.DeclMeta);
				}
			};

			// Structural operands and data constants can share an interned pool slot.
			// Rewrite the structural slot, then redirect only affected data constants
			// to exact original bytes. Never infer the role from the string contents.
			std::vector<FBytecodeConstant*> DataConstants;
			std::unordered_set<std::size_t> DataIndices;
			const auto RememberData = [&](FBytecodeConstant& Constant)
			{
				if (Constant.Kind == EConstantKind::Str && !Constant.bIsLinkageString &&
					Constant.StringIndex < Module.StringPool.size())
				{
					DataConstants.push_back(&Constant);
					DataIndices.insert(Constant.StringIndex);
				}
			};
			for (auto& Constant : Module.Constants)
			{
				RememberData(Constant);
			}
			const auto RememberDefaults = [&](auto& Types)
			{
				for (auto& Type : Types)
				{
					for (auto* Fields : {&Type.Fields, &Type.StaticFields})
					{
						for (auto& Field : *Fields)
						{
							if (Field.bHasDefault)
							{
								RememberData(Field.DefaultValue);
							}
						}
					}
				}
			};
			RememberDefaults(Module.Types);
			RememberDefaults(Module.Interfaces);
			std::unordered_map<std::size_t, std::string> OriginalData;
			for (std::size_t Index = 0; Index < Module.StringPool.size(); ++Index)
			{
				auto& Value = Module.StringPool[Index];
				if (DataIndices.count(Index) && HasPrivatePrefix(Value))
				{
					OriginalData.emplace(Index, Value);
				}
				Sanitize(Value);
			}
			std::unordered_map<std::size_t, std::uint16_t> PreservedData;
			for (auto* Constant : DataConstants)
			{
				const auto Original = OriginalData.find(Constant->StringIndex);
				if (Original == OriginalData.end() ||
					Original->second == Module.StringPool[Constant->StringIndex])
				{
					continue;
				}
				auto Existing = PreservedData.find(Constant->StringIndex);
				if (Existing == PreservedData.end())
				{
					if (Module.StringPool.size() >= std::numeric_limits<std::uint16_t>::max())
					{
						OutError = "package data string pool exceeds the runtime slot limit";
						return false;
					}
					const auto Index = static_cast<std::uint16_t>(Module.StringPool.size());
					Module.StringPool.push_back(Original->second);
					Existing = PreservedData.emplace(Constant->StringIndex, Index).first;
				}
				Constant->StringIndex = Existing->second;
			}
			for (FBytecodeTypeDesc& Type : Module.Types)
			{
				SanitizeTypeScope(Type);
				Sanitize(Type.Name);
				SanitizeMeta(Type.DeclMeta);
				SanitizeFields(Type.Fields);
				SanitizeFields(Type.StaticFields);
			}
			for (FBytecodeInterfaceDesc& Interface : Module.Interfaces)
			{
				SanitizeTypeScope(Interface);
				Sanitize(Interface.Name);
				SanitizeMeta(Interface.DeclMeta);
				SanitizeFields(Interface.Fields);
				SanitizeFields(Interface.StaticFields);
			}
			for (FBytecodeEnumDesc& Enum : Module.Enums)
			{
				SanitizeScope(Enum.Scope);
				Sanitize(Enum.Name);
				SanitizeMeta(Enum.DeclMeta);
				for (auto& M : Enum.Members)
				{
					SanitizeMeta(M.DeclMeta);
				}

				Sanitize(Enum.UnderlyingTypeName);
			}
			for (FBytecodeFunction& Function : Module.Functions)
			{
				Sanitize(Function.Name);
				SanitizeMeta(Function.DeclMeta);
				SanitizeModulePath(Function.ModulePath);
				Sanitize(Function.ClosureTypeName);
				for (auto& Capture : Function.Captures)
				{
					Sanitize(Capture.TypeName);
				}
				if (bStripDebugInfo)
				{
					Function.DebugLines.clear();
					Function.LocalNames.clear();
					Function.Location = FSourceLocation{"<package>", 1, 1};
				}
			}
			for (std::string& Global : Module.GlobalNames)
			{
				Sanitize(Global);
			}
			for (std::string& GlobalType : Module.GlobalTypeNames)
			{
				Sanitize(GlobalType);
			}
			for (FBytecodeGlobalMeta& Meta : Module.GlobalMetadata)
			{
				SanitizeModulePath(Meta.ModulePath);
				if (Meta.ConstantValue)
				{
					Sanitize(Meta.ConstantValue->Type.Name);
				}
				SanitizeMeta(Meta.DeclMeta);
			}
			SanitizeModulePath(Module.EntryModulePath);
			for (FBytecodePackageTemplateModule& Template :
			    Module.PackageTemplateModules)
			{
				if (!Template.bFromPackage)
				{
					const auto Replacement =
					    TemplatePathReplacements.find(Template.ModulePath);
					if (Replacement != TemplatePathReplacements.end())
					{
						Template.ModulePath = Replacement->second;
					}
					Template.FacadePath = Template.bPackageSurface
					    ? "<vslib:" + PackageName + ">"
					    : std::string{};
					Template.bFromPackage = true;
				}
				for (auto& Op : Template.GeneratedOperations)
				{
					Op.TargetId = RewriteGenerationKey(Op.TargetId);
					Op.GenerationId = RewriteGenerationKey(Op.GenerationId);
					SanitizeModulePath(Op.Origin.FileLabel);
				}
				Template.InitialSourceMetadata = Snapshot(Template.InitialSourceMetadata);
				for (auto& O : Template.CompileGeneratedOperations)
				{
					SanitizeModulePath(O.Scope.ModuleId);
					RewriteMetadataId(O.Scope.OwnerId);
					RewriteMetadataId(O.Scope.ScopeId);
					RewriteMetadataId(O.Scope.InsertionBeforeId);
					RewriteMetadataId(O.TargetId);
					RewriteMetadataId(O.GenerationId);
					Range(O.EditRange);
					Origin(O.Origin);
					for (auto& Mapping : O.SourceMappings)
					{
						Origin(Mapping.Origin);
					}

					if (O.Metadata.Annotations)
					{
						Annotations(*O.Metadata.Annotations);
					}

					if (O.Metadata.Comments)
					{
						Comments(*O.Metadata.Comments);
					}

					for (auto& C : O.Metadata.Children)
					{
						Annotations(C.Metadata.Annotations);
						Comments(C.Metadata.Comments);
						Range(C.Metadata.Range);
						Origin(C.Metadata.Origin);
					}
				}
				for (FBytecodePackageTemplateImport& Import : Template.Imports)
				{
					const auto Replacement =
					    TemplatePathReplacements.find(Import.ResolvedModulePath);
					if (Replacement != TemplatePathReplacements.end())
					{
						Import.ResolvedModulePath = Replacement->second;
					}
				}
			}

			if (bStripDebugInfo)
			{
				Module.DebugFiles.clear();
				Module.InitDebugLines.clear();
			}
			return true;
		}
	}

	bool BuildPackage(
	    const FBytecodeModule& Module,
	    const FPackageBuildOptions& Options,
	    std::vector<std::uint8_t>& OutBytes,
	    std::string& OutError)
	try
	{
		OutBytes.clear();
		OutError.clear();
		if (Options.Kind != EPackageKind::Application && Options.Kind != EPackageKind::Library)
		{
			OutError = "invalid package kind";
			return false;
		}
		if (!IsPackageNameValid(Options.PackageName))
		{
			OutError = "invalid package name: " + Options.PackageName;
			return false;
		}
		if (Options.Kind == EPackageKind::Application && Module.MainFunctionIndex < 0)
		{
			OutError = "application package requires a main entry point";
			return false;
		}
		if (Options.Kind == EPackageKind::Library && Module.MainFunctionIndex >= 0)
		{
			OutError = "library package must not define a main entry point";
			return false;
		}
		if (Options.Kind == EPackageKind::Library && !Module.PackageInterfaceError.empty())
		{
			OutError = Module.PackageInterfaceError;
			return false;
		}
		// Check source provenance before rewriting it or stripping locations.
		if (Options.Kind == EPackageKind::Library && !ValidatePublicMacros(Module.PublicMacros, OutError))
		{
			return false;
		}

		if (Options.HostMetadata.size() > kMaxMetadataEntries)
		{
			OutError = "package metadata entry count exceeds safety limit";
			return false;
		}

		std::uint64_t TotalMetadataBytes = 0;
		for (const auto& Entry : Options.HostMetadata)
		{
			if (!IsMetadataKeyValid(Entry.first))
			{
				OutError = "invalid package metadata key: " + Entry.first;
				return false;
			}
			if (Entry.second.size() > kMaxMetadataValueBytes)
			{
				OutError = "package metadata value exceeds safety limit: " + Entry.first;
				return false;
			}
			const std::uint64_t EntryBytes =
			    static_cast<std::uint64_t>(Entry.first.size()) + static_cast<std::uint64_t>(Entry.second.size());
			if (TotalMetadataBytes > kMaxTotalMetadataBytes - EntryBytes)
			{
				OutError = "total package metadata exceeds safety limit";
				return false;
			}
			TotalMetadataBytes += EntryBytes;
		}

		// Private library linkage names must live in the package namespace: two
		// independently built libraries may otherwise collide, and the old stripped
		// form used the same synthetic prefix in every package. An unstripped
		// application keeps source identities so source -> deployed-module hot reload
		// can migrate private state. Public exported names have no module delimiter
		// and deliberately remain unchanged.
		const FBytecodeModule* ModuleToSerialize = &Module;
		FBytecodeModule PackagedModule;
		if (Options.Kind == EPackageKind::Library || Options.bStripDebugInfo ||
		    !Module.PublicMacros.empty() || (Options.Kind == EPackageKind::Application && !Module.PackageTemplateModules.empty()))
		{
			PackagedModule = Module;
			if (Options.Kind == EPackageKind::Library || Options.bStripDebugInfo)
			{
				if (!PreparePackageModule(PackagedModule, Options.PackageName, Options.bStripDebugInfo, OutError))
				{
					return false;
				}
			}

			if (Options.Kind == EPackageKind::Application)
			{
				PackagedModule.PublicMacros.clear();
				PackagedModule.PackageTemplateModules.clear();
			}

			else
			{
				std::unordered_map<std::string, std::string> PortableOrigins;
				for (auto& M : PackagedModule.PublicMacros)
				{
					if (M.OriginModule.rfind("<vslib:", 0) != 0)
					{
						const auto Portable = "<vslib:" + Options.PackageName + "@" +
						    std::to_string(Options.PackageVersion) + ":macro-" +
						    StableModuleId(M.OriginModule) + ">";
						const auto Existing = PortableOrigins.emplace(Portable, M.OriginModule);
						if (!Existing.second && Existing.first->second != M.OriginModule)
						{
							OutError = "colliding portable public macro origins";
							return false;
						}
						M.OriginModule = Portable;
					}
					if (Options.bStripDebugInfo)
					{
						M.SourceName.clear();
						M.Line = M.Column = M.DirectiveStartLine = M.DirectiveEndLine = 0;
					}
				}
			}
			ModuleToSerialize = &PackagedModule;
		}
		if (Options.Kind == EPackageKind::Library)
		{
			// Fingerprint the complete prepared image before adding its own ownership
			// record. Dependency records are already present and therefore participate
			// in the identity without making the hash self-referential.
			std::vector<std::uint8_t> IdentityBytes;
			if (!VeroScriptBytecodeIO::SerializeBytecode(
			        PackagedModule, 0, IdentityBytes, OutError))
			{
				return false;
			}
			const std::string OwnerId = Options.PackageName + "@" + std::to_string(Options.PackageVersion);
			const auto ExistingComponent = std::find_if(
			    PackagedModule.PackageComponents.begin(),
			    PackagedModule.PackageComponents.end(),
			    [&](const FBytecodePackageComponent& Component)
			{
				return Component.Id == OwnerId;
			});
			if (ExistingComponent != PackagedModule.PackageComponents.end())
			{
				OutError = "package dependency cycle or reused package identity: " + OwnerId;
				return false;
			}
			PackagedModule.PackageComponents.push_back(
			    FBytecodePackageComponent{
			        OwnerId, FingerprintBytes(IdentityBytes)});
			FillPackageOwners(
			    PackagedModule.PackageTypeOwners,
			    PackagedModule.Types.size(),
			    OwnerId);
			FillPackageOwners(
			    PackagedModule.PackageInterfaceOwners,
			    PackagedModule.Interfaces.size(),
			    OwnerId);
			FillPackageOwners(
			    PackagedModule.PackageEnumOwners,
			    PackagedModule.Enums.size(),
			    OwnerId);
			FillPackageOwners(
			    PackagedModule.PackageFunctionOwners,
			    PackagedModule.Functions.size(),
			    OwnerId);
			FillPackageOwners(
			    PackagedModule.PackageGlobalOwners,
			    PackagedModule.GlobalNames.size(),
			    OwnerId);
			FillPackageInitOwnership(PackagedModule, OwnerId);
			for (auto& Template : PackagedModule.PackageTemplateModules)
			{
				if (Template.OwnerComponentId.empty())
				{
					Template.OwnerComponentId = OwnerId;
				}
			}
		}
		if (!ValidatePackageMacroOrigins(
		        *ModuleToSerialize, Options.Kind, Options.PackageName, Options.PackageVersion, OutError))
		{
			return false;
		}

		std::vector<std::uint8_t> BytecodeBytes;
		std::vector<std::uint8_t> ExtensionBytes;
		if (Options.CompileExtension)
		{
			if (Options.Kind != EPackageKind::Library)
			{
				OutError = "compilation extensions require a library package";
				return false;
			}
			auto M = *Options.CompileExtension;
			M.ModuleId = ModuleToSerialize->EntryModulePath;
			if (M.ServiceFingerprint.empty())
			{
				M.ServiceFingerprint = GetScriptCompileServiceFingerprint();
			}

			if (M.Version != "0.1.1" || M.ServiceFingerprint != GetScriptCompileServiceFingerprint() ||
			    M.RegistrationEntry.empty() || M.Dependencies.size() > 64)
			{
				OutError = "invalid compilation extension manifest";
				return false;
			}
			Bytecode::WriteU32LE(ExtensionBytes, 0x45435356u);
			auto Text = [&](const std::string& S)
			{
				if (S.size() > 65536)
				{
					OutError = "compilation extension manifest text too large";
					return false;
				}
				Bytecode::WriteU32LE(ExtensionBytes, static_cast<std::uint32_t>(S.size()));
				ExtensionBytes.insert(ExtensionBytes.end(), S.begin(), S.end());
				return true;
			};
			if (!Text(M.ModuleId) || !Text(M.RegistrationEntry) || !Text(M.Version) ||
			    !Text(M.ServiceFingerprint) || !Text(M.HostBindingFingerprint))
			{
				return false;
			}

			Bytecode::WriteU32LE(ExtensionBytes, static_cast<std::uint32_t>(M.Dependencies.size()));
			std::unordered_set<std::string> Paths;
			for (const auto& Dep : M.Dependencies)
			{
				if (Dep.Path.empty() || !Paths.insert(Dep.Path).second)
				{
					OutError = "invalid compilation extension dependency";
					return false;
				}
				if (!Text(Dep.Path) || !Text(Dep.RegistrationEntry))
				{
					return false;
				}
			}
			if (!ValidateCompileExtensionEntry(*ModuleToSerialize, M))
			{
				OutError = "compilation extension registration entry is missing, ambiguous or invalid";
				return false;
			}
		}
		if (!VeroScriptBytecodeIO::SerializeBytecode(
		        *ModuleToSerialize, 0, BytecodeBytes, OutError))
		{
			return false;
		}
		if (BytecodeBytes.size() > VEROSCRIPT_BYTECODE_MAX_BYTES)
		{
			OutError = "package bytecode exceeds safety limit";
			return false;
		}
		const std::uint64_t EncodedPackageSize =
		    static_cast<std::uint64_t>(kPackageHeaderBytes) + static_cast<std::uint64_t>(Options.PackageName.size()) + static_cast<std::uint64_t>(Options.HostMetadata.size()) * 12u + TotalMetadataBytes + ExtensionBytes.size() + 8u + static_cast<std::uint64_t>(BytecodeBytes.size());
		if (EncodedPackageSize > VEROSCRIPT_PACKAGE_MAX_BYTES)
		{
			OutError = "package exceeds safety limit";
			return false;
		}

		std::vector<std::uint8_t> Buffer;
		Buffer.reserve(static_cast<std::size_t>(EncodedPackageSize));
		Bytecode::WriteU32LE(Buffer, VEROSCRIPT_PACKAGE_MAGIC);
		Bytecode::WriteU16LE(Buffer, VEROSCRIPT_PACKAGE_FORMAT_MAJOR);
		Bytecode::WriteU16LE(Buffer, VEROSCRIPT_PACKAGE_FORMAT_MINOR);
		Bytecode::WriteU64LE(Buffer, Options.PackageVersion);
		Bytecode::WriteU8(
		    Buffer, static_cast<std::uint8_t>(Options.Kind));
		// Reserved bytes must remain zero so future format changes cannot acquire
		// multiple encodings under the current version.
		Bytecode::WriteU8(Buffer, Options.CompileExtension ? 1 : 0);
		Bytecode::WriteU8(Buffer, 0);
		Bytecode::WriteU8(Buffer, 0);
		Bytecode::WriteU32LE(
		    Buffer, static_cast<std::uint32_t>(Options.PackageName.size()));
		Bytecode::WriteU32LE(
		    Buffer, static_cast<std::uint32_t>(Options.HostMetadata.size()));
		Buffer.insert(
		    Buffer.end(), Options.PackageName.begin(), Options.PackageName.end());

		for (const auto& Entry : Options.HostMetadata)
		{
			const std::uint64_t EncodedSize =
			    4u + Entry.first.size() + 8u + Entry.second.size();
			if (!CanAppend(Buffer, EncodedSize))
			{
				OutError = "package exceeds safety limit";
				return false;
			}
			Bytecode::WriteU32LE(
			    Buffer, static_cast<std::uint32_t>(Entry.first.size()));
			Buffer.insert(Buffer.end(), Entry.first.begin(), Entry.first.end());
			Bytecode::WriteU64LE(
			    Buffer, static_cast<std::uint64_t>(Entry.second.size()));
			Buffer.insert(Buffer.end(), Entry.second.begin(), Entry.second.end());
		}

		Buffer.insert(Buffer.end(), ExtensionBytes.begin(), ExtensionBytes.end());
		if (!CanAppend(Buffer, 8u + BytecodeBytes.size()))
		{
			OutError = "package exceeds safety limit";
			return false;
		}
		Bytecode::WriteU64LE(
		    Buffer, static_cast<std::uint64_t>(BytecodeBytes.size()));
		Buffer.insert(Buffer.end(), BytecodeBytes.begin(), BytecodeBytes.end());
		OutBytes = std::move(Buffer);
		return true;
	}
	catch (const std::bad_alloc&)
	{
		OutBytes.clear();
		OutError.clear();
		OutError = "out of memory";
		return false;
	}
	catch (const std::length_error&)
	{
		OutBytes.clear();
		OutError.clear();
		OutError = "size overflow";
		return false;
	}

	bool ReadPackage(
	    const FByteView Bytes,
	    FPackageData& OutPackage,
	    std::string& OutError)
	try
	{
		OutError.clear();
		if (Bytes.Size != 0 && Bytes.Data == nullptr)
		{
			OutError = "package data pointer is null";
			return false;
		}
		if (Bytes.Size < kPackageHeaderBytes)
		{
			OutError = "package header is too small";
			return false;
		}
		if (Bytes.Size > VEROSCRIPT_PACKAGE_MAX_BYTES)
		{
			OutError = "package exceeds safety limit";
			return false;
		}

		const std::uint8_t* const Data = Bytes.Data;
		const std::size_t Size = Bytes.Size;
		std::size_t Offset = 0;
		const std::uint32_t Magic = Bytecode::ReadU32LE(Data + Offset);
		Offset += 4;
		const std::uint16_t FormatMajor = Bytecode::ReadU16LE(Data + Offset);
		Offset += 2;
		const std::uint16_t FormatMinor = Bytecode::ReadU16LE(Data + Offset);
		Offset += 2;
		std::uint64_t PackageVersion = 0;
		std::uint32_t PackageNameLength = 0;
		std::uint32_t MetadataCount = 0;
		if (!ReadU64(Data, Size, Offset, PackageVersion) || Offset > Size || Size - Offset < 4)
		{
			OutError = "package header is truncated";
			return false;
		}
		const EPackageKind Kind = static_cast<EPackageKind>(Data[Offset]);
		const bool HasCompileExtension = Data[Offset + 1] == 1;
		if (Data[Offset + 1] > 1 || Data[Offset + 2] != 0 || Data[Offset + 3] != 0)
		{
			OutError = "package reserved header bytes must be zero";
			return false;
		}
		Offset += 4;
		if (!ReadU32(Data, Size, Offset, PackageNameLength) || !ReadU32(Data, Size, Offset, MetadataCount))
		{
			OutError = "package header is truncated";
			return false;
		}
		if (Magic != VEROSCRIPT_PACKAGE_MAGIC)
		{
			OutError = "invalid package magic";
			return false;
		}
		if (FormatMajor != VEROSCRIPT_PACKAGE_FORMAT_MAJOR || FormatMinor != VEROSCRIPT_PACKAGE_FORMAT_MINOR)
		{
			OutError = "package format version mismatch";
			return false;
		}
		if (Kind != EPackageKind::Application && Kind != EPackageKind::Library)
		{
			OutError = "invalid package kind";
			return false;
		}
		if (PackageNameLength == 0 || PackageNameLength > kMaxPackageNameBytes || !FitsRemaining(Size, Offset, PackageNameLength))
		{
			OutError = "invalid or truncated package name";
			return false;
		}
		std::string PackageName(
		    reinterpret_cast<const char*>(Data + Offset),
		    PackageNameLength);
		Offset += PackageNameLength;
		if (!IsPackageNameValid(PackageName))
		{
			OutError = "invalid package name: " + PackageName;
			return false;
		}
		if (MetadataCount > kMaxMetadataEntries)
		{
			OutError = "package metadata entry count exceeds safety limit";
			return false;
		}

		std::map<std::string, std::vector<std::uint8_t>> Metadata;
		std::string PreviousKey;
		std::uint64_t TotalMetadataBytes = 0;
		for (std::uint32_t Index = 0; Index < MetadataCount; ++Index)
		{
			std::uint32_t KeyLength = 0;
			if (!ReadU32(Data, Size, Offset, KeyLength) || KeyLength == 0 || KeyLength > kMaxMetadataKeyBytes || !FitsRemaining(Size, Offset, KeyLength))
			{
				OutError = "invalid or truncated package metadata key";
				return false;
			}
			std::string Key(
			    reinterpret_cast<const char*>(Data + Offset), KeyLength);
			Offset += KeyLength;
			if (!IsMetadataKeyValid(Key))
			{
				OutError = "invalid package metadata key: " + Key;
				return false;
			}
			if (!PreviousKey.empty() && !(PreviousKey < Key))
			{
				OutError = "package metadata keys are duplicate or noncanonical";
				return false;
			}

			std::uint64_t ValueLength = 0;
			if (!ReadU64(Data, Size, Offset, ValueLength) || ValueLength > kMaxMetadataValueBytes || !FitsRemaining(Size, Offset, ValueLength))
			{
				OutError = "invalid or truncated package metadata value: " + Key;
				return false;
			}
			const std::uint64_t EntryBytes =
			    static_cast<std::uint64_t>(KeyLength) + ValueLength;
			if (TotalMetadataBytes > kMaxTotalMetadataBytes - EntryBytes)
			{
				OutError = "total package metadata exceeds safety limit";
				return false;
			}
			TotalMetadataBytes += EntryBytes;
			const std::size_t NativeValueLength =
			    static_cast<std::size_t>(ValueLength);
			Metadata.emplace(
			    Key,
			    std::vector<std::uint8_t>(
			        Data + Offset, Data + Offset + NativeValueLength));
			Offset += NativeValueLength;
			PreviousKey = std::move(Key);
		}

		std::optional<FCompileExtensionManifest> CompileExtension;
		if (HasCompileExtension)
		{
			std::uint32_t SectionMagic = 0, Count = 0;
			if (Kind != EPackageKind::Library || !ReadU32(Data, Size, Offset, SectionMagic) || SectionMagic != 0x45435356u)
			{
				OutError = "invalid compilation extension section";
				return false;
			}
			auto Text = [&](std::string& S)
			{
				std::uint32_t Length = 0;
				if (!ReadU32(Data, Size, Offset, Length) || Length > 65536 || !FitsRemaining(Size, Offset, Length))
				{
					OutError = "invalid compilation extension manifest text";
					return false;
				}
				S.assign(reinterpret_cast<const char*>(Data + Offset), Length);
				Offset += Length;
				return true;
			};
			FCompileExtensionManifest M;
			if (!Text(M.ModuleId) || !Text(M.RegistrationEntry) || !Text(M.Version) ||
			    !Text(M.ServiceFingerprint) || !Text(M.HostBindingFingerprint))
			{
				return false;
			}

			if (M.ModuleId.empty() || M.RegistrationEntry.empty() || M.Version != "0.1.1" ||
			    M.ServiceFingerprint != GetScriptCompileServiceFingerprint() ||
			    !ReadU32(Data, Size, Offset, Count) || Count > 64)
			{
				OutError = "compilation extension service contract mismatch";
				return false;
			}
			std::unordered_set<std::string> Paths;
			for (std::uint32_t I = 0; I < Count; ++I)
			{
				FCompileExtensionDependency D;
				if (!Text(D.Path) || !Text(D.RegistrationEntry))
				{
					return false;
				}

				if (D.Path.empty() || !Paths.insert(D.Path).second)
				{
					OutError = "invalid compilation extension dependency";
					return false;
				}
				M.Dependencies.push_back(std::move(D));
			}
			CompileExtension = std::move(M);
		}
		std::uint64_t BytecodeLength = 0;
		if (!ReadU64(Data, Size, Offset, BytecodeLength) || BytecodeLength == 0 || BytecodeLength > VEROSCRIPT_BYTECODE_MAX_BYTES || !FitsRemaining(Size, Offset, BytecodeLength))
		{
			OutError = "invalid or truncated package bytecode payload";
			return false;
		}
		if (BytecodeLength != static_cast<std::uint64_t>(Size - Offset))
		{
			OutError = "package contains trailing or noncanonical data";
			return false;
		}

		auto Module = VeroScriptBytecodeIO::DeserializeBytecode(
		    FByteView(Data + Offset, static_cast<std::size_t>(BytecodeLength)),
		    // Deployed packages have one canonical source-cache identity: zero.
		    // Accepting arbitrary ignored fingerprints would permit multiple signed
		    // byte sequences for the same logical package.
		    std::uint64_t{0},
		    OutError);
		if (!Module)
		{
			OutError = "invalid package bytecode: " + OutError;
			return false;
		}
		if (Kind == EPackageKind::Application && Module->MainFunctionIndex < 0)
		{
			OutError = "application package is missing its main entry point";
			return false;
		}
		if (Kind == EPackageKind::Library && Module->MainFunctionIndex >= 0)
		{
			OutError = "library package contains an application entry point";
			return false;
		}
		if (Kind == EPackageKind::Library && !Module->PackageInterfaceError.empty())
		{
			OutError = "invalid library package interface: " + Module->PackageInterfaceError;
			return false;
		}

		if (Kind == EPackageKind::Library)
		{
			for (const auto& Template : Module->PackageTemplateModules)
			{
				if (Template.OwnerComponentId.empty())
				{
					OutError = "library template is missing its owner component";
					return false;
				}
			}
		}
		if (!ValidatePackageMacroOrigins(*Module, Kind, PackageName, PackageVersion, OutError))
		{
			return false;
		}

		FPackageData Parsed;
		Parsed.Kind = Kind;
		Parsed.PackageName = std::move(PackageName);
		Parsed.PackageVersion = PackageVersion;
		Parsed.HostMetadata = std::move(Metadata);
		Parsed.CanonicalSignedBytes.assign(Data, Data + Size);
		Parsed.Module = std::move(Module);
		if (CompileExtension)
		{
			if (CompileExtension->ModuleId != Parsed.Module->EntryModulePath)
			{
				OutError = "compilation extension module identity mismatch";
				return false;
			}
			if (!ValidateCompileExtensionEntry(*Parsed.Module, *CompileExtension))
			{
				OutError = "invalid or ambiguous compilation extension registration entry";
				return false;
			}
			Parsed.CompileExtension = std::move(CompileExtension);
		}
		OutPackage = std::move(Parsed);
		return true;
	}
	catch (const std::bad_alloc&)
	{
		OutError.clear();
		OutError = "out of memory";
		return false;
	}
	catch (const std::length_error&)
	{
		OutError.clear();
		OutError = "size overflow";
		return false;
	}
}
