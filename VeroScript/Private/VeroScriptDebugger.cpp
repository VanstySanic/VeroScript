// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptDebugger.h"
#include "VeroScriptVariantInternal.h"

#include "VeroScriptDebugEval.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptVm.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <unordered_set>

namespace VeroScript
{
	namespace
	{
		thread_local VeroScriptVm* GMemoryInspectionVm = nullptr;
		struct FMemoryInspectionScope
		{
			explicit FMemoryInspectionScope(VeroScriptVm* Vm)
			{
				GMemoryInspectionVm = Vm;
			}

			~FMemoryInspectionScope()
			{
				GMemoryInspectionVm = Previous;
			}

			VeroScriptVm* Previous = GMemoryInspectionVm;
		};
		const FValue& VisibleDebugValue(const FValue& Value)
		{
			if (const FValue* const Visible = UnwrapClosureReferenceCell(&Value))
			{
				return *Visible;
			}
			static const FValue InvalidReference = std::string("<invalid reference>");
			return InvalidReference;
		}

		FDebugVariable MakeDebugVariable(std::string Name, const FValue& Value)
		{
			const FValue& VisibleValue = VisibleDebugValue(Value);
			FDebugVariable Variable;
			Variable.Name = std::move(Name);
			Variable.Value = Runtime::ValueToString(VisibleValue);
			Variable.RawValue = VisibleValue;
			Variable.bHasRawValue = true;
			if (GMemoryInspectionVm)
			{
				Variable.Memory = GMemoryInspectionVm->DebugInspectMemory(VisibleValue);
			}
			return Variable;
		}

		FDebugVariable MakeMutableDebugVariable(
		    std::string Name,
		    const FValue& Value,
		    const FValue& Owner,
		    const FDebugVariable::ETargetKind TargetKind,
		    const std::size_t TargetIndex = 0,
		    FMapEntryKey TargetKey = std::int32_t{0})
		{
			FDebugVariable Variable = MakeDebugVariable(std::move(Name), Value);
			Variable.TargetKind = TargetKind;
			Variable.OwnerValue = Owner;
			Variable.TargetIndex = TargetIndex;
			Variable.TargetKey = std::move(TargetKey);
			if (GMemoryInspectionVm)
			{
				Variable.Memory = GMemoryInspectionVm->DebugInspectMemory(Value, const_cast<FValue*>(&Value));
			}
			return Variable;
		}

		bool IsDebugExpressionIdentifier(const std::string_view Name)
		{
			if (Name.empty() || !((Name.front() >= 'A' && Name.front() <= 'Z') || (Name.front() >= 'a' && Name.front() <= 'z') || Name.front() == '_'))
			{
				return false;
			}
			for (const char Char : Name)
			{
				if (!((Char >= 'A' && Char <= 'Z') || (Char >= 'a' && Char <= 'z') || (Char >= '0' && Char <= '9') || Char == '_'))
				{
					return false;
				}
			}
			return true;
		}

		bool DebugExpressionReferencesIdentifier(
		    const std::string_view Expression,
		    const std::string_view Name)
		{
			const auto IsIdentifierChar = [](const char Char)
			{
				return (Char >= 'A' && Char <= 'Z') || (Char >= 'a' && Char <= 'z') || (Char >= '0' && Char <= '9') || Char == '_';
			};
			std::size_t Offset = 0;
			while ((Offset = Expression.find(Name, Offset)) != std::string_view::npos)
			{
				const std::size_t End = Offset + Name.size();
				if ((Offset == 0 || !IsIdentifierChar(Expression[Offset - 1])) && (End == Expression.size() || !IsIdentifierChar(Expression[End])))
				{
					return true;
				}
				Offset = End;
			}
			return false;
		}

		std::string EscapeDebugString(const std::string_view Value)
		{
			std::string Escaped;
			Escaped.reserve(Value.size());
			for (const char Char : Value)
			{
				switch (Char)
				{
				case '\\':
					{
						Escaped += "\\\\";
						break;
					}
				case '"':
					{
						Escaped += "\\\"";
						break;
					}
				case '\n':
					{
						Escaped += "\\n";
						break;
					}
				case '\r':
					{
						Escaped += "\\r";
						break;
					}
				case '\t':
					{
						Escaped += "\\t";
						break;
					}
				default:
					{
						Escaped.push_back(Char);
						break;
					}
				}
			}
			return Escaped;
		}

		std::string DebugExpressionTypeName(const FValue& Value, const FBytecodeModule* Module = nullptr)
		{
			std::string TypeName = DebugEval::TypeNameOfValue(Value);
			if (Module)
			{
				// Enum identities nested in a container are runtime metadata, not
				// source tokens. Rebuild only complete enum type tokens for the probe.
				for (const auto& Enum : Module->Enums)
				{
					const auto Start = FindLinkageIdentityStart(Enum.Name);
					if (!Start)
					{
						continue;
					}
					const auto Name = Enum.Name.substr(Start);
					if (!IsDebugExpressionIdentifier(Name))
					{
						continue;
					}
					std::size_t Position = 0;
					while ((Position = TypeName.find(Enum.Name, Position)) != std::string::npos)
					{
						const auto End = Position + Enum.Name.size();
						const bool Before = Position == 0 || std::string_view("<,( ").find(TypeName[Position - 1]) != std::string_view::npos;
						const bool After = End == TypeName.size() ||
						    std::string_view(">,)[ ").find(TypeName[End]) != std::string_view::npos;
						if (Before && After)
						{
							TypeName.replace(Position, Enum.Name.size(), Name);
						}
						Position += Before && After ? Name.size() : Enum.Name.size();
					}
				}
			}
			const std::size_t IdentityStart = FindLinkageIdentityStart(TypeName);
			if (IdentityStart > 0)
			{
				TypeName.erase(0, IdentityStart);
			}
			return TypeName;
		}

		bool IsDebugEvalBindableValue(const FValue& Value)
		{
			return !std::holds_alternative<FObjectHandle>(Value) && !std::holds_alternative<FHostObjectHandle>(Value) &&
			    !std::holds_alternative<FFunctionHandle>(Value);
		}

		std::string DebugDeclarationName(std::string Name)
		{
			const std::size_t IdentityStart = FindLinkageIdentityStart(Name);
			if (IdentityStart > 0)
			{
				Name.erase(0, IdentityStart);
			}
			return IsDebugExpressionIdentifier(Name) ? Name : std::string{};
		}

		std::string BuildDebugEvalPrelude(const FBytecodeModule* const Module)
		{
			if (!Module)
			{
				return {};
			}
			std::ostringstream Source;
			std::unordered_set<std::string> DeclaredNames;
			for (const FBytecodeEnumDesc& Enum : Module->Enums)
			{
				const std::string Name = DebugDeclarationName(Enum.Name);
				if (Name.empty() || !DeclaredNames.insert(Name).second)
				{
					continue;
				}
				Source << "enum ";
				if (Enum.bScoped)
				{
					Source << "class ";
				}
				Source << Name << " { ";
				bool bHasMember = false;
				for (std::size_t Index = 0; Index < Enum.Members.size(); ++Index)
				{
					const std::string MemberName =
					    DebugDeclarationName(Enum.Members[Index].Name);
					if (MemberName.empty())
					{
						continue;
					}
					if (bHasMember)
					{
						Source << ", ";
					}
					Source << MemberName << " = " << Enum.Members[Index].Value;
					bHasMember = true;
				}
				Source << " };\n";
			}
			return Source.str();
		}

		bool IsPotentiallyMutatingDebugExpression(const std::string_view Expression)
		{
			static constexpr std::string_view MutatingMethods[] = {
			    "push", "pop", "clear", "remove", "remove_at", "add",
			    "push_front", "push_back", "pop_front", "pop_back", "send", "close"};
			char Quote = '\0';
			bool bEscape = false;
			for (std::size_t Index = 0; Index < Expression.size(); ++Index)
			{
				const char Char = Expression[Index];
				if (Quote != '\0')
				{
					if (bEscape)
					{
						bEscape = false;
					}
					else if (Char == '\\')
					{
						bEscape = true;
					}
					else if (Char == Quote)
					{
						Quote = '\0';
					}
					continue;
				}
				if (Char == '"' || Char == '\'')
				{
					Quote = Char;
					continue;
				}
				if ((Char == '+' || Char == '-') && Index + 1 < Expression.size() && Expression[Index + 1] == Char)
				{
					return true;
				}
				if (Char == '.')
				{
					std::size_t NameBegin = Index + 1;
					while (NameBegin < Expression.size() && (Expression[NameBegin] == ' ' || Expression[NameBegin] == '\t'))
					{
						++NameBegin;
					}
					std::size_t NameEnd = NameBegin;
					while (NameEnd < Expression.size() && ((Expression[NameEnd] >= 'a' && Expression[NameEnd] <= 'z') || Expression[NameEnd] == '_'))
					{
						++NameEnd;
					}
					std::size_t OpenParen = NameEnd;
					while (OpenParen < Expression.size() && (Expression[OpenParen] == ' ' || Expression[OpenParen] == '\t'))
					{
						++OpenParen;
					}
					if (OpenParen < Expression.size() && Expression[OpenParen] == '(')
					{
						const std::string_view Method =
						    Expression.substr(NameBegin, NameEnd - NameBegin);
						for (const std::string_view MutatingMethod : MutatingMethods)
						{
							if (Method == MutatingMethod)
							{
								return true;
							}
						}
					}
				}
				if (Char != '=')
				{
					continue;
				}
				const char Before = Index > 0 ? Expression[Index - 1] : '\0';
				const char After =
				    Index + 1 < Expression.size() ? Expression[Index + 1] : '\0';
				if (Before != '=' && Before != '!' && Before != '<' && Before != '>' && After != '=')
				{
					return true;
				}
			}
			return false;
		}

		std::string_view TrimDebugExpression(std::string_view Expression)
		{
			while (!Expression.empty() && (Expression.front() == ' ' || Expression.front() == '\t' || Expression.front() == '\r' || Expression.front() == '\n'))
			{
				Expression.remove_prefix(1);
			}
			while (!Expression.empty() && (Expression.back() == ' ' || Expression.back() == '\t' || Expression.back() == '\r' || Expression.back() == '\n'))
			{
				Expression.remove_suffix(1);
			}
			return Expression;
		}
	}

	void VeroScriptDebugger::SetStopCallback(FStopCallback Callback)
	{
		StopCallback = std::move(Callback);
	}

	void VeroScriptDebugger::SetRunMode(const EDebugRunMode Mode)
	{
		RunMode = Mode;
		bCaptureStepDepth = false;
		switch (Mode)
		{
		case EDebugRunMode::StepIn:
			{
				bStopOnNext = true;
				StepTargetDepth = 0;
				break;
			}
		case EDebugRunMode::StepOver:
			{
				bStopOnNext = true;
				if (AttachedVm)
				{
					StepTargetDepth = AttachedVm->DebugGetCallDepth();
				}
				else
				{
					bCaptureStepDepth = true;
					StepTargetDepth = 0;
				}
				break;
			}
		case EDebugRunMode::StepOut:
			{
				bStopOnNext = true;
				if (AttachedVm)
				{
					const std::size_t Depth = AttachedVm->DebugGetCallDepth();
					StepTargetDepth = Depth > 0 ? Depth - 1 : 0;
				}
				else
				{
					bCaptureStepDepth = true;
					StepTargetDepth = 0;
				}
				break;
			}
		case EDebugRunMode::Continue:
		default:
			{
				bStopOnNext = false;
				StepTargetDepth = 0;
				break;
			}
		}
	}

	void VeroScriptDebugger::RequestPause() noexcept
	{
		bPauseRequested.store(true, std::memory_order_release);
	}

	void VeroScriptDebugger::ResetPauseRequest() noexcept
	{
		bPauseRequested.store(false, std::memory_order_release);
	}

	int VeroScriptDebugger::SetBreakpoint(
	    std::string FileLabel,
	    const std::uint32_t Line,
	    std::string Condition)
	{
		return AddBreakpoint(std::move(FileLabel), Line, std::move(Condition), false);
	}

	int VeroScriptDebugger::SetExactBreakpoint(
	    std::string FileLabel,
	    const std::uint32_t Line,
	    std::string Condition)
	{
		return AddBreakpoint(std::move(FileLabel), Line, std::move(Condition), true);
	}

	void VeroScriptDebugger::ClearBreakpoints()
	{
		Breakpoints.clear();
	}

	std::vector<FDebugStackFrame> VeroScriptDebugger::GetStackTrace() const
	{
		std::vector<FDebugStackFrame> Frames;
		if (!AttachedVm || !AttachedVm->DebugGetModule())
		{
			return Frames;
		}
		const FBytecodeModule& Module = *AttachedVm->DebugGetModule();
		const std::size_t Depth = AttachedVm->DebugGetCallDepth();
		Frames.reserve(Depth);
		for (std::size_t Index = Depth; Index > 0; --Index)
		{
			const VeroScriptVm::FDebugCallFrameView Frame = AttachedVm->DebugGetFrame(Index - 1);
			FDebugStackFrame Out;
			Out.FunctionName = Frame.Function ? Frame.Function->Name : "<init>";
			Out.InstructionPointer = Frame.InstructionPointer;
			const FSourceLocation Location = ResolveLocation(
			    Module,
			    Frame.Function,
			    Frame.InstructionPointer);
			Out.FileLabel = std::string(Location.FileLabel);
			Out.Line = Location.Line;
			Out.Column = Location.Column;
			Frames.push_back(std::move(Out));
		}
		return Frames;
	}

	std::vector<FDebugVariable> VeroScriptDebugger::GetLocals(const std::size_t FrameIndex) const
	{
		std::vector<FDebugVariable> Variables;
		if (!AttachedVm)
		{
			return Variables;
		}
		const std::size_t Depth = AttachedVm->DebugGetCallDepth();
		if (FrameIndex >= Depth)
		{
			return Variables;
		}
		const std::size_t InternalIndex = Depth - 1 - FrameIndex;
		const VeroScriptVm::FDebugCallFrameView Frame = AttachedVm->DebugGetFrame(InternalIndex);
		if (!Frame.Locals)
		{
			return Variables;
		}
		for (std::size_t Slot = 0; Slot < Frame.Locals->size(); ++Slot)
		{
			if (!Frame.Function || Slot >= Frame.Function->LocalNames.size() || !IsDebugExpressionIdentifier(Frame.Function->LocalNames[Slot]))
			{
				continue;
			}
			FDebugVariable Variable;
			Variable.Name = Frame.Function->LocalNames[Slot];
			if (Slot >= Frame.Function->ParamCount && Slot - Frame.Function->ParamCount < Frame.Function->Captures.size())
			{
				Variable.bReadOnly = !Frame.Function->Captures[Slot - Frame.Function->ParamCount].bWritable;
			}
			const FValue& VisibleValue = VisibleDebugValue((*Frame.Locals)[Slot]);
			Variable.Value = Runtime::ValueToString(VisibleValue);
			Variable.RawValue = VisibleValue;
			Variable.bHasRawValue = true;
			Variable.RootFrameIndex = InternalIndex;
			Variable.RootLocalSlot = Slot;
			Variable.Memory =
			    AttachedVm->DebugInspectMemory(VisibleValue, const_cast<FValue*>(&(*Frame.Locals)[Slot]));
			Variables.push_back(std::move(Variable));
		}
		for (const auto& Static : AttachedVm->DebugGetLocalStatics(InternalIndex))
		{
			FDebugVariable Variable;
			Variable.Name = Static.Name;
			Variable.bReadOnly = true;
			if (Static.Value)
			{
				Variable.RawValue = VisibleDebugValue(*Static.Value);
				Variable.Value = Runtime::ValueToString(Variable.RawValue);
				Variable.bHasRawValue = true;
				Variable.Memory = AttachedVm->DebugInspectMemory(*Static.Value);
			}
			else
			{
				Variable.Value = Static.bInitializing ? "<initializing>" : "<uninitialized>";
			}
			Variables.push_back(std::move(Variable));
		}
		return Variables;
	}

	std::vector<FDebugVariable> VeroScriptDebugger::GetChildren(
	    const FDebugVariable& Variable) const
	{
		auto Children = GetChildrenImpl(Variable);
		if (Variable.bReadOnly)
		{
			for (auto& Child : Children)
			{
				Child.bReadOnly = true;
				Child.TargetKind = FDebugVariable::ETargetKind::None;
			}
		}
		return Children;
	}

	FDebugEvaluateResult VeroScriptDebugger::Evaluate(
	    const std::string_view Expression,
	    const std::size_t FrameIndex) const
	{
		FDebugEvaluateResult Result;
		if (!AttachedVm)
		{
			Result.Error = "debugger is not attached to a paused VM";
			return Result;
		}
		const std::size_t Depth = AttachedVm->DebugGetCallDepth();
		if (FrameIndex >= Depth)
		{
			Result.Error = "invalid frame index";
			return Result;
		}
		const std::size_t InternalIndex = Depth - 1 - FrameIndex;
		const VeroScriptVm::FDebugCallFrameView Frame = AttachedVm->DebugGetFrame(InternalIndex);
		if (!Frame.Locals)
		{
			Result.Error = "frame has no locals";
			return Result;
		}
		if (IsPotentiallyMutatingDebugExpression(Expression))
		{
			Result.Error = "debug evaluate expressions are read-only; use setVariable to edit";
			return Result;
		}
		const std::string_view TrimmedExpression = TrimDebugExpression(Expression);
		const auto LocalStatics = AttachedVm->DebugGetLocalStatics(InternalIndex);
		if (Frame.Function)
		{
			for (std::size_t Slot = 0;
			    Slot < Frame.Locals->size() && Slot < Frame.Function->LocalNames.size();
			    ++Slot)
			{
				if (Frame.Function->LocalNames[Slot] != TrimmedExpression)
				{
					continue;
				}
				const FValue& LocalValue = VisibleDebugValue((*Frame.Locals)[Slot]);
				Result.bOk = true;
				Result.Value = Runtime::ValueToString(LocalValue);
				Result.RawValue = LocalValue;
				Result.bHasRawValue = true;
				return Result;
			}
		}

		std::vector<std::string> ParamNames;
		std::vector<std::string> ParamTypes;
		std::vector<FValue> Arguments;
		for (std::size_t Slot = 0; Slot < Frame.Locals->size(); ++Slot)
		{
			std::string Name;
			if (Frame.Function && Slot < Frame.Function->LocalNames.size() && IsDebugExpressionIdentifier(Frame.Function->LocalNames[Slot]))
			{
				Name = Frame.Function->LocalNames[Slot];
			}
			else
			{
				continue;
			}
			if (!DebugExpressionReferencesIdentifier(Expression, Name))
			{
				continue;
			}
			const FValue& LocalValue = VisibleDebugValue((*Frame.Locals)[Slot]);
			if (!IsDebugEvalBindableValue(LocalValue))
			{
				continue;
			}
			ParamNames.push_back(std::move(Name));
			ParamTypes.push_back(DebugExpressionTypeName(LocalValue, AttachedVm->DebugGetModule()));
			Arguments.push_back(LocalValue);
		}
		for (auto It = LocalStatics.rbegin(); It != LocalStatics.rend(); ++It)
		{
			const std::string Name(It->Name);
			if (!DebugExpressionReferencesIdentifier(Expression, Name) || std::find(ParamNames.begin(), ParamNames.end(), Name) != ParamNames.end())
			{
				continue;
			}
			if (!It->Value)
			{
				Result.Error = It->bInitializing ? "local static is initializing" : "local static is uninitialized";
				return Result;
			}
			const FValue& Value = VisibleDebugValue(*It->Value);
			if (Name == TrimmedExpression)
			{
				Result.bOk = true;
				Result.Value = Runtime::ValueToString(Value);
				Result.RawValue = Value;
				Result.bHasRawValue = true;
				return Result;
			}
			if (IsDebugEvalBindableValue(Value))
			{
				ParamNames.push_back(Name);
				ParamTypes.push_back(DebugExpressionTypeName(Value, AttachedVm->DebugGetModule()));
				Arguments.push_back(Value);
			}
		}

		bInsideEvaluate = true;
		const std::string DeclarationPrelude =
		    BuildDebugEvalPrelude(AttachedVm->DebugGetModule());
		const DebugEval::FEvalResult Eval =
		    DebugEval::EvaluateExpression(
		        Expression,
		        ParamNames,
		        ParamTypes,
		        Arguments,
		        {},
		        DeclarationPrelude, Frame.Function ? Frame.Function->Location.FileLabel : std::string_view("<debug-eval>"), AttachedVm->GetVmBackendFactory());
		bInsideEvaluate = false;

		if (!Eval.bOk)
		{
			Result.Error = Eval.Error;
			return Result;
		}
		Result.bOk = true;
		Result.Value = Runtime::ValueToString(Eval.Value);
		Result.RawValue = Eval.Value;
		Result.bHasRawValue = true;
		return Result;
	}

	FDebugEvaluateResult VeroScriptDebugger::SetLocal(
	    const std::string_view Name,
	    const std::string_view ValueExpression,
	    const std::size_t FrameIndex)
	{
		FDebugEvaluateResult Result;
		if (!AttachedVm)
		{
			Result.Error = "debugger is not attached to a paused VM";
			return Result;
		}
		const std::size_t Depth = AttachedVm->DebugGetCallDepth();
		if (FrameIndex >= Depth)
		{
			Result.Error = "invalid frame index";
			return Result;
		}
		const std::size_t InternalIndex = Depth - 1 - FrameIndex;
		const VeroScriptVm::FDebugCallFrameView Frame = AttachedVm->DebugGetFrame(InternalIndex);
		if (!Frame.Locals || !Frame.Function)
		{
			Result.Error = "frame has no locals";
			return Result;
		}

		std::size_t Slot = Frame.Locals->size();
		for (std::size_t Index = 0; Index < Frame.Function->LocalNames.size(); ++Index)
		{
			if (Frame.Function->LocalNames[Index] == Name)
			{
				Slot = Index;
				break;
			}
		}
		if (Slot >= Frame.Locals->size())
		{
			Result.Error = "unknown local: " + std::string(Name);
			return Result;
		}

		// Single evaluate typed as the target local (avoids silent cross-type writes).
		const std::string TargetType = DebugExpressionTypeName(
		    VisibleDebugValue((*Frame.Locals)[Slot]), AttachedVm->DebugGetModule());
		std::vector<std::string> ParamNames;
		std::vector<std::string> ParamTypes;
		std::vector<FValue> Arguments;
		for (std::size_t Index = 0; Index < Frame.Locals->size(); ++Index)
		{
			if (Index >= Frame.Function->LocalNames.size() || !IsDebugExpressionIdentifier(Frame.Function->LocalNames[Index]))
			{
				continue;
			}
			if (!DebugExpressionReferencesIdentifier(
			        ValueExpression, Frame.Function->LocalNames[Index]))
			{
				continue;
			}
			ParamNames.push_back(Frame.Function->LocalNames[Index]);
			const FValue& LocalValue = VisibleDebugValue((*Frame.Locals)[Index]);
			if (!IsDebugEvalBindableValue(LocalValue))
			{
				ParamNames.pop_back();
				continue;
			}
			ParamTypes.push_back(DebugExpressionTypeName(LocalValue, AttachedVm->DebugGetModule()));
			Arguments.push_back(LocalValue);
		}
		bInsideEvaluate = true;
		const std::string DeclarationPrelude =
		    BuildDebugEvalPrelude(AttachedVm->DebugGetModule());
		const DebugEval::FEvalResult Raw = DebugEval::EvaluateExpression(
		    ValueExpression,
		    ParamNames,
		    ParamTypes,
		    Arguments,
		    TargetType,
		    DeclarationPrelude, Frame.Function ? Frame.Function->Location.FileLabel : std::string_view("<debug-eval>"), AttachedVm->GetVmBackendFactory());
		bInsideEvaluate = false;
		if (!Raw.bOk)
		{
			Result.Error = Raw.Error;
			return Result;
		}
		if (!AttachedVm->DebugSetLocal(InternalIndex, Slot, Raw.Value))
		{
			Result.Error = "failed to write local";
			return Result;
		}
		Result.bOk = true;
		Result.Value = Runtime::ValueToString(Raw.Value);
		Result.RawValue = Raw.Value;
		Result.bHasRawValue = true;
		return Result;
	}

	FDebugEvaluateResult VeroScriptDebugger::SetChild(
	    const FDebugVariable& Variable,
	    const std::string_view ValueExpression,
	    const std::size_t FrameIndex)
	{
		FDebugEvaluateResult Result;
		if (!AttachedVm || Variable.bReadOnly || Variable.TargetKind == FDebugVariable::ETargetKind::None)
		{
			Result.Error = "variable is not an editable child";
			return Result;
		}
		const std::size_t Depth = AttachedVm->DebugGetCallDepth();
		if (FrameIndex >= Depth)
		{
			Result.Error = "invalid frame index";
			return Result;
		}
		const std::size_t InternalIndex = Depth - 1 - FrameIndex;
		const VeroScriptVm::FDebugCallFrameView Frame = AttachedVm->DebugGetFrame(InternalIndex);
		if (!Frame.Locals || !Frame.Function)
		{
			Result.Error = "frame has no locals";
			return Result;
		}

		std::vector<std::string> ParamNames;
		std::vector<std::string> ParamTypes;
		std::vector<FValue> Arguments;
		for (std::size_t Index = 0; Index < Frame.Locals->size(); ++Index)
		{
			if (Index >= Frame.Function->LocalNames.size() || !IsDebugExpressionIdentifier(Frame.Function->LocalNames[Index]))
			{
				continue;
			}
			if (!DebugExpressionReferencesIdentifier(
			        ValueExpression, Frame.Function->LocalNames[Index]))
			{
				continue;
			}
			ParamNames.push_back(Frame.Function->LocalNames[Index]);
			const FValue& LocalValue = VisibleDebugValue((*Frame.Locals)[Index]);
			if (!IsDebugEvalBindableValue(LocalValue))
			{
				ParamNames.pop_back();
				continue;
			}
			ParamTypes.push_back(DebugExpressionTypeName(LocalValue, AttachedVm->DebugGetModule()));
			Arguments.push_back(LocalValue);
		}
		bInsideEvaluate = true;
		const std::string DeclarationPrelude =
		    BuildDebugEvalPrelude(AttachedVm->DebugGetModule());
		const DebugEval::FEvalResult Raw = DebugEval::EvaluateExpression(
		    ValueExpression,
		    ParamNames,
		    ParamTypes,
		    Arguments,
		    DebugExpressionTypeName(Variable.RawValue, AttachedVm->DebugGetModule()),
		    DeclarationPrelude, Frame.Function ? Frame.Function->Location.FileLabel : std::string_view("<debug-eval>"), AttachedVm->GetVmBackendFactory());
		bInsideEvaluate = false;
		if (!Raw.bOk)
		{
			Result.Error = Raw.Error;
			return Result;
		}

		bool bWritten = false;
		const auto AssignExistingMapValue =
		    [this, &Raw](auto& Entries, const auto& Key)
		{
			const auto Found = Entries.find(Key);
			return Found != Entries.end() && AttachedVm->DebugAssignValue(Found->second, Raw.Value);
		};
		const auto ReplaceSetElement =
		    [](auto& Elements, const auto& OldValue, const auto& NewValue)
		{
			if (Elements.find(OldValue) == Elements.end())
			{
				return false;
			}
			if (OldValue == NewValue)
			{
				return true;
			}
			if (Elements.find(NewValue) != Elements.end() || !Elements.insert(NewValue).second)
			{
				return false;
			}
			Elements.erase(OldValue);
			return true;
		};
		switch (Variable.TargetKind)
		{
		case FDebugVariable::ETargetKind::NativeArrayLocalElement:
			{
				if (Variable.RootFrameIndex < AttachedVm->DebugGetCallDepth())
				{
					const VeroScriptVm::FDebugCallFrameView RootFrame =
					    AttachedVm->DebugGetFrame(Variable.RootFrameIndex);
					if (RootFrame.Locals && Variable.RootLocalSlot < RootFrame.Locals->size())
					{
						FValue Root = VisibleDebugValue(
						    (*RootFrame.Locals)[Variable.RootLocalSlot]);
						FValue* Cursor = &Root;
						bool bValidPath = !Variable.NativeArrayPath.empty();
						for (const std::size_t Index : Variable.NativeArrayPath)
						{
							auto* Array = std::get_if<FNativeArray>(Cursor);
							if (!Array || Index >= Array->Storage().Elements.size())
							{
								bValidPath = false;
								break;
							}
							Cursor = &Array->Storage().Elements[Index];
						}
						if (bValidPath)
						{
							bWritten = AttachedVm->DebugAssignValue(*Cursor, Raw.Value) && AttachedVm->DebugSetLocal(Variable.RootFrameIndex, Variable.RootLocalSlot, std::move(Root));
						}
					}
				}
				break;
			}
		case FDebugVariable::ETargetKind::NativeArrayObjectFieldElement:
			{
				if (const auto* Owner = std::get_if<FObjectHandle>(&Variable.OwnerValue);
				    Owner && *Owner && Variable.TargetIndex < (*Owner)->Fields.size())
				{
					FValue Root = VisibleDebugValue(
					    (*Owner)->Fields[Variable.TargetIndex]);
					FValue* Cursor = &Root;
					bool bValidPath = !Variable.NativeArrayPath.empty();
					for (const std::size_t Index : Variable.NativeArrayPath)
					{
						auto* Array = std::get_if<FNativeArray>(Cursor);
						if (!Array || Index >= Array->Storage().Elements.size())
						{
							bValidPath = false;
							break;
						}
						Cursor = &Array->Storage().Elements[Index];
					}
					if (bValidPath)
					{
						bWritten = AttachedVm->DebugAssignValue(*Cursor, Raw.Value) && AttachedVm->DebugAssignValue((*Owner)->Fields[Variable.TargetIndex], std::move(Root));
					}
				}
				break;
			}
		case FDebugVariable::ETargetKind::ArrayElement:
			{
				if (const auto* Owner = std::get_if<FDynamicArrayHandle>(&Variable.OwnerValue);
				    Owner && *Owner && Variable.TargetIndex < (*Owner)->Elements.size())
				{
					bWritten = AttachedVm->DebugAssignValue(
					    (*Owner)->Elements[Variable.TargetIndex], Raw.Value);
				}
				break;
			}
		case FDebugVariable::ETargetKind::QueueElement:
			{
				if (const auto* Owner = std::get_if<FQueueHandle>(&Variable.OwnerValue);
				    Owner && *Owner && Variable.TargetIndex < (*Owner)->Elements.size())
				{
					bWritten = AttachedVm->DebugAssignValue(
					    (*Owner)->Elements[Variable.TargetIndex], Raw.Value);
				}
				break;
			}
		case FDebugVariable::ETargetKind::StackElement:
			{
				if (const auto* Owner = std::get_if<FStackHandle>(&Variable.OwnerValue);
				    Owner && *Owner && Variable.TargetIndex < (*Owner)->Elements.size())
				{
					bWritten = AttachedVm->DebugAssignValue(
					    (*Owner)->Elements[Variable.TargetIndex], Raw.Value);
				}
				break;
			}
		case FDebugVariable::ETargetKind::DequeElement:
			{
				if (const auto* Owner = std::get_if<FDequeHandle>(&Variable.OwnerValue);
				    Owner && *Owner && Variable.TargetIndex < (*Owner)->Elements.size())
				{
					bWritten = AttachedVm->DebugAssignValue(
					    (*Owner)->Elements[Variable.TargetIndex], Raw.Value);
				}
				break;
			}
		case FDebugVariable::ETargetKind::ObjectField:
			{
				if (const auto* Owner = std::get_if<FObjectHandle>(&Variable.OwnerValue);
				    Owner && *Owner && Variable.TargetIndex < (*Owner)->Fields.size())
				{
					bWritten = AttachedVm->DebugAssignValue(
					    (*Owner)->Fields[Variable.TargetIndex], Raw.Value);
				}
				break;
			}
		case FDebugVariable::ETargetKind::MapIntValue:
			{
				if (const auto* Owner = std::get_if<FMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->IntEntries, std::get<std::int32_t>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::MapUIntValue:
			{
				if (const auto* Owner = std::get_if<FMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->UIntEntries, std::get<std::uint32_t>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::MapInt64Value:
			{
				if (const auto* Owner = std::get_if<FMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->Int64Entries, std::get<std::int64_t>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::MapUInt64Value:
			{
				if (const auto* Owner = std::get_if<FMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->UInt64Entries, std::get<std::uint64_t>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::MapStringValue:
			{
				if (const auto* Owner = std::get_if<FMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->StrEntries, std::get<std::string>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::MapWStringValue:
			{
				if (const auto* Owner = std::get_if<FMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->WStrEntries, std::get<std::u16string>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::MapEnumValue:
			{
				if (const auto* Owner = std::get_if<FMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->EnumEntries, std::get<FEnumValue>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::MapHostValue:
			{
				if (const auto* Owner = std::get_if<FMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->HostEntries,
					    std::get<FHostObjectHandle>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::OrderedMapIntValue:
			{
				if (const auto* Owner = std::get_if<FOrderedMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->IntEntries, std::get<std::int32_t>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::OrderedMapUIntValue:
			{
				if (const auto* Owner = std::get_if<FOrderedMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->UIntEntries, std::get<std::uint32_t>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::OrderedMapInt64Value:
			{
				if (const auto* Owner = std::get_if<FOrderedMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->Int64Entries, std::get<std::int64_t>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::OrderedMapUInt64Value:
			{
				if (const auto* Owner = std::get_if<FOrderedMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->UInt64Entries, std::get<std::uint64_t>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::OrderedMapStringValue:
			{
				if (const auto* Owner = std::get_if<FOrderedMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->StrEntries, std::get<std::string>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::OrderedMapWStringValue:
			{
				if (const auto* Owner = std::get_if<FOrderedMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->WStrEntries, std::get<std::u16string>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::OrderedMapEnumValue:
			{
				if (const auto* Owner =
				        std::get_if<FOrderedMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->EnumEntries, std::get<FEnumValue>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::OrderedMapHostValue:
			{
				if (const auto* Owner =
				        std::get_if<FOrderedMapHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					bWritten = AssignExistingMapValue(
					    (*Owner)->HostEntries,
					    std::get<FHostObjectHandle>(Variable.TargetKey));
				}
				break;
			}
		case FDebugVariable::ETargetKind::SetIntElement:
			{
				if (const auto* Owner = std::get_if<FSetHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					std::int32_t NewValue = 0;
					bool bValidValue = false;
					if ((*Owner)->ElementTypeName == "char")
					{
						if (const auto* Character = std::get_if<std::int8_t>(&Raw.Value))
						{
							NewValue = *Character;
							bValidValue = true;
						}
					}
					else if (const auto* Integer = std::get_if<std::int32_t>(&Raw.Value))
					{
						NewValue = *Integer;
						bValidValue = true;
					}
					if (bValidValue)
					{
						bWritten = ReplaceSetElement(
						    (*Owner)->IntElements,
						    std::get<std::int32_t>(Variable.TargetKey),
						    NewValue);
					}
				}
				break;
			}
		case FDebugVariable::ETargetKind::SetUIntElement:
			{
				if (const auto* Owner = std::get_if<FSetHandle>(&Variable.OwnerValue);
				    Owner && *Owner && std::holds_alternative<std::uint32_t>(Raw.Value))
				{
					bWritten = ReplaceSetElement(
					    (*Owner)->UIntElements,
					    std::get<std::uint32_t>(Variable.TargetKey),
					    std::get<std::uint32_t>(Raw.Value));
				}
				break;
			}
		case FDebugVariable::ETargetKind::SetInt64Element:
			{
				if (const auto* Owner = std::get_if<FSetHandle>(&Variable.OwnerValue);
				    Owner && *Owner && std::holds_alternative<std::int64_t>(Raw.Value))
				{
					bWritten = ReplaceSetElement(
					    (*Owner)->Int64Elements,
					    std::get<std::int64_t>(Variable.TargetKey),
					    std::get<std::int64_t>(Raw.Value));
				}
				break;
			}
		case FDebugVariable::ETargetKind::SetUInt64Element:
			{
				if (const auto* Owner = std::get_if<FSetHandle>(&Variable.OwnerValue);
				    Owner && *Owner && std::holds_alternative<std::uint64_t>(Raw.Value))
				{
					bWritten = ReplaceSetElement(
					    (*Owner)->UInt64Elements,
					    std::get<std::uint64_t>(Variable.TargetKey),
					    std::get<std::uint64_t>(Raw.Value));
				}
				break;
			}
		case FDebugVariable::ETargetKind::SetStringElement:
			{
				if (const auto* Owner = std::get_if<FSetHandle>(&Variable.OwnerValue);
				    Owner && *Owner && std::holds_alternative<std::string>(Raw.Value))
				{
					bWritten = ReplaceSetElement(
					    (*Owner)->StrElements,
					    std::get<std::string>(Variable.TargetKey),
					    std::get<std::string>(Raw.Value));
				}
				break;
			}
		case FDebugVariable::ETargetKind::SetWStringElement:
			{
				if (const auto* Owner = std::get_if<FSetHandle>(&Variable.OwnerValue);
				    Owner && *Owner && std::holds_alternative<std::u16string>(Raw.Value))
				{
					bWritten = ReplaceSetElement(
					    (*Owner)->WStrElements,
					    std::get<std::u16string>(Variable.TargetKey),
					    std::get<std::u16string>(Raw.Value));
				}
				break;
			}
		case FDebugVariable::ETargetKind::SetEnumElement:
			{
				if (const auto* Owner = std::get_if<FSetHandle>(&Variable.OwnerValue);
				    Owner && *Owner)
				{
					const FEnumValue& OldValue =
					    std::get<FEnumValue>(Variable.TargetKey);
					if (const auto* NewValue = std::get_if<FEnumValue>(&Raw.Value);
					    NewValue && NewValue->TypeName == OldValue.TypeName)
					{
						bWritten = ReplaceSetElement(
						    (*Owner)->EnumElements, OldValue, *NewValue);
					}
				}
				break;
			}
		case FDebugVariable::ETargetKind::None:
		default:
			{
				break;
			}
		}
		if (!bWritten)
		{
			Result.Error = "failed to write container child";
			return Result;
		}
		if (Variable.TargetKind == FDebugVariable::ETargetKind::ObjectField || Variable.TargetKind == FDebugVariable::ETargetKind::NativeArrayObjectFieldElement)
		{
			const auto* const Owner = std::get_if<FObjectHandle>(&Variable.OwnerValue);
			if (!Owner || !AttachedVm->DebugCommitObjectFieldMutation(*Owner, static_cast<std::uint16_t>(Variable.TargetIndex)))
			{
				Result.Error = "failed to commit object-field mutation";
				return Result;
			}
		}
		else if (Variable.TargetKind != FDebugVariable::ETargetKind::NativeArrayLocalElement)
		{
			if (!AttachedVm->DebugCommitContainerMutation(
			        Variable.OwnerValue, EMutationKind::ContainerSet))
			{
				Result.Error = "failed to commit container mutation";
				return Result;
			}
		}
		Result.bOk = true;
		Result.Value = Runtime::ValueToString(Raw.Value);
		Result.RawValue = Raw.Value;
		Result.bHasRawValue = true;
		return Result;
	}

	void VeroScriptDebugger::AttachVm(VeroScriptVm* const Vm)
	{
		AttachedVm = Vm;
	}

	void VeroScriptDebugger::DetachVm()
	{
		AttachedVm = nullptr;
	}

	bool VeroScriptDebugger::ShouldStopBeforeInstruction(
	    const FBytecodeModule& Module,
	    const FBytecodeFunction* const Function,
	    const std::size_t InstructionPointer,
	    const std::size_t CallDepth,
	    FSourceLocation& OutLocation,
	    EDebugStopReason& OutReason)
	{
		if (bInsideEvaluate)
		{
			return false;
		}

		OutLocation = ResolveLocation(Module, Function, InstructionPointer);
		if (bPauseRequested.exchange(false, std::memory_order_acq_rel))
		{
			OutReason = EDebugStopReason::Pause;
			return true;
		}

		if (bCaptureStepDepth)
		{
			if (RunMode == EDebugRunMode::StepOut)
			{
				StepTargetDepth = CallDepth > 0 ? CallDepth - 1 : 0;
			}
			else
			{
				StepTargetDepth = CallDepth;
			}
			bCaptureStepDepth = false;
		}

		if (bStopOnNext)
		{
			bool bStop = false;
			switch (RunMode)
			{
			case EDebugRunMode::StepIn:
				{
					bStop = true;
					break;
				}
			case EDebugRunMode::StepOver:
				{
					bStop = CallDepth <= StepTargetDepth;
					break;
				}
			case EDebugRunMode::StepOut:
				{
					bStop = CallDepth <= StepTargetDepth;
					break;
				}
			case EDebugRunMode::Continue:
			default:
				{
					break;
				}
			}
			if (bStop)
			{
				bStopOnNext = false;
				RunMode = EDebugRunMode::Continue;
				OutReason = EDebugStopReason::Step;
				return true;
			}
		}

		for (const FBreakpoint& Breakpoint : Breakpoints)
		{
			if (Breakpoint.Line == OutLocation.Line && FileMatches(Breakpoint.FileLabel, OutLocation.FileLabel, Breakpoint.bExactFile))
			{
				if (!ConditionHolds(Breakpoint))
				{
					continue;
				}
				OutReason = EDebugStopReason::Breakpoint;
				return true;
			}
		}
		return false;
	}

	void VeroScriptDebugger::NotifyStopped(const EDebugStopReason Reason)
	{
		if (StopCallback)
		{
			StopCallback(*this, Reason);
		}
	}

	std::vector<FDebugVariable> VeroScriptDebugger::GetChildrenImpl(
	    const FDebugVariable& Variable) const
	{
		FMemoryInspectionScope MemoryScope(AttachedVm);
		std::vector<FDebugVariable> Children;
		if (!Variable.bHasRawValue)
		{
			return Children;
		}
		const FValue& Value = Variable.RawValue;
		if (const auto* Box = std::get_if<FScriptVariantHandle>(&Value))
		{
			Children.push_back(MakeDebugVariable("$present", static_cast<bool>(*Box)));
			Children.push_back(MakeDebugVariable("$type", std::string(Variant::TypeName(Value))));
			if (*Box)
			{
				Children.push_back(MakeDebugVariable("$value", (*Box)->Value));
			}
			for (auto& Child : Children)
			{
				Child.bReadOnly = true;
			}
			return Children;
		}
		if (AttachedVm && Memory::IsRegion(Value))
		{
			const FMemoryInspection Inspection = AttachedVm->DebugInspectMemory(Value);
			const auto Append = [&Children](const char* Name, const auto& Number)
			{
				if (Number)
				{
					FDebugVariable Child;
					Child.Name = Name;
					Child.Value = std::to_string(*Number);
					// Inspection values have no mutation target or invented storage slot.
					Children.push_back(std::move(Child));
				}
			};
			if (Inspection.ObjectAddress || Inspection.DataAddress)
			{
				Append("[storage_address]", Variable.Memory.StorageAddress);
			}
			Append("[object_id]", Inspection.ObjectId);
			Append("[object_address]", Inspection.ObjectAddress);
			Append("[data_address]", Inspection.DataAddress);
			Append("[byte_length]", Inspection.ByteLength);
			return Children;
		}
		const auto AppendIndexed =
		    [&Children](const std::vector<FValue>& Elements)
		{
			Children.reserve(Elements.size());
			for (std::size_t Index = 0; Index < Elements.size(); ++Index)
			{
				Children.push_back(
				    MakeDebugVariable("[" + std::to_string(Index) + "]", Elements[Index]));
			}
		};
		const auto AppendDeque =
		    [&Children](const std::deque<FValue>& Elements)
		{
			Children.reserve(Elements.size());
			for (std::size_t Index = 0; Index < Elements.size(); ++Index)
			{
				Children.push_back(
				    MakeDebugVariable("[" + std::to_string(Index) + "]", Elements[Index]));
			}
		};
		if (const auto* const Object = std::get_if<FObjectHandle>(&Value))
		{
			if (!*Object)
			{
				return Children;
			}
			const FBytecodeTypeDesc* Type = nullptr;
			if (AttachedVm && AttachedVm->DebugGetModule())
			{
				for (const FBytecodeTypeDesc& Candidate : AttachedVm->DebugGetModule()->Types)
				{
					if (Candidate.Name == (*Object)->GetTypeName())
					{
						Type = &Candidate;
						break;
					}
				}
			}
			Children.reserve((*Object)->Fields.size() + ((*Object)->GetHostBase() ? 1u : 0u));
			for (std::size_t Index = 0; Index < (*Object)->Fields.size(); ++Index)
			{
				const std::string Name =
				    Type && Index < Type->Fields.size() && !Type->Fields[Index].Name.empty()
				    ? Type->Fields[Index].Name
				    : "field_" + std::to_string(Index);
				Children.push_back(MakeMutableDebugVariable(
				    Name,
				    (*Object)->Fields[Index],
				    Value,
				    FDebugVariable::ETargetKind::ObjectField,
				    Index));
			}
			if ((*Object)->GetHostBase())
			{
				Children.push_back(MakeDebugVariable("$hostBase", FValue{(*Object)->GetHostBase()}));
			}
			return Children;
		}
		if (const auto* const NativeArray = std::get_if<FNativeArray>(&Value))
		{
			AppendIndexed(NativeArray->Storage().Elements);
			if (Variable.RootFrameIndex != static_cast<std::size_t>(-1) && Variable.RootLocalSlot != static_cast<std::size_t>(-1))
			{
				for (std::size_t Index = 0; Index < Children.size(); ++Index)
				{
					Children[Index].TargetKind =
					    FDebugVariable::ETargetKind::NativeArrayLocalElement;
					Children[Index].RootFrameIndex = Variable.RootFrameIndex;
					Children[Index].RootLocalSlot = Variable.RootLocalSlot;
					Children[Index].NativeArrayPath = Variable.NativeArrayPath;
					Children[Index].NativeArrayPath.push_back(Index);
				}
			}
			else if (Variable.TargetKind == FDebugVariable::ETargetKind::ObjectField || Variable.TargetKind == FDebugVariable::ETargetKind::NativeArrayObjectFieldElement)
			{
				for (std::size_t Index = 0; Index < Children.size(); ++Index)
				{
					Children[Index].TargetKind =
					    FDebugVariable::ETargetKind::NativeArrayObjectFieldElement;
					Children[Index].OwnerValue = Variable.OwnerValue;
					Children[Index].TargetIndex = Variable.TargetIndex;
					Children[Index].NativeArrayPath = Variable.NativeArrayPath;
					Children[Index].NativeArrayPath.push_back(Index);
				}
			}
			FValue* Root = nullptr;
			if (AttachedVm && Variable.RootFrameIndex < AttachedVm->DebugGetCallDepth())
			{
				const auto Frame = AttachedVm->DebugGetFrame(Variable.RootFrameIndex);
				if (Frame.Locals && Variable.RootLocalSlot < Frame.Locals->size())
				{
					Root = const_cast<FValue*>(&(*Frame.Locals)[Variable.RootLocalSlot]);
				}
			}
			else if (const auto* Owner = std::get_if<FObjectHandle>(&Variable.OwnerValue);
			    Owner && *Owner && Variable.TargetIndex < (*Owner)->Fields.size())
			{
				Root = &(*Owner)->Fields[Variable.TargetIndex];
			}
			if (Root)
			{
				Root = const_cast<FValue*>(UnwrapClosureReferenceCell(Root));
				// Only actual frame/field storage is used; snapshot-only paths omit addresses.
				for (const auto Index : Variable.NativeArrayPath)
				{
					auto* Array = Root ? std::get_if<FNativeArray>(Root) : nullptr;
					Root = Array && Index < Array->Storage().Elements.size()
					    ? &Array->Storage().Elements[Index]
					    : nullptr;
				}
				if (auto* Actual = Root ? std::get_if<FNativeArray>(Root) : nullptr)
				{
					for (std::size_t Index = 0;
					    Index < Children.size() && Index < Actual->Storage().Elements.size(); ++Index)
					{
						Children[Index].Memory = AttachedVm->DebugInspectMemory(
						    Actual->Storage().Elements[Index], &Actual->Storage().Elements[Index]);
					}
				}
			}
			return Children;
		}
		if (const auto* const Array = std::get_if<FDynamicArrayHandle>(&Value))
		{
			if (*Array)
			{
				// Debug children and setVariable need stable addressable FValue slots.
				// Materialize lazily only while the array is inspected for mutation.
				(*Array)->MaterializeBoxedElements();
				AppendIndexed((*Array)->Elements);
				for (std::size_t Index = 0; Index < Children.size(); ++Index)
				{
					Children[Index].TargetKind = FDebugVariable::ETargetKind::ArrayElement;
					if (AttachedVm)
					{
						Children[Index].Memory = AttachedVm->DebugInspectMemory(
						    (*Array)->Elements[Index], &(*Array)->Elements[Index]);
					}
					Children[Index].OwnerValue = Value;
					Children[Index].TargetIndex = Index;
				}
			}
			return Children;
		}
		if (const auto* const Map = std::get_if<FMapHandle>(&Value))
		{
			if (!*Map)
			{
				return Children;
			}
			if ((*Map)->KeyTypeName == "string")
			{
				for (const auto& Entry : (*Map)->StrEntries)
				{
					Children.push_back(
					    MakeMutableDebugVariable(
					        "[\"" + EscapeDebugString(Entry.first) + "\"]",
					        Entry.second,
					        Value,
					        FDebugVariable::ETargetKind::MapStringValue,
					        0,
					        Entry.first));
				}
			}
			else if ((*Map)->KeyTypeName == "wstring")
			{
				for (const auto& Entry : (*Map)->WStrEntries)
				{
					const std::string Key = Runtime::ValueToString(FValue{Entry.first});
					Children.push_back(
					    MakeMutableDebugVariable(
					        "[L\"" + EscapeDebugString(Key) + "\"]",
					        Entry.second,
					        Value,
					        FDebugVariable::ETargetKind::MapWStringValue,
					        0,
					        Entry.first));
				}
			}
			else if ((*Map)->KeyTypeName == "int" || (*Map)->KeyTypeName == "char")
			{
				for (const auto& Entry : (*Map)->IntEntries)
				{
					Children.push_back(
					    MakeMutableDebugVariable(
					        "[" + std::to_string(Entry.first) + "]",
					        Entry.second,
					        Value,
					        FDebugVariable::ETargetKind::MapIntValue,
					        0,
					        Entry.first));
				}
			}
			else if ((*Map)->KeyTypeName == "unsigned int")
			{
				for (const auto& Entry : (*Map)->UIntEntries)
				{
					Children.push_back(MakeMutableDebugVariable(
					    "[" + std::to_string(Entry.first) + "]",
					    Entry.second,
					    Value,
					    FDebugVariable::ETargetKind::MapUIntValue,
					    0,
					    Entry.first));
				}
			}
			else if ((*Map)->KeyTypeName == "int64")
			{
				for (const auto& Entry : (*Map)->Int64Entries)
				{
					Children.push_back(MakeMutableDebugVariable(
					    "[" + std::to_string(Entry.first) + "]",
					    Entry.second,
					    Value,
					    FDebugVariable::ETargetKind::MapInt64Value,
					    0,
					    Entry.first));
				}
			}
			else if ((*Map)->KeyTypeName == "uint64")
			{
				for (const auto& Entry : (*Map)->UInt64Entries)
				{
					Children.push_back(MakeMutableDebugVariable(
					    "[" + std::to_string(Entry.first) + "]",
					    Entry.second,
					    Value,
					    FDebugVariable::ETargetKind::MapUInt64Value,
					    0,
					    Entry.first));
				}
			}
			else if (!(*Map)->EnumEntries.empty())
			{
				for (const auto& Entry : (*Map)->EnumEntries)
				{
					Children.push_back(MakeMutableDebugVariable(
					    "[" + Runtime::ValueToString(FValue{Entry.first}) + "]",
					    Entry.second,
					    Value,
					    FDebugVariable::ETargetKind::MapEnumValue,
					    0,
					    Entry.first));
				}
			}
			else
			{
				for (const auto& Entry : (*Map)->HostEntries)
				{
					const std::string Identity = Entry.first
					    ? Entry.first->TypeName + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(Entry.first.get()))
					    : "null";
					Children.push_back(
					    MakeMutableDebugVariable(
					        "[" + Identity + "]",
					        Entry.second,
					        Value,
					        FDebugVariable::ETargetKind::MapHostValue,
					        0,
					        Entry.first));
				}
			}
			std::sort(
			    Children.begin(),
			    Children.end(),
			    [](const FDebugVariable& Left, const FDebugVariable& Right)
			{
				return Left.Name < Right.Name;
			});
			return Children;
		}
		if (const auto* const Set = std::get_if<FSetHandle>(&Value))
		{
			if (!*Set)
			{
				return Children;
			}
			std::vector<FValue> Elements;
			if ((*Set)->ElementTypeName == "string")
			{
				Elements.reserve((*Set)->StrElements.size());
				for (const std::string& Element : (*Set)->StrElements)
				{
					Elements.emplace_back(Element);
				}
			}
			else if ((*Set)->ElementTypeName == "wstring")
			{
				Elements.reserve((*Set)->WStrElements.size());
				for (const std::u16string& Element : (*Set)->WStrElements)
				{
					Elements.emplace_back(Element);
				}
			}
			else if ((*Set)->ElementTypeName == "int" || (*Set)->ElementTypeName == "char")
			{
				Elements.reserve((*Set)->IntElements.size());
				for (const std::int32_t Element : (*Set)->IntElements)
				{
					Elements.emplace_back((*Set)->ElementTypeName == "char"
					        ? FValue{static_cast<std::int8_t>(Element)}
					        : FValue{Element});
				}
			}
			else if ((*Set)->ElementTypeName == "unsigned int")
			{
				Elements.reserve((*Set)->UIntElements.size());
				for (const std::uint32_t Element : (*Set)->UIntElements)
				{
					Elements.emplace_back(Element);
				}
			}
			else if ((*Set)->ElementTypeName == "int64")
			{
				Elements.reserve((*Set)->Int64Elements.size());
				for (const std::int64_t Element : (*Set)->Int64Elements)
				{
					Elements.emplace_back(Element);
				}
			}
			else if ((*Set)->ElementTypeName == "uint64")
			{
				Elements.reserve((*Set)->UInt64Elements.size());
				for (const std::uint64_t Element : (*Set)->UInt64Elements)
				{
					Elements.emplace_back(Element);
				}
			}
			else if (!(*Set)->EnumElements.empty())
			{
				Elements.reserve((*Set)->EnumElements.size());
				for (const FEnumValue& Element : (*Set)->EnumElements)
				{
					Elements.emplace_back(Element);
				}
			}
			else
			{
				Elements.reserve((*Set)->HostElements.size());
				for (const FHostObjectHandle& Element : (*Set)->HostElements)
				{
					Elements.emplace_back(Element);
				}
			}
			std::sort(
			    Elements.begin(),
			    Elements.end(),
			    [](const FValue& Left, const FValue& Right)
			{
				return Runtime::ValueToString(Left) < Runtime::ValueToString(Right);
			});
			AppendIndexed(Elements);
			for (std::size_t Index = 0; Index < Children.size(); ++Index)
			{
				Children[Index].OwnerValue = Value;
				if ((*Set)->ElementTypeName == "string")
				{
					Children[Index].TargetKind = FDebugVariable::ETargetKind::SetStringElement;
					Children[Index].TargetKey = std::get<std::string>(Elements[Index]);
				}
				else if ((*Set)->ElementTypeName == "wstring")
				{
					Children[Index].TargetKind = FDebugVariable::ETargetKind::SetWStringElement;
					Children[Index].TargetKey = std::get<std::u16string>(Elements[Index]);
				}
				else
				{
					if ((*Set)->ElementTypeName == "int" || (*Set)->ElementTypeName == "char")
					{
						Children[Index].TargetKind =
						    FDebugVariable::ETargetKind::SetIntElement;
						Children[Index].TargetKey = (*Set)->ElementTypeName == "char"
						    ? static_cast<std::int32_t>(
						          std::get<std::int8_t>(Elements[Index]))
						    : std::get<std::int32_t>(Elements[Index]);
					}
					else if ((*Set)->ElementTypeName == "unsigned int")
					{
						Children[Index].TargetKind =
						    FDebugVariable::ETargetKind::SetUIntElement;
						Children[Index].TargetKey =
						    std::get<std::uint32_t>(Elements[Index]);
					}
					else if ((*Set)->ElementTypeName == "int64")
					{
						Children[Index].TargetKind =
						    FDebugVariable::ETargetKind::SetInt64Element;
						Children[Index].TargetKey =
						    std::get<std::int64_t>(Elements[Index]);
					}
					else if ((*Set)->ElementTypeName == "uint64")
					{
						Children[Index].TargetKind =
						    FDebugVariable::ETargetKind::SetUInt64Element;
						Children[Index].TargetKey =
						    std::get<std::uint64_t>(Elements[Index]);
					}
					else if (const auto* const EnumElement =
					             std::get_if<FEnumValue>(&Elements[Index]))
					{
						Children[Index].TargetKind =
						    FDebugVariable::ETargetKind::SetEnumElement;
						Children[Index].TargetKey = *EnumElement;
					}
					// Host identity keys are intentionally read-only in the debugger:
					// replacing one requires an object-valued debug expression.
				}
			}
			return Children;
		}
		if (const auto* const Queue = std::get_if<FQueueHandle>(&Value))
		{
			if (*Queue)
			{
				AppendDeque((*Queue)->Elements);
				for (std::size_t Index = 0; Index < Children.size(); ++Index)
				{
					Children[Index].TargetKind = FDebugVariable::ETargetKind::QueueElement;
					Children[Index].OwnerValue = Value;
					Children[Index].TargetIndex = Index;
				}
			}
			return Children;
		}
		if (const auto* const Stack = std::get_if<FStackHandle>(&Value))
		{
			if (*Stack)
			{
				AppendIndexed((*Stack)->Elements);
				for (std::size_t Index = 0; Index < Children.size(); ++Index)
				{
					Children[Index].TargetKind = FDebugVariable::ETargetKind::StackElement;
					Children[Index].OwnerValue = Value;
					Children[Index].TargetIndex = Index;
				}
			}
			return Children;
		}
		if (const auto* const Deque = std::get_if<FDequeHandle>(&Value))
		{
			if (*Deque)
			{
				AppendDeque((*Deque)->Elements);
				for (std::size_t Index = 0; Index < Children.size(); ++Index)
				{
					Children[Index].TargetKind = FDebugVariable::ETargetKind::DequeElement;
					Children[Index].OwnerValue = Value;
					Children[Index].TargetIndex = Index;
				}
			}
			return Children;
		}
		if (const auto* const OrderedMap = std::get_if<FOrderedMapHandle>(&Value))
		{
			if (!*OrderedMap)
			{
				return Children;
			}
			if ((*OrderedMap)->KeyTypeName == "string")
			{
				for (const std::string& Key : (*OrderedMap)->StrKeyOrder)
				{
					const auto Found = (*OrderedMap)->StrEntries.find(Key);
					if (Found != (*OrderedMap)->StrEntries.end())
					{
						Children.push_back(MakeMutableDebugVariable(
						    "[\"" + EscapeDebugString(Key) + "\"]",
						    Found->second,
						    Value,
						    FDebugVariable::ETargetKind::OrderedMapStringValue,
						    0,
						    Key));
					}
				}
			}
			else if ((*OrderedMap)->KeyTypeName == "wstring")
			{
				for (const std::u16string& Key : (*OrderedMap)->WStrKeyOrder)
				{
					const auto Found = (*OrderedMap)->WStrEntries.find(Key);
					if (Found != (*OrderedMap)->WStrEntries.end())
					{
						Children.push_back(MakeMutableDebugVariable(
						    "[L\"" + EscapeDebugString(Runtime::ValueToString(FValue{Key})) + "\"]",
						    Found->second,
						    Value,
						    FDebugVariable::ETargetKind::OrderedMapWStringValue,
						    0,
						    Key));
					}
				}
			}
			else if ((*OrderedMap)->KeyTypeName == "int" || (*OrderedMap)->KeyTypeName == "char")
			{
				for (const std::int32_t Key : (*OrderedMap)->IntKeyOrder)
				{
					const auto Found = (*OrderedMap)->IntEntries.find(Key);
					if (Found != (*OrderedMap)->IntEntries.end())
					{
						Children.push_back(MakeMutableDebugVariable(
						    "[" + std::to_string(Key) + "]",
						    Found->second,
						    Value,
						    FDebugVariable::ETargetKind::OrderedMapIntValue,
						    0,
						    Key));
					}
				}
			}
			else if ((*OrderedMap)->KeyTypeName == "unsigned int")
			{
				for (const std::uint32_t Key : (*OrderedMap)->UIntKeyOrder)
				{
					const auto Found = (*OrderedMap)->UIntEntries.find(Key);
					if (Found != (*OrderedMap)->UIntEntries.end())
					{
						Children.push_back(MakeMutableDebugVariable(
						    "[" + std::to_string(Key) + "]",
						    Found->second,
						    Value,
						    FDebugVariable::ETargetKind::OrderedMapUIntValue,
						    0,
						    Key));
					}
				}
			}
			else if ((*OrderedMap)->KeyTypeName == "int64")
			{
				for (const std::int64_t Key : (*OrderedMap)->Int64KeyOrder)
				{
					const auto Found = (*OrderedMap)->Int64Entries.find(Key);
					if (Found != (*OrderedMap)->Int64Entries.end())
					{
						Children.push_back(MakeMutableDebugVariable(
						    "[" + std::to_string(Key) + "]",
						    Found->second,
						    Value,
						    FDebugVariable::ETargetKind::OrderedMapInt64Value,
						    0,
						    Key));
					}
				}
			}
			else if ((*OrderedMap)->KeyTypeName == "uint64")
			{
				for (const std::uint64_t Key : (*OrderedMap)->UInt64KeyOrder)
				{
					const auto Found = (*OrderedMap)->UInt64Entries.find(Key);
					if (Found != (*OrderedMap)->UInt64Entries.end())
					{
						Children.push_back(MakeMutableDebugVariable(
						    "[" + std::to_string(Key) + "]",
						    Found->second,
						    Value,
						    FDebugVariable::ETargetKind::OrderedMapUInt64Value,
						    0,
						    Key));
					}
				}
			}
			else if (!(*OrderedMap)->EnumKeyOrder.empty())
			{
				for (const FEnumValue& Key : (*OrderedMap)->EnumKeyOrder)
				{
					const auto Found = (*OrderedMap)->EnumEntries.find(Key);
					if (Found != (*OrderedMap)->EnumEntries.end())
					{
						Children.push_back(MakeMutableDebugVariable(
						    "[" + Runtime::ValueToString(FValue{Key}) + "]",
						    Found->second,
						    Value,
						    FDebugVariable::ETargetKind::OrderedMapEnumValue,
						    0,
						    Key));
					}
				}
			}
			else
			{
				for (const FHostObjectHandle& Key : (*OrderedMap)->HostKeyOrder)
				{
					const auto Found = (*OrderedMap)->HostEntries.find(Key);
					if (Found != (*OrderedMap)->HostEntries.end())
					{
						const std::string Identity = Key
						    ? Key->TypeName + "@" + std::to_string(reinterpret_cast<std::uintptr_t>(Key.get()))
						    : "null";
						Children.push_back(MakeMutableDebugVariable(
						    "[" + Identity + "]",
						    Found->second,
						    Value,
						    FDebugVariable::ETargetKind::OrderedMapHostValue,
						    0,
						    Key));
					}
				}
			}
			return Children;
		}
		if (const auto* const Function = std::get_if<FFunctionHandle>(&Value))
		{
			if (!*Function)
			{
				return Children;
			}
			if ((*Function)->CapturedThis)
			{
				Children.push_back(MakeDebugVariable("this", FValue{(*Function)->CapturedThis}));
			}
			for (std::size_t Index = 0; Index < (*Function)->Captures.size(); ++Index)
			{
				Children.push_back(MakeDebugVariable(
				    "[capture " + std::to_string(Index) + "]",
				    (*Function)->Captures[Index]));
			}
			return Children;
		}
		if (const auto* const Host = std::get_if<FHostObjectHandle>(&Value))
		{
			if (*Host)
			{
				Children.push_back(MakeDebugVariable("$type", FValue{(*Host)->TypeName}));
				Children.push_back(MakeDebugVariable("$typeId", FValue{(*Host)->TypeId}));
			}
		}
		return Children;
	}

	FSourceLocation VeroScriptDebugger::ResolveLocation(
	    const FBytecodeModule& Module,
	    const FBytecodeFunction* const Function,
	    const std::size_t InstructionPointer)
	{
		FSourceLocation Location = Function ? Function->Location : FSourceLocation{};
		const std::vector<FDebugLine>* Lines = nullptr;
		if (Function)
		{
			Lines = &Function->DebugLines;
		}
		else
		{
			Lines = &Module.InitDebugLines;
		}
		if (Lines && InstructionPointer < Lines->size())
		{
			const FDebugLine& Line = (*Lines)[InstructionPointer];
			if (Line.Line != 0)
			{
				Location.Line = Line.Line;
				Location.Column = Line.Column;
				if (Line.FileIndex < Module.DebugFiles.size() && !Module.DebugFiles[Line.FileIndex].empty())
				{
					Location.FileLabel = Module.DebugFiles[Line.FileIndex];
				}
			}
		}
		return Location;
	}

	bool VeroScriptDebugger::FileMatches(
	    const std::string& BreakFile,
	    const std::string_view LocFile,
	    const bool bExactFile)
	{
		if (bExactFile)
		{
			return BreakFile == LocFile;
		}
		if (BreakFile.empty())
		{
			return true;
		}
		if (LocFile.empty())
		{
			return false;
		}
		if (BreakFile == LocFile)
		{
			return true;
		}
		const std::size_t Slash = LocFile.find_last_of("/\\");
		const std::string_view Base = Slash == std::string_view::npos ? LocFile : LocFile.substr(Slash + 1);
		return BreakFile == Base;
	}

	int VeroScriptDebugger::AddBreakpoint(
	    std::string FileLabel,
	    const std::uint32_t Line,
	    std::string Condition,
	    const bool bExactFile)
	{
		FBreakpoint Breakpoint;
		Breakpoint.Id = NextBreakpointId++;
		Breakpoint.FileLabel = std::move(FileLabel);
		Breakpoint.Line = Line;
		Breakpoint.Condition = std::move(Condition);
		Breakpoint.bExactFile = bExactFile;
		Breakpoints.push_back(std::move(Breakpoint));
		return Breakpoints.back().Id;
	}

	bool VeroScriptDebugger::ConditionHolds(const FBreakpoint& Breakpoint) const
	{
		if (Breakpoint.Condition.empty())
		{
			return true;
		}
		const FDebugEvaluateResult Result = Evaluate(Breakpoint.Condition, 0);
		if (!Result.bOk)
		{
			return false;
		}
		// Bool expressions stringify to true/false; also accept non-zero numbers / non-empty.
		if (Result.Value == "true")
		{
			return true;
		}
		if (Result.Value == "false" || Result.Value.empty())
		{
			return false;
		}
		char* End = nullptr;
		const double Number = std::strtod(Result.Value.c_str(), &End);
		if (End != Result.Value.c_str() && End && *End == '\0')
		{
			return Number != 0.0;
		}
		return true;
	}
}
