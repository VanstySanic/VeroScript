// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "TypeSupport/VeroScriptTemplateReflection.h"
#include "VeroScriptNumericBindings.h"
#include "VeroScriptStandardLibrary.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptJit.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptMethodBindingInternal.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptPath.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptSourceMetadata.h"
#include "VeroScriptConsole.h"
#include "VeroScriptVmInternal.h"
#include "VeroScriptVmArguments.h"
#include "VeroScriptVariant.h"
#include "VeroScriptVmCleanup.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace VeroScript
{
	namespace
	{
		bool DecodeReflectionDescriptorType(
		    const std::string& DescriptorType,
		    const std::string_view Prefix,
		    std::string& OutArgument)
		{
			const std::size_t Hash = FindGenericTypeMarker(DescriptorType);
			if (Hash == std::string::npos || ModuleMeta::TypeSimpleName(DescriptorType.substr(0, Hash)) != Prefix)
			{
				return false;
			}
			const std::size_t Colon = DescriptorType.find(':', Hash + 1);
			if (Colon == std::string::npos || Colon == Hash + 1)
			{
				return false;
			}
			std::size_t Length = 0;
			const auto Parsed = std::from_chars(
			    DescriptorType.data() + Hash + 1,
			    DescriptorType.data() + Colon, Length);
			if (Parsed.ec != std::errc{} || Parsed.ptr != DescriptorType.data() + Colon || Length == 0 || Length != DescriptorType.size() - Colon - 1)
			{
				return false;
			}
			OutArgument = DescriptorType.substr(Colon + 1);
			return true;
		}

		using namespace MethodBindingDetail;

		bool IsPublicReflectedField(
		    const FBytecodeModule& Module,
		    const std::uint16_t TypeIndex,
		    const std::uint16_t FieldIndex)
		{
			if (TypeIndex >= Module.Types.size())
			{
				return false;
			}
			const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
			if (FieldIndex >= Type.Fields.size())
			{
				return false;
			}
			if (FieldIndex >= Type.OwnFieldStartIndex)
			{
				return Type.Fields[FieldIndex].Access == EMemberAccess::Public;
			}
			std::size_t Offset = 0;
			for (std::size_t Index = 0; Index < Type.BaseTypeIndices.size(); ++Index)
			{
				const std::uint16_t BaseIndex = Type.BaseTypeIndices[Index];
				if (BaseIndex >= Module.Types.size())
				{
					return false;
				}
				const std::size_t BaseCount = Module.Types[BaseIndex].Fields.size();
				if (FieldIndex < Offset + BaseCount)
				{
					for (const FBytecodeTypeDesc::FUsingMember& Using : Type.UsingMembers)
					{
						if (Using.Access == EMemberAccess::Public && Using.MemberName == Type.Fields[FieldIndex].Name && UsingNamesDirectBase(Module, Type, Using, Index) && Type.Fields[FieldIndex].Access != EMemberAccess::Private)
						{
							return true;
						}
					}
					return Index < Type.BaseInheritanceAccess.size() && Type.BaseInheritanceAccess[Index] == EMemberAccess::Public && IsPublicReflectedField(Module, BaseIndex, static_cast<std::uint16_t>(FieldIndex - Offset));
				}
				Offset += BaseCount;
			}
			return false;
		}

		bool FindDeclaringFieldType(
		    const FBytecodeModule& Module,
		    const std::uint16_t TypeIndex,
		    const std::uint16_t FieldIndex,
		    std::uint16_t& OutDeclaringType)
		{
			if (TypeIndex >= Module.Types.size())
			{
				return false;
			}
			const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
			if (FieldIndex >= Type.Fields.size())
			{
				return false;
			}
			if (FieldIndex >= Type.OwnFieldStartIndex)
			{
				OutDeclaringType = TypeIndex;
				return true;
			}
			std::size_t Offset = 0;
			for (const std::uint16_t Base : Type.BaseTypeIndices)
			{
				if (Base >= Module.Types.size())
				{
					return false;
				}
				const std::size_t Count = Module.Types[Base].Fields.size();
				if (FieldIndex >= Offset && FieldIndex - Offset < Count)
				{
					return FindDeclaringFieldType(Module, Base,
					    static_cast<std::uint16_t>(FieldIndex - Offset),
					    OutDeclaringType);
				}
				Offset += Count;
			}
			return false;
		}

		bool MapPublicReflectedFieldIndex(
		    const FBytecodeModule& Module,
		    const std::uint16_t ReceiverType,
		    const std::uint16_t OwnerType,
		    const std::uint16_t OwnerField,
		    std::uint16_t& OutField)
		{
			if (ReceiverType == OwnerType)
			{
				OutField = OwnerField;
				return ReceiverType < Module.Types.size() && OwnerField < Module.Types[ReceiverType].Fields.size();
			}
			if (ReceiverType >= Module.Types.size())
			{
				return false;
			}
			const FBytecodeTypeDesc& Type = Module.Types[ReceiverType];
			std::size_t Offset = 0;
			bool bFound = false;
			for (std::size_t Index = 0; Index < Type.BaseTypeIndices.size(); ++Index)
			{
				const std::uint16_t Base = Type.BaseTypeIndices[Index];
				if (Base >= Module.Types.size())
				{
					return false;
				}
				std::uint16_t BaseField = 0;
				if (Index < Type.BaseInheritanceAccess.size() && Type.BaseInheritanceAccess[Index] == EMemberAccess::Public && MapPublicReflectedFieldIndex(Module, Base, OwnerType, OwnerField, BaseField))
				{
					const std::size_t Mapped = Offset + BaseField;
					if (bFound || Mapped >= Type.Fields.size())
					{
						return false;
					}
					OutField = static_cast<std::uint16_t>(Mapped);
					bFound = true;
				}
				Offset += Module.Types[Base].Fields.size();
			}
			return bFound;
		}

		bool TryNarrowSignedInteger(
		    const FValue& Value,
		    const std::int64_t Minimum,
		    const std::int64_t Maximum,
		    std::int64_t& OutValue)
		{
			if (const auto* BoolValue = std::get_if<bool>(&Value))
			{
				OutValue = *BoolValue ? 1 : 0;
				return true;
			}
			if (!Runtime::IsNumericValue(Value))
			{
				return false;
			}
			const long double Numeric = static_cast<long double>(Runtime::ToDouble(Value));
			if (!std::isfinite(Numeric))
			{
				return false;
			}
			const long double Truncated = std::trunc(Numeric);
			if (Truncated < static_cast<long double>(Minimum) || Truncated > static_cast<long double>(Maximum))
			{
				return false;
			}
			OutValue = static_cast<std::int64_t>(Truncated);
			return true;
		}

		bool TryNarrowUnsignedInteger(
		    const FValue& Value,
		    const std::uint64_t Maximum,
		    std::uint64_t& OutValue)
		{
			if (const auto* BoolValue = std::get_if<bool>(&Value))
			{
				OutValue = *BoolValue ? 1u : 0u;
				return true;
			}
			if (!Runtime::IsNumericValue(Value))
			{
				return false;
			}
			const long double Numeric = static_cast<long double>(Runtime::ToDouble(Value));
			if (!std::isfinite(Numeric) || Numeric < 0.0L)
			{
				return false;
			}
			const long double Truncated = std::trunc(Numeric);
			if (Truncated > static_cast<long double>(Maximum))
			{
				return false;
			}
			OutValue = static_cast<std::uint64_t>(Truncated);
			return true;
		}

		std::int64_t ResolveJoinTimeoutMs()
		{
			const auto Env = Environment::GetUtf8("VERO_JOIN_TIMEOUT_MS");
			if (Env && !Env->empty())
			{
				std::int64_t Parsed = 0;
				const std::string_view Text(*Env);
				const auto Result = std::from_chars(
				    Text.data(), Text.data() + Text.size(), Parsed, 10);
				if (Result.ec == std::errc{} && Result.ptr == Text.data() + Text.size() && Parsed >= 0)
				{
					return Parsed;
				}
			}
			return 30000;
		}

		std::chrono::steady_clock::time_point MakeSaturatingWaitDeadline(
		    const std::int64_t TimeoutMs)
		{
			const auto Now = std::chrono::steady_clock::now();
			if (TimeoutMs <= 0)
			{
				return Now;
			}

			const auto MaximumRemaining =
			    std::chrono::steady_clock::time_point::max() - Now;
			const long double MaximumMilliseconds =
			    std::chrono::duration<long double, std::milli>(MaximumRemaining).count();
			if (MaximumMilliseconds <= 0.0L || static_cast<long double>(TimeoutMs) >= MaximumMilliseconds)
			{
				return std::chrono::steady_clock::time_point::max();
			}

			return Now + std::chrono::milliseconds(TimeoutMs);
		}

		constexpr std::int64_t JoinTimeoutGraceMs = 1000;

	}

	bool VeroScriptVm::CallBuiltin(const EBuiltinId Builtin, const std::uint8_t ArgumentCount)
	{
		// These pure builtins have no capability gate; the caller still checks every
		// execution step. They can replace their live operand directly. Failed
		// probes retain the ordinary argument consumption, diagnostics and cleanup.
		const std::size_t StackBase = Frames.empty() ? 0u : Frames.back().StackBase;
		if (!bHasError && ArgumentCount == 1u && OperandStack.size() > StackBase)
		{
			FValue& Value = OperandStack.back();
			if (Builtin == EBuiltinId::ToInt)
			{
				if (std::holds_alternative<std::int32_t>(Value)) return true;
				if (const auto* Wide = std::get_if<std::int64_t>(&Value);
				    Wide && *Wide >= std::numeric_limits<std::int32_t>::min() && *Wide <= std::numeric_limits<std::int32_t>::max())
				{
					Value = static_cast<std::int32_t>(*Wide);
					return true;
				}
			}
			else if (Builtin == EBuiltinId::ToInt64)
			{
				if (std::holds_alternative<std::int64_t>(Value)) return true;
				if (const auto* Narrow = std::get_if<std::int32_t>(&Value))
				{
					Value = static_cast<std::int64_t>(*Narrow);
					return true;
				}
			}
			else if (Builtin == EBuiltinId::Len || Builtin == EBuiltinId::ArrayPrepareSort)
			{
				std::int64_t Length = 0;
				if (Runtime::TryGetStringLength(Value, Length))
				{
					Value = Length;
					return true;
				}
				if (const auto* Array = std::get_if<FDynamicArrayHandle>(&Value);
				    Array && *Array && (*Array)->HasScalarElements() && (*Array)->Elements.empty())
				{
					const std::size_t Size = (*Array)->ScalarSize();
					if (Size <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
					{
						// Sort prepares the same length slot; zero skips the script heap.
						const bool Sorted = Builtin == EBuiltinId::ArrayPrepareSort && Size > 1 && TrySortIntegerArray(**Array);
						Value = Sorted ? std::int64_t{0} : static_cast<std::int64_t>(Size);
						return true;
					}
				}
			}
			else if (Builtin == EBuiltinId::ToString)
			{
				if (std::holds_alternative<std::string>(Value)) return true;
				if (Runtime::IsIntegerValue(Value) || std::holds_alternative<bool>(Value))
				{
					Value = Runtime::ValueToString(Value);
					return true;
				}
			}
		}
		// Always consume the operands the instruction claimed were pushed, even when
		// validation fails, so a bad CallBuiltin cannot leave the operand stack unbalanced.
		// Builtin arity is normally 1-3. Keep those arguments inline so ordinary VM
		// conversion/math/IO calls do not allocate a vector on every invocation.
		VmDetail::FArgumentBuffer Arguments(ArgumentCount);
		const auto Dispatch = [&]() -> bool
		{
			for (int Index = static_cast<int>(ArgumentCount) - 1; Index >= 0; --Index)
			{
				Arguments[static_cast<std::size_t>(Index)] = Pop();
				if (bHasError)
				{
					return false;
				}
			}

			const Builtins::FDescriptor* const BuiltinDescriptor = Builtins::LookupById(Builtin);
			if (!BuiltinDescriptor)
			{
				ReportError("unknown builtin");
				return false;
			}
			if (!BuiltinDescriptor->AcceptsArgCount(ArgumentCount))
			{
				if (BuiltinDescriptor->ArgCount != BuiltinDescriptor->MaxArgCount)
				{
					ReportErrorId(
					    "diag.name_expects_between_n_and_m_arguments",
					    {},
					    {std::string(BuiltinDescriptor->Name),
					        std::to_string(BuiltinDescriptor->ArgCount),
					        std::to_string(BuiltinDescriptor->MaxArgCount)});
				}
				else
				{
					ReportError(
					    std::string(BuiltinDescriptor->Name) + " expects exactly " + std::to_string(BuiltinDescriptor->ArgCount) + " argument(s)");
				}
				return false;
			}
			if (ActiveModule && (Builtin == EBuiltinId::Spawn || Builtin == EBuiltinId::SpawnResult || Builtin == EBuiltinId::SpawnCallable || Builtin == EBuiltinId::SpawnCallableResult))
			{
				for (const auto& Type : ActiveModule->GlobalTypeNames)
				{
					if (Variant::ContainsType(*ActiveModule, Type))
					{
						ReportErrorId("diag.variant_worker_transfer_unsupported");
						return false;
					}
				}
			}
			if (Builtin == EBuiltinId::VariantEmpty)
			{
				Push(Variant::Empty());
				return true;
			}
			if (Builtin == EBuiltinId::VariantHasValue || Builtin == EBuiltinId::VariantTypeName)
			{
				if (!ValidateVariantValue(Arguments[0])) return false;
				if (Builtin == EBuiltinId::VariantHasValue)
				{
					Push(Variant::HasValue(Arguments[0]));
				}
				else
				{
					Push(std::string(Variant::TypeName(Arguments[0])));
				}
				return true;
			}
			std::string_view RequiredCapability;
			if (!Capabilities.bAllowNondeterminism &&
			    (Builtin == EBuiltinId::Now || Builtin == EBuiltinId::SteadyNow ||
			        Builtin == EBuiltinId::Cwd || Builtin == EBuiltinId::Sleep ||
			        Builtin == EBuiltinId::MemoryObjectId || Builtin == EBuiltinId::Exit ||
			        (Builtin >= EBuiltinId::ConsoleWrite && Builtin <= EBuiltinId::ConsoleHeight)))
			{
				ReportError("non-deterministic builtin is disabled during compilation");
				return false;
			}
			if (Builtin >= EBuiltinId::SourceMetadataString && Builtin <= EBuiltinId::SourceMetadataDefinition)
			{
				return CallSourceMetadataBuiltin(Builtin, ArgumentCount ? &Arguments[0] : nullptr, ArgumentCount);
			}

			if ((Builtin >= EBuiltinId::MemoryAvailable && Builtin <= EBuiltinId::MemoryWrite))
			{
				return CallMemoryBuiltin(Builtin, ArgumentCount ? &Arguments[0] : nullptr, ArgumentCount);
			}
			if (!IsBuiltinAllowed(Builtin, RequiredCapability))
			{
				ReportError(
				    "builtin '" + std::string(BuiltinDescriptor->Name) + "' is denied by the " + std::string(RequiredCapability) + " capability");
				return false;
			}

			if (Builtin == EBuiltinId::MethodBindingResolve || Builtin == EBuiltinId::MethodBindingValidate ||
			    Builtin == EBuiltinId::MethodBindingInvoke)
			{
				return CallMethodBindingBuiltin(Builtin, ArgumentCount ? &Arguments[0] : nullptr, ArgumentCount);
			}

			if (Builtin >= EBuiltinId::ScalarParse && Builtin <= EBuiltinId::NumericTypeValidate)
			{
				return CallStandardLibraryBuiltin(Builtin, ArgumentCount ? &Arguments[0] : nullptr, ArgumentCount);
			}
			if (Builtin >= EBuiltinId::MakeMutex && Builtin <= EBuiltinId::SynchronizedFetchAdd)
			{
				return CallSynchronizationBuiltin(Builtin, ArgumentCount ? &Arguments[0] : nullptr, ArgumentCount);
			}
			if (Builtin >= EBuiltinId::TypedTaskBind && Builtin <= EBuiltinId::TaskArrayCancelAll)
			{
				return CallTypedTaskBuiltin(Builtin, ArgumentCount ? &Arguments[0] : nullptr, ArgumentCount);
			}
			if (Builtin >= EBuiltinId::ArrayReverse && Builtin <= EBuiltinId::RandomIndex)
			{
				return CallArrayAlgorithmBuiltin(Builtin, ArgumentCount ? &Arguments[0] : nullptr, ArgumentCount);
			}
			if (Builtin == EBuiltinId::ChannelSend || Builtin == EBuiltinId::ChannelTrySend || Builtin == EBuiltinId::ChannelSendFor || Builtin == EBuiltinId::ChannelRecv || Builtin == EBuiltinId::ChannelTryRecv || Builtin == EBuiltinId::ChannelRecvFor || Builtin == EBuiltinId::ChannelSetCapacity)
			{
				if (const auto* Channel = std::get_if<FChannelHandle>(&Arguments[0]); Channel && *Channel && (*Channel)->GetRole() != FChannel::ERole::Ordinary)
				{
					ReportError("generic channel operations cannot modify synchronization capabilities");
					return false;
				}
			}

			if (Arguments.size() == 1 && Numerics::IsValue(Arguments.front()))
			{
				FNumericOperationContext Context = GetNumericOperationContext();
				FNumericError NumericError;
				std::string_view Target;
				switch (Builtin)
				{
				case EBuiltinId::ToInt:
					{
						Target = "int";
						break;
					}
				case EBuiltinId::ToUnsigned:
					{
						Target = "unsigned int";
						break;
					}
				case EBuiltinId::ToInt64:
					{
						Target = "int64";
						break;
					}
				case EBuiltinId::ToUInt64:
					{
						Target = "uint64";
						break;
					}
				case EBuiltinId::ToInt8:
					{
						Target = "int8";
						break;
					}
				case EBuiltinId::ToUInt8:
					{
						Target = "uint8";
						break;
					}
				case EBuiltinId::ToInt16:
					{
						Target = "int16";
						break;
					}
				case EBuiltinId::ToUInt16:
					{
						Target = "uint16";
						break;
					}
				case EBuiltinId::ToFloat:
					{
						Target = "float";
						break;
					}
				case EBuiltinId::ToDouble:
					{
						Target = "double";
						break;
					}
				default:
					{
						break;
					}
				}
				if (!Target.empty())
				{
					FValue Converted;
					if (!Numerics::ConvertValue(Arguments.front(), Target, Converted, Context, NumericError))
					{
						ReportErrorId(NumericError.MessageId, {},
						    {NumericError.Reason ? NumericError.Reason : "",
						        std::to_string(NumericError.Offset)});
						return false;
					}
					Push(std::move(Converted));
					return true;
				}
				if (Builtin == EBuiltinId::ToString || Builtin == EBuiltinId::Print ||
				    Builtin == EBuiltinId::Eprint || Builtin == EBuiltinId::ConsoleWrite ||
				    Builtin == EBuiltinId::ConsoleError)
				{
					std::string Text;
					if (!Numerics::FormatValue(Arguments.front(), Text, Context, NumericError))
					{
						ReportErrorId(NumericError.MessageId, {},
						    {NumericError.Reason ? NumericError.Reason : "",
						        std::to_string(NumericError.Offset)});
						return false;
					}
					Arguments[0] = std::move(Text);
				}
			}
			switch (Builtin)
			{
			case EBuiltinId::Print:
				{
					if (Arguments.size() != 1)
					{
						ReportError("print expects exactly 1 argument");
						return false;
					}
					if (!Runtime::WriteValueLine(OutputStream, Arguments.front()))
					{
						ReportError("print failed to write output");
						return false;
					}
					return true;
				}
			case EBuiltinId::Eprint:
				{
					if (Arguments.size() != 1)
					{
						ReportError("eprint expects exactly 1 argument");
						return false;
					}
					if (!Runtime::WriteValueLine(ErrorStream, Arguments.front()))
					{
						ReportError("eprint failed to write output");
						return false;
					}
					return true;
				}
			case EBuiltinId::ConsoleWrite:
				{
					if (Arguments.size() != 1)
					{
						ReportError("std::console::write expects exactly 1 argument");
						return false;
					}
					auto* ConsoleHost = dynamic_cast<Console::IHost*>(OutputStream.rdbuf());
					const bool bWritten = ConsoleHost && ConsoleHost->HasConsoleTextOutput()
					    ? (OutputStream.flush(), static_cast<bool>(OutputStream) && ConsoleHost->WriteConsoleText(Runtime::ValueToString(Arguments.front())))
					    : Runtime::WriteValue(OutputStream, Arguments.front());
					if (!bWritten)
					{
						ReportError("std::console::write failed to write output");
						return false;
					}
					return true;
				}
			case EBuiltinId::ConsoleError:
				{
					if (Arguments.size() != 1)
					{
						ReportError("std::console::error expects exactly 1 argument");
						return false;
					}
					if (!Runtime::WriteValue(ErrorStream, Arguments.front()))
					{
						ReportError("std::console::error failed to write output");
						return false;
					}
					return true;
				}
			case EBuiltinId::ConsoleFlush:
				{
					if (Arguments.size() != 0)
					{
						ReportError("std::console::flush expects no arguments");
						return false;
					}
					OutputStream.flush();
					auto* ConsoleHost = dynamic_cast<Console::IHost*>(OutputStream.rdbuf());
					if (!OutputStream || (ConsoleHost && !ConsoleHost->FlushConsoleText()))
					{
						ReportError("std::console::flush failed to flush output");
						return false;
					}
					return true;
				}
			case EBuiltinId::ConsoleClear:
				{
					if (Arguments.size() != 0)
					{
						ReportError("std::console::clear expects no arguments");
						return false;
					}
					Push(Runtime::ClearInteractiveOutput(OutputStream));
					return true;
				}
			case EBuiltinId::ConsoleIsInteractive:
				{
					if (Arguments.size() != 0)
					{
						ReportError("std::console::is_interactive expects no arguments");
						return false;
					}
					Push(
					    Runtime::IsInteractiveInput(InputStream) && Runtime::IsInteractiveOutput(OutputStream));
					return true;
				}
			case EBuiltinId::ConsoleSupportsColor:
			case EBuiltinId::ConsoleSupportsCursor:
			case EBuiltinId::ConsoleSupportsClear:
			case EBuiltinId::ConsoleIsOutputTerminal:
			case EBuiltinId::ConsoleSetColor:
			case EBuiltinId::ConsoleResetStyle:
			case EBuiltinId::ConsoleClearLine:
			case EBuiltinId::ConsoleMoveCursor:
			case EBuiltinId::ConsoleSetCursorVisible:
			case EBuiltinId::ConsoleWidth:
			case EBuiltinId::ConsoleHeight:
				{
					const auto* Descriptor = Builtins::LookupById(Builtin);
					if (!Descriptor || !Descriptor->AcceptsArgCount(static_cast<std::uint8_t>(Arguments.size())))
					{
						ReportError("std::console: invalid argument count");
						return false;
					}
					try
					{
						using Console::EOperation;
						if (Builtin == EBuiltinId::ConsoleSetColor || Builtin == EBuiltinId::ConsoleClearLine)
						{
							for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
							{
								if (!std::holds_alternative<std::string>(Arguments[Index]))
								{
									ReportError("std::console: expected string argument");
									return false;
								}
							}

							const int First = Builtin == EBuiltinId::ConsoleSetColor
							    ? Console::ColorIndex(std::get<std::string>(Arguments[0]))
							    : Console::LineMode(std::get<std::string>(Arguments[0]));
							const int Second = Builtin == EBuiltinId::ConsoleSetColor
							    ? Console::ColorIndex(std::get<std::string>(Arguments[1]))
							    : 0;
							Push(Console::Execute(OutputStream, Builtin == EBuiltinId::ConsoleSetColor ? EOperation::SetColor : EOperation::ClearLine, First, Second));
							return true;
						}
						if (Builtin == EBuiltinId::ConsoleSetCursorVisible)
						{
							if (!std::holds_alternative<bool>(Arguments[0]))
							{
								ReportError("std::console::set_cursor_visible expects bool");
								return false;
							}
							Push(Console::Execute(OutputStream, EOperation::SetCursorVisible,
							    std::get<bool>(Arguments[0]) ? 1 : 0));
							return true;
						}
						if (Builtin == EBuiltinId::ConsoleMoveCursor)
						{
							std::int64_t Column = 0, Row = 0;
							if (!Runtime::TryGetInteger64(Arguments[0], Column) ||
							    !Runtime::TryGetInteger64(Arguments[1], Row) || Column < 0 || Row < 0 ||
							    Column > 32766 || Row > 32766)
							{
								ReportError("std::console::move_cursor expects integer coordinates in 0..32766");
								return false;
							}
							Push(Console::Execute(OutputStream, EOperation::MoveCursor,
							    static_cast<std::int32_t>(Column), static_cast<std::int32_t>(Row)));
							return true;
						}
						if (Builtin == EBuiltinId::ConsoleResetStyle)
						{
							Push(Console::Execute(OutputStream, EOperation::ResetStyle));
							return true;
						}
						const Console::FInfo Info = Console::Query(OutputStream);
						switch (Builtin)
						{
						case EBuiltinId::ConsoleWidth:
							{
								Push(Info.Width);
								break;
							}
						case EBuiltinId::ConsoleHeight:
							{
								Push(Info.Height);
								break;
							}
						case EBuiltinId::ConsoleSupportsColor:
							{
								Push(Info.bColor);
								break;
							}
						case EBuiltinId::ConsoleSupportsCursor:
							{
								Push(Info.bCursor);
								break;
							}
						case EBuiltinId::ConsoleSupportsClear:
							{
								Push(Info.bClear);
								break;
							}
						default:
							{
								Push(Info.bOutputTerminal);
								break;
							}
						}
						return true;
					}
					catch (const std::exception& Error)
					{
						ReportError(Error.what());
						return false;
					}
				}
			case EBuiltinId::ToString:
				{
					if (Arguments.size() != 1)
					{
						ReportError("to_string expects exactly 1 argument");
						return false;
					}
					Push(Runtime::ValueToString(Arguments.front()));
					return true;
				}
			case EBuiltinId::ArrayPrepareSort:
			case EBuiltinId::Len:
				{
					if (Arguments.size() != 1)
					{
						ReportError("len expects exactly 1 argument");
						return false;
					}
					const auto PushLength = [this](const std::uint64_t Length) -> bool
					{
						if (Length > static_cast<std::uint64_t>(
						                 std::numeric_limits<std::int64_t>::max()))
						{
							ReportError("len result exceeds int64 range");
							return false;
						}
						Push(static_cast<std::int64_t>(Length));
						return true;
					};
					if (const auto* StringValue = std::get_if<std::string>(&Arguments.front()))
					{
						return PushLength(StringValue->size());
					}
					if (const auto* WStringValue = std::get_if<std::u16string>(&Arguments.front()))
					{
						return PushLength(WStringValue->size());
					}
					if (const auto* NativeArray = std::get_if<FNativeArray>(&Arguments.front()))
					{
						return PushLength(NativeArray->Length());
					}
					if (const auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&Arguments.front()))
					{
						if (!*DynamicArray)
						{
							ReportError("len expects non-null dynamic array");
							return false;
						}
						return PushLength((*DynamicArray)->HasScalarElements()
						        ? (*DynamicArray)->ScalarSize()
						        : (*DynamicArray)->Elements.size());
					}
					if (const auto* MapHandle = std::get_if<FMapHandle>(&Arguments.front()))
					{
						if (!*MapHandle)
						{
							ReportError("len expects non-null map");
							return false;
						}
						if (Runtime::IsMultiSetTypeName((*MapHandle)->TypeName))
						{
							const auto TotalOccurrences = [&](const auto& Entries)
							{
								constexpr std::uint64_t MaxLength = static_cast<std::uint64_t>(
								    (std::numeric_limits<std::int64_t>::max)());
								std::uint64_t Total = 0;
								for (const auto& Entry : Entries)
								{
									if (const auto* Values =
									        std::get_if<FDynamicArrayHandle>(&Entry.second);
									    Values && *Values)
									{
										const std::size_t Count = (*Values)->HasScalarElements()
										    ? (*Values)->ScalarSize()
										    : (*Values)->Elements.size();
										// Accumulate in uint64 even when native size_t is only 32 bits.
										if (Count > MaxLength - Total)
										{
											return MaxLength + 1;
										}
										Total += Count;
									}
								}
								return Total;
							};
							const std::uint64_t Total = (*MapHandle)->KeyTypeName == "string"
							    ? TotalOccurrences((*MapHandle)->StrEntries)
							    : (*MapHandle)->KeyTypeName == "wstring"
							    ? TotalOccurrences((*MapHandle)->WStrEntries)
							    : (*MapHandle)->KeyTypeName == "int64"
							    ? TotalOccurrences((*MapHandle)->Int64Entries)
							    : (*MapHandle)->KeyTypeName == "uint64"
							    ? TotalOccurrences((*MapHandle)->UInt64Entries)
							    : (*MapHandle)->KeyTypeName == "unsigned int"
							    ? TotalOccurrences((*MapHandle)->UIntEntries)
							    : (!(*MapHandle)->EnumEntries.empty() || VmDetail::IsScopedEnumType(ActiveModule, (*MapHandle)->KeyTypeName))
							    ? TotalOccurrences((*MapHandle)->EnumEntries)
							    : VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName)
							    ? TotalOccurrences((*MapHandle)->IntEntries)
							    : TotalOccurrences((*MapHandle)->HostEntries);
							return PushLength(Total);
						}
						std::size_t Size = 0;
						if ((*MapHandle)->KeyTypeName == "string")
						{
							Size = (*MapHandle)->StrEntries.size();
						}
						else if ((*MapHandle)->KeyTypeName == "wstring")
						{
							Size = (*MapHandle)->WStrEntries.size();
						}
						else if ((*MapHandle)->KeyTypeName == "int64")
						{
							Size = (*MapHandle)->Int64Entries.size();
						}
						else if ((*MapHandle)->KeyTypeName == "uint64")
						{
							Size = (*MapHandle)->UInt64Entries.size();
						}
						else if ((*MapHandle)->KeyTypeName == "unsigned int")
						{
							Size = (*MapHandle)->UIntEntries.size();
						}
						else if (!(*MapHandle)->EnumEntries.empty() || VmDetail::IsScopedEnumType(ActiveModule, (*MapHandle)->KeyTypeName))
						{
							Size = (*MapHandle)->EnumEntries.size();
						}
						else if (!VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
						{
							Size = (*MapHandle)->HostEntries.size();
						}
						else
						{
							Size = (*MapHandle)->IntEntries.size();
						}
						return PushLength(Size);
					}
					if (const auto* SetHandle = std::get_if<FSetHandle>(&Arguments.front()))
					{
						if (!*SetHandle)
						{
							ReportError("len expects non-null set");
							return false;
						}
						if ((*SetHandle)->ScalarElements.index() != 0)
						{
							const std::size_t Size = std::visit(
							    [](const auto& Elements) -> std::size_t
							{
								using TElements = std::decay_t<decltype(Elements)>;
								if constexpr (std::is_same_v<TElements, std::monostate>)
								{
									return 0;
								}
								else
								{
									return Elements.size();
								}
							},
							    (*SetHandle)->ScalarElements);
							return PushLength(Size);
						}
						const std::size_t Size =
						    (!(*SetHandle)->EnumElements.empty() || VmDetail::IsScopedEnumType(ActiveModule, (*SetHandle)->ElementTypeName))
						    ? (*SetHandle)->EnumElements.size()
						    : ((*SetHandle)->ElementTypeName == "string"
						              ? (*SetHandle)->StrElements.size()
						              : ((*SetHandle)->ElementTypeName == "wstring"
						                        ? (*SetHandle)->WStrElements.size()
						                        : ((*SetHandle)->ElementTypeName == "int64"
						                                  ? (*SetHandle)->Int64Elements.size()
						                                  : ((*SetHandle)->ElementTypeName == "uint64"
						                                            ? (*SetHandle)->UInt64Elements.size()
						                                            : ((*SetHandle)->ElementTypeName == "unsigned int"
						                                                      ? (*SetHandle)->UIntElements.size()
						                                                      : (VmDetail::IsSigned32KeyStorageType((*SetHandle)->ElementTypeName)
						                                                                ? (*SetHandle)->IntElements.size()
						                                                                : (*SetHandle)->HostElements.size()))))));
						return PushLength(Size);
					}
					if (const auto* QueueHandle = std::get_if<FQueueHandle>(&Arguments.front()))
					{
						if (!*QueueHandle)
						{
							ReportError("len expects non-null queue");
							return false;
						}
						return PushLength((*QueueHandle)->Elements.size());
					}
					if (const auto* StackHandle = std::get_if<FStackHandle>(&Arguments.front()))
					{
						if (!*StackHandle)
						{
							ReportError("len expects non-null stack");
							return false;
						}
						return PushLength((*StackHandle)->Elements.size());
					}
					if (const auto* OrderedMapHandle = std::get_if<FOrderedMapHandle>(&Arguments.front()))
					{
						if (!*OrderedMapHandle)
						{
							ReportError("len expects non-null ordered_map");
							return false;
						}
						if (Runtime::IsOrderedMultiSetTypeName((*OrderedMapHandle)->TypeName))
						{
							const auto TotalOccurrences = [&](const auto& Entries)
							{
								constexpr std::uint64_t MaxLength = static_cast<std::uint64_t>(
								    (std::numeric_limits<std::int64_t>::max)());
								std::uint64_t Total = 0;
								for (const auto& Entry : Entries)
								{
									if (const auto* Values =
									        std::get_if<FDynamicArrayHandle>(&Entry.second);
									    Values && *Values)
									{
										const std::size_t Count = (*Values)->HasScalarElements()
										    ? (*Values)->ScalarSize()
										    : (*Values)->Elements.size();
										if (Count > MaxLength - Total)
										{
											return MaxLength + 1;
										}
										Total += Count;
									}
								}
								return Total;
							};
							const std::uint64_t Total =
							    (*OrderedMapHandle)->KeyTypeName == "string"
							    ? TotalOccurrences((*OrderedMapHandle)->StrEntries)
							    : (*OrderedMapHandle)->KeyTypeName == "wstring"
							    ? TotalOccurrences((*OrderedMapHandle)->WStrEntries)
							    : (*OrderedMapHandle)->KeyTypeName == "int64"
							    ? TotalOccurrences((*OrderedMapHandle)->Int64Entries)
							    : (*OrderedMapHandle)->KeyTypeName == "uint64"
							    ? TotalOccurrences((*OrderedMapHandle)->UInt64Entries)
							    : (*OrderedMapHandle)->KeyTypeName == "unsigned int"
							    ? TotalOccurrences((*OrderedMapHandle)->UIntEntries)
							    : (!(*OrderedMapHandle)->EnumEntries.empty() || VmDetail::IsScopedEnumType(ActiveModule, (*OrderedMapHandle)->KeyTypeName))
							    ? TotalOccurrences((*OrderedMapHandle)->EnumEntries)
							    : VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName)
							    ? TotalOccurrences((*OrderedMapHandle)->IntEntries)
							    : TotalOccurrences(
							          (*OrderedMapHandle)->HostEntries);
							return PushLength(Total);
						}
						const std::size_t Size =
						    (!(*OrderedMapHandle)->EnumEntries.empty() || VmDetail::IsScopedEnumType(ActiveModule, (*OrderedMapHandle)->KeyTypeName))
						    ? (*OrderedMapHandle)->EnumEntries.size()
						    : ((*OrderedMapHandle)->KeyTypeName == "string"
						              ? (*OrderedMapHandle)->StrEntries.size()
						              : ((*OrderedMapHandle)->KeyTypeName == "wstring"
						                        ? (*OrderedMapHandle)->WStrEntries.size()
						                        : ((*OrderedMapHandle)->KeyTypeName == "int64"
						                                  ? (*OrderedMapHandle)->Int64Entries.size()
						                                  : ((*OrderedMapHandle)->KeyTypeName == "uint64"
						                                            ? (*OrderedMapHandle)->UInt64Entries.size()
						                                            : ((*OrderedMapHandle)->KeyTypeName == "unsigned int"
						                                                      ? (*OrderedMapHandle)->UIntEntries.size()
						                                                      : (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName)
						                                                                ? (*OrderedMapHandle)->IntEntries.size()
						                                                                : (*OrderedMapHandle)->HostEntries.size()))))));
						return PushLength(Size);
					}
					if (const auto* DequeHandle = std::get_if<FDequeHandle>(&Arguments.front()))
					{
						if (!*DequeHandle)
						{
							ReportError("len expects non-null deque");
							return false;
						}
						return PushLength((*DequeHandle)->Elements.size());
					}
					ReportError("len expects string, wstring, array, or container");
					return false;
				}
			case EBuiltinId::ToInt:
				{
					if (Arguments.size() != 1)
					{
						ReportError("to_int expects exactly 1 argument");
						return false;
					}
					{
						FValue Converted = Arguments.front();
						std::string ConversionError;
						if (!Runtime::CoerceValueToPrimitiveTypeName(
						        "int", Converted, ConversionError))
						{
							ReportError(Runtime::IsNumericValue(Arguments.front()) || std::holds_alternative<bool>(Arguments.front())
							        ? "to_int overflow"
							        : "to_int expects numeric or bool argument");
							return false;
						}
						Push(std::move(Converted));
						return true;
					}
				}
			case EBuiltinId::ToDouble:
				{
					if (Arguments.size() != 1)
					{
						ReportError("to_double expects exactly 1 argument");
						return false;
					}
					if (Runtime::IsNumericValue(Arguments.front()))
					{
						Push(Runtime::ToDouble(Arguments.front()));
						return true;
					}
					if (const auto* BoolValue = std::get_if<bool>(&Arguments.front()))
					{
						Push(*BoolValue ? 1.0 : 0.0);
						return true;
					}
					ReportError("to_double expects numeric or bool argument");
					return false;
				}
			case EBuiltinId::ToFloat:
				{
					if (Arguments.size() != 1)
					{
						ReportError("to_float expects exactly 1 argument");
						return false;
					}
					if (const auto* BoolValue = std::get_if<bool>(&Arguments.front()))
					{
						Push(*BoolValue ? 1.0f : 0.0f);
						return true;
					}
					if (Runtime::IsNumericValue(Arguments.front()))
					{
						Push(static_cast<float>(Runtime::ToDouble(Arguments.front())));
						return true;
					}
					ReportError("to_float expects numeric or bool argument");
					return false;
				}
			case EBuiltinId::ToUnsigned:
				{
					if (Arguments.size() != 1)
					{
						ReportError("to_unsigned expects exactly 1 argument");
						return false;
					}
					if (const auto* UIntValue = std::get_if<std::uint32_t>(&Arguments.front()))
					{
						Push(*UIntValue);
						return true;
					}
					if (const auto* IntValue = std::get_if<std::int32_t>(&Arguments.front()))
					{
						if (*IntValue < 0)
						{
							ReportError("to_unsigned overflow");
							return false;
						}
						Push(static_cast<std::uint32_t>(*IntValue));
						return true;
					}
					if (const auto* UInt64Value = std::get_if<std::uint64_t>(&Arguments.front()))
					{
						if (*UInt64Value > std::numeric_limits<std::uint32_t>::max())
						{
							ReportError("to_unsigned overflow");
							return false;
						}
						Push(static_cast<std::uint32_t>(*UInt64Value));
						return true;
					}
					if (const auto* Int64Value = std::get_if<std::int64_t>(&Arguments.front()))
					{
						if (*Int64Value < 0 || static_cast<std::uint64_t>(*Int64Value) > std::numeric_limits<std::uint32_t>::max())
						{
							ReportError("to_unsigned overflow");
							return false;
						}
						Push(static_cast<std::uint32_t>(*Int64Value));
						return true;
					}
					if (const auto* DoubleValue = std::get_if<double>(&Arguments.front()))
					{
						if (!std::isfinite(*DoubleValue) || *DoubleValue < 0.0 || *DoubleValue >= 4294967296.0)
						{
							ReportError("to_unsigned overflow");
							return false;
						}
						Push(static_cast<std::uint32_t>(*DoubleValue));
						return true;
					}
					if (const auto* Value = std::get_if<std::uint8_t>(&Arguments.front()))
					{
						Push(static_cast<std::uint32_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::uint16_t>(&Arguments.front()))
					{
						Push(static_cast<std::uint32_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::int8_t>(&Arguments.front()))
					{
						if (*Value < 0)
						{
							ReportError("to_unsigned overflow");
							return false;
						}
						Push(static_cast<std::uint32_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::int16_t>(&Arguments.front()))
					{
						if (*Value < 0)
						{
							ReportError("to_unsigned overflow");
							return false;
						}
						Push(static_cast<std::uint32_t>(*Value));
						return true;
					}
					if (const auto* FloatValue = std::get_if<float>(&Arguments.front()))
					{
						if (!std::isfinite(*FloatValue) || *FloatValue < 0.0f || *FloatValue >= 4294967296.0f)
						{
							ReportError("to_unsigned overflow");
							return false;
						}
						Push(static_cast<std::uint32_t>(*FloatValue));
						return true;
					}
					if (const auto* BoolValue = std::get_if<bool>(&Arguments.front()))
					{
						Push(*BoolValue ? std::uint32_t{1} : std::uint32_t{0});
						return true;
					}
					ReportError("to_unsigned expects numeric or bool argument");
					return false;
				}
			case EBuiltinId::ToInt64:
				{
					if (Arguments.size() != 1)
					{
						ReportError("to_int64 expects exactly 1 argument");
						return false;
					}
					if (const auto* Value = std::get_if<std::int8_t>(&Arguments.front()))
					{
						Push(static_cast<std::int64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::uint8_t>(&Arguments.front()))
					{
						Push(static_cast<std::int64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::int16_t>(&Arguments.front()))
					{
						Push(static_cast<std::int64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::uint16_t>(&Arguments.front()))
					{
						Push(static_cast<std::int64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::int64_t>(&Arguments.front()))
					{
						Push(*Value);
						return true;
					}
					if (const auto* Value = std::get_if<std::int32_t>(&Arguments.front()))
					{
						Push(static_cast<std::int64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::uint32_t>(&Arguments.front()))
					{
						Push(static_cast<std::int64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::uint64_t>(&Arguments.front()))
					{
						if (*Value > static_cast<std::uint64_t>(
						                 std::numeric_limits<std::int64_t>::max()))
						{
							ReportError("to_int64 overflow");
							return false;
						}
						Push(static_cast<std::int64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<double>(&Arguments.front()))
					{
						if (!std::isfinite(*Value) || *Value < static_cast<double>(std::numeric_limits<std::int64_t>::min()) || *Value >= 9223372036854775808.0)
						{
							ReportError("to_int64 overflow");
							return false;
						}
						Push(static_cast<std::int64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<float>(&Arguments.front()))
					{
						const double Wide = static_cast<double>(*Value);
						if (!std::isfinite(*Value) || Wide < static_cast<double>(std::numeric_limits<std::int64_t>::min()) || Wide >= 9223372036854775808.0)
						{
							ReportError("to_int64 overflow");
							return false;
						}
						Push(static_cast<std::int64_t>(Wide));
						return true;
					}
					if (const auto* Value = std::get_if<bool>(&Arguments.front()))
					{
						Push(*Value ? std::int64_t{1} : std::int64_t{0});
						return true;
					}
					ReportError("to_int64 expects numeric or bool argument");
					return false;
				}
			case EBuiltinId::ToUInt64:
				{
					if (Arguments.size() != 1)
					{
						ReportError("to_uint64 expects exactly 1 argument");
						return false;
					}
					if (const auto* Value = std::get_if<std::uint8_t>(&Arguments.front()))
					{
						Push(static_cast<std::uint64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::uint16_t>(&Arguments.front()))
					{
						Push(static_cast<std::uint64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::int8_t>(&Arguments.front()))
					{
						if (*Value < 0)
						{
							ReportError("to_uint64 overflow");
							return false;
						}
						Push(static_cast<std::uint64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::int16_t>(&Arguments.front()))
					{
						if (*Value < 0)
						{
							ReportError("to_uint64 overflow");
							return false;
						}
						Push(static_cast<std::uint64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::uint64_t>(&Arguments.front()))
					{
						Push(*Value);
						return true;
					}
					if (const auto* Value = std::get_if<std::uint32_t>(&Arguments.front()))
					{
						Push(static_cast<std::uint64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::int32_t>(&Arguments.front()))
					{
						if (*Value < 0)
						{
							ReportError("to_uint64 overflow");
							return false;
						}
						Push(static_cast<std::uint64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<std::int64_t>(&Arguments.front()))
					{
						if (*Value < 0)
						{
							ReportError("to_uint64 overflow");
							return false;
						}
						Push(static_cast<std::uint64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<double>(&Arguments.front()))
					{
						if (!std::isfinite(*Value) || *Value < 0.0 || *Value >= 18446744073709551616.0)
						{
							ReportError("to_uint64 overflow");
							return false;
						}
						Push(static_cast<std::uint64_t>(*Value));
						return true;
					}
					if (const auto* Value = std::get_if<float>(&Arguments.front()))
					{
						const double Wide = static_cast<double>(*Value);
						if (!std::isfinite(*Value) || Wide < 0.0 || Wide >= 18446744073709551616.0)
						{
							ReportError("to_uint64 overflow");
							return false;
						}
						Push(static_cast<std::uint64_t>(Wide));
						return true;
					}
					if (const auto* Value = std::get_if<bool>(&Arguments.front()))
					{
						Push(*Value ? std::uint64_t{1} : std::uint64_t{0});
						return true;
					}
					ReportError("to_uint64 expects numeric or bool argument");
					return false;
				}
			case EBuiltinId::ToInt8:
			case EBuiltinId::ToInt16:
				{
					if (Arguments.size() != 1)
					{
						ReportError("integer conversion expects exactly 1 argument");
						return false;
					}
					const bool bInt8 = Builtin == EBuiltinId::ToInt8;
					std::int64_t Converted = 0;
					if (!TryNarrowSignedInteger(
					        Arguments.front(),
					        bInt8 ? std::numeric_limits<std::int8_t>::min()
					              : std::numeric_limits<std::int16_t>::min(),
					        bInt8 ? std::numeric_limits<std::int8_t>::max()
					              : std::numeric_limits<std::int16_t>::max(),
					        Converted))
					{
						ReportError(bInt8 ? "char conversion overflow" : "short conversion overflow");
						return false;
					}
					if (bInt8)
					{
						Push(static_cast<std::int8_t>(Converted));
					}
					else
					{
						Push(static_cast<std::int16_t>(Converted));
					}
					return true;
				}
			case EBuiltinId::ToUInt8:
			case EBuiltinId::ToUInt16:
				{
					if (Arguments.size() != 1)
					{
						ReportError("unsigned integer conversion expects exactly 1 argument");
						return false;
					}
					const bool bUInt8 = Builtin == EBuiltinId::ToUInt8;
					std::uint64_t Converted = 0;
					if (!TryNarrowUnsignedInteger(
					        Arguments.front(),
					        bUInt8 ? std::numeric_limits<std::uint8_t>::max()
					               : std::numeric_limits<std::uint16_t>::max(),
					        Converted))
					{
						ReportError(bUInt8
						        ? "unsigned char conversion overflow"
						        : "unsigned short conversion overflow");
						return false;
					}
					if (bUInt8)
					{
						Push(static_cast<std::uint8_t>(Converted));
					}
					else
					{
						Push(static_cast<std::uint16_t>(Converted));
					}
					return true;
				}
			case EBuiltinId::ToWString:
				{
					if (Arguments.size() != 1)
					{
						ReportError("to_wstring expects exactly 1 argument");
						return false;
					}
					if (const auto* StringValue = std::get_if<std::string>(&Arguments.front()))
					{
						std::u16string Converted;
						std::string ConvertError;
						if (!Runtime::Utf8ToUtf16(*StringValue, Converted, &ConvertError))
						{
							if (ConvertError.empty())
							{
								ReportErrorId("diag.invalid_utf_8_sequence");
							}
							else
							{
								ReportError(std::move(ConvertError));
							}
							return false;
						}
						Push(std::move(Converted));
						return true;
					}
					if (const auto* WStringValue = std::get_if<std::u16string>(&Arguments.front()))
					{
						Push(*WStringValue);
						return true;
					}
					ReportError("to_wstring expects string or wstring argument");
					return false;
				}
			case EBuiltinId::Abs:
				{
					if (Arguments.size() != 1)
					{
						ReportError("abs expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsNumericValue(Arguments.front()))
					{
						ReportError("abs expects numeric argument");
						return false;
					}
					if (const auto* IntValue = std::get_if<std::int32_t>(&Arguments.front()))
					{
						if (*IntValue == std::numeric_limits<std::int32_t>::min())
						{
							ReportErrorId("diag.integer_overflow");
							return false;
						}
						Push(*IntValue < 0 ? -*IntValue : *IntValue);
						return true;
					}
					if (const auto* UIntValue = std::get_if<std::uint32_t>(&Arguments.front()))
					{
						Push(*UIntValue);
						return true;
					}
					if (const auto* Int64Value = std::get_if<std::int64_t>(&Arguments.front()))
					{
						if (*Int64Value == std::numeric_limits<std::int64_t>::min())
						{
							ReportErrorId("diag.integer_overflow");
							return false;
						}
						Push(*Int64Value < 0 ? -*Int64Value : *Int64Value);
						return true;
					}
					if (const auto* UInt64Value = std::get_if<std::uint64_t>(&Arguments.front()))
					{
						Push(*UInt64Value);
						return true;
					}
					if (const auto* Int8Value = std::get_if<std::int8_t>(&Arguments.front()))
					{
						if (*Int8Value == std::numeric_limits<std::int8_t>::min())
						{
							ReportErrorId("diag.integer_overflow");
							return false;
						}
						Push(static_cast<std::int8_t>(*Int8Value < 0 ? -*Int8Value : *Int8Value));
						return true;
					}
					if (const auto* UInt8Value = std::get_if<std::uint8_t>(&Arguments.front()))
					{
						Push(*UInt8Value);
						return true;
					}
					if (const auto* Int16Value = std::get_if<std::int16_t>(&Arguments.front()))
					{
						if (*Int16Value == std::numeric_limits<std::int16_t>::min())
						{
							ReportErrorId("diag.integer_overflow");
							return false;
						}
						Push(static_cast<std::int16_t>(*Int16Value < 0 ? -*Int16Value : *Int16Value));
						return true;
					}
					if (const auto* UInt16Value = std::get_if<std::uint16_t>(&Arguments.front()))
					{
						Push(*UInt16Value);
						return true;
					}
					if (const auto* FloatValue = std::get_if<float>(&Arguments.front()))
					{
						// Match the standard-library abs contract for floating values: clear
						// the sign bit for negative zero as well as finite negative values.
						Push(std::fabs(*FloatValue));
						return true;
					}
					{
						const double Value = std::get<double>(Arguments.front());
						Push(std::fabs(Value));
					}
					return true;
				}
			case EBuiltinId::Min:
			case EBuiltinId::Max:
				{
					if (Arguments.size() != 2)
					{
						ReportErrorId(
						    Builtin == EBuiltinId::Min
						        ? "diag.min_expects_exactly_2_arguments"
						        : "diag.max_expects_exactly_2_arguments");
						return false;
					}
					if (!Runtime::IsNumericValue(Arguments[0]) || !Runtime::IsNumericValue(Arguments[1]))
					{
						ReportErrorId(
						    Builtin == EBuiltinId::Min
						        ? "diag.min_expects_numeric_arguments"
						        : "diag.max_expects_numeric_arguments");
						return false;
					}
					if ((Runtime::IsSignedIntegerValue(Arguments[0]) && Runtime::IsUnsignedIntegerValue(Arguments[1])) || (Runtime::IsUnsignedIntegerValue(Arguments[0]) && Runtime::IsSignedIntegerValue(Arguments[1])))
					{
						ReportError("cannot mix signed and unsigned");
						return false;
					}
					if (Runtime::IsSignedIntegerValue(Arguments[0]) && Runtime::IsSignedIntegerValue(Arguments[1]))
					{
						const std::int64_t Left = Runtime::ToSignedInteger64(Arguments[0]);
						const std::int64_t Right = Runtime::ToSignedInteger64(Arguments[1]);
						const std::int64_t Result = Builtin == EBuiltinId::Min
						    ? (Left < Right ? Left : Right)
						    : (Left > Right ? Left : Right);
						Push(std::holds_alternative<std::int64_t>(Arguments[0]) || std::holds_alternative<std::int64_t>(Arguments[1])
						        ? FValue{Result}
						        : FValue{static_cast<std::int32_t>(Result)});
						return true;
					}
					if (Runtime::IsUnsignedIntegerValue(Arguments[0]) && Runtime::IsUnsignedIntegerValue(Arguments[1]))
					{
						const std::uint64_t Left = Runtime::ToUnsignedInteger64(Arguments[0]);
						const std::uint64_t Right = Runtime::ToUnsignedInteger64(Arguments[1]);
						const std::uint64_t Result = Builtin == EBuiltinId::Min
						    ? (Left < Right ? Left : Right)
						    : (Left > Right ? Left : Right);
						Push(std::holds_alternative<std::uint64_t>(Arguments[0]) || std::holds_alternative<std::uint64_t>(Arguments[1])
						        ? FValue{Result}
						        : FValue{static_cast<std::uint32_t>(Result)});
						return true;
					}
					if (!std::holds_alternative<double>(Arguments[0]) && !std::holds_alternative<double>(Arguments[1]) && (std::holds_alternative<float>(Arguments[0]) || std::holds_alternative<float>(Arguments[1])))
					{
						const float Left = static_cast<float>(Runtime::ToDouble(Arguments[0]));
						const float Right = static_cast<float>(Runtime::ToDouble(Arguments[1]));
						Push(Builtin == EBuiltinId::Min
						        ? (Left < Right ? Left : Right)
						        : (Left > Right ? Left : Right));
						return true;
					}
					{
						const double Left = Runtime::ToDouble(Arguments[0]);
						const double Right = Runtime::ToDouble(Arguments[1]);
						Push(Builtin == EBuiltinId::Min ? (Left < Right ? Left : Right) : (Left > Right ? Left : Right));
					}
					return true;
				}
			case EBuiltinId::Sqrt:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__sqrt expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsNumericValue(Arguments.front()))
					{
						ReportError("__sqrt expects numeric argument");
						return false;
					}
					Push(std::sqrt(Runtime::ToDouble(Arguments.front())));
					return true;
				}
			case EBuiltinId::Sin:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__sin expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsNumericValue(Arguments.front()))
					{
						ReportError("__sin expects numeric argument");
						return false;
					}
					Push(std::sin(Runtime::ToDouble(Arguments.front())));
					return true;
				}
			case EBuiltinId::Cos:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__cos expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsNumericValue(Arguments.front()))
					{
						ReportError("__cos expects numeric argument");
						return false;
					}
					Push(std::cos(Runtime::ToDouble(Arguments.front())));
					return true;
				}
			case EBuiltinId::Tan:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__tan expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsNumericValue(Arguments.front()))
					{
						ReportError("__tan expects numeric argument");
						return false;
					}
					Push(std::tan(Runtime::ToDouble(Arguments.front())));
					return true;
				}
			case EBuiltinId::Floor:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__floor expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsNumericValue(Arguments.front()))
					{
						ReportError("__floor expects numeric argument");
						return false;
					}
					Push(std::floor(Runtime::ToDouble(Arguments.front())));
					return true;
				}
			case EBuiltinId::Ceil:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__ceil expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsNumericValue(Arguments.front()))
					{
						ReportError("__ceil expects numeric argument");
						return false;
					}
					Push(std::ceil(Runtime::ToDouble(Arguments.front())));
					return true;
				}
			case EBuiltinId::Round:
			case EBuiltinId::Trunc:
			case EBuiltinId::Exp:
			case EBuiltinId::Log:
			case EBuiltinId::Log2:
			case EBuiltinId::Asin:
			case EBuiltinId::Acos:
				{
					if (Arguments.size() != 1 || !Runtime::IsNumericValue(Arguments.front()))
					{
						ReportError("numeric math builtin expects exactly one numeric argument");
						return false;
					}
					{
						const double Value = Runtime::ToDouble(Arguments.front());
						switch (Builtin)
						{
						case EBuiltinId::Round:
							{
								Push(std::round(Value));
								break;
							}
						case EBuiltinId::Trunc:
							{
								Push(std::trunc(Value));
								break;
							}
						case EBuiltinId::Exp:
							{
								Push(std::exp(Value));
								break;
							}
						case EBuiltinId::Log:
							{
								Push(std::log(Value));
								break;
							}
						case EBuiltinId::Log2:
							{
								Push(std::log2(Value));
								break;
							}
						case EBuiltinId::Asin:
							{
								Push(std::asin(Value));
								break;
							}
						case EBuiltinId::Acos:
							{
								Push(std::acos(Value));
								break;
							}
						default:
							{
								break;
							}
						}
					}
					return true;
				}
			case EBuiltinId::Pow:
			case EBuiltinId::Atan2:
				{
					if (Arguments.size() != 2 || !Runtime::IsNumericValue(Arguments[0]) || !Runtime::IsNumericValue(Arguments[1]))
					{
						ReportError("numeric math builtin expects exactly two numeric arguments");
						return false;
					}
					if (Builtin == EBuiltinId::Pow)
					{
						Push(std::pow(Runtime::ToDouble(Arguments[0]), Runtime::ToDouble(Arguments[1])));
					}
					else
					{
						Push(std::atan2(Runtime::ToDouble(Arguments[0]), Runtime::ToDouble(Arguments[1])));
					}
					return true;
				}
			case EBuiltinId::IsFinite:
			case EBuiltinId::IsNan:
				{
					if (Arguments.size() != 1 || !Runtime::IsNumericValue(Arguments.front()))
					{
						ReportError("numeric math builtin expects exactly one numeric argument");
						return false;
					}
					if (Builtin == EBuiltinId::IsFinite)
					{
						Push(std::isfinite(Runtime::ToDouble(Arguments.front())));
					}
					else
					{
						Push(std::isnan(Runtime::ToDouble(Arguments.front())));
					}
					return true;
				}
			case EBuiltinId::ReadFile:
				return CallStandardLibraryBuiltin(Builtin, &Arguments[0], ArgumentCount);
			case EBuiltinId::WriteFile:
				return CallStandardLibraryBuiltin(Builtin, &Arguments[0], ArgumentCount);
			case EBuiltinId::AppendFile:
				return CallStandardLibraryBuiltin(Builtin, &Arguments[0], ArgumentCount);
			case EBuiltinId::DeleteFile:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__delete_file expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments.front()))
					{
						ReportError("__delete_file expects string path");
						return false;
					}
					if (std::get<std::string>(Arguments.front()).find('\0') != std::string::npos)
					{
						ReportError("__delete_file path contains NUL");
						return false;
					}
					{
						const std::filesystem::path FilePath =
						    PathUtil::FromUtf8(std::get<std::string>(Arguments.front()));
						std::error_code StatusError;
						const std::filesystem::file_status Status =
						    std::filesystem::symlink_status(FilePath, StatusError);
						if (StatusError && StatusError != std::errc::no_such_file_or_directory)
						{
							ReportError("cannot inspect file: " + std::get<std::string>(Arguments.front()));
							return false;
						}
						if (std::filesystem::is_directory(Status))
						{
							ReportError("delete_file cannot delete a directory");
							return false;
						}
						std::error_code RemoveError;
						const bool bRemoved = std::filesystem::remove(
						    FilePath,
						    RemoveError);
						if (RemoveError)
						{
							ReportError("cannot delete file: " + std::get<std::string>(Arguments.front()));
							return false;
						}
						Push(bRemoved);
					}
					return true;
				}
			case EBuiltinId::RemoveDir:
				{
					if (Arguments.size() != 1)
					{
						ReportErrorId("diag.name_expects_exactly_n_arguments", {},
						    {"__remove_dir", "1"});
						return false;
					}
					if (!Runtime::IsStringValue(Arguments.front()))
					{
						ReportErrorId("diag.expects_string_path", {}, {"__remove_dir"});
						return false;
					}
					if (std::get<std::string>(Arguments.front()).find('\0') != std::string::npos)
					{
						ReportError("__remove_dir path contains NUL");
						return false;
					}
					{
						const std::filesystem::path DirPath =
						    PathUtil::FromUtf8(std::get<std::string>(Arguments.front()));
						std::error_code StatusError;
						const std::filesystem::file_status Status =
						    std::filesystem::symlink_status(DirPath, StatusError);
						if (StatusError == std::errc::no_such_file_or_directory || StatusError == std::errc::not_a_directory || Status.type() == std::filesystem::file_type::not_found)
						{
							Push(false);
							return true;
						}
						if (StatusError)
						{
							ReportErrorId("diag.cannot_inspect_directory", {},
							    {std::get<std::string>(Arguments.front())});
							return false;
						}
						if (!std::filesystem::is_directory(Status))
						{
							ReportErrorId("diag.remove_dir_requires_directory");
							return false;
						}
						std::error_code RemoveError;
						const bool bRemoved = std::filesystem::remove(DirPath, RemoveError);
						if (RemoveError && RemoveError != std::errc::no_such_file_or_directory)
						{
							ReportErrorId("diag.cannot_remove_directory", {},
							    {std::get<std::string>(Arguments.front())});
							return false;
						}
						Push(bRemoved);
					}
					return true;
				}
			case EBuiltinId::Exists:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__exists expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments.front()))
					{
						ReportError("__exists expects string path");
						return false;
					}
					if (std::get<std::string>(Arguments.front()).find('\0') != std::string::npos)
					{
						ReportError("__exists path contains NUL");
						return false;
					}
					{
						std::error_code PathError;
						const bool bExists = std::filesystem::exists(
						    PathUtil::FromUtf8(std::get<std::string>(Arguments.front())),
						    PathError);
						if (PathError)
						{
							ReportError("cannot inspect path: " + std::get<std::string>(Arguments.front()));
							return false;
						}
						Push(bExists);
					}
					return true;
				}
			case EBuiltinId::IsFile:
				{
					// Both path predicates share validation and filesystem dispatch.
					[[fallthrough]];
				}
			case EBuiltinId::IsDirectory:
				{
					if (Arguments.size() != 1)
					{
						ReportErrorId("diag.name_expects_exactly_n_arguments", {},
						    {Builtin == EBuiltinId::IsFile ? "__is_file" : "__is_directory", "1"});
						return false;
					}
					if (!Runtime::IsStringValue(Arguments.front()))
					{
						ReportErrorId("diag.expects_string_path", {},
						    {Builtin == EBuiltinId::IsFile ? "__is_file" : "__is_directory"});
						return false;
					}
					if (std::get<std::string>(Arguments.front()).find('\0') != std::string::npos)
					{
						ReportError("filesystem predicate path contains NUL");
						return false;
					}
					{
						std::error_code StatusError;
						const std::filesystem::file_status Status = std::filesystem::status(
						    PathUtil::FromUtf8(std::get<std::string>(Arguments.front())),
						    StatusError);
						if (StatusError && StatusError != std::errc::no_such_file_or_directory && StatusError != std::errc::not_a_directory)
						{
							ReportErrorId("diag.cannot_inspect_path", {},
							    {std::get<std::string>(Arguments.front())});
							return false;
						}
						Push(!StatusError && (Builtin == EBuiltinId::IsFile ? std::filesystem::is_regular_file(Status) : std::filesystem::is_directory(Status)));
					}
					return true;
				}
			case EBuiltinId::ListDir:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__list_dir expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments.front()))
					{
						ReportError("__list_dir expects string path");
						return false;
					}
					if (std::get<std::string>(Arguments.front()).find('\0') != std::string::npos)
					{
						ReportError("__list_dir path contains NUL");
						return false;
					}
					{
						std::error_code DirError;
						const std::filesystem::path DirPath =
						    PathUtil::FromUtf8(std::get<std::string>(Arguments.front()));
						FDynamicArrayHandle Array = CreateDynamicArray("array<string>", {});
						if (!Array)
						{
							return false;
						}
						std::filesystem::directory_iterator Iterator(DirPath, DirError);
						const std::filesystem::directory_iterator End;
						while (!DirError && Iterator != End)
						{
							FValue Entry = PathUtil::ToUtf8(Iterator->path().filename());
							if (!Array->TryPushScalar(Entry))
							{
								Array->Elements.push_back(std::move(Entry));
							}
							Iterator.increment(DirError);
						}
						if (DirError)
						{
							ReportError("cannot list directory: " + std::get<std::string>(Arguments.front()));
							return false;
						}
						Push(std::move(Array));
					}
					return true;
				}
			case EBuiltinId::Substr:
				{
					if (Arguments.size() != 3)
					{
						ReportError("__substr expects exactly 3 arguments");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments[0]) || !Runtime::IsIntegerValue(Arguments[1]) || !Runtime::IsIntegerValue(Arguments[2]))
					{
						ReportError("__substr expects (string, int64, int64)");
						return false;
					}
					{
						const std::string& Text = std::get<std::string>(Arguments[0]);
						std::size_t Start = 0;
						std::int64_t Count = 0;
						if (!Runtime::TryGetSizeValue(Arguments[1], Start) || !Runtime::TryGetInteger64(Arguments[2], Count) || Count < 0 || Start > Text.size())
						{
							ReportError("substr start/count out of range");
							return false;
						}
						const std::size_t MaxCount = Text.size() - static_cast<std::size_t>(Start);
						const std::uint64_t RequestedCount = static_cast<std::uint64_t>(Count);
						const std::size_t UseCount = RequestedCount < MaxCount
						    ? static_cast<std::size_t>(RequestedCount)
						    : MaxCount;
						const StandardLibrary::FServiceContext Context{StandardLibraryLimits, GetMemoryWork()};
						if (!ReportStandardLibraryResult(Context.CheckText(Text.size()))) return false;
						std::string Result;
						if (!ReportStandardLibraryResult(StandardLibrary::Repeat(std::string_view(Text).substr(Start, UseCount), 1, Result, Context))) return false;
						Push(std::move(Result));
					}
					return true;
				}
			case EBuiltinId::Find:
				{
					if (Arguments.size() != 2)
					{
						ReportError("__find expects exactly 2 arguments");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments[0]) || !Runtime::IsStringValue(Arguments[1]))
					{
						ReportError("__find expects two string arguments");
						return false;
					}
					{
						const std::string& Haystack = std::get<std::string>(Arguments[0]);
						const std::string& Needle = std::get<std::string>(Arguments[1]);
						std::int64_t Position = -1;
						if (!ReportStandardLibraryResult(StandardLibrary::FirstFind(Haystack, Needle, Position,
						        {StandardLibraryLimits, GetMemoryWork()})))
						{
							return false;
						}
						Push(Position);
					}
					return true;
				}
			case EBuiltinId::CharCode:
				{
					if (Arguments.size() != 2)
					{
						ReportError("__char_code expects exactly 2 arguments");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments[0]) || !Runtime::IsIntegerValue(Arguments[1]))
					{
						ReportError("__char_code expects (string, int64)");
						return false;
					}
					{
						const std::string& Text = std::get<std::string>(Arguments[0]);
						std::size_t Index = 0;
						if (!Runtime::TryGetSizeValue(Arguments[1], Index) || Index >= Text.size())
						{
							ReportError("char index out of range");
							return false;
						}
						Push(static_cast<std::int32_t>(
						    static_cast<unsigned char>(Text[static_cast<std::size_t>(Index)])));
					}
					return true;
				}
			case EBuiltinId::Split:
			case EBuiltinId::Join:
				return CallStandardLibraryBuiltin(Builtin, &Arguments[0], ArgumentCount);
			case EBuiltinId::ToLower:
			case EBuiltinId::ToUpper:
				{
					if (Arguments.size() != 1)
					{
						ReportError(
						    Builtin == EBuiltinId::ToLower
						        ? "__to_lower expects exactly 1 argument"
						        : "__to_upper expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments.front()))
					{
						ReportError(
						    Builtin == EBuiltinId::ToLower
						        ? "__to_lower expects string argument"
						        : "__to_upper expects string argument");
						return false;
					}
					{
						const StandardLibrary::FServiceContext Context{StandardLibraryLimits, GetMemoryWork()};
						std::string Text;
						if (!ReportStandardLibraryResult(StandardLibrary::Repeat(std::get<std::string>(Arguments.front()), 1, Text, Context))) return false;
						std::size_t Index = 0;
						for (char& Ch : Text)
						{
							if (Index % 1024 == 0 && !ReportStandardLibraryResult(Context.Check(std::min<std::size_t>(1024, Text.size() - Index)))) return false;
							++Index;
							const unsigned char Byte = static_cast<unsigned char>(Ch);
							if (Builtin == EBuiltinId::ToLower && Byte >= 'A' && Byte <= 'Z')
							{
								Ch = static_cast<char>(Byte + ('a' - 'A'));
							}
							else if (Builtin == EBuiltinId::ToUpper && Byte >= 'a' && Byte <= 'z')
							{
								Ch = static_cast<char>(Byte - ('a' - 'A'));
							}
						}
						Push(std::move(Text));
					}
					return true;
				}
			case EBuiltinId::ReadLine:
				{
					std::optional<std::string> Line;
					if (!ReportStandardLibraryResult(StandardLibrary::ReadInput(InputStream, false, Line,
					        {StandardLibraryLimits, GetMemoryWork()})))
					{
						return false;
					}
					Push(Line ? std::move(*Line) : std::string{});
					return !bHasError;
				}
			case EBuiltinId::Exit:
				{
					if (ExecutionCleanup)
					{
						ReportErrorId("diag.execution_cleanup_failed");
						return false;
					}
					if (Arguments.size() != 1)
					{
						ReportError("exit expects exactly 1 argument");
						return false;
					}
					{
						const auto* const Signed = std::get_if<std::int32_t>(&Arguments.front());
						if (!Signed)
						{
							ReportError("exit expects int argument");
							return false;
						}
						bExitRequested = true;
						RequestedExitCode = *Signed;
						if (!DiscardExecutionAbove(0, 0, {}))
						{
							return false;
						}
					}
					return true;
				}
			case EBuiltinId::GetEnv:
				{
					if (Arguments.size() != 1)
					{
						ReportError("getenv expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments.front()))
					{
						ReportError("getenv expects string argument");
						return false;
					}
					{
						const std::string& Name = std::get<std::string>(Arguments.front());
						if (Name.empty() || Name.find_first_of(std::string_view("=\0", 2)) != std::string::npos)
						{
							ReportError("getenv name must be non-empty");
							return false;
						}
						std::optional<std::string> Value;
						const StandardLibrary::FServiceContext Context{StandardLibraryLimits, GetMemoryWork()};
						if (!ReportStandardLibraryResult(Context.CheckText(Name.size()))) return false;
						if (!Environment::ReadUtf8(Name, Value)) return ReportStandardLibraryResult({StandardLibrary::EServiceStatus::IoError, 0});
						if (Value && !ReportStandardLibraryResult(Context.CheckText(Value->size()))) return false;
						Push(Value ? *Value : std::string{});
					}
					return true;
				}
			case EBuiltinId::MkDir:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__mkdir expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments.front()))
					{
						ReportError("__mkdir expects string path");
						return false;
					}
					if (std::get<std::string>(Arguments.front()).find('\0') != std::string::npos)
					{
						ReportError("__mkdir path contains NUL");
						return false;
					}
					{
						std::error_code DirError;
						std::filesystem::create_directories(
						    PathUtil::FromUtf8(std::get<std::string>(Arguments.front())),
						    DirError);
						if (DirError)
						{
							ReportError("cannot create directory: " + std::get<std::string>(Arguments.front()));
							return false;
						}
					}
					return true;
				}
			case EBuiltinId::Assert:
				{
					if (Arguments.size() < 1 || Arguments.size() > 2)
					{
						ReportErrorId(
						    "diag.name_expects_between_n_and_m_arguments",
						    {},
						    {"assert", "1", "2"});
						return false;
					}
					if (Arguments.size() == 2 && !Runtime::IsStringValue(Arguments[1]))
					{
						ReportErrorId("diag.assert_expects_string_message");
						return false;
					}
					if (!ExpectBool(Arguments.front(), {}))
					{
						return false;
					}
					if (!std::get<bool>(Arguments.front()))
					{
						if (Arguments.size() == 2 && !std::get<std::string>(Arguments[1]).empty())
						{
							ReportErrorId(
							    "diag.assertion_failed_with_message",
							    {},
							    {std::get<std::string>(Arguments[1])});
						}
						else
						{
							ReportErrorId("diag.assertion_failed");
						}
						return false;
					}
					return true;
				}
			case EBuiltinId::Sleep:
				{
					if (Arguments.size() != 1)
					{
						ReportError("sleep expects exactly 1 argument");
						return false;
					}
					{
						const auto* const Signed = std::get_if<std::int32_t>(&Arguments.front());
						if (!Signed)
						{
							ReportError("sleep expects int argument");
							return false;
						}
						if (*Signed < 0)
						{
							ReportError("sleep expects non-negative milliseconds");
							return false;
						}
						std::int32_t RemainingMilliseconds = *Signed;
						while (RemainingMilliseconds > 0)
						{
							const std::int32_t Slice = HasExecutionGovernance()
							    ? std::min<std::int32_t>(RemainingMilliseconds, 10)
							    : RemainingMilliseconds;
							std::this_thread::sleep_for(std::chrono::milliseconds(Slice));
							RemainingMilliseconds -= Slice;
							if (HasExecutionGovernance() && !CheckExecutionGovernance())
							{
								return false;
							}
						}
					}
					return true;
				}
			case EBuiltinId::Format:
			case EBuiltinId::FormatPrintf:
				{
					if (Arguments.size() != 2)
					{
						ReportError(
						    Builtin == EBuiltinId::FormatPrintf
						        ? "__format_printf expects exactly 2 arguments"
						        : "__format expects exactly 2 arguments");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments[0]))
					{
						ReportError(
						    Builtin == EBuiltinId::FormatPrintf
						        ? "__format_printf expects (string, array<string>)"
						        : "__format expects (string, array<string>)");
						return false;
					}
					{
						const auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&Arguments[1]);
						if (!DynamicArray || !*DynamicArray || (*DynamicArray)->TypeName != "array<string>" || (*DynamicArray)->ElementTypeName != "string")
						{
							ReportError(
							    Builtin == EBuiltinId::FormatPrintf
							        ? "__format_printf expects (string, array<string>)"
							        : "__format expects (string, array<string>)");
							return false;
						}
						const StandardLibrary::FServiceContext Context{StandardLibraryLimits, GetMemoryWork()};
						std::vector<std::string> Parts;
						Parts.reserve((*DynamicArray)->ElementCount());
						for (std::size_t Index = 0; Index < (*DynamicArray)->ElementCount(); ++Index)
						{
							if (!Context.Check(1)) return false;
							const std::string* const Part = (*DynamicArray)->BorrowString(Index);
							if (!Part)
							{
								ReportError(
								    Builtin == EBuiltinId::FormatPrintf
								        ? "__format_printf expects array<string> elements"
								        : "__format expects array<string> elements");
								return false;
							}
							std::string Copy;
							if (!ReportStandardLibraryResult(StandardLibrary::Repeat(*Part, 1, Copy, Context))) return false;
							Parts.push_back(std::move(Copy));
						}
						std::string Result;
						std::string FormatError;
						StandardLibrary::FServiceResult Failure;
						const bool bFormatted = Builtin == EBuiltinId::FormatPrintf
						    ? Runtime::TryPrintfFormatString(
						          std::get<std::string>(Arguments[0]),
						          Parts,
						          Result,
						          FormatError, &Context, &Failure)
						    : Runtime::TryFormatString(
						          std::get<std::string>(Arguments[0]),
						          Parts,
						          Result,
						          FormatError, &Context, &Failure);
						if (!bFormatted)
						{
							if (!Failure) return ReportStandardLibraryResult(Failure);
							if (FormatError.empty())
							{
								ReportErrorId("diag.format_failed");
							}
							else
							{
								ReportError(std::move(FormatError));
							}
							return false;
						}
						Push(std::move(Result));
					}
					return true;
				}
			case EBuiltinId::ReadToken:
			case EBuiltinId::ReadString:
				{
					auto Token = ReadNextToken();
					if (bHasError) return false;
					if (Token.empty())
					{
						ReportErrorId("diag.stdlib_eof");
						return false;
					}
					Push(std::move(Token));
					return !bHasError;
				}
			case EBuiltinId::ReadInt:
			case EBuiltinId::ReadDouble:
			case EBuiltinId::ReadBool:
				{
					const auto Token = ReadNextToken();
					if (bHasError) return false;
					if (Token.empty())
					{
						ReportErrorId(Builtin == EBuiltinId::ReadInt ? "diag.read_int_unexpected_eof"
						        : Builtin == EBuiltinId::ReadDouble  ? "diag.read_double_unexpected_eof"
						                                             : "diag.read_bool_unexpected_eof");
						return false;
					}
					FValue Value;
					const auto Type = Builtin == EBuiltinId::ReadInt ? "int" : Builtin == EBuiltinId::ReadDouble ? "double"
					                                                                                             : "bool";
					const auto Result = StandardLibrary::ParseScalar(Token, Type, 10, false, Value,
					    {StandardLibraryLimits, GetMemoryWork()});
					if (!ReportStandardLibraryResult(Result)) return false;
					Push(std::move(Value));
					return !bHasError;
				}
			case EBuiltinId::SetEnv:
				{
					if (Arguments.size() != 2)
					{
						ReportErrorId("diag.setenv_expects_exactly_2_arguments");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments[0]) || !Runtime::IsStringValue(Arguments[1]))
					{
						ReportErrorId("diag.setenv_expects_string_string");
						return false;
					}
					{
						const std::string& Name = std::get<std::string>(Arguments[0]);
						const std::string& Value = std::get<std::string>(Arguments[1]);
						const StandardLibrary::FServiceContext Context{StandardLibraryLimits, GetMemoryWork()};
						if (!ReportStandardLibraryResult(Context.CheckText(Name.size())) || !ReportStandardLibraryResult(Context.CheckText(Value.size())))
						{
							return false;
						}
						if (Name.empty() || Name.find_first_of(std::string_view("=\0", 2)) != std::string::npos || Value.find('\0') != std::string::npos)
						{
							ReportErrorId("diag.setenv_name_must_be_non_empty_and_must_not_contain");
							return false;
						}
						if (!Environment::SetUtf8(Name, Value))
						{
							ReportErrorId("diag.setenv_failed", {}, {Name});
							return false;
						}
					}
					return true;
				}
			case EBuiltinId::Range:
				{
					if (Arguments.size() != 1)
					{
						ReportError("range expects exactly 1 argument");
						return false;
					}
					{
						std::int64_t Count = 0;
						if (!Runtime::IsIntegerValue(Arguments.front()) || !Runtime::TryGetInteger64(Arguments.front(), Count))
						{
							ReportError("range expects int64 argument");
							return false;
						}
						constexpr std::int32_t MaxRangeCount = 1000000;
						if (Count > MaxRangeCount)
						{
							ReportError("range count too large (max 1000000)");
							return false;
						}
						FDynamicArrayHandle Array = CreateDynamicArray("array<int>", {});
						if (!Array)
						{
							return false;
						}
						if (Count > 0)
						{
							auto* const Values = std::get_if<std::vector<std::int32_t>>(
							    &Array->ScalarElements);
							if (!Values)
							{
								ReportError("invalid scalar range storage");
								return false;
							}
							Values->reserve(static_cast<std::size_t>(Count));
							for (std::int32_t Index = 0; Index < Count; ++Index)
							{
								Values->push_back(Index);
							}
						}
						Push(std::move(Array));
					}
					return true;
				}
			case EBuiltinId::Cwd:
				{
					{
						std::error_code PathError;
						const std::filesystem::path Current = std::filesystem::current_path(PathError);
						if (PathError)
						{
							ReportError("cannot get current directory");
							return false;
						}
						Push(PathUtil::ToUtf8(Current));
					}
					return true;
				}
			case EBuiltinId::ChDir:
				{
					if (Arguments.size() != 1)
					{
						ReportError("__chdir expects exactly 1 argument");
						return false;
					}
					if (!Runtime::IsStringValue(Arguments.front()))
					{
						ReportError("__chdir expects string path");
						return false;
					}
					if (std::get<std::string>(Arguments.front()).find('\0') != std::string::npos)
					{
						ReportError("__chdir path contains NUL");
						return false;
					}
					{
						std::error_code PathError;
						std::filesystem::current_path(
						    PathUtil::FromUtf8(std::get<std::string>(Arguments.front())),
						    PathError);
						if (PathError)
						{
							ReportError("cannot change directory: " + std::get<std::string>(Arguments.front()));
							return false;
						}
					}
					return true;
				}
			case EBuiltinId::Now:
				{
					{
						using clock = std::chrono::system_clock;
						const auto Duration = clock::now().time_since_epoch();
						const double Seconds = std::chrono::duration<double>(Duration).count();
						Push(Seconds);
					}
					return true;
				}
			case EBuiltinId::SteadyNow:
				{
					{
						using clock = std::chrono::steady_clock;
						const auto Duration = clock::now().time_since_epoch();
						Push(std::chrono::duration<double>(Duration).count());
					}
					return true;
				}
			case EBuiltinId::MakeChannel:
				{
					ReportError("__make_channel removed; use channel<T>()");
					return false;
				}
			case EBuiltinId::ChannelSend:
				{
					{
						const auto* ChannelHandle = std::get_if<FChannelHandle>(&Arguments[0]);
						if (!ChannelHandle || !*ChannelHandle)
						{
							ReportError("__channel_send expects (channel<T>, T)");
							return false;
						}
						std::string ChannelTypeName;
						std::string ChannelElementTypeName;
						(*ChannelHandle)->GetTypeNames(ChannelTypeName, ChannelElementTypeName);
						if (!ValueMatchesElementType(
						        Arguments[1], ChannelElementTypeName, {}))
						{
							ReportErrorId(
							    "diag.channel_send_value_type_mismatch_for",
							    {},
							    {ChannelTypeName});
							return false;
						}
						std::string SendError;
						bool bSent = false;
						if (!HasExecutionGovernance())
						{
							FValue SendValue;
							if (!CloneValueOrReport(Arguments[1], SendValue, {}))
							{
								return false;
							}
							bSent = (*ChannelHandle)->Send(std::move(SendValue), SendError);
						}
						else
						{
							// A blocking channel operation must periodically return to the VM
							// so cancellation, deadlines, and instruction limits remain observable.
							for (;;)
							{
								FValue SendValue;
								if (!CloneValueOrReport(Arguments[1], SendValue, {}))
								{
									return false;
								}
								bool bTimedOut = false;
								bSent = (*ChannelHandle)->SendFor(std::move(SendValue), 10, bTimedOut, SendError);
								if (bSent || !bTimedOut)
								{
									break;
								}
								if (!CheckExecutionGovernance())
								{
									return false;
								}
							}
						}
						if (!bSent)
						{
							if (SendError.empty())
							{
								ReportErrorId("diag.channel_send_failed");
							}
							else
							{
								ReportError(std::move(SendError));
							}
							return false;
						}
						NotifyContainerMutation(Arguments[0], EMutationKind::ContainerAdd);
					}
					return true;
				}
			case EBuiltinId::ChannelTrySend:
			case EBuiltinId::ChannelSendFor:
				{
					{
						const auto* ChannelHandle = std::get_if<FChannelHandle>(&Arguments[0]);
						if (!ChannelHandle || !*ChannelHandle)
						{
							ReportError("channel send expects (channel<T>, T)");
							return false;
						}
						std::string ChannelTypeName;
						std::string ChannelElementTypeName;
						(*ChannelHandle)->GetTypeNames(ChannelTypeName, ChannelElementTypeName);
						if (!ValueMatchesElementType(
						        Arguments[1], ChannelElementTypeName, {}))
						{
							ReportErrorId(
							    "diag.channel_send_value_type_mismatch_for",
							    {},
							    {ChannelTypeName});
							return false;
						}
						bool bSent = false;
						if (Builtin == EBuiltinId::ChannelTrySend)
						{
							FValue SendValue;
							if (!CloneValueOrReport(Arguments[1], SendValue, {}))
							{
								return false;
							}
							bSent = (*ChannelHandle)->TrySend(std::move(SendValue));
						}
						else
						{
							const auto* Timeout = std::get_if<std::int32_t>(&Arguments[2]);
							if (!Timeout || *Timeout < 0)
							{
								ReportError("channel send timeout must be a non-negative int");
								return false;
							}
							std::string SendError;
							bool bTimedOut = false;
							if (!HasExecutionGovernance())
							{
								FValue SendValue;
								if (!CloneValueOrReport(Arguments[1], SendValue, {}))
								{
									return false;
								}
								bSent = (*ChannelHandle)->SendFor(std::move(SendValue), *Timeout, bTimedOut, SendError);
							}
							else
							{
								const auto WaitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(*Timeout);
								for (;;)
								{
									const auto Remaining = std::max<std::int64_t>(
									    0,
									    std::chrono::duration_cast<std::chrono::milliseconds>(
									        WaitDeadline - std::chrono::steady_clock::now())
									        .count());
									const std::int64_t Slice = std::min<std::int64_t>(10, Remaining);
									FValue SendValue;
									if (!CloneValueOrReport(Arguments[1], SendValue, {}))
									{
										return false;
									}
									bSent = (*ChannelHandle)->SendFor(std::move(SendValue), Slice, bTimedOut, SendError);
									if (bSent || !bTimedOut || Slice == 0)
									{
										break;
									}
									if (!CheckExecutionGovernance())
									{
										return false;
									}
								}
							}
							if (!bSent && !bTimedOut && !SendError.empty() && !(*ChannelHandle)->IsClosed())
							{
								ReportError(std::move(SendError));
								return false;
							}
						}
						if (bSent)
						{
							NotifyContainerMutation(Arguments[0], EMutationKind::ContainerAdd);
						}
						Push(bSent);
					}
					return true;
				}
			case EBuiltinId::ChannelRecv:
				{
					{
						const auto* ChannelHandle = std::get_if<FChannelHandle>(&Arguments[0]);
						if (!ChannelHandle || !*ChannelHandle)
						{
							ReportError("__channel_recv expects channel<T>");
							return false;
						}
						FValue Received;
						std::string RecvError;
						bool bReceived = false;
						if (!HasExecutionGovernance())
						{
							bReceived = (*ChannelHandle)->Recv(Received, RecvError);
						}
						else
						{
							// See ChannelSend: a blocking wait must retain a cancellation
							// observation point while execution governance is active.
							for (;;)
							{
								bool bTimedOut = false;
								bReceived = (*ChannelHandle)->RecvFor(Received, 10, bTimedOut, RecvError);
								if (bReceived || !bTimedOut)
								{
									break;
								}
								if (!CheckExecutionGovernance())
								{
									return false;
								}
							}
						}
						if (!bReceived)
						{
							if (RecvError.empty())
							{
								ReportErrorId("diag.channel_recv_failed");
							}
							else
							{
								ReportError(std::move(RecvError));
							}
							return false;
						}
						NotifyContainerMutation(Arguments[0], EMutationKind::ContainerRemove);
						Push(std::move(Received));
					}
					return true;
				}
			case EBuiltinId::ChannelTryRecv:
			case EBuiltinId::ChannelRecvFor:
				{
					{
						const auto* ChannelHandle = std::get_if<FChannelHandle>(&Arguments[0]);
						if (!ChannelHandle || !*ChannelHandle)
						{
							ReportError("channel receive expects channel<T>");
							return false;
						}
						FValue Received;
						bool bReceived = false;
						if (Builtin == EBuiltinId::ChannelTryRecv)
						{
							bReceived = (*ChannelHandle)->TryRecv(Received);
						}
						else
						{
							const auto* Timeout = std::get_if<std::int32_t>(&Arguments[1]);
							if (!Timeout || *Timeout < 0)
							{
								ReportError("channel receive timeout must be a non-negative int");
								return false;
							}
							std::string RecvError;
							bool bTimedOut = false;
							if (!HasExecutionGovernance())
							{
								bReceived = (*ChannelHandle)->RecvFor(Received, *Timeout, bTimedOut, RecvError);
							}
							else
							{
								const auto WaitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(*Timeout);
								for (;;)
								{
									const auto Remaining = std::max<std::int64_t>(
									    0,
									    std::chrono::duration_cast<std::chrono::milliseconds>(
									        WaitDeadline - std::chrono::steady_clock::now())
									        .count());
									const std::int64_t Slice = std::min<std::int64_t>(10, Remaining);
									bReceived = (*ChannelHandle)->RecvFor(Received, Slice, bTimedOut, RecvError);
									if (bReceived || !bTimedOut || Slice == 0)
									{
										break;
									}
									if (!CheckExecutionGovernance())
									{
										return false;
									}
								}
							}
							if (!bReceived && !bTimedOut && !RecvError.empty() && !(*ChannelHandle)->IsClosed())
							{
								ReportError(std::move(RecvError));
								return false;
							}
						}
						std::string ChannelTypeName;
						std::string ChannelElementTypeName;
						(*ChannelHandle)->GetTypeNames(ChannelTypeName, ChannelElementTypeName);
						const std::string ResultType =
						    "array<" + ChannelElementTypeName + ">";
						FDynamicArrayHandle Result = CreateDynamicArray(ResultType, {});
						if (!Result)
						{
							return false;
						}
						if (bReceived)
						{
							if (!AppendDynamicArrayValue(*Result, Received, {}))
							{
								return false;
							}
							NotifyContainerMutation(Arguments[0], EMutationKind::ContainerRemove);
						}
						Push(std::move(Result));
					}
					return true;
				}
			case EBuiltinId::ChannelIsClosed:
			case EBuiltinId::ChannelSize:
			case EBuiltinId::ChannelCapacity:
				{
					{
						const auto* ChannelHandle = std::get_if<FChannelHandle>(&Arguments[0]);
						if (!ChannelHandle || !*ChannelHandle)
						{
							ReportError("channel query expects channel<T>");
							return false;
						}
						if (Builtin == EBuiltinId::ChannelIsClosed)
						{
							Push((*ChannelHandle)->IsClosed());
							return true;
						}
						const std::size_t Value = Builtin == EBuiltinId::ChannelSize
						    ? (*ChannelHandle)->Size()
						    : (*ChannelHandle)->Capacity();
						if (static_cast<std::uint64_t>(Value) > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
						{
							ReportError("channel size exceeds int64 range");
							return false;
						}
						Push(static_cast<std::int64_t>(Value));
					}
					return true;
				}
			case EBuiltinId::ChannelSetCapacity:
				{
					{
						const auto* ChannelHandle = std::get_if<FChannelHandle>(&Arguments[0]);
						std::size_t Capacity = 0;
						if (!ChannelHandle || !*ChannelHandle || !Runtime::TryGetSizeValue(Arguments[1], Capacity) || Capacity == 0)
						{
							ReportError("channel capacity must be a positive int64");
							return false;
						}
						std::string CapacityError;
						if (!(*ChannelHandle)->ConfigureCapacity(Capacity, CapacityError))
						{
							ReportError(std::move(CapacityError));
							return false;
						}
					}
					return true;
				}
			case EBuiltinId::ChannelClose:
				{
					{
						const auto* ChannelHandle = std::get_if<FChannelHandle>(&Arguments[0]);
						if (!ChannelHandle || !*ChannelHandle)
						{
							ReportError("__channel_close expects channel<T>");
							return false;
						}
						(*ChannelHandle)->Close();
						NotifyContainerMutation(Arguments[0], EMutationKind::ContainerClear);
					}
					return true;
				}
			case EBuiltinId::SpawnCallable:
				{
					// Both callable task forms share setup and differ only in result delivery.
					[[fallthrough]];
				}
			case EBuiltinId::SpawnCallableResult:
				{
					const bool bWithResult = Builtin == EBuiltinId::SpawnCallableResult;
					const std::size_t CallableOffset = bWithResult ? 1u : 0u;
					if (!ActiveModule || Arguments.size() <= CallableOffset)
					{
						ReportError("callable task requires an active module and a function");
						return false;
					}
					const auto* CallableHandle =
					    std::get_if<FFunctionHandle>(&Arguments[CallableOffset]);
					if (!CallableHandle || !*CallableHandle)
					{
						ReportError("callable task requires a script function value");
						return false;
					}
					const FFunctionValue& Callable = **CallableHandle;
					if (Callable.ProgramGeneration != ProgramGeneration || Callable.ProgramOwner != ProgramOwner || Callable.FunctionIndex >= ActiveModule->Functions.size() || Callable.CapturedThis)
					{
						ReportError("callable task target is stale, foreign, or captures this");
						return false;
					}
					const FBytecodeFunction& Target =
					    ActiveModule->Functions[Callable.FunctionIndex];
					std::vector<std::string> ParamTypes;
					std::string ReturnType;
					if (Target.Name != Callable.FunctionName || Target.ClosureTypeName != Callable.TypeName || !Runtime::ParseFunctionType(Callable.TypeName, ParamTypes, ReturnType) || Target.ParamCount != ParamTypes.size() || Arguments.size() - CallableOffset - 1 != ParamTypes.size() || BytecodeFunctionHasRefParams(Target) || Target.Captures.size() != Callable.Captures.size())
					{
						ReportError("callable task signature or capture schema is invalid");
						return false;
					}
					for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
					{
						const FValue& Value = Arguments[CallableOffset + 1 + Index];
						if (!Runtime::IsAllowedChannelElementType(ParamTypes[Index]) || !Concurrent::IsChannelPayload(Value) || !ValueMatchesElementType(Value, ParamTypes[Index], {}))
						{
							ReportError("callable task argument is not transferable");
							return false;
						}
					}
					for (std::size_t Index = 0; Index < Callable.Captures.size(); ++Index)
					{
						const auto& Capture = Target.Captures[Index];
						const std::string& CaptureType = Capture.TypeName;
						const FValue* Payload = UnwrapClosureReferenceCell(&Callable.Captures[Index]);
						if (Capture.Mode == EClosureCaptureMode::Reference || !Payload)
						{
							ReportError("callable task cannot transfer an external reference capture");
							return false;
						}
						const FValue& Value = *Payload;
						if (!Runtime::IsAllowedChannelElementType(CaptureType) || !Concurrent::IsChannelPayload(Value) || !ValueMatchesElementType(Value, CaptureType, {}))
						{
							ReportError("callable task capture is not transferable");
							return false;
						}
					}
					FObjectHandle TaskObject;
					FChannelHandle ResultChannel;
					std::uint16_t TaskIdField = static_cast<std::uint16_t>(-1);
					std::uint16_t ResultField = static_cast<std::uint16_t>(-1);
					if (bWithResult)
					{
						const auto* TaskTypeName = std::get_if<std::string>(&Arguments[0]);
						StandardLibrary::FTaskLayout Layout;
						if (!TaskTypeName || !Runtime::IsAllowedChannelElementType(ReturnType) || !StandardLibrary::ResolveTaskLayout(*ActiveModule, *TaskTypeName, Layout, ReturnType))
						{
							ReportError("callable task result class has an invalid layout");
							return false;
						}
						const auto TaskTypeIndex = FindTypeIndex(*TaskTypeName);
						TaskIdField = Layout.Id;
						ResultField = Layout.Result;
						ResultChannel = CreateChannel("channel<" + ReturnType + ">", {});
						if (!ResultChannel)
						{
							return false;
						}
						std::string CapacityError;
						if (!ResultChannel->ConfigureCapacity(1, CapacityError))
						{
							ReportError(CapacityError);
							return false;
						}
						TaskObject = CreateObject(TaskTypeIndex, {});
						if (!TaskObject || TaskIdField >= TaskObject->Fields.size() || ResultField >= TaskObject->Fields.size())
						{
							ReportError("callable task result class could not be created");
							return false;
						}
					}
					else if (ReturnType != "void")
					{
						ReportError("callable spawn requires void return");
						return false;
					}
					std::vector<FValue> WorkerArguments;
					WorkerArguments.reserve(ParamTypes.size());
					for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
					{
						WorkerArguments.push_back(Arguments[CallableOffset + 1 + Index]);
					}
					std::vector<FValue> WorkerCaptures;
					WorkerCaptures.reserve(Callable.Captures.size());
					for (const auto& Capture : Callable.Captures)
					{
						const auto* Payload = UnwrapClosureReferenceCell(&Capture);
						if (!Payload) return false;
						WorkerCaptures.push_back(*Payload);
					}
					const std::string TargetName = Callable.FunctionName;
					const std::uint16_t TargetIndex = Callable.FunctionIndex;
					auto SharedModule = std::make_shared<FBytecodeModule>(*ActiveModule);
					std::shared_ptr<const std::vector<FNativeFunction>> SharedNatives(SharedModule, ActiveNatives);
					if (ActiveNatives && ActiveNatives != &StandardLibrary::GetBindingCatalog().GetNativeFunctions())
					{
						SharedNatives = std::make_shared<const std::vector<FNativeFunction>>(*ActiveNatives);
					}
					const EExecutionBackend WorkerBackend = RequestedExecutionBackend;
					const FJitPolicy WorkerJitPolicy = JitPolicy;
					const auto WorkerJitBackendFactory = JitBackendFactory;
					const auto WorkerVmBackendFactory = VmExecutionDomain->GetFactory();
					const std::size_t WorkerMaxCallDepth = CallDepthLimit;
					const FExecutionLimits WorkerExecutionLimits = ExecutionLimits;
					const std::shared_ptr<FExecutionControl> WorkerExecutionControl = ExecutionControl;
					const FScriptCapabilities WorkerCapabilities = Capabilities;
					const auto WorkerI18nServices = I18nServices;
					const auto WorkerI18nLimits = I18nLimits;
					const auto WorkerStandardLibraryLimits = StandardLibraryLimits;
					const auto WorkerNumericLimits = NumericLimits;
					(void)GetMemoryContext();
					const auto WorkerMemoryParent = MemoryContext;
					const auto WorkerModuleEnvironment = ModuleEnvironment;
					const bool WorkerUseEnvironmentBindings = bUseEnvironmentBindings;
					// Owned workers are joined before the parent execution and its borrowed catalog end.
					const auto* WorkerMemoryBindings = MemoryBindings;
					const FInstanceCallInterceptor WorkerInterceptor = InstanceCallInterceptor;
					const FMutationCallback WorkerMutationCallback = MutationCallback;
					const std::uint64_t WorkerProgramGeneration = ProgramGeneration;
					auto TypedState = bWithResult ? std::make_shared<Concurrent::FTypedTaskState>() : nullptr;
					if (TypedState)
					{
						TypedState->Executor = Executor->Identity();
						TypedState->ProgramOwner = ProgramOwner;
						TypedState->Generation = ProgramGeneration;
						TypedState->ElementType = ReturnType;
					}
					const std::int32_t TaskId = SpawnedTasks.Spawn(
					    [SharedModule, SharedNatives, TargetName, TargetIndex,
					        WorkerArguments = std::move(WorkerArguments), WorkerCaptures, ResultChannel,
					        ReturnType, WorkerBackend, WorkerJitPolicy, WorkerJitBackendFactory, WorkerVmBackendFactory,
					        WorkerMaxCallDepth, WorkerExecutionLimits, WorkerExecutionControl, WorkerCapabilities,
					        WorkerMemoryParent, WorkerMemoryBindings, WorkerInterceptor, WorkerMutationCallback,
					        WorkerProgramGeneration, WorkerModuleEnvironment, WorkerUseEnvironmentBindings, WorkerI18nServices, WorkerI18nLimits, WorkerStandardLibraryLimits, WorkerNumericLimits, bWithResult](
					        std::shared_ptr<std::atomic_bool> Cancellation, std::string& OutError) -> bool
					{
						const auto CloseResult = [&]()
						{
							if (ResultChannel)
							{
								ResultChannel->Close();
							}
						};
						if (TargetIndex >= SharedModule->Functions.size() || SharedModule->Functions[TargetIndex].Name != TargetName)
						{
							OutError = "callable task target changed before execution";
							CloseResult();
							return false;
						}
						std::ostringstream DiscardOut;
						std::istringstream EmptyIn;
						std::ostringstream DiscardErr;
						VeroScriptVm WorkerVm(DiscardOut, EmptyIn, DiscardErr);
						WorkerVm.SetMemoryContext(WorkerMemoryParent->CreateWorker(), true);
						if (!WorkerVm.SetModuleEnvironment(WorkerModuleEnvironment, OutError, WorkerUseEnvironmentBindings)) return false;
						WorkerVm.SetMemoryHostBindings(WorkerMemoryBindings);
						WorkerVm.SetI18nServices(WorkerI18nServices, WorkerI18nLimits);
						WorkerVm.SetStandardLibraryLimits(WorkerStandardLibraryLimits);
						WorkerVm.SetNumericLimits(WorkerNumericLimits);
						WorkerVm.SetExecutionBackend(WorkerBackend);
						auto TaskJitPolicy = WorkerJitPolicy;
						TaskJitPolicy.bReuseSessionCode = false;
						WorkerVm.SetJitPolicy(TaskJitPolicy);
						if (!WorkerVm.SetVmBackendFactory(WorkerVmBackendFactory, OutError) || !WorkerVm.SetJitBackendFactory(WorkerJitBackendFactory, OutError) || !WorkerVm.SetJitProgramOwner(SharedModule, OutError))
						{
							CloseResult();
							return false;
						}
						WorkerVm.SetMaxCallDepth(WorkerMaxCallDepth);
						WorkerVm.SetExecutionLimits(WorkerExecutionLimits);
						WorkerVm.SetExecutionControl(WorkerExecutionControl);
						WorkerVm.SetCapabilities(WorkerCapabilities);
						WorkerVm.SetInstanceCallInterceptor(WorkerInterceptor);
						WorkerVm.SetMutationCallback(WorkerMutationCallback);
						WorkerVm.SetProgramGeneration(WorkerProgramGeneration);
						WorkerVm.SetWorkerCancellation(std::move(Cancellation));
						FValue Result;
						const std::vector<FNativeFunction>* Natives =
						    SharedNatives ? SharedNatives.get() : nullptr;
						if (!WorkerVm.ExecuteFunction(
						        *SharedModule, TargetIndex, WorkerArguments,
						        Natives, bWithResult ? &Result : nullptr,
						        &WorkerCaptures))
						{
							CloseResult();
							const FScriptError& Error = WorkerVm.GetError();
							std::ostringstream Formatted;
							if (!Error.Location.FileLabel.empty())
							{
								Formatted << Error.Location.FileLabel << ':'
								          << Error.Location.Line << ':'
								          << Error.Location.Column << ": ";
							}
							Formatted << (Error.Message.empty()
							        ? "callable task failed"
							        : Error.Message);
							OutError = Formatted.str();
							return false;
						}
						if (WorkerVm.WasExitRequested())
						{
							CloseResult();
							OutError = "callable task called exit(" + std::to_string(WorkerVm.GetRequestedExitCode()) + ')';
							return false;
						}
						if (bWithResult)
						{
							std::string SendError;
							if (!Concurrent::IsChannelPayload(Result) || !ResultChannel->Send(std::move(Result), SendError))
							{
								if (!ResultChannel->IsClosed())
								{
									OutError = SendError.empty()
									    ? "callable task result could not be delivered"
									    : SendError;
									CloseResult();
									return false;
								}
							}
							CloseResult();
						}
						return true;
					});
					if (TaskId < 0)
					{
						if (ResultChannel)
						{
							ResultChannel->Close();
						}
						ReportErrorId("diag.failed_to_spawn_task_thread", {});
						return false;
					}
					if (bWithResult)
					{
						TypedState->Id = TaskId;
						TypedState->Ticket = SpawnedTasks.RetainTask(TaskId);
						std::atomic_store(&ResultChannel->TypedTask, TypedState);
						TaskObject->Fields[TaskIdField] = TaskId;
						TaskObject->Fields[ResultField] = ResultChannel;
						Push(TaskObject);
					}
					else
					{
						Push(TaskId);
					}
					return true;
				}
			case EBuiltinId::Spawn:
			case EBuiltinId::SpawnResult:
				{
					{
						const bool bCloseChannelOnExit =
						    Builtin == EBuiltinId::SpawnResult;
						if (!ActiveModule)
						{
							ReportError("__spawn requires an active module");
							return false;
						}
						if (!Runtime::IsStringValue(Arguments[0]))
						{
							ReportError("__spawn expects (string, channel<T>)");
							return false;
						}
						const auto* ChannelHandle = std::get_if<FChannelHandle>(&Arguments[1]);
						if (!ChannelHandle || !*ChannelHandle)
						{
							ReportError("__spawn expects (string, channel<T>)");
							return false;
						}
						const std::string& FunctionName = std::get<std::string>(Arguments[0]);
						std::int32_t FunctionIndex = -1;
						const std::vector<std::string> ReferenceModules = GetSpawnReferenceModules();
						{
							const auto It = ActiveModule->FunctionIndices.find(FunctionName);
							if (It != ActiveModule->FunctionIndices.end())
							{
								FunctionIndex = static_cast<std::int32_t>(It->second);
							}
							else
							{
								// Free functions are keyed as `name#fn#…` (and private as `mod\x1Fname#fn#…`).
								// Spawn takes the unmangled entry name; resolve uniquely by leaf basename.
								const auto LeafBaseName = [](const std::string& Name) -> std::string
								{
									const std::size_t IdentityStart = FindLinkageIdentityStart(Name);
									const std::size_t Signature = FindFunctionSignatureMarker(Name);
									std::string_view Leaf = std::string_view(Name).substr(
									    IdentityStart,
									    (Signature == std::string_view::npos ? Name.size() : Signature) - IdentityStart);
									const std::size_t Generic = Leaf.find('#');
									if (Generic != std::string_view::npos)
									{
										Leaf = Leaf.substr(0, Generic);
									}
									return std::string(Leaf);
								};
								std::vector<std::size_t> Matches;
								for (std::size_t Index = 0; Index < ActiveModule->Functions.size(); ++Index)
								{
									const std::string& LinkageName =
									    ActiveModule->Functions[Index].Name;
									if (LeafBaseName(LinkageName) != FunctionName)
									{
										continue;
									}
									const std::size_t Sep = LinkageName.find('\x1F');
									if (Sep != std::string::npos)
									{
										const std::string OwnerModule =
										    LinkageName.substr(0, Sep);
										bool bVisible = false;
										for (const std::string& RefModule : ReferenceModules)
										{
											if (RefModule == OwnerModule)
											{
												bVisible = true;
												break;
											}
										}
										if (!bVisible)
										{
											continue;
										}
									}
									Matches.push_back(Index);
								}
								if (Matches.size() == 1)
								{
									FunctionIndex = static_cast<std::int32_t>(Matches.front());
								}
								else if (Matches.size() > 1)
								{
									ReportError(
									    "spawn target is ambiguous (multiple overloads): " + FunctionName);
									return false;
								}
							}
						}
						if (FunctionIndex < 0)
						{
							ReportErrorId("diag.spawn_target_not_found", {}, {FunctionName});
							return false;
						}
						const FBytecodeFunction& Target =
						    ActiveModule->Functions[static_cast<std::size_t>(FunctionIndex)];
						if (Target.ParamCount != 1)
						{
							ReportErrorId(
							    "diag.spawn_target_must_take_exactly_one_channel_parameter",
							    {},
							    {FunctionName});
							return false;
						}
						if (!Target.bReturnsVoid)
						{
							ReportErrorId("diag.spawn_target_must_return_void", {}, {FunctionName});
							return false;
						}
						if (Target.ClosureTypeName.empty())
						{
							ReportErrorId(
							    "diag.spawn_target_must_take_exactly_one_channel_parameter",
							    {},
							    {FunctionName});
							return false;
						}
						{
							std::vector<std::string> ParamTypes;
							std::string ReturnType;
							if (!Runtime::ParseFunctionType(Target.ClosureTypeName, ParamTypes, ReturnType) || ParamTypes.size() != 1 || !Runtime::IsChannelTypeName(ParamTypes[0]))
							{
								ReportErrorId(
								    "diag.spawn_target_must_take_exactly_one_channel_parameter",
								    {},
								    {FunctionName});
								return false;
							}
							std::string ActualChannelType;
							std::string ActualElementType;
							(*ChannelHandle)->GetTypeNames(ActualChannelType, ActualElementType);
							if (ActualChannelType.empty() || !NativeElementTypeNamesEqual(ActualChannelType, ParamTypes[0]))
							{
								ReportErrorId(
								    "diag.spawn_channel_type_mismatch",
								    {},
								    {FunctionName, ParamTypes[0], ActualChannelType});
								return false;
							}
						}

						auto SharedModule = std::make_shared<FBytecodeModule>(*ActiveModule);
						const FChannelHandle SharedChannel = *ChannelHandle;
						if (bCloseChannelOnExit && (SharedChannel->GetRole() != FChannel::ERole::Ordinary || std::atomic_load(&SharedChannel->TypedTask)))
						{
							ReportError("result tasks require a fresh ordinary channel");
							return false;
						}
						auto TypedState = bCloseChannelOnExit ? std::make_shared<Concurrent::FTypedTaskState>() : nullptr;
						if (TypedState)
						{
							TypedState->Executor = Executor->Identity();
							TypedState->ProgramOwner = ProgramOwner;
							TypedState->Generation = ProgramGeneration;
							std::string ChannelType;
							SharedChannel->GetTypeNames(ChannelType, TypedState->ElementType);
						}
						const std::uint16_t TargetIndex = static_cast<std::uint16_t>(FunctionIndex);
						std::shared_ptr<const std::vector<FNativeFunction>> SharedNatives(SharedModule, ActiveNatives);
						if (ActiveNatives && ActiveNatives != &StandardLibrary::GetBindingCatalog().GetNativeFunctions())
						{
							SharedNatives = std::make_shared<const std::vector<FNativeFunction>>(*ActiveNatives);
						}
						const EExecutionBackend WorkerBackend = RequestedExecutionBackend;
						const FJitPolicy WorkerJitPolicy = JitPolicy;
						const auto WorkerJitBackendFactory = JitBackendFactory;
						const auto WorkerVmBackendFactory = VmExecutionDomain->GetFactory();
						const std::size_t WorkerMaxCallDepth = CallDepthLimit;
						const FExecutionLimits WorkerExecutionLimits = ExecutionLimits;
						const std::shared_ptr<FExecutionControl> WorkerExecutionControl = ExecutionControl;
						const FScriptCapabilities WorkerCapabilities = Capabilities;
						const auto WorkerI18nServices = I18nServices;
						const auto WorkerI18nLimits = I18nLimits;
						const auto WorkerStandardLibraryLimits = StandardLibraryLimits;
						const auto WorkerNumericLimits = NumericLimits;
						(void)GetMemoryContext();
						const auto WorkerMemoryParent = MemoryContext;
						const auto WorkerModuleEnvironment = ModuleEnvironment;
						const bool WorkerUseEnvironmentBindings = bUseEnvironmentBindings;
						const auto* WorkerMemoryBindings = MemoryBindings;
						const FInstanceCallInterceptor WorkerInterceptor = InstanceCallInterceptor;
						const FMutationCallback WorkerMutationCallback = MutationCallback;
						const std::uint64_t WorkerProgramGeneration = ProgramGeneration;
						const std::int32_t TaskId = SpawnedTasks.Spawn(
						    [SharedModule, SharedChannel, TargetIndex, SharedNatives, WorkerBackend,
						        WorkerJitPolicy, WorkerJitBackendFactory, WorkerVmBackendFactory,

						        WorkerMaxCallDepth, WorkerExecutionLimits, WorkerExecutionControl,
						        WorkerCapabilities, WorkerMemoryParent, WorkerMemoryBindings, WorkerInterceptor,
						        WorkerMutationCallback,
						        WorkerProgramGeneration, WorkerModuleEnvironment, WorkerUseEnvironmentBindings, WorkerI18nServices, WorkerI18nLimits, WorkerStandardLibraryLimits, WorkerNumericLimits,
						        bCloseChannelOnExit](std::shared_ptr<std::atomic_bool> WorkerCancellation,
						        std::string& OutError) -> bool
						{
							const auto CloseResultChannel = [&]()
							{
								if (bCloseChannelOnExit && SharedChannel)
								{
									SharedChannel->Close();
								}
							};
							std::ostringstream DiscardOut;
							std::istringstream EmptyIn;
							std::ostringstream DiscardErr;
							VeroScriptVm WorkerVm(DiscardOut, EmptyIn, DiscardErr);
							WorkerVm.SetMemoryContext(WorkerMemoryParent->CreateWorker(), true);
							if (!WorkerVm.SetModuleEnvironment(WorkerModuleEnvironment, OutError, WorkerUseEnvironmentBindings)) return false;
							WorkerVm.SetMemoryHostBindings(WorkerMemoryBindings);
							WorkerVm.SetExecutionBackend(WorkerBackend);
							WorkerVm.SetI18nServices(WorkerI18nServices, WorkerI18nLimits);
							WorkerVm.SetStandardLibraryLimits(WorkerStandardLibraryLimits);
							WorkerVm.SetNumericLimits(WorkerNumericLimits);
							auto TaskJitPolicy = WorkerJitPolicy;
							TaskJitPolicy.bReuseSessionCode = false;
							WorkerVm.SetJitPolicy(TaskJitPolicy);
							if (!WorkerVm.SetVmBackendFactory(WorkerVmBackendFactory, OutError) || !WorkerVm.SetJitBackendFactory(WorkerJitBackendFactory, OutError) ||
							    !WorkerVm.SetJitProgramOwner(SharedModule, OutError))
							{
								CloseResultChannel();
								return false;
							}
							WorkerVm.SetMaxCallDepth(WorkerMaxCallDepth);
							WorkerVm.SetExecutionLimits(WorkerExecutionLimits);
							WorkerVm.SetExecutionControl(WorkerExecutionControl);
							WorkerVm.SetCapabilities(WorkerCapabilities);
							WorkerVm.SetInstanceCallInterceptor(WorkerInterceptor);
							WorkerVm.SetMutationCallback(WorkerMutationCallback);
							WorkerVm.SetProgramGeneration(WorkerProgramGeneration);
							WorkerVm.SetWorkerCancellation(std::move(WorkerCancellation));
							const std::vector<FNativeFunction>* const NativesPtr =
							    SharedNatives ? SharedNatives.get() : nullptr;
							if (!WorkerVm.ExecuteFunction(
							        *SharedModule, TargetIndex, {SharedChannel}, NativesPtr))
							{
								CloseResultChannel();
								const FScriptError& WorkerError = WorkerVm.GetError();
								std::ostringstream Formatted;
								if (!WorkerError.Location.FileLabel.empty())
								{
									Formatted << WorkerError.Location.FileLabel << ':'
									          << WorkerError.Location.Line << ':'
									          << WorkerError.Location.Column << ": ";
								}
								Formatted << (WorkerError.Message.empty() ? "spawned task failed"
								                                          : WorkerError.Message);
								OutError = Formatted.str();
								return false;
							}
							if (WorkerVm.WasExitRequested())
							{
								CloseResultChannel();
								OutError = "spawned task called exit(" +
								    std::to_string(WorkerVm.GetRequestedExitCode()) + ")";
								return false;
							}
							CloseResultChannel();
							return true;
						});
						if (TaskId < 0)
						{
							ReportErrorId("diag.failed_to_spawn_task_thread", {});
							return false;
						}
						if (TypedState)
						{
							TypedState->Id = TaskId;
							TypedState->Ticket = SpawnedTasks.RetainTask(TaskId);
							std::atomic_store(&SharedChannel->TypedTask, TypedState);
						}
						SpawnTaskChannels[TaskId] = SharedChannel;
						Push(TaskId);
					}
					return true;
				}
			case EBuiltinId::JoinTask:
				{
					{
						const auto* TaskIdValue = std::get_if<std::int32_t>(&Arguments[0]);
						if (!TaskIdValue)
						{
							ReportErrorId("diag.join_task_expects_int_task_id");
							return false;
						}
						std::string JoinError;
						bool bTimedOut = false;
						const std::int64_t TimeoutMs = ResolveJoinTimeoutMs();
						bool bJoined = false;
						if (!HasExecutionGovernance())
						{
							bJoined = SpawnedTasks.Join(
							    *TaskIdValue, JoinError, &bTimedOut, TimeoutMs);
						}
						else
						{
							const auto WaitDeadline = MakeSaturatingWaitDeadline(TimeoutMs);
							for (;;)
							{
								const auto Remaining = std::max<std::int64_t>(
								    0,
								    std::chrono::duration_cast<std::chrono::milliseconds>(
								        WaitDeadline - std::chrono::steady_clock::now())
								        .count());
								const std::int64_t Slice = std::min<std::int64_t>(10, Remaining);
								bJoined = SpawnedTasks.Join(
								    *TaskIdValue, JoinError, &bTimedOut, Slice);
								if (bJoined || !bTimedOut || Slice == 0)
								{
									break;
								}
								if (!CheckExecutionGovernance())
								{
									return false;
								}
							}
						}
						if (!bJoined)
						{
							if (bTimedOut)
							{
								// Unblock only this worker's channel (not every live channel).
								const auto ChannelIt = SpawnTaskChannels.find(*TaskIdValue);
								if (ChannelIt != SpawnTaskChannels.end() && ChannelIt->second)
								{
									ChannelIt->second->Close();
								}
								std::string GraceError;
								bool bGraceTimedOut = false;
								(void)SpawnedTasks.Join(
								    *TaskIdValue,
								    GraceError,
								    &bGraceTimedOut,
								    JoinTimeoutGraceMs);
								// Cancellation is cooperative. Keep ownership even when the grace
								// period elapses: detaching here could let a worker invoke copied
								// native callbacks after the owning Session has been destroyed.
								SpawnTaskChannels.erase(*TaskIdValue);
								ReportErrorId("diag.join_task_timed_out");
								return false;
							}
							if (JoinError.empty())
							{
								ReportErrorId("diag.join_task_failed");
							}
							else
							{
								ReportError(std::move(JoinError));
							}
							return false;
						}
						SpawnTaskChannels.erase(*TaskIdValue);
					}
					return true;
				}
			case EBuiltinId::JoinTaskFor:
				{
					{
						const auto* TaskIdValue = std::get_if<std::int32_t>(&Arguments[0]);
						const auto* TimeoutValue = std::get_if<std::int32_t>(&Arguments[1]);
						if (!TaskIdValue || !TimeoutValue || *TimeoutValue < 0)
						{
							ReportError("task await timeout expects (int task_id, non-negative int timeout_ms)");
							return false;
						}
						std::string JoinError;
						bool bTimedOut = false;
						bool bJoined = false;
						if (!HasExecutionGovernance())
						{
							bJoined = SpawnedTasks.Join(
							    *TaskIdValue, JoinError, &bTimedOut, *TimeoutValue);
						}
						else
						{
							const auto WaitDeadline = MakeSaturatingWaitDeadline(*TimeoutValue);
							for (;;)
							{
								const auto Remaining = std::max<std::int64_t>(
								    0,
								    std::chrono::duration_cast<std::chrono::milliseconds>(
								        WaitDeadline - std::chrono::steady_clock::now())
								        .count());
								const std::int64_t Slice = std::min<std::int64_t>(10, Remaining);
								bJoined = SpawnedTasks.Join(
								    *TaskIdValue, JoinError, &bTimedOut, Slice);
								if (bJoined || !bTimedOut || Slice == 0)
								{
									break;
								}
								if (!CheckExecutionGovernance())
								{
									return false;
								}
							}
						}
						if (!bJoined)
						{
							if (bTimedOut)
							{
								Push(false);
								return true;
							}
							ReportError(JoinError.empty()
							        ? "spawned task failed"
							        : std::move(JoinError));
							return false;
						}
						SpawnTaskChannels.erase(*TaskIdValue);
						Push(true);
					}
					return true;
				}
			case EBuiltinId::TaskDone:
				{
					{
						const auto* TaskIdValue = std::get_if<std::int32_t>(&Arguments[0]);
						if (!TaskIdValue)
						{
							ReportError("task done query expects int task id");
							return false;
						}
						bool bFinished = false;
						std::string QueryError;
						if (!SpawnedTasks.IsFinished(*TaskIdValue, bFinished, QueryError))
						{
							ReportError(std::move(QueryError));
							return false;
						}
						Push(bFinished);
					}
					return true;
				}
			case EBuiltinId::CancelRequested:
				{
					Push(WorkerCancellation && WorkerCancellation->load(std::memory_order_acquire));
					return true;
				}
			case EBuiltinId::CancelTask:
				{
					{
						const auto* TaskIdValue = std::get_if<std::int32_t>(&Arguments[0]);
						if (!TaskIdValue)
						{
							ReportError("__cancel_task expects int task id");
							return false;
						}
						SpawnedTasks.RequestCancel(*TaskIdValue);
						const auto ChannelIt = SpawnTaskChannels.find(*TaskIdValue);
						if (ChannelIt != SpawnTaskChannels.end() && ChannelIt->second)
						{
							ChannelIt->second->Close();
						}
						std::string CancelJoinError;
						bool bCancelTimedOut = false;
						(void)SpawnedTasks.Join(
						    *TaskIdValue,
						    CancelJoinError,
						    &bCancelTimedOut,
						    JoinTimeoutGraceMs);
						// Cancellation is cooperative. A worker that ignores its channel is
						// retained and joined during VM cleanup so host callback/user-data
						// lifetimes can never be crossed by a detached script worker.
						(void)bCancelTimedOut;
						SpawnTaskChannels.erase(*TaskIdValue);
					}
					return true;
				}
			case EBuiltinId::WeakCreate:
				{
					{
						FWeakTarget Target;
						if (const auto* Script = std::get_if<FObjectHandle>(&Arguments[0]))
						{
							if (*Script && (*Script)->Kind != EScriptObjectKind::Class)
							{
								ReportError("__weak_create requires a class reference");
								return false;
							}
							Target.Script = *Script;
						}
						else if (const auto* Host =
						             std::get_if<FHostObjectHandle>(&Arguments[0]))
						{
							Target.Host = *Host;
							Target.bHost = true;
						}
						else
						{
							ReportError("__weak_create requires a class reference");
							return false;
						}
						const auto SameTarget = [&Target](const FWeakTarget& Candidate)
						{
							if (Candidate.bHost != Target.bHost)
							{
								return false;
							}
							if (Target.bHost)
							{
								const FHostObjectHandle Left = Candidate.Host.lock();
								const FHostObjectHandle Right = Target.Host.lock();
								return Left && Right && Left == Right;
							}
							const FObjectHandle Left = Candidate.Script.lock();
							const FObjectHandle Right = Target.Script.lock();
							return Left && Right && Left == Right;
						};
						const auto Expired = [](const FWeakTarget& Candidate)
						{
							if (Candidate.bHost) return Candidate.Host.expired();
							const auto Object = Candidate.Script.lock();
							return !Object || Object->bDestroying || Object->bDestroyed;
						};
						std::size_t SlotIndex = WeakTargets.size();
						for (std::size_t Index = 0; Index < WeakTargets.size(); ++Index)
						{
							if (SameTarget(WeakTargets[Index]))
							{
								SlotIndex = Index;
								break;
							}
							if (SlotIndex == WeakTargets.size() && Expired(WeakTargets[Index]))
							{
								SlotIndex = Index;
							}
						}
						if (SlotIndex == WeakTargets.size() && WeakTargets.size() >= static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()))
						{
							ReportError("weak reference table is full");
							return false;
						}
						if (SlotIndex == WeakTargets.size())
						{
							WeakTargets.push_back(std::move(Target));
						}
						else if (!SameTarget(WeakTargets[SlotIndex]))
						{
							std::uint32_t NextGeneration =
							    WeakTargets[SlotIndex].Generation + 1u;
							if (NextGeneration == 0u || NextGeneration > 0x7fffffffu)
							{
								NextGeneration = 1u;
							}
							Target.Generation = NextGeneration;
							WeakTargets[SlotIndex] = std::move(Target);
						}
						const std::uint64_t Encoded =
						    (static_cast<std::uint64_t>(WeakTargets[SlotIndex].Generation) << 32u) | static_cast<std::uint64_t>(SlotIndex);
						Push(static_cast<std::int64_t>(Encoded));
					}
					return true;
				}
			case EBuiltinId::WeakLock:
				{
					{
						const auto* Id = std::get_if<std::int64_t>(&Arguments[0]);
						const std::uint64_t Encoded =
						    Id && *Id >= 0 ? static_cast<std::uint64_t>(*Id) : 0u;
						const std::size_t Index =
						    static_cast<std::size_t>(Encoded & 0xffffffffu);
						const std::uint32_t Generation =
						    static_cast<std::uint32_t>(Encoded >> 32u);
						if (!Id || *Id < 0 || Index >= WeakTargets.size() || Generation == 0u || WeakTargets[Index].Generation != Generation)
						{
							Push(FObjectHandle{});
							return true;
						}
						const FWeakTarget& Target = WeakTargets[Index];
						if (Target.bHost)
						{
							const FHostObjectHandle Locked = Target.Host.lock();
							Push(Locked ? FValue(Locked) : FValue(FHostObjectHandle{}));
						}
						else
						{
							FObjectHandle Locked = Target.Script.lock();
							if (Locked && (Locked->bDestroying || Locked->bDestroyed))
							{
								Locked.reset();
							}
							Push(Locked ? FValue(Locked) : FValue(FObjectHandle{}));
						}
					}
					return true;
				}
			case EBuiltinId::WeakExpired:
				{
					{
						const auto* Id = std::get_if<std::int64_t>(&Arguments[0]);
						const std::uint64_t Encoded =
						    Id && *Id >= 0 ? static_cast<std::uint64_t>(*Id) : 0u;
						const std::size_t Index =
						    static_cast<std::size_t>(Encoded & 0xffffffffu);
						const std::uint32_t Generation =
						    static_cast<std::uint32_t>(Encoded >> 32u);
						bool bExpired = !Id || *Id < 0 || Index >= WeakTargets.size() || Generation == 0u || WeakTargets[Index].Generation != Generation;
						if (!bExpired)
						{
							const FWeakTarget& Target = WeakTargets[Index];
							const auto Object = Target.Script.lock();
							bExpired = Target.bHost ? Target.Host.expired()
							    : !Object || Object->bDestroying || Object->bDestroyed;
						}
						Push(bExpired);
					}
					return true;
				}
			case EBuiltinId::WeakReset:
				{
					{
						const auto* Id = std::get_if<std::int64_t>(&Arguments[0]);
						const std::uint64_t Encoded =
						    Id && *Id >= 0 ? static_cast<std::uint64_t>(*Id) : 0u;
						const std::size_t Index =
						    static_cast<std::size_t>(Encoded & 0xffffffffu);
						const std::uint32_t Generation =
						    static_cast<std::uint32_t>(Encoded >> 32u);
						if (Id && *Id >= 0 && Index < WeakTargets.size() && Generation != 0u && WeakTargets[Index].Generation == Generation)
						{
							WeakTargets[Index].Script.reset();
							WeakTargets[Index].Host.reset();
						}
					}
					return true;
				}
			case EBuiltinId::ReflectionBind:
				{
					const auto* Handle = std::get_if<FObjectHandle>(&Arguments[0]);
					if (!ActiveModule || !Handle || !*Handle || (*Handle)->ProgramOwner != ProgramOwner)
					{
						ReportError("invalid reflection descriptor");
						return false;
					}
					FScriptObject& Descriptor = **Handle;
					BindSourceMetadataDescriptor(Descriptor);
					auto Data = Descriptor.GetDescriptorData();
					const auto Publish = [&]()
					{
						Descriptor.DescriptorData = std::make_shared<const FScriptObjectDescriptorData>(std::move(Data));
						return true;
					};
					Data.ReflectionOwnerType.clear();
					Data.ReflectionDeclaringType.clear();
					Data.ReflectionMemberName.clear();
					Data.ReflectionSignature.clear();
					Data.bReflectionMethod = false;
					Data.bReflectionStatic = false;
					const std::string_view DescriptorBase = std::string_view(
					    Descriptor.GetTypeName())
					                                            .substr(
					                                                0, FindGenericTypeMarker(Descriptor.GetTypeName()));
					const bool bMethod = ModuleMeta::TypeSimpleName(DescriptorBase) == "method_info";
					const bool bField = ModuleMeta::TypeSimpleName(DescriptorBase) == "field_info";
					std::string TypeArgument;
					if (ModuleMeta::TypeSimpleName(DescriptorBase) == "type" && DecodeReflectionDescriptorType(Descriptor.GetTypeName(), "type", TypeArgument))
					{
						Data.ReflectionOwnerType = TypeArgument;
						if (ArgumentCount == 2)
						{
							const auto* Grants = std::get_if<std::string>(&Arguments[1]);
							if (!Grants)
							{
								ReportError("invalid reflection scope binding");
								return false;
							}
							Data.ReflectionSignature = *Grants;
							Data.ReflectionMemberName = "$scope-bound";
						}
						return Publish();
					}
					if ((!bMethod && !bField) || !DecodeReflectionDescriptorType(Descriptor.GetTypeName(), bMethod ? "method_info" : "field_info", TypeArgument))
					{
						return Publish();
					}
					std::uint16_t OwnerField = 0;
					std::uint16_t IndexField = 0;
					if (!ModuleMeta::FindFieldIndex(
					        *ActiveModule, Descriptor.GetTypeName(), "owner_name", OwnerField) ||
					    !ModuleMeta::FindFieldIndex(
					        *ActiveModule, Descriptor.GetTypeName(), "index", IndexField) ||
					    OwnerField >= Descriptor.Fields.size() || IndexField >= Descriptor.Fields.size())
					{
						return Publish();
					}
					const auto* Owner = std::get_if<std::string>(&Descriptor.Fields[OwnerField]);
					const auto* Index = std::get_if<std::int32_t>(&Descriptor.Fields[IndexField]);
					if (!Owner || !Index || *Index < 0)
					{
						return Publish();
					}
					std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
					std::uint16_t InterfaceIndex = static_cast<std::uint16_t>(-1);
					bool bAmbiguous = false;
					const bool bOwnerIsType = ModuleMeta::FindTypeIndex(
					    *ActiveModule, *Owner, TypeIndex, &bAmbiguous);
					bool bInterfaceAmbiguous = false;
					const bool bOwnerIsInterface = !bOwnerIsType && !bAmbiguous && ModuleMeta::FindInterfaceIndex(*ActiveModule, *Owner, InterfaceIndex, &bInterfaceAmbiguous);
					if (bAmbiguous || bInterfaceAmbiguous || (!bOwnerIsType && !bOwnerIsInterface) || (bOwnerIsType && (TypeIndex >= ActiveModule->Types.size() || ActiveModule->Types[TypeIndex].Name != TypeArgument)) || (bOwnerIsInterface && (InterfaceIndex >= ActiveModule->Interfaces.size() || ActiveModule->Interfaces[InterfaceIndex].Name != TypeArgument)))
					{
						return Publish();
					}
					const std::string CanonicalOwner = bOwnerIsType
					    ? ActiveModule->Types[TypeIndex].Name
					    : ActiveModule->Interfaces[InterfaceIndex].Name;
					if (bField)
					{
						if (!bOwnerIsType)
						{
							return Publish();
						}
						std::vector<FLoadedFieldInfo> Fields;
						if (!ModuleMeta::EnumerateFields(
						        *ActiveModule, CanonicalOwner, Fields, false) ||
						    static_cast<std::size_t>(*Index) >= Fields.size() || Fields[*Index].bIsStatic)
						{
							return Publish();
						}
						const FLoadedFieldInfo& Field = Fields[*Index];
						std::uint16_t DeclaringTypeIndex = 0;
						if (!FindDeclaringFieldType(*ActiveModule, TypeIndex,
						        Field.FieldIndex, DeclaringTypeIndex))
						{
							return Publish();
						}
						Data.ReflectionOwnerType = CanonicalOwner;
						Data.ReflectionDeclaringType =
						    ActiveModule->Types[DeclaringTypeIndex].Name;
						Data.ReflectionMemberName = Field.Name;
						Data.ReflectionSignature = Field.RuntimeTypeName;
						return Publish();
					}
					std::vector<FInstanceMethodInfo> Methods;
					std::vector<FStaticMethodInfo> StaticMethods;
					(void)ModuleMeta::EnumerateInstanceMethods(
					    *ActiveModule, CanonicalOwner, Methods, true, true, true);
					(void)ModuleMeta::EnumerateStaticMethods(
					    *ActiveModule, CanonicalOwner, StaticMethods);
					std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
					if (static_cast<std::size_t>(*Index) < Methods.size())
					{
						FunctionIndex = Methods[*Index].FunctionIndex;
					}
					else if (static_cast<std::size_t>(*Index) - Methods.size() < StaticMethods.size())
					{
						FunctionIndex = StaticMethods[*Index - Methods.size()].FunctionIndex;
					}
					if (FunctionIndex < ActiveModule->Functions.size())
					{
						const FBytecodeFunction& Function = ActiveModule->Functions[FunctionIndex];
						Data.ReflectionOwnerType = CanonicalOwner;
						Data.ReflectionMemberName = Function.Name;
						Data.ReflectionSignature = Function.ClosureTypeName;
						Data.bReflectionMethod = true;
						Data.bReflectionStatic = Function.bIsStaticMethod;
					}
					return Publish();
				}
			// These operations share descriptor validation and select behavior by Builtin.
			case EBuiltinId::ReflectionGet:
				{
					[[fallthrough]];
				}
			case EBuiltinId::ReflectionSet:
				{
					[[fallthrough]];
				}
			case EBuiltinId::ReflectionInvoke:
				{
					[[fallthrough]];
				}
			case EBuiltinId::ReflectionInvokeStatic:
				{
					const bool bGet = Builtin == EBuiltinId::ReflectionGet;
					const bool bSet = Builtin == EBuiltinId::ReflectionSet;
					const bool bStatic = Builtin == EBuiltinId::ReflectionInvokeStatic;
					const bool bMethod = !bGet && !bSet;
					const auto* DescriptorHandle = std::get_if<FObjectHandle>(&Arguments[0]);
					if (!ActiveModule || !DescriptorHandle || !*DescriptorHandle || (*DescriptorHandle)->ProgramOwner != ProgramOwner)
					{
						ReportError("invalid or foreign reflection descriptor");
						return false;
					}
					const FScriptObject& Descriptor = **DescriptorHandle;
					std::string TypeArgument;
					if (!DecodeReflectionDescriptorType(
					        Descriptor.GetTypeName(), bMethod ? "method_info" : "field_info",
					        TypeArgument) ||
					    Descriptor.GetDescriptorData().ReflectionOwnerType.empty() || Descriptor.GetDescriptorData().ReflectionOwnerType != TypeArgument || (!bMethod && Descriptor.GetDescriptorData().ReflectionDeclaringType.empty()) || Descriptor.GetDescriptorData().bReflectionMethod != bMethod || (bMethod && Descriptor.GetDescriptorData().bReflectionStatic != bStatic))
					{
						ReportError("reflection descriptor has no valid executable binding");
						return false;
					}
					std::uint16_t OwnerTypeIndex = static_cast<std::uint16_t>(-1);
					std::uint16_t OwnerInterfaceIndex = static_cast<std::uint16_t>(-1);
					bool bOwnerAmbiguous = false;
					const bool bOwnerIsType = ModuleMeta::FindTypeIndex(
					    *ActiveModule, Descriptor.GetDescriptorData().ReflectionOwnerType,
					    OwnerTypeIndex, &bOwnerAmbiguous);
					bool bInterfaceAmbiguous = false;
					const bool bOwnerIsInterface = !bOwnerIsType && !bOwnerAmbiguous && ModuleMeta::FindInterfaceIndex(*ActiveModule, Descriptor.GetDescriptorData().ReflectionOwnerType, OwnerInterfaceIndex, &bInterfaceAmbiguous);
					if (bOwnerAmbiguous || bInterfaceAmbiguous || (!bOwnerIsType && !bOwnerIsInterface) || (bOwnerIsType && OwnerTypeIndex >= ActiveModule->Types.size()) || (bOwnerIsInterface && OwnerInterfaceIndex >= ActiveModule->Interfaces.size()) || (!bMethod && !bOwnerIsType))
					{
						ReportError("reflected owner type is unavailable");
						return false;
					}
					const std::size_t TypeArgumentSlot = bStatic ? 1u : 2u;
					const auto* ExplicitType =
					    std::get_if<std::string>(&Arguments[TypeArgumentSlot]);
					if (!ExplicitType || ExplicitType->empty())
					{
						ReportError("invalid executable reflection type argument");
						return false;
					}
					std::vector<std::string> CallParamTypes;
					std::string CallReturnType;
					if (bMethod && (ExplicitType->rfind(Runtime::ReflectionCallSignaturePrefix, 0) != 0 || !Runtime::ParseFunctionType(std::string_view(*ExplicitType).substr(Runtime::ReflectionCallSignaturePrefix.size()), CallParamTypes, CallReturnType) || std::string_view(*ExplicitType).substr(Runtime::ReflectionCallSignaturePrefix.size()) != Runtime::MakeFunctionTypeName(CallParamTypes, CallReturnType)))
					{
						ReportError("invalid reflected call signature");
						return false;
					}
					FObjectHandle Receiver;
					std::uint16_t ReceiverTypeIndex = 0;
					if (!bStatic)
					{
						FValue* const ReceiverStorage = ResolveRefStorage(Arguments[1]);
						const auto* ReceiverHandle = ReceiverStorage
						    ? std::get_if<FObjectHandle>(ReceiverStorage)
						    : nullptr;
						if (!ReceiverHandle || !*ReceiverHandle || (*ReceiverHandle)->ProgramOwner != ProgramOwner || !ModuleMeta::FindTypeIndex(*ActiveModule, (*ReceiverHandle)->GetTypeName(), ReceiverTypeIndex) || ReceiverTypeIndex >= ActiveModule->Types.size() || (*ReceiverHandle)->Fields.size() != ActiveModule->Types[ReceiverTypeIndex].Fields.size())
						{
							ReportError("reflected receiver is null, foreign, or has an invalid layout");
							return false;
						}
						bool bReceiverMatchesOwner = false;
						if (bOwnerIsType)
						{
							std::unordered_set<std::uint16_t> Visited;
							bReceiverMatchesOwner = HasPublicBasePath(
							    *ActiveModule, ReceiverTypeIndex, OwnerTypeIndex, Visited);
						}
						else
						{
							bReceiverMatchesOwner = ModuleMeta::IsObjectTypeAssignableTo(
							    *ActiveModule, (*ReceiverHandle)->GetTypeName(),
							    Descriptor.GetDescriptorData().ReflectionOwnerType);
						}
						if (!bReceiverMatchesOwner)
						{
							ReportError("reflected receiver is not compatible with the owner type");
							return false;
						}
						Receiver = *ReceiverHandle;
					}
					if (!bMethod)
					{
						std::vector<FLoadedFieldInfo> Fields;
						if (!ModuleMeta::EnumerateFields(
						        *ActiveModule, Descriptor.GetDescriptorData().ReflectionOwnerType, Fields, false))
						{
							ReportError("reflected field metadata is unavailable");
							return false;
						}
						const FLoadedFieldInfo* Matched = nullptr;
						for (const FLoadedFieldInfo& Field : Fields)
						{
							if (!Field.bIsStatic && Field.Name == Descriptor.GetDescriptorData().ReflectionMemberName && Field.RuntimeTypeName == Descriptor.GetDescriptorData().ReflectionSignature)
							{
								std::uint16_t DeclaringTypeIndex = 0;
								if (!FindDeclaringFieldType(*ActiveModule, OwnerTypeIndex,
								        Field.FieldIndex, DeclaringTypeIndex) ||
								    ActiveModule->Types[DeclaringTypeIndex].Name != Descriptor.GetDescriptorData().ReflectionDeclaringType)
								{
									continue;
								}
								if (Matched)
								{
									ReportError("reflected field binding is ambiguous");
									return false;
								}
								Matched = &Field;
							}
						}
						std::uint16_t RuntimeFieldIndex = 0;
						if (!Matched || !MapPublicReflectedFieldIndex(*ActiveModule, ReceiverTypeIndex, OwnerTypeIndex, Matched->FieldIndex, RuntimeFieldIndex) || RuntimeFieldIndex >= Receiver->Fields.size() || !VmDetail::RuntimeTypeNamesEqual(*ExplicitType, Matched->RuntimeTypeName) || !IsPublicReflectedField(*ActiveModule, OwnerTypeIndex, Matched->FieldIndex))
						{
							ReportError("reflected field is unavailable, inaccessible, or has a different type");
							return false;
						}
						std::uint16_t FieldInterfaceIndex = static_cast<std::uint16_t>(-1);
						const bool bInterfaceField = ModuleMeta::FindInterfaceIndex(
						    *ActiveModule, Matched->RuntimeTypeName, FieldInterfaceIndex);
						if (bGet)
						{
							FValue Value;
							FValue Snapshot;
							if (!GetField(*Receiver, RuntimeFieldIndex, Value, {})) return false;
							// An interface holds an already-boxed object. Reading that handle
							// must preserve its identity even when the payload is a struct.
							if (bInterfaceField)
							{
								Snapshot = std::move(Value);
							}
							else if (!CopyLanguageValue(Value, Snapshot, {}, false, Matched->RuntimeTypeName))
							{
								return false;
							}
							Push(std::move(Snapshot));
							return !bHasError;
						}
						const auto* bConstReceiver = std::get_if<bool>(&Arguments[3]);
						const FBytecodeFieldDesc& BoundField =
						    ActiveModule->Types[OwnerTypeIndex].Fields[Matched->FieldIndex];
						if (!bConstReceiver || BoundField.bIsConst || (*bConstReceiver && !BoundField.bIsMutable) || !ValueMatchesElementType(Arguments[4], Matched->RuntimeTypeName, {}))
						{
							ReportError("reflected field assignment violates const or type rules");
							return false;
						}
						if (bInterfaceField)
						{
							return SetField(*Receiver, RuntimeFieldIndex, Arguments[4], {});
						}
						FValue Binding = FThisFieldRef{Receiver, RuntimeFieldIndex};
						if (!AssignClonedValue(Binding, Arguments[4], {}))
						{
							return false;
						}
						StampContainerMutationOrigin(Receiver->Fields.at(RuntimeFieldIndex), *Receiver, RuntimeFieldIndex);
						NotifyFieldMutation(*Receiver, RuntimeFieldIndex);
						return !bHasError;
					}
					const auto It = ActiveModule->FunctionIndices.find(
					    Descriptor.GetDescriptorData().ReflectionMemberName);
					if (It == ActiveModule->FunctionIndices.end() || It->second >= ActiveModule->Functions.size())
					{
						ReportError("reflected method no longer exists");
						return false;
					}
					std::uint16_t FunctionIndex = It->second;
					const FBytecodeFunction& Bound = ActiveModule->Functions[FunctionIndex];
					const std::size_t ArgumentStart = bStatic ? 2u : 4u;
					std::vector<std::string> ParamTypes;
					std::string ReturnType;
					const auto* bConstReceiver = bStatic ? nullptr
					                                     : std::get_if<bool>(&Arguments[3]);
					if (Bound.ClosureTypeName != Descriptor.GetDescriptorData().ReflectionSignature || Bound.bIsStaticMethod != bStatic || Bound.bDeleted || (!bStatic && (!Bound.bIsMethod || !bConstReceiver || (*bConstReceiver && !Bound.bIsConstMethod))) || !Runtime::ParseFunctionType(Bound.ClosureTypeName, ParamTypes, ReturnType) || !VmDetail::RuntimeTypeNamesEqual(ReturnType, CallReturnType) || Bound.ParamCount != ParamTypes.size() || ParamTypes.size() != Arguments.size() - ArgumentStart || CallParamTypes.size() != ParamTypes.size())
					{
						ReportError("reflected method is inaccessible or has an incompatible signature");
						return false;
					}
					std::uint16_t DeclaringTypeIndex = static_cast<std::uint16_t>(-1);
					std::uint16_t DeclaringInterfaceIndex = static_cast<std::uint16_t>(-1);
					std::string DeclaringOwnerName;
					std::size_t DeclaringOwnerLength = 0;
					for (std::size_t Index = 0; Index < ActiveModule->Types.size(); ++Index)
					{
						const std::string& Name = ActiveModule->Types[Index].Name;
						if (Name.size() > DeclaringOwnerLength && Bound.Name.rfind(Name + "::", 0) == 0)
						{
							DeclaringTypeIndex = static_cast<std::uint16_t>(Index);
							DeclaringInterfaceIndex = static_cast<std::uint16_t>(-1);
							DeclaringOwnerName = Name;
							DeclaringOwnerLength = Name.size();
						}
					}
					for (std::size_t Index = 0; Index < ActiveModule->Interfaces.size(); ++Index)
					{
						const std::string& Name = ActiveModule->Interfaces[Index].Name;
						if (Name.size() > DeclaringOwnerLength && Bound.Name.rfind(Name + "::", 0) == 0)
						{
							DeclaringTypeIndex = static_cast<std::uint16_t>(-1);
							DeclaringInterfaceIndex = static_cast<std::uint16_t>(Index);
							DeclaringOwnerName = Name;
							DeclaringOwnerLength = Name.size();
						}
					}
					if (DeclaringOwnerName.empty())
					{
						ReportError("reflected method has an invalid declaring type");
						return false;
					}
					const std::string Leaf = Bound.Name.substr(DeclaringOwnerName.size() + 2);
					const std::string BareMethod = Leaf.substr(0, Leaf.find('#'));
					bool bPublicUsing = false;
					if (DeclaringInterfaceIndex != static_cast<std::uint16_t>(-1))
					{
						std::unordered_set<std::uint16_t> Visited;
						if (Bound.Access != EMemberAccess::Public || !bOwnerIsInterface || !HasInterfaceBasePath(*ActiveModule, OwnerInterfaceIndex, DeclaringInterfaceIndex, Visited))
						{
							ReportError("reflected interface method is not publicly accessible through this interface");
							return false;
						}
					}
					else if (Bound.Access != EMemberAccess::Private)
					{
						for (const FBytecodeTypeDesc::FUsingMember& Using :
						    ActiveModule->Types[OwnerTypeIndex].UsingMembers)
						{
							if (Using.Access == EMemberAccess::Public && Using.MemberName == BareMethod)
							{
								const FBytecodeTypeDesc& Owner =
								    ActiveModule->Types[OwnerTypeIndex];
								for (std::size_t BasePosition = 0;
								    BasePosition < Owner.BaseTypeIndices.size(); ++BasePosition)
								{
									std::unordered_set<std::uint16_t> UsingVisited;
									if (UsingNamesDirectBase(*ActiveModule, Owner,
									        Using, BasePosition) &&
									    HasBasePath(*ActiveModule,
									        Owner.BaseTypeIndices[BasePosition],
									        DeclaringTypeIndex, UsingVisited))
									{
										bPublicUsing = true;
										break;
									}
								}
								if (bPublicUsing)
								{
									break;
								}
							}
						}
					}
					std::unordered_set<std::uint16_t> Visited;
					if (DeclaringInterfaceIndex == static_cast<std::uint16_t>(-1) && !bPublicUsing && (Bound.Access != EMemberAccess::Public || !bOwnerIsType || !HasPublicBasePath(*ActiveModule, OwnerTypeIndex, DeclaringTypeIndex, Visited)))
					{
						ReportError("reflected method is not publicly accessible through this type");
						return false;
					}
					if (Leaf.rfind("~", 0) == 0 || Leaf.rfind(ModuleMeta::TypeSimpleName(DeclaringOwnerName) + "#", 0) == 0)
					{
						ReportError("constructors and destructors cannot be reflected calls");
						return false;
					}
					for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
					{
						FValue& Argument = Arguments[ArgumentStart + Index];
						const bool bRefParam = Runtime::IsReferenceTypeName(ParamTypes[Index]);
						const std::string& CallParamType = CallParamTypes[Index];
						if (CallParamType == "void" || Runtime::IsReferenceTypeName(CallParamType) != bRefParam || (bRefParam && Runtime::IsConstReferenceTypeName(CallParamType) && !Runtime::IsConstReferenceTypeName(ParamTypes[Index])) || (bRefParam && !VmDetail::RuntimeTypeNamesEqual(Runtime::StripReferenceTypeName(CallParamType), Runtime::StripReferenceTypeName(ParamTypes[Index]))))
						{
							ReportError("reflected method argument violates type or ref/const rules");
							return false;
						}
						if (!bRefParam && CallParamType != ParamTypes[Index])
						{
							// A boxed object's concrete type must not authorize a downcast
							// from an interface/base value. The compiler snapshots by the
							// source type, so such a cast could also bypass value copying.
							std::uint16_t TargetTypeIndex = static_cast<std::uint16_t>(-1);
							std::uint16_t SourceTypeIndex = static_cast<std::uint16_t>(-1);
							std::uint16_t TargetInterfaceIndex = static_cast<std::uint16_t>(-1);
							std::uint16_t SourceInterfaceIndex = static_cast<std::uint16_t>(-1);
							bool bSourceAssignable = true;
							std::unordered_set<std::uint16_t> SourceTypeVisited;
							if (ModuleMeta::FindTypeIndex(*ActiveModule, ParamTypes[Index], TargetTypeIndex))
							{
								bSourceAssignable = CallParamType == "nullptr"
								    ? ActiveModule->Types[TargetTypeIndex].Kind == EScriptObjectKind::Class
								    : ModuleMeta::FindTypeIndex(*ActiveModule, CallParamType, SourceTypeIndex) && HasPublicBasePath(*ActiveModule, SourceTypeIndex, TargetTypeIndex, SourceTypeVisited);
							}
							else if (ModuleMeta::FindInterfaceIndex(*ActiveModule, ParamTypes[Index], TargetInterfaceIndex))
							{
								bSourceAssignable = CallParamType == "nullptr" || (ModuleMeta::FindInterfaceIndex(*ActiveModule, CallParamType, SourceInterfaceIndex) ? HasInterfaceBasePath(*ActiveModule, SourceInterfaceIndex, TargetInterfaceIndex, SourceTypeVisited) : ModuleMeta::IsObjectTypeAssignableTo(*ActiveModule, CallParamType, ParamTypes[Index]));
							}
							if (!bSourceAssignable)
							{
								ReportError("reflected method argument requires an invalid implicit object conversion");
								return false;
							}
						}
						FValue* const Storage = bRefParam ? ResolveRefStorage(Argument) : &Argument;
						if (StandardLibrary::IsDiscreteArgument(*ActiveModule, Bound, Index) && !Runtime::IsIntegerValue(Argument))
						{
							ReportErrorId("diag.stdlib_arguments");
							return false;
						}
						if (!Storage || (bRefParam && Storage == &Argument))
						{
							ReportError("reflected reference argument has no valid storage");
							return false;
						}
						const std::string_view CallValueType = Runtime::StripReferenceTypeName(CallParamType);
						const std::string_view ParamValueType = Runtime::StripReferenceTypeName(ParamTypes[Index]);
						if (CallValueType != ParamValueType)
						{
							const auto* NullHandle = std::get_if<FObjectHandle>(Storage);
							const bool bMatchesCallType = CallValueType == "nullptr"
							    ? NullHandle && !*NullHandle
							    : ValueMatchesElementType(*Storage, std::string(CallValueType), {});
							if (!bMatchesCallType)
							{
								ReportError("reflected method argument does not match its call-site type");
								return false;
							}
						}
						if (!ValueMatchesElementType(
						        *Storage, std::string(ParamValueType), {}))
						{
							ReportError("reflected method argument type mismatch");
							return false;
						}
					}
					const auto DispatchTypeIndex = Receiver
					    ? ResolveObjectDispatchTypeIndex(*Receiver, ReceiverTypeIndex)
					    : ReceiverTypeIndex;
					if (!bStatic && DeclaringInterfaceIndex != static_cast<std::uint16_t>(-1))
					{
						std::vector<FInstanceMethodInfo> RuntimeMethods;
						if (!ModuleMeta::EnumerateInstanceMethods(
						        *ActiveModule, ActiveModule->Types[DispatchTypeIndex].Name, RuntimeMethods,
						        true, false, false))
						{
							ReportError("reflected interface receiver methods are unavailable");
							return false;
						}
						bool bResolved = false;
						const auto ConversionLeaf = VmDetail::ConversionDispatchLeaf(Bound);
						for (const FInstanceMethodInfo& Method : RuntimeMethods)
						{
							if (Method.bIsConstructor || Method.bIsDestructor || Method.FunctionIndex >= ActiveModule->Functions.size())
							{
								continue;
							}
							const FBytecodeFunction& Candidate =
							    ActiveModule->Functions[Method.FunctionIndex];
							const bool bConversionMatches = !ConversionLeaf.empty() &&
							    VmDetail::ConversionDispatchLeavesMatch(VmDetail::ConversionDispatchLeaf(Candidate), ConversionLeaf);
							if (Method.Name != BareMethod && !bConversionMatches)
							{
								continue;
							}

							if (!ConversionLeaf.empty() && Candidate.bIsAbstract)
							{
								continue;
							}

							if ((Candidate.ClosureTypeName == Bound.ClosureTypeName || bConversionMatches) &&
							    Candidate.bIsConstMethod == Bound.bIsConstMethod)
							{
								FunctionIndex = Method.FunctionIndex;
								bResolved = true;
								break;
							}
						}
						if (!bResolved)
						{
							ReportError("reflected interface method has no matching implementation");
							return false;
						}
					}
					else if (!bStatic && Bound.bIsVirtual)
					{
						FunctionIndex = static_cast<std::uint16_t>(-1);
						const auto ConversionLeaf = VmDetail::ConversionDispatchLeaf(Bound);
						const std::string Suffix = Bound.Name.substr(
						    ActiveModule->Types[DeclaringTypeIndex].Name.size());
						std::vector<std::uint16_t> Queue{DispatchTypeIndex};
						std::unordered_set<std::uint16_t> Seen;
						for (std::size_t Index = 0; Index < Queue.size(); ++Index)
						{
							const std::uint16_t CandidateTypeIndex = Queue[Index];
							if (CandidateTypeIndex >= ActiveModule->Types.size() || !Seen.insert(CandidateTypeIndex).second)
							{
								continue;
							}
							const FBytecodeTypeDesc& CandidateType =
							    ActiveModule->Types[CandidateTypeIndex];
							if (!ConversionLeaf.empty())
							{
								const auto ConversionIndex = VmDetail::FindVirtualConversionMethod(
								    *ActiveModule, CandidateType.Name, ConversionLeaf);
								if (ConversionIndex != static_cast<std::uint16_t>(-1))
								{
									FunctionIndex = ConversionIndex;
									break;
								}
							}
							const auto Candidate = ActiveModule->FunctionIndices.find(
							    CandidateType.Name + Suffix);
							if (Candidate != ActiveModule->FunctionIndices.end() && Candidate->second < ActiveModule->Functions.size() && ActiveModule->Functions[Candidate->second].bIsVirtual && ActiveModule->Functions[Candidate->second].ClosureTypeName == Bound.ClosureTypeName)
							{
								FunctionIndex = Candidate->second;
								break;
							}
							Queue.insert(Queue.end(), CandidateType.BaseTypeIndices.begin(),
							    CandidateType.BaseTypeIndices.end());
						}
					}
					if (FunctionIndex >= ActiveModule->Functions.size() || ActiveModule->Functions[FunctionIndex].bIsAbstract || ActiveModule->Functions[FunctionIndex].bDeleted)
					{
						ReportError("reflected method has no executable implementation");
						return false;
					}
					const std::size_t DepthBefore = Frames.size();
					const std::size_t StackBefore = OperandStack.size();
					if (bStatic)
					{
						return CallFunctionAt(FunctionIndex,
						           ParamTypes.empty() ? nullptr : &Arguments[ArgumentStart],
						           static_cast<std::uint8_t>(ParamTypes.size()), {}, false) &&
						    JitFinishNestedCall(DepthBefore, StackBefore);
					}
					return InvokeResolvedInstanceMethod(
					    FunctionIndex, Receiver,
					    ParamTypes.empty() ? nullptr : &Arguments[ArgumentStart],
					    static_cast<std::uint8_t>(ParamTypes.size()), {}, true, false);
				}
			case EBuiltinId::ReflectionString:
			case EBuiltinId::ReflectionInt:
			case EBuiltinId::ReflectionBool:
			case EBuiltinId::ReflectionInt64:
				{
					{
						const auto* Owner = std::get_if<std::string>(&Arguments[0]);
						const auto* QueryValue = std::get_if<std::int32_t>(&Arguments[1]);
						const auto* IndexValue = std::get_if<std::int32_t>(&Arguments[2]);
						const auto* DetailValue = std::get_if<std::int32_t>(&Arguments[3]);
						const auto* QueryKey = std::get_if<std::string>(&Arguments[4]);
						if (!ActiveModule || !Owner || !QueryValue || !IndexValue || !DetailValue || !QueryKey || *IndexValue < 0 || *DetailValue < 0)
						{
							ReportError("invalid reflection query arguments");
							return false;
						}
						const auto Query = static_cast<Builtins::EReflectionQuery>(*QueryValue);
						const std::size_t Index = static_cast<std::size_t>(*IndexValue);
						const std::size_t Detail = static_cast<std::size_t>(*DetailValue);
						if (*QueryValue >= static_cast<std::int32_t>(ETemplateReflectionQuery::DeclarationCount) && *QueryValue <= static_cast<std::int32_t>(ETemplateReflectionQuery::ParameterIsEnclosing))
						{
							FValue Result;
							std::string Failure;
							if (!QueryOpenTemplateMetadata(*ActiveModule, *Owner, static_cast<ETemplateReflectionQuery>(*QueryValue),
							        Index, Detail, Result, Failure))
							{
								ReportError(std::move(Failure));
								return false;
							}
							Push(std::move(Result));
							return true;
						}
						std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
						std::uint16_t InterfaceIndex = static_cast<std::uint16_t>(-1);
						bool bTypeAmbiguous = false;
						bool bInterfaceAmbiguous = false;
						const bool bScriptType = ModuleMeta::FindTypeIndex(
						    *ActiveModule, *Owner, TypeIndex, &bTypeAmbiguous);
						const bool bInterface = !bScriptType && ModuleMeta::FindInterfaceIndex(*ActiveModule, *Owner, InterfaceIndex, &bInterfaceAmbiguous);
						const FBytecodeEnumDesc* Enum = nullptr;
						if (!bScriptType && !bInterface)
						{
							for (const FBytecodeEnumDesc& Candidate : ActiveModule->Enums)
							{
								if (Candidate.Name == *Owner || ModuleMeta::TypeSimpleName(Candidate.Name) == *Owner)
								{
									if (Enum)
									{
										ReportError("ambiguous reflected enum type: " + *Owner);
										return false;
									}
									Enum = &Candidate;
								}
							}
						}
						if (bTypeAmbiguous || bInterfaceAmbiguous)
						{
							ReportError("ambiguous reflected type: " + *Owner);
							return false;
						}

						const FDeclMeta* TypeMeta = nullptr;
						std::vector<std::string> BaseNames;
						const bool bPrimitive = *Owner == "bool" || *Owner == "char" || *Owner == "short" || *Owner == "int" || *Owner == "long" || Runtime::IsUnsignedTypeName(*Owner) || *Owner == "int64" || *Owner == "float" || *Owner == "double" || *Owner == "string" || *Owner == "wstring" || *Owner == "nullptr" || *Owner == "void";
						std::string Kind = bPrimitive ? "primitive"
						                              : (Runtime::IsFunctionTypeName(*Owner) ? "function"
						                                                                     : ((Runtime::IsCommonContainerTypeName(*Owner) || Runtime::IsNativeArrayTypeName(*Owner))
						                                                                               ? "container"
						                                                                               : "host"));
						bool bTypeIsAbstract = false;
						if (*Owner == "variant")
						{
							Kind = "variant";
						}
						if (bScriptType)
						{
							const FBytecodeTypeDesc& Type = ActiveModule->Types[TypeIndex];
							Kind = Type.Kind == EScriptObjectKind::Struct ? "struct" : "class";
							bTypeIsAbstract = Type.bIsAbstract;
							TypeMeta = &Type.DeclMeta;
							for (const std::uint16_t BaseIndex : Type.BaseTypeIndices)
							{
								if (BaseIndex < ActiveModule->Types.size())
								{
									BaseNames.push_back(ActiveModule->Types[BaseIndex].Name);
								}
							}
							if (!Type.HostBaseTypeName.empty())
							{
								BaseNames.push_back(Type.HostBaseTypeName);
							}
						}
						else if (bInterface)
						{
							const FBytecodeInterfaceDesc& Type = ActiveModule->Interfaces[InterfaceIndex];
							Kind = "interface";
							bTypeIsAbstract = true;
							TypeMeta = &Type.DeclMeta;
							for (const std::uint16_t BaseIndex : Type.BaseInterfaceIndices)
							{
								if (BaseIndex < ActiveModule->Interfaces.size())
								{
									BaseNames.push_back(ActiveModule->Interfaces[BaseIndex].Name);
								}
							}
						}
						else if (Enum)
						{
							Kind = "enum";
							TypeMeta = &Enum->DeclMeta;
						}

						std::vector<FLoadedFieldInfo> Fields;
						if (bScriptType || bInterface)
						{
							(void)ModuleMeta::EnumerateFields(*ActiveModule, *Owner, Fields, false);
						}
						struct FMethodView
						{
							std::string Name;
							std::string ReturnType;
							std::uint8_t ParamCount = 0;
							std::vector<std::string> ParamTypes;
							std::vector<std::string> ParamNames;
							std::vector<std::uint8_t> ParamFlags;
							bool bStatic = false;
							bool bVirtual = false;
							bool bAbstract = false;
							bool bConst = false;
							bool bDeleted = false;
							bool bConstructor = false;
							bool bDestructor = false;
							FDeclMeta DeclMeta;
							std::uint16_t FunctionIndex = 0xFFFF;
						};
						std::vector<FMethodView> Methods;
						std::vector<FInstanceMethodInfo> InstanceMethods;
						if ((bScriptType || bInterface) && ModuleMeta::EnumerateInstanceMethods(*ActiveModule, *Owner, InstanceMethods, true, true, true))
						{
							for (const FInstanceMethodInfo& Method : InstanceMethods)
							{
								Methods.push_back({Method.Name, Method.ReturnTypeName, Method.ParamCount, Method.ParamTypeNames,
								    Method.ParamNames, Method.ParamFlags, false, Method.bIsVirtual,
								    Method.bIsAbstract, Method.bIsConst, Method.bDeleted,
								    Method.bIsConstructor, Method.bIsDestructor, Method.DeclMeta});
								Methods.back().FunctionIndex = Method.FunctionIndex;
							}
						}
						std::vector<FStaticMethodInfo> StaticMethods;
						if ((bScriptType || bInterface) && ModuleMeta::EnumerateStaticMethods(*ActiveModule, *Owner, StaticMethods))
						{
							for (const FStaticMethodInfo& Method : StaticMethods)
							{
								Methods.push_back({Method.Name, Method.ReturnTypeName, Method.ParamCount, Method.ParamTypeNames,
								    Method.ParamNames, Method.ParamFlags, true, false, false,
								    Method.bIsConst, Method.bDeleted, false, false, Method.DeclMeta});
								Methods.back().FunctionIndex = Method.FunctionIndex;
							}
						}
						std::vector<FFreeFunctionInfo> FreeFunctions;
						ModuleMeta::EnumerateFreeFunctions(*ActiveModule, FreeFunctions, true);
						std::vector<FLoadedGlobalInfo> ReflectedGlobals;
						ModuleMeta::EnumerateGlobals(*ActiveModule, ReflectedGlobals, true, false);
						const auto IsVisibleFromEntry = [&](const std::string_view LinkageName)
						{
							const std::size_t IdentityStart = FindLinkageIdentityStart(LinkageName);
							return IdentityStart == 0 || ActiveModule->EntryModulePath.empty() || LinkageName.substr(0, IdentityStart - 1) == ActiveModule->EntryModulePath;
						};
						std::vector<std::string> VisibleTypeNames;
						VisibleTypeNames.reserve(
						    ActiveModule->Types.size() + ActiveModule->Interfaces.size() + ActiveModule->Enums.size());
						for (const FBytecodeTypeDesc& Type : ActiveModule->Types)
						{
							if (IsVisibleFromEntry(Type.Name) && !Type.Scope.bLocal && Type.Scope.bPublicPath)
							{
								VisibleTypeNames.push_back(Type.Name);
							}
						}
						for (const FBytecodeInterfaceDesc& Interface : ActiveModule->Interfaces)
						{
							if (IsVisibleFromEntry(Interface.Name) && !Interface.Scope.bLocal && Interface.Scope.bPublicPath)
							{
								VisibleTypeNames.push_back(Interface.Name);
							}
						}
						for (const FBytecodeEnumDesc& EnumDesc : ActiveModule->Enums)
						{
							if (IsVisibleFromEntry(EnumDesc.Name) && !EnumDesc.Scope.bLocal && EnumDesc.Scope.bPublicPath)
							{
								VisibleTypeNames.push_back(EnumDesc.Name);
							}
						}

						const auto PushString = [&](std::string Value)
						{
							if (Builtin != EBuiltinId::ReflectionString)
							{
								return false;
							}
							Push(std::move(Value));
							return true;
						};
						const auto PushInt = [&](const std::size_t Value)
						{
							if (Builtin != EBuiltinId::ReflectionInt || Value > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()))
							{
								return false;
							}
							Push(static_cast<std::int32_t>(Value));
							return true;
						};
						const auto PushBool = [&](const bool Value)
						{
							if (Builtin != EBuiltinId::ReflectionBool)
							{
								return false;
							}
							Push(Value);
							return true;
						};
						const auto PushInt64 = [&](const std::int64_t Value)
						{
							if (Builtin != EBuiltinId::ReflectionInt64)
							{
								return false;
							}
							Push(Value);
							return true;
						};
						const auto MetadataString = [](const FDeclMeta& Meta, const std::size_t I, const bool bValue)
						{
							return I < Meta.size() ? (bValue ? Meta[I].second : Meta[I].first) : std::string{};
						};
						const auto FindMetadata = [](const FDeclMeta& Meta, const std::string_view Key)
						    -> const std::string*
						{
							for (const auto& Pair : Meta)
							{
								if (Pair.first == Key)
								{
									return &Pair.second;
								}
							}
							return nullptr;
						};
						const auto HasAnnotation = [&](const FDeclMeta& Meta, std::string_view Name)
						{
							if (!Meta.Annotations.empty())
							{
								const FSourceMetadataSnapshot Empty;
								const auto& Snapshot = ActiveModule->SourceMetadata ? *ActiveModule->SourceMetadata : Empty;
								for (const auto& A : Meta.Annotations)
								{
									if (Snapshot.MatchesAnnotationName(A, Name, true)) return true;
								}

								return false;
							}
							if (!Name.empty() && Name.front() == '@')
							{
								Name.remove_prefix(1);
							}
							for (const auto& Pair : Meta)
							{
								if (Pair.first != "__annot")
								{
									continue;
								}
								std::string_view AnnotationName = Pair.second;
								if (!AnnotationName.empty() && AnnotationName.front() == '@')
								{
									AnnotationName.remove_prefix(1);
								}
								if (AnnotationName == Name)
								{
									return true;
								}
							}
							return false;
						};
						const auto UserFacingParameterType = [](std::string TypeName)
						{
							if (TypeName.rfind("const ref ", 0) == 0)
							{
								TypeName.erase(0, 10);
							}
							else if (TypeName.rfind("ref ", 0) == 0)
							{
								TypeName.erase(0, 4);
							}
							return TypeName;
						};

						bool bHandled = false;
						switch (Query)
						{
						case Builtins::EReflectionQuery::TypeKind:
							{
								bHandled = PushString(Kind);
								break;
							}
						case Builtins::EReflectionQuery::TypeIsAbstract:
							{
								bHandled = PushBool(bTypeIsAbstract);
								break;
							}
						case Builtins::EReflectionQuery::TypeBaseCount:
							{
								bHandled = PushInt(BaseNames.size());
								break;
							}
						case Builtins::EReflectionQuery::TypeBaseName:
							{
								bHandled = Index < BaseNames.size() && PushString(BaseNames[Index]);
								break;
							}
						case Builtins::EReflectionQuery::TypeFieldCount:
							{
								bHandled = PushInt(Fields.size());
								break;
							}
						case Builtins::EReflectionQuery::TypeMethodCount:
							{
								bHandled = PushInt(Methods.size());
								break;
							}
						case Builtins::EReflectionQuery::TypeMetadataCount:
							{
								bHandled = PushInt(TypeMeta ? TypeMeta->size() : 0);
								break;
							}
						case Builtins::EReflectionQuery::TypeMetadataKey:
							{
								bHandled = TypeMeta && Index < TypeMeta->size() && PushString(MetadataString(*TypeMeta, Index, false));
								break;
							}
						case Builtins::EReflectionQuery::TypeMetadataValue:
							{
								bHandled = TypeMeta && Index < TypeMeta->size() && PushString(MetadataString(*TypeMeta, Index, true));
								break;
							}
						case Builtins::EReflectionQuery::TypeEnumMemberCount:
							{
								bHandled = PushInt(Enum ? Enum->Members.size() : 0);
								break;
							}
						case Builtins::EReflectionQuery::TypeEnumMemberName:
							{
								bHandled = Enum && Index < Enum->Members.size() && PushString(Enum->Members[Index].Name);
								break;
							}
						case Builtins::EReflectionQuery::TypeEnumMemberValue:
							{
								bHandled = Enum && Index < Enum->Members.size() && PushInt64(Enum->Members[Index].Value);
								break;
							}
						case Builtins::EReflectionQuery::TypeEnumUnderlyingType:
							{
								bHandled = Enum && PushString(Enum->UnderlyingTypeName);
								break;
							}
						case Builtins::EReflectionQuery::TypeEnumIsScoped:
							{
								bHandled = PushBool(Enum && Enum->bScoped);
								break;
							}
						case Builtins::EReflectionQuery::TypeHasMetadata:
							{
								bHandled = PushBool(TypeMeta && FindMetadata(*TypeMeta, *QueryKey));
								break;
							}
						case Builtins::EReflectionQuery::TypeMetadataByKey:
							{
								bHandled = PushString(TypeMeta && FindMetadata(*TypeMeta, *QueryKey)
								        ? *FindMetadata(*TypeMeta, *QueryKey)
								        : std::string{});
								break;
							}
						case Builtins::EReflectionQuery::TypeHasAnnotation:
							{
								bHandled = PushBool(TypeMeta && HasAnnotation(*TypeMeta, *QueryKey));
								break;
							}
						case Builtins::EReflectionQuery::FieldName:
							{
								bHandled = Index < Fields.size() && PushString(Fields[Index].Name);
								break;
							}
						case Builtins::EReflectionQuery::FieldDeclaredType:
							{
								bHandled = Index < Fields.size() && PushString(Fields[Index].TypeName);
								break;
							}
						case Builtins::EReflectionQuery::FieldRuntimeType:
							{
								bHandled = Index < Fields.size() && PushString(Fields[Index].RuntimeTypeName);
								break;
							}
						case Builtins::EReflectionQuery::FieldIsStatic:
							{
								bHandled = Index < Fields.size() && PushBool(Fields[Index].bIsStatic);
								break;
							}
						case Builtins::EReflectionQuery::FieldIsMutable:
							{
								bHandled = Index < Fields.size() && PushBool(Fields[Index].bIsMutable);
								break;
							}
						case Builtins::EReflectionQuery::FieldIsDeclaredOnType:
							{
								bHandled = Index < Fields.size() && PushBool(Fields[Index].bDeclaredOnType);
								break;
							}
						case Builtins::EReflectionQuery::FieldMetadataCount:
							{
								bHandled = Index < Fields.size() && PushInt(Fields[Index].DeclMeta.size());
								break;
							}
						case Builtins::EReflectionQuery::FieldMetadataKey:
							{
								bHandled = Index < Fields.size() && Detail < Fields[Index].DeclMeta.size() && PushString(MetadataString(Fields[Index].DeclMeta, Detail, false));
								break;
							}
						case Builtins::EReflectionQuery::FieldMetadataValue:
							{
								bHandled = Index < Fields.size() && Detail < Fields[Index].DeclMeta.size() && PushString(MetadataString(Fields[Index].DeclMeta, Detail, true));
								break;
							}
						case Builtins::EReflectionQuery::FieldHasMetadata:
							{
								bHandled = Index < Fields.size() && PushBool(FindMetadata(Fields[Index].DeclMeta, *QueryKey));
								break;
							}

						case Builtins::EReflectionQuery::FieldMetadataByKey:
							{
								bHandled = Index < Fields.size() && PushString(FindMetadata(Fields[Index].DeclMeta, *QueryKey) ? *FindMetadata(Fields[Index].DeclMeta, *QueryKey) : std::string{});
								break;
							}
						case Builtins::EReflectionQuery::FieldHasAnnotation:
							{
								bHandled = Index < Fields.size() && PushBool(HasAnnotation(Fields[Index].DeclMeta, *QueryKey));
								break;
							}
						case Builtins::EReflectionQuery::ModulePath:
							{
								bHandled = PushString(ActiveModule->EntryModulePath);
								break;
							}
						case Builtins::EReflectionQuery::ModuleFunctionCount:
							{
								bHandled = PushInt(FreeFunctions.size());
								break;
							}
						case Builtins::EReflectionQuery::ModuleGlobalCount:
							{
								bHandled = PushInt(ReflectedGlobals.size());
								break;
							}
						case Builtins::EReflectionQuery::ModuleTypeCount:
							{
								bHandled = PushInt(VisibleTypeNames.size());
								break;
							}
						case Builtins::EReflectionQuery::ModuleTypeName:
							{
								bHandled = Index < VisibleTypeNames.size() && PushString(VisibleTypeNames[Index]);
								break;
							}
						case Builtins::EReflectionQuery::FunctionName:
							{
								bHandled = Index < FreeFunctions.size() && PushString(FreeFunctions[Index].Name);
								break;
							}
						case Builtins::EReflectionQuery::FunctionLinkageName:
							{
								bHandled = Index < FreeFunctions.size() && PushString(FreeFunctions[Index].LinkageName);
								break;
							}
						case Builtins::EReflectionQuery::FunctionModulePath:
							{
								bHandled = Index < FreeFunctions.size() && PushString(FreeFunctions[Index].ModulePath);
								break;
							}
						case Builtins::EReflectionQuery::FunctionReturnType:
							{
								bHandled = Index < FreeFunctions.size() && PushString(FreeFunctions[Index].ReturnTypeName);
								break;
							}
						case Builtins::EReflectionQuery::FunctionParameterCount:
							{
								bHandled = Index < FreeFunctions.size() && PushInt(FreeFunctions[Index].ParamCount);
								break;
							}

						case Builtins::EReflectionQuery::FunctionParameterType:
							{
								bHandled = Index < FreeFunctions.size() && Detail < FreeFunctions[Index].ParamTypeNames.size() && PushString(UserFacingParameterType(FreeFunctions[Index].ParamTypeNames[Detail]));
								break;
							}
						case Builtins::EReflectionQuery::FunctionParameterName:
							{
								bHandled = Index < FreeFunctions.size() && Detail < FreeFunctions[Index].ParamNames.size() && PushString(FreeFunctions[Index].ParamNames[Detail]);
								break;
							}
						case Builtins::EReflectionQuery::FunctionParameterIsRef:
							{
								bHandled = Index < FreeFunctions.size() && Detail < FreeFunctions[Index].ParamCount && PushBool(Detail < FreeFunctions[Index].ParamFlags.size() && (FreeFunctions[Index].ParamFlags[Detail] & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0);
								break;
							}
						case Builtins::EReflectionQuery::FunctionParameterIsConst:
							{
								bHandled = Index < FreeFunctions.size() && Detail < FreeFunctions[Index].ParamCount && PushBool(Detail < FreeFunctions[Index].ParamFlags.size() && (FreeFunctions[Index].ParamFlags[Detail] & BytecodeParamFlagConstRef) != 0);
								break;
							}
						case Builtins::EReflectionQuery::FunctionIsDeleted:
							{
								bHandled = Index < FreeFunctions.size() && PushBool(FreeFunctions[Index].bDeleted);
								break;
							}
						case Builtins::EReflectionQuery::FunctionIsExported:
							{
								bHandled = Index < FreeFunctions.size() && PushBool(FreeFunctions[Index].bExported);
								break;
							}
						case Builtins::EReflectionQuery::FunctionMetadataCount:
							{
								bHandled = Index < FreeFunctions.size() && PushInt(FreeFunctions[Index].DeclMeta.size());
								break;
							}
						case Builtins::EReflectionQuery::FunctionMetadataKey:
							{
								bHandled = Index < FreeFunctions.size() && Detail < FreeFunctions[Index].DeclMeta.size() && PushString(MetadataString(FreeFunctions[Index].DeclMeta, Detail, false));
								break;
							}
						case Builtins::EReflectionQuery::FunctionMetadataValue:
							{
								bHandled = Index < FreeFunctions.size() && Detail < FreeFunctions[Index].DeclMeta.size() && PushString(MetadataString(FreeFunctions[Index].DeclMeta, Detail, true));
								break;
							}
						case Builtins::EReflectionQuery::FunctionHasMetadata:
							{
								bHandled = Index < FreeFunctions.size() && PushBool(FindMetadata(FreeFunctions[Index].DeclMeta, *QueryKey));
								break;
							}

						case Builtins::EReflectionQuery::FunctionMetadataByKey:
							{
								bHandled = Index < FreeFunctions.size() && PushString(FindMetadata(FreeFunctions[Index].DeclMeta, *QueryKey) ? *FindMetadata(FreeFunctions[Index].DeclMeta, *QueryKey) : std::string{});
								break;
							}
						case Builtins::EReflectionQuery::FunctionHasAnnotation:
							{
								bHandled = Index < FreeFunctions.size() && PushBool(HasAnnotation(FreeFunctions[Index].DeclMeta, *QueryKey));
								break;
							}
						case Builtins::EReflectionQuery::GlobalName:
							{
								bHandled = Index < ReflectedGlobals.size() && PushString(ReflectedGlobals[Index].Name);
								break;
							}
						case Builtins::EReflectionQuery::GlobalLinkageName:
							{
								bHandled = Index < ReflectedGlobals.size() && PushString(ReflectedGlobals[Index].LinkageName);
								break;
							}
						case Builtins::EReflectionQuery::GlobalModulePath:
							{
								bHandled = Index < ReflectedGlobals.size() && PushString(ReflectedGlobals[Index].ModulePath);
								break;
							}
						case Builtins::EReflectionQuery::GlobalDeclaredType:
							{
								bHandled = Index < ReflectedGlobals.size() && PushString(ReflectedGlobals[Index].DeclaredTypeName);
								break;
							}
						case Builtins::EReflectionQuery::GlobalRuntimeType:
							{
								bHandled = Index < ReflectedGlobals.size() && PushString(ReflectedGlobals[Index].RuntimeTypeName);
								break;
							}
						case Builtins::EReflectionQuery::GlobalIsConstexpr:
							{
								bHandled = Index < ReflectedGlobals.size() && PushBool(ReflectedGlobals[Index].bConstexpr);
								break;
							}
						case Builtins::EReflectionQuery::FunctionIsConstexpr:
							{
								bHandled = Index < FreeFunctions.size() && PushBool(FreeFunctions[Index].bConstexpr);
								break;
							}
						case Builtins::EReflectionQuery::FieldIsConstexpr:
							{
								bHandled = Index < Fields.size() && PushBool(Fields[Index].bConstexpr);
								break;
							}
						case Builtins::EReflectionQuery::GlobalIsConst:
							{
								bHandled = Index < ReflectedGlobals.size() && PushBool(ReflectedGlobals[Index].bConst);
								break;
							}
						case Builtins::EReflectionQuery::GlobalIsExported:
							{
								bHandled = Index < ReflectedGlobals.size() && PushBool(ReflectedGlobals[Index].bExported);
								break;
							}
						case Builtins::EReflectionQuery::GlobalMetadataCount:
							{
								bHandled = Index < ReflectedGlobals.size() && PushInt(ReflectedGlobals[Index].DeclMeta.size());
								break;
							}
						case Builtins::EReflectionQuery::GlobalMetadataKey:
							{
								bHandled = Index < ReflectedGlobals.size() && Detail < ReflectedGlobals[Index].DeclMeta.size() && PushString(MetadataString(ReflectedGlobals[Index].DeclMeta, Detail, false));
								break;
							}
						case Builtins::EReflectionQuery::GlobalMetadataValue:
							{
								bHandled = Index < ReflectedGlobals.size() && Detail < ReflectedGlobals[Index].DeclMeta.size() && PushString(MetadataString(ReflectedGlobals[Index].DeclMeta, Detail, true));
								break;
							}
						case Builtins::EReflectionQuery::GlobalHasMetadata:
							{
								bHandled = Index < ReflectedGlobals.size() && PushBool(FindMetadata(ReflectedGlobals[Index].DeclMeta, *QueryKey));
								break;
							}

						case Builtins::EReflectionQuery::GlobalMetadataByKey:
							{
								bHandled = Index < ReflectedGlobals.size() && PushString(FindMetadata(ReflectedGlobals[Index].DeclMeta, *QueryKey) ? *FindMetadata(ReflectedGlobals[Index].DeclMeta, *QueryKey) : std::string{});
								break;
							}
						case Builtins::EReflectionQuery::GlobalHasAnnotation:
							{
								bHandled = Index < ReflectedGlobals.size() && PushBool(HasAnnotation(ReflectedGlobals[Index].DeclMeta, *QueryKey));
								break;
							}
						case Builtins::EReflectionQuery::EnumMemberName:
							{
								bHandled = Enum && Index < Enum->Members.size() && PushString(Enum->Members[Index].Name);
								break;
							}
						case Builtins::EReflectionQuery::EnumMemberValue:
							{
								bHandled = Enum && Index < Enum->Members.size() && PushInt64(Enum->Members[Index].Value);
								break;
							}
						case Builtins::EReflectionQuery::EnumMemberMetadataCount:
							{
								bHandled = Enum && Index < Enum->Members.size() && PushInt(Enum->Members[Index].DeclMeta.size());
								break;
							}
						case Builtins::EReflectionQuery::EnumMemberMetadataKey:
							{
								bHandled = Enum && Index < Enum->Members.size() && Detail < Enum->Members[Index].DeclMeta.size() && PushString(MetadataString(Enum->Members[Index].DeclMeta, Detail, false));
								break;
							}
						case Builtins::EReflectionQuery::EnumMemberMetadataValue:
							{
								bHandled = Enum && Index < Enum->Members.size() && Detail < Enum->Members[Index].DeclMeta.size() && PushString(MetadataString(Enum->Members[Index].DeclMeta, Detail, true));
								break;
							}
						case Builtins::EReflectionQuery::EnumMemberHasMetadata:
							{
								bHandled = Enum && Index < Enum->Members.size() && PushBool(FindMetadata(Enum->Members[Index].DeclMeta, *QueryKey));
								break;
							}

						case Builtins::EReflectionQuery::EnumMemberMetadataByKey:
							{
								bHandled = Enum && Index < Enum->Members.size() && PushString(FindMetadata(Enum->Members[Index].DeclMeta, *QueryKey) ? *FindMetadata(Enum->Members[Index].DeclMeta, *QueryKey) : std::string{});
								break;
							}
						case Builtins::EReflectionQuery::EnumMemberHasAnnotation:
							{
								bHandled = Enum && Index < Enum->Members.size() && PushBool(HasAnnotation(Enum->Members[Index].DeclMeta, *QueryKey));
								break;
							}

						default:
							{
								if (Index < Methods.size())
								{
									const FMethodView& Method = Methods[Index];
									switch (Query)
									{
									case Builtins::EReflectionQuery::MethodName:
										{
											bHandled = PushString(Method.Name);
											break;
										}
									case Builtins::EReflectionQuery::MethodReturnType:
										{
											bHandled = PushString(Method.ReturnType);
											break;
										}
									case Builtins::EReflectionQuery::MethodParameterCount:
										{
											bHandled = PushInt(Method.ParamCount);
											break;
										}
									case Builtins::EReflectionQuery::MethodParameterType:
										{
											if (Detail < Method.ParamTypes.size())
											{
												bHandled = PushString(UserFacingParameterType(Method.ParamTypes[Detail]));
											}
											break;
										}
									case Builtins::EReflectionQuery::MethodParameterName:
										{
											bHandled = Detail < Method.ParamNames.size() && PushString(Method.ParamNames[Detail]);
											break;
										}
									case Builtins::EReflectionQuery::MethodParameterIsRef:
										{
											bHandled = Detail < Method.ParamFlags.size() && PushBool((Method.ParamFlags[Detail] & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0);
											break;
										}
									case Builtins::EReflectionQuery::MethodParameterIsConst:
										{
											bHandled = Detail < Method.ParamFlags.size() && PushBool((Method.ParamFlags[Detail] & BytecodeParamFlagConstRef) != 0);
											break;
										}
									case Builtins::EReflectionQuery::MethodIsStatic:
										{
											bHandled = PushBool(Method.bStatic);
											break;
										}
									case Builtins::EReflectionQuery::MethodIsVirtual:
										{
											bHandled = PushBool(Method.bVirtual);
											break;
										}
									case Builtins::EReflectionQuery::MethodIsAbstract:
										{
											bHandled = PushBool(Method.bAbstract);
											break;
										}
									case Builtins::EReflectionQuery::MethodIsConstexpr:
										{
											bHandled = Method.FunctionIndex < ActiveModule->Functions.size() && PushBool(ActiveModule->Functions[Method.FunctionIndex].bConstexpr);
											break;
										}
									case Builtins::EReflectionQuery::MethodIsConst:
										{
											bHandled = PushBool(Method.bConst);
											break;
										}
									case Builtins::EReflectionQuery::MethodIsDeleted:
										{
											bHandled = PushBool(Method.bDeleted);
											break;
										}
									case Builtins::EReflectionQuery::MethodIsDefaulted:
									case Builtins::EReflectionQuery::MethodIsAvailable:
									case Builtins::EReflectionQuery::MethodSpecialMemberKind:
									case Builtins::EReflectionQuery::MethodUnavailableReason:
										{
											if (Method.FunctionIndex >= ActiveModule->Functions.size())
											{
												break;
											}
											const auto& Function = ActiveModule->Functions[Method.FunctionIndex];
											if (Query == Builtins::EReflectionQuery::MethodIsDefaulted)
											{
												bHandled = PushBool(Function.bDefaulted);
											}
											else if (Query == Builtins::EReflectionQuery::MethodIsAvailable)
											{
												bHandled = PushBool(!Function.bDeleted && !Function.bIsAbstract && Function.UnavailableReason.empty());
											}
											else if (Query == Builtins::EReflectionQuery::MethodUnavailableReason)
											{
												bHandled = PushString(Function.UnavailableReason);
											}
											else
											{
												const auto SpecialKind = Function.SpecialMemberKind;
												bHandled = PushString(SpecialKind == ESpecialMemberKind::CopyConstructor ? "copy_constructor"
												        : SpecialKind == ESpecialMemberKind::CopyAssignment              ? "copy_assignment"
												        : SpecialKind == ESpecialMemberKind::Equality                    ? "equality"
												        : SpecialKind == ESpecialMemberKind::Inequality                  ? "inequality"
												                                                                         : "");
											}
											break;
										}
									case Builtins::EReflectionQuery::MethodIsConstructor:
										{
											bHandled = PushBool(Method.bConstructor);
											break;
										}
									case Builtins::EReflectionQuery::MethodIsDestructor:
										{
											bHandled = PushBool(Method.bDestructor);
											break;
										}
									case Builtins::EReflectionQuery::MethodMetadataCount:
										{
											bHandled = PushInt(Method.DeclMeta.size());
											break;
										}
									case Builtins::EReflectionQuery::MethodMetadataKey:
										{
											bHandled = Detail < Method.DeclMeta.size() && PushString(MetadataString(Method.DeclMeta, Detail, false));
											break;
										}
									case Builtins::EReflectionQuery::MethodMetadataValue:
										{
											bHandled = Detail < Method.DeclMeta.size() && PushString(MetadataString(Method.DeclMeta, Detail, true));
											break;
										}
									case Builtins::EReflectionQuery::MethodHasMetadata:
										{
											bHandled = PushBool(FindMetadata(Method.DeclMeta, *QueryKey));
											break;
										}
									case Builtins::EReflectionQuery::MethodMetadataByKey:
										{
											bHandled = PushString(FindMetadata(Method.DeclMeta, *QueryKey)
											        ? *FindMetadata(Method.DeclMeta, *QueryKey)
											        : std::string{});
											break;
										}
									case Builtins::EReflectionQuery::MethodHasAnnotation:
										{
											bHandled = PushBool(HasAnnotation(Method.DeclMeta, *QueryKey));
											break;
										}
									default:
										{
											break;
										}
									}
								}
								break;
							}
						}
						if (!bHandled)
						{
							ReportError("invalid or out-of-range reflection query");
							return false;
						}
					}
					return true;
				}
			default:
				{
					ReportErrorId("diag.unknown_builtin");
					return false;
				}
			}
		};
		const auto Cleanup = [&]() -> bool
		{
			const bool HadError = bHasError;
			FScriptError Pending = std::move(Error), Failure;
			bHasError = false;
			Error = {};
			bool Ok = true;
			for (std::size_t I = Arguments.size(); I > 0; --I)
			{
				if (!ReleaseValue(Arguments[I - 1], {}))
				{
					VmDetail::RecordCleanupFailure(Ok, Failure, bHasError, Error);
				}
				Arguments[I - 1] = std::int32_t{0};
			}
			return VmDetail::FinishCleanup(Ok, HadError, std::move(Pending), std::move(Failure), bExitRequested, bHasError, Error);
		};
		bool Ok = false;
		try
		{
			Ok = Dispatch();
		}
		catch (...)
		{
			(void)Cleanup();
			throw;
		}
		return Cleanup() && Ok;
	}

	bool VeroScriptVm::IsBuiltinAllowed(
	    const EBuiltinId Builtin,
	    std::string_view& OutCapability) const
	{
		auto Requires = [&OutCapability](const bool bAllowed, const std::string_view Name)
		{
			if (!bAllowed)
			{
				OutCapability = Name;
				return false;
			}
			return true;
		};

		switch (Builtin)
		{
		case EBuiltinId::ReadFile:
		case EBuiltinId::ReadBytes:
			{
				return Requires(Capabilities.bAllowFileRead, "file read");
			}
		case EBuiltinId::Exists:
			{
				return Requires(Capabilities.bAllowFileRead, "file read");
			}
		case EBuiltinId::IsFile:
			{
				return Requires(Capabilities.bAllowFileRead, "file read");
			}
		case EBuiltinId::IsDirectory:
			{
				return Requires(Capabilities.bAllowFileRead, "file read");
			}
		case EBuiltinId::ListDir:
			{
				return Requires(Capabilities.bAllowFileRead, "file read");
			}
		case EBuiltinId::Cwd:
			{
				return Requires(Capabilities.bAllowFileRead, "file read");
			}
		case EBuiltinId::WriteFile:
		case EBuiltinId::WriteBytes:
			{
				return Requires(Capabilities.bAllowFileWrite, "file write");
			}
		case EBuiltinId::AppendFile:
		case EBuiltinId::AppendBytes:
			{
				return Requires(Capabilities.bAllowFileWrite, "file write");
			}
		case EBuiltinId::MkDir:
			{
				return Requires(Capabilities.bAllowFileWrite, "file write");
			}
		case EBuiltinId::DeleteFile:
			{
				return Requires(Capabilities.bAllowFileDelete, "file delete");
			}
		case EBuiltinId::RemoveDir:
			{
				return Requires(Capabilities.bAllowFileDelete, "file delete");
			}
		case EBuiltinId::GetEnv:
		case EBuiltinId::TryGetEnv:
			{
				return Requires(Capabilities.bAllowEnvironmentRead, "environment read");
			}
		case EBuiltinId::SetEnv:
		case EBuiltinId::UnsetEnv:
			{
				return Requires(Capabilities.bAllowEnvironmentWrite, "environment write");
			}
		case EBuiltinId::ChDir:
			{
				return Requires(Capabilities.bAllowEnvironmentWrite, "environment write") && Requires(Capabilities.bAllowProcessDirectoryChange, "process directory change");
			}
		case EBuiltinId::Print:
			{
				return Requires(Capabilities.bAllowConsoleOutput, "console output");
			}
		case EBuiltinId::Eprint:
			{
				return Requires(Capabilities.bAllowConsoleOutput, "console output");
			}
		case EBuiltinId::ConsoleWrite:
			{
				return Requires(Capabilities.bAllowConsoleOutput, "console output");
			}
		case EBuiltinId::ConsoleError:
			{
				return Requires(Capabilities.bAllowConsoleOutput, "console output");
			}
		case EBuiltinId::ConsoleFlush:
			{
				return Requires(Capabilities.bAllowConsoleOutput, "console output");
			}
		case EBuiltinId::ConsoleSetColor:
		case EBuiltinId::ConsoleResetStyle:
		case EBuiltinId::ConsoleClearLine:
		case EBuiltinId::ConsoleMoveCursor:
		case EBuiltinId::ConsoleSetCursorVisible:
		case EBuiltinId::ConsoleClear:
			{
				return Requires(Capabilities.bAllowConsoleOutput, "console output");
			}
		case EBuiltinId::ReadLine:
		case EBuiltinId::TryReadLine:
		case EBuiltinId::TryReadToken:
			{
				return Requires(Capabilities.bAllowConsoleInput && !ExecutionCleanup, "console input");
			}
		case EBuiltinId::ReadToken:
			{
				return Requires(Capabilities.bAllowConsoleInput && !ExecutionCleanup, "console input");
			}
		case EBuiltinId::ReadString:
			{
				return Requires(Capabilities.bAllowConsoleInput && !ExecutionCleanup, "console input");
			}
		case EBuiltinId::ReadInt:
			{
				return Requires(Capabilities.bAllowConsoleInput && !ExecutionCleanup, "console input");
			}
		case EBuiltinId::ReadDouble:
			{
				return Requires(Capabilities.bAllowConsoleInput && !ExecutionCleanup, "console input");
			}
		case EBuiltinId::ReadBool:
			{
				return Requires(Capabilities.bAllowConsoleInput && !ExecutionCleanup, "console input");
			}
		case EBuiltinId::Spawn:
			{
				return Requires(Capabilities.bAllowTaskSpawn && !ExecutionCleanup, "task spawn");
			}
		case EBuiltinId::SpawnResult:
			{
				return Requires(Capabilities.bAllowTaskSpawn && !ExecutionCleanup, "task spawn");
			}
		case EBuiltinId::SpawnCallable:
			{
				return Requires(Capabilities.bAllowTaskSpawn && !ExecutionCleanup, "task spawn");
			}
		case EBuiltinId::SpawnCallableResult:
			{
				return Requires(Capabilities.bAllowTaskSpawn && !ExecutionCleanup, "task spawn");
			}
		default:
			{
				return true;
			}
		}
	}
}
