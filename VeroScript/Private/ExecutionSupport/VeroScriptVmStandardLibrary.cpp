// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptPath.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace VeroScript
{
	bool VeroScriptVm::CallStandardLibraryBuiltin(EBuiltinId Builtin, FValue* A, std::uint8_t Count)
	{
		using namespace StandardLibrary;
		const FServiceContext Context{StandardLibraryLimits, GetMemoryWork()};
		const auto BadArguments = [&]()
		{
			ReportErrorId("diag.stdlib_arguments");
			return false;
		};
		if (!A || !Context.Check()) return bHasError ? false : BadArguments();
		if (OperandStack.size() >= MaxOperandStackDepth)
		{
			ReportError("operand stack overflow");
			return false;
		}
		// Prepare the return slot before any referenced scalar or external state is
		// changed. Scalar assignment then cannot allocate or call script code.
		OperandStack.reserve(OperandStack.size() + 1);
		if (Builtin == EBuiltinId::Split)
		{
			const auto* Text = std::get_if<std::string>(&A[0]);
			const auto* Separator = std::get_if<std::string>(&A[1]);
			if (!Text || !Separator) return BadArguments();
			std::vector<std::string> Parts;
			if (!ReportStandardLibraryResult(Split(*Text, *Separator, Parts, Context))) return false;
			auto Result = CreateDynamicArray("array<string>", {});
			if (!Result) return false;
			Result->MaterializeBoxedElements();
			Result->Elements.reserve(Parts.size());
			for (auto& Part : Parts)
			{
				if (!Context.Check(1)) return false;
				Result->Elements.emplace_back(std::move(Part));
			}
			Push(std::move(Result));
			return !bHasError;
		}
		if (Builtin == EBuiltinId::Join)
		{
			const auto* Array = std::get_if<FDynamicArrayHandle>(&A[0]);
			const auto* Separator = std::get_if<std::string>(&A[1]);
			if (!Array || !*Array || !Separator || (*Array)->TypeName != "array<string>") return BadArguments();
			std::vector<std::string_view> Parts;
			Parts.reserve((*Array)->ElementCount());
			for (std::size_t I = 0; I < (*Array)->ElementCount(); ++I)
			{
				if (!Context.Check(1)) return false;
				const auto* Part = (*Array)->BorrowString(I);
				if (!Part) return BadArguments();
				Parts.emplace_back(*Part);
			}
			std::string Result;
			if (!ReportStandardLibraryResult(Join(Parts, *Separator, Result, Context))) return false;
			Push(std::move(Result));
			return !bHasError;
		}
		if (Builtin == EBuiltinId::NumericTypeValidate)
		{
			const auto* Type = std::get_if<std::string>(&A[0]);
			return Type && *Type != "bool" && IsScalarParseType(*Type) ? true : BadArguments();
		}
		if (Builtin == EBuiltinId::TryReadLine || Builtin == EBuiltinId::TryReadToken || Builtin == EBuiltinId::TryGetEnv)
		{
			const auto* TypeName = std::get_if<std::string>(&A[Count - 1]);
			if (!TypeName || !ActiveModule) return BadArguments();
			const auto TypeIndex = FindTypeIndex(*TypeName);
			if (TypeIndex >= ActiveModule->Types.size()) return BadArguments();
			const auto& Type = ActiveModule->Types[TypeIndex];
			if (Type.Fields.size() != 3 || Type.Fields[0].Name != "present" || Type.Fields[0].TypeName != "bool" || Type.Fields[1].Name != "value" || Type.Fields[1].TypeName != "string" || Type.Fields[2].Name != "empty_value" || Type.Fields[2].TypeName != "string")
			{
				return BadArguments();
			}
			auto Optional = CreateObject(TypeIndex, {});
			if (!Optional) return false;
			std::optional<std::string> Value;
			if (Builtin == EBuiltinId::TryGetEnv)
			{
				const auto* Name = std::get_if<std::string>(&A[0]);
				if (!Name || Name->empty() || Name->find_first_of(std::string_view("=\0", 2)) != std::string::npos) return BadArguments();
				if (!ReportStandardLibraryResult(Context.CheckText(Name->size()))) return false;
				if (!Environment::ReadUtf8(*Name, Value)) return ReportStandardLibraryResult({EServiceStatus::IoError, 0});
				if (Value && !ReportStandardLibraryResult(Context.CheckText(Value->size()))) return false;
			}
			else if (!ReportStandardLibraryResult(ReadInput(InputStream, Builtin == EBuiltinId::TryReadToken, Value, Context)))
			{
				return false;
			}
			if (Value)
			{
				Optional->Fields[1] = std::move(*Value);
				Optional->Fields[0] = true;
			}
			Push(std::move(Optional));
			return !bHasError;
		}
		if (Builtin == EBuiltinId::ReadBytes || Builtin == EBuiltinId::WriteBytes || Builtin == EBuiltinId::AppendBytes || Builtin == EBuiltinId::ReadFile || Builtin == EBuiltinId::WriteFile || Builtin == EBuiltinId::AppendFile)
		{
			const auto* Path = std::get_if<std::string>(&A[0]);
			if (!Path || Path->find('\0') != std::string::npos) return BadArguments();
			if (!ReportStandardLibraryResult(Context.CheckText(Path->size()))) return false;
			if (Builtin == EBuiltinId::ReadBytes || Builtin == EBuiltinId::ReadFile)
			{
				std::optional<std::uint64_t> Maximum;
				if (Count == 2)
				{
					std::int64_t N = 0;
					if (!Runtime::TryGetInteger64(A[1], N) || N < 0) return BadArguments();
					Maximum = static_cast<std::uint64_t>(N);
				}
				const auto NativePath = PathUtil::FromUtf8(*Path);
				std::error_code FileError;
				// Some filebuf implementations report directories as immediate EOF.
				if (std::filesystem::is_directory(NativePath, FileError) || FileError)
				{
					return ReportStandardLibraryResult({EServiceStatus::IoError, 0});
				}
				std::ifstream Input(NativePath, std::ios::binary);
				if (!Input) return ReportStandardLibraryResult({EServiceStatus::IoError, 0});
				FValue Buffer;
				FMemoryResult MemoryError;
				FServiceResult R;
				if (Builtin == EBuiltinId::ReadFile)
				{
					std::string Text;
					R = ReadText(Input, Text, Context);
					if (R)
					{
						Buffer = std::move(Text);
					}
				}
				else
				{
					R = ReadBytes(Input, GetMemoryContext(), Maximum, Buffer, MemoryError, Context);
				}
				if (!MemoryError)
				{
					if (!bHasError)
					{
						ReportErrorId(MemoryError.MessageId);
					}
					return false;
				}
				if (!ReportStandardLibraryResult(R)) return false;
				Input.clear();
				Input.close();
				if (Input.fail()) return ReportStandardLibraryResult({EServiceStatus::IoError, 0});
				Push(std::move(Buffer));
				return !bHasError;
			}
			FMemoryLease Lease;
			if (Builtin == EBuiltinId::WriteFile || Builtin == EBuiltinId::AppendFile)
			{
				const auto* Text = std::get_if<std::string>(&A[1]);
				if (!Text) return BadArguments();
				if (!ReportStandardLibraryResult(Context.CheckText(Text->size()))) return false;
				Lease.Data = reinterpret_cast<const std::uint8_t*>(Text->data());
				Lease.Size = Text->size();
			}
			else
			{
				const auto Borrowed = Memory::AcquireLease(GetMemoryContext(), A[1], false, Lease);
				if (!Borrowed)
				{
					ReportErrorId(Borrowed.MessageId);
					return false;
				}
			}
			struct FRelease
			{
				FMemoryLeaseToken Token;
				~FRelease()
				{
					if (Token.Serial)
					{
						(void)Memory::ReleaseLease(Token);
					}
				}
			} Release{Lease.Token};
			if (!Context.Check()) return false;
			std::ofstream Output(PathUtil::FromUtf8(*Path), std::ios::binary | (Builtin == EBuiltinId::AppendBytes || Builtin == EBuiltinId::AppendFile ? std::ios::app : std::ios::trunc));
			if (!Output) return ReportStandardLibraryResult({EServiceStatus::IoError, 0});
			if (!ReportStandardLibraryResult(WriteBytes(Output, Lease, Context))) return false;
			Output.close();
			return !Output.fail() || ReportStandardLibraryResult({EServiceStatus::IoError, 0});
		}
		if (Builtin == EBuiltinId::PathIsAbsolute || Builtin == EBuiltinId::PathRoot || Builtin == EBuiltinId::PathNormalize)
		{
			const auto* Path = std::get_if<std::string>(&A[0]);
			const auto* Style = std::get_if<FEnumValue>(&A[1]);
			if (!Path || !Style || Style->TypeName != "std.filesystem.path_style" || Style->Value < 0 || Style->Value > 2) return BadArguments();
			FLexicalPath Result;
			if (!ReportStandardLibraryResult(LexicalPath(*Path, static_cast<EPathStyle>(Style->Value), Result, Context))) return false;
			if (Builtin == EBuiltinId::PathIsAbsolute)
			{
				Push(Result.Absolute);
			}
			else
			{
				Push(Builtin == EBuiltinId::PathRoot ? std::move(Result.Root) : std::move(Result.Normal));
			}
			return !bHasError;
		}
		if (Builtin == EBuiltinId::UnsetEnv)
		{
			const auto* Name = std::get_if<std::string>(&A[0]);
			if (!Name || Name->empty() || Name->find_first_of(std::string_view("=\0", 2)) != std::string::npos) return BadArguments();
			if (!ReportStandardLibraryResult(Context.CheckText(Name->size()))) return false;
			return Environment::UnsetUtf8(*Name) || ReportStandardLibraryResult({EServiceStatus::IoError, 0});
		}
		if (Builtin == EBuiltinId::ScalarParse || Builtin == EBuiltinId::ScalarTryParse)
		{
			const bool Try = Builtin == EBuiltinId::ScalarTryParse;
			const auto* Text = std::get_if<std::string>(&A[0]);
			const auto* Type = std::get_if<std::string>(&A[Count - 1]);
			std::int64_t Base = 0;
			if (!Text || !Type || !Runtime::TryGetInteger64(A[Try ? 2 : 1], Base) || Base < 2 || Base > 36)
			{
				return BadArguments();
			}
			FValue* Target = nullptr;
			if (Try)
			{
				if (!std::holds_alternative<FLocalRef>(A[1]) && !std::holds_alternative<FGlobalRef>(A[1]) && !std::holds_alternative<FThisFieldRef>(A[1]) && !std::holds_alternative<FIndexRef>(A[1]) && !std::holds_alternative<FMapEntryRef>(A[1]) && !IsClosureReferenceCell(A[1]))
				{
					return BadArguments();
				}
				Target = ResolveRefStorage(A[1]);
				if (!Target || !Runtime::ValueMatchesPrimitiveTypeName(*Target, *Type)) return BadArguments();
			}
			FValue Parsed;
			const auto Result = ParseScalar(*Text, *Type, static_cast<int>(Base), true, Parsed, Context);
			if (Try && Result.IsParseFailure())
			{
				Push(false);
				return true;
			}
			if (!ReportStandardLibraryResult(Result)) return false;
			if (!Try)
			{
				Push(std::move(Parsed));
				return !bHasError;
			}
			*Target = std::move(Parsed);
			Push(true);
			NotifyReferenceMutation(A[1]);
			return !bHasError;
		}
		if (Builtin == EBuiltinId::StringDecodeUtf8 || Builtin == EBuiltinId::StringIsValidUtf8)
		{
			if (const auto* Text = std::get_if<std::string>(&A[0]))
			{
				if (Builtin != EBuiltinId::StringIsValidUtf8) return BadArguments();
				const auto R = ValidateUtf8(*Text, Context);
				if (R || R.Status == EServiceStatus::InvalidText)
				{
					Push(static_cast<bool>(R));
					return true;
				}
				return ReportStandardLibraryResult(R);
			}
			FMemoryLease Lease;
			const auto Borrowed = Memory::AcquireLease(GetMemoryContext(), A[0], false, Lease);
			if (!Borrowed)
			{
				ReportErrorId(Borrowed.MessageId);
				return false;
			}
			struct FRelease
			{
				FMemoryLeaseToken Token;
				~FRelease()
				{
					(void)Memory::ReleaseLease(Token);
				}
			} Release{Lease.Token};
			if (Lease.Size > std::string{}.max_size())
			{
				return ReportStandardLibraryResult({EServiceStatus::LimitExceeded, 0});
			}
			const std::string_view Text(Lease.Size ? reinterpret_cast<const char*>(Lease.Data) : "", static_cast<std::size_t>(Lease.Size));
			const auto R = ValidateUtf8(Text, Context);
			if (Builtin == EBuiltinId::StringIsValidUtf8 && (R || R.Status == EServiceStatus::InvalidText))
			{
				Push(static_cast<bool>(R));
				return true;
			}
			if (!ReportStandardLibraryResult(R)) return false;
			std::string Result;
			if (!ReportStandardLibraryResult(Repeat(Text, 1, Result, Context))) return false;
			Push(std::move(Result));
			return !bHasError;
		}
		const auto* Text = std::get_if<std::string>(&A[0]);
		if (!Text) return BadArguments();
		const auto* Other = Count > 1 ? std::get_if<std::string>(&A[1]) : nullptr;
		std::string Result;
		FServiceResult Status;
		bool Bool = false;
		std::int64_t Number = 0;
		switch (Builtin)
		{
		case EBuiltinId::StringStartsWith:
		case EBuiltinId::StringEndsWith:
			{
				if (!Other) return BadArguments();
				Status = Builtin == EBuiltinId::StringStartsWith ? StartsWith(*Text, *Other, Bool, Context)
				                                                 : EndsWith(*Text, *Other, Bool, Context);
				if (!ReportStandardLibraryResult(Status)) return false;
				Push(Bool);
				return !bHasError;
			}
		case EBuiltinId::StringLastFind:
			{
				if (!Other) return BadArguments();
				if (!ReportStandardLibraryResult(LastFind(*Text, *Other, Number, Context))) return false;
				Push(Number);
				return !bHasError;
			}
		case EBuiltinId::StringRepeat:
			{
				if (!Runtime::TryGetInteger64(A[1], Number)) return BadArguments();
				Status = Repeat(*Text, Number, Result, Context);
				break;
			}
		case EBuiltinId::StringReplace:
			{
				if (!Other || !std::holds_alternative<std::string>(A[2])) return BadArguments();
				Status = Replace(*Text, *Other, std::get<std::string>(A[2]), Result, Context);
				break;
			}
		case EBuiltinId::StringTrim:
			{
				if (!std::holds_alternative<bool>(A[1]) || !std::holds_alternative<bool>(A[2])) return BadArguments();
				Status = Trim(*Text, std::get<bool>(A[1]), std::get<bool>(A[2]), Result, Context);
				break;
			}
		case EBuiltinId::StringEncodeUtf8:
			{
				if (!ReportStandardLibraryResult(ValidateUtf8(*Text, Context))) return false;
				FValue Buffer;
				const auto Created = Memory::CreateBuffer(GetMemoryContext(), static_cast<std::int64_t>(Text->size()), Buffer, Context.Work);
				if (!Created)
				{
					ReportErrorId(Created.MessageId);
					return false;
				}
				FMemoryLease Lease;
				const auto Borrowed = Memory::AcquireLease(GetMemoryContext(), Buffer, true, Lease);
				if (!Borrowed)
				{
					ReportErrorId(Borrowed.MessageId);
					return false;
				}
				struct FRelease
				{
					FMemoryLeaseToken Token;
					~FRelease()
					{
						(void)Memory::ReleaseLease(Token);
					}
				} Release{Lease.Token};
				for (std::size_t I = 0; I < Text->size();)
				{
					const auto N = std::min<std::size_t>(1024, Text->size() - I);
					if (!Context.Check(N)) return false;
					std::memcpy(Lease.WritableData + I, Text->data() + I, N);
					I += N;
				}
				Push(std::move(Buffer));
				return !bHasError;
			}
		default:
			return BadArguments();
		}
		if (!ReportStandardLibraryResult(Status)) return false;
		Push(std::move(Result));
		return !bHasError;
	}

	bool VeroScriptVm::ReportStandardLibraryResult(StandardLibrary::FServiceResult Result)
	{
		using E = StandardLibrary::EServiceStatus;
		if (Result) return true;
		if (bHasError) return false;
		const auto Id = Result.Status == E::InvalidArgument ? "diag.stdlib_arguments"
		    : Result.Status == E::LimitExceeded             ? "diag.stdlib_limit"
		    : Result.Status == E::Interrupted               ? "diag.stdlib_interrupted"
		    : Result.Status == E::IoError                   ? "diag.stdlib_io"
		    : Result.Status == E::OutOfRange                ? "diag.stdlib_range"
		                                                    : "diag.stdlib_text";
		if (Result.Status == E::InvalidText || Result.Status == E::OutOfRange)
		{
			ReportErrorId(Id, {}, {std::to_string(Result.Offset)});
		}
		else
		{
			ReportErrorId(Id);
		}
		return false;
	}
}
