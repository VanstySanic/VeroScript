// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmDispatchSelection.h"
#include "VeroScriptVmIndexedExpressions.h"

#include <algorithm>
#include <functional>
#include <new>
#include <string_view>

#if defined(_MSC_VER) && defined(_M_X64)
#pragma code_seg(push, ".vstext")
#endif

namespace VeroScript::VmDispatchSelection
{
	namespace
	{
		constexpr std::size_t MaxFunctions = 65536u;
		constexpr std::size_t MaxInstructions = 1048576u;
		constexpr std::size_t MaxCallEdges = 262144u;
		constexpr std::uint8_t PropagatedFeatures = TextFeature | IndexedFeature;

		std::uint8_t GetLocalKind(const std::string_view Type) noexcept
		{
			return Type == "int" || Type == "int64" ? 1u : Type == "string" ? 2u :
			    Type == "wstring" ? 3u : Type == "array<int>" ? 4u : 0u;
		}

		void InitializeParameterKinds(const FBytecodeFunction& Function,
		    std::uint8_t* Kinds, std::uint8_t* Stored, const std::size_t LocalCount)
		{
			const std::string_view Signature(Function.ClosureTypeName);
			const auto End = Signature.find(')');
			if (Signature.substr(0u, 3u) != "fn(" || End == Signature.npos ||
				Signature.substr(3u, End - 3u).find('(') != Signature.npos)
			{
				return;
			}
			std::size_t Begin = 3u;
			for (std::size_t Parameter = 0u; Parameter < Function.ParamCount && Parameter < LocalCount && Begin <= End; ++Parameter)
			{
				std::size_t Stop = Begin;
				std::size_t Depth = 0u;
				for (; Stop < End; ++Stop)
				{
					if (Signature[Stop] == '<')
					{
						++Depth;
					}
					else if (Signature[Stop] == '>' && Depth)
					{
						--Depth;
					}
					else if (Signature[Stop] == ',' && !Depth)
					{
						break;
					}
				}
				Kinds[Parameter] = GetLocalKind(Signature.substr(Begin, Stop - Begin));
				Stored[Parameter] = 1u;
				Begin = Stop + 1u;
			}
			for (std::size_t Capture = 0u; Capture < Function.Captures.size(); ++Capture)
			{
				const auto Slot = Function.ParamCount + Capture;
				if (Slot >= LocalCount)
				{
					break;
				}
				if (Function.Captures[Capture].Mode == EClosureCaptureMode::Value)
				{
					Kinds[Slot] = GetLocalKind(Function.Captures[Capture].TypeName);
					Stored[Slot] = 1u;
				}
			}
		}

		bool IsIdentityLeaf(const FBytecodeFunction& Function) noexcept
		{
			const auto& Code = Function.Code;
			if (Function.ParamCount != 1u || Function.LocalCount != 1u || Function.bIsMethod ||
				Function.bReturnsVoid || Function.bReturnsRef || Function.bDeleted ||
				!Function.UnavailableReason.empty() || !Function.Captures.empty() ||
				!Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function) ||
				Code.size() < 4u || Code.size() > 8u)
			{
				return false;
			}
			const auto First = Code.size() - 4u;
			for (std::size_t Offset = 0u; Offset < First; ++Offset)
			{
				if (static_cast<EOpcode>(Code[Offset]) != EOpcode::Nop) return false;
			}
			return static_cast<EOpcode>(Code[First]) == EOpcode::LoadLocal &&
			    Bytecode::ReadU16LE(Code.data() + First + 1u) == 0u && static_cast<EOpcode>(Code.back()) == EOpcode::Return;
		}

		template <typename FVisitCall>
		bool TryDescribeFeaturelessFunction(const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t& Instructions,
		    const std::size_t InstructionLimit, FVisitCall&& VisitCall) noexcept
		{
			if (IsIdentityLeaf(Function) || Function.ClosureTypeName.find("array<int>") != std::string::npos) return false;
			for (const auto &Capture : Function.Captures)
			{
				if (Capture.TypeName.find("array<int>") != std::string::npos) return false;
			}
			std::array<std::uint16_t, 64> Calls;
			std::size_t CallCount = 0u, Count = 0u;
			const auto& Code = Function.Code;
			for (std::size_t Cursor = 0u; Cursor < Code.size();)
			{
				if (++Count > InstructionLimit - Instructions) return false;
				const auto Opcode = static_cast<EOpcode>(Code[Cursor]);
				const auto Size = Bytecode::InstructionSize(Opcode);
				if (!Size || Size > Code.size() - Cursor) return false;
				if (Opcode == EOpcode::CallBuiltin &&
					(static_cast<EBuiltinId>(Code[Cursor + 1u]) == EBuiltinId::CharCode ||
						static_cast<EBuiltinId>(Code[Cursor + 1u]) == EBuiltinId::ToString))
				{
					return false;
				}
				if (Opcode == EOpcode::NewDynamicArray || Opcode == EOpcode::Call || Opcode == EOpcode::CallBoundMethod)
				{
					const auto Operand = Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
					if (Opcode == EOpcode::NewDynamicArray)
					{
						if (Operand < Module.StringPool.size() && Module.StringPool[Operand] == "array<int>") return false;
					}
					else if (Operand < Module.Functions.size())
					{
						if (CallCount == Calls.size()) return false;
						Calls[CallCount++] = Operand;
					}
				}
				Cursor += Size;
			}
			// These are every possible source of an own feature. Only a completed
			// negative proof publishes call edges or consumes the shared work budget.
			for (std::size_t Index = 0u; Index < CallCount; ++Index)
			{
				if (!VisitCall(Calls[Index])) return false;
			}
			Instructions += Count;
			return true;
		}

		template <typename FVisitCall>
		bool DescribeFunctionFeatures(const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::uint8_t* Kinds,
		    std::uint8_t* Stored, std::size_t& Instructions,
		    const std::size_t InstructionLimit, FVisitCall&& VisitCall,
		    std::uint8_t& Features)
		{
			const auto LocalCount = Function.LocalCount;
			InitializeParameterKinds(Function, Kinds, Stored, LocalCount);
			std::uint8_t PreviousKind = 0u;
			const auto& Code = Function.Code;
			for (std::size_t Cursor = 0u; Cursor < Code.size();)
			{
				if (++Instructions > InstructionLimit) return false;
				const auto Opcode = static_cast<EOpcode>(Code[Cursor]);
				const auto Size = Bytecode::InstructionSize(Opcode);
				if (!Size || Size > Code.size() - Cursor) return false;
				const auto Operand = Size >= 3u ? Bytecode::ReadU16LE(Code.data() + Cursor + 1u) : std::uint16_t{0};
				std::uint8_t CurrentKind = 0u;
				if (Opcode == EOpcode::LoadLocal && Operand < LocalCount)
				{
					CurrentKind = Kinds[Operand];
				}
				else if (Opcode == EOpcode::LoadGlobal && Operand < Module.GlobalTypeNames.size())
				{
					CurrentKind = Module.GlobalTypeNames[Operand] == "string" ? 2u : Module.GlobalTypeNames[Operand] == "wstring" ? 3u
																																  : 0u;
				}
				else if ((Opcode == EOpcode::ConstI32 || Opcode == EOpcode::ConstStr) && Operand < Module.Constants.size())
				{
					const auto ConstantKind = Module.Constants[Operand].Kind;
					CurrentKind = ConstantKind == EConstantKind::I32 || ConstantKind == EConstantKind::I64 ? 1u :
					    ConstantKind == EConstantKind::Str ? 2u : ConstantKind == EConstantKind::WStr ? 3u : 0u;
				}
				else if (Opcode == EOpcode::NewDynamicArray && Operand < Module.StringPool.size() && Module.StringPool[Operand] == "array<int>")
				{
					CurrentKind = 4u;
				}
				else if (Opcode == EOpcode::StoreLocal && Operand < LocalCount && !Stored[Operand])
				{
					Kinds[Operand] = PreviousKind;
					Stored[Operand] = 1u;
				}
				else if ((Opcode == EOpcode::Call || Opcode == EOpcode::CallBoundMethod) && Operand < Module.Functions.size())
				{
					if (!VisitCall(Operand)) return false;
				}
				else if (Opcode == EOpcode::CallBuiltin)
				{
					if (static_cast<EBuiltinId>(Code[Cursor + 1u]) == EBuiltinId::CharCode)
					{
						Features |= TextFeature;
					}
					if (static_cast<EBuiltinId>(Code[Cursor + 1u]) == EBuiltinId::ToString && Code[Cursor + 2u] == 1u && (PreviousKind == 1u || PreviousKind == 2u || PreviousKind == 3u) &&
					    Code.size() - Cursor >= Size + 4u && static_cast<EOpcode>(Code[Cursor + Size]) == EOpcode::Add &&
					    static_cast<EOpcode>(Code[Cursor + Size + 1u]) == EOpcode::StoreLocal)
					{
						const auto Target = Bytecode::ReadU16LE(Code.data() + Cursor + Size + 2u);
						if (Target < LocalCount && Kinds[Target] == 2u)
						{
							Features |= TextFeature;
						}
					}
				}
				if (Opcode != EOpcode::Nop)
				{
					PreviousKind = CurrentKind;
				}
				Cursor += Size;
			}
			if (IsIdentityLeaf(Function))
			{
				Features |= Function.ClosureTypeName == "fn(string)->string" ||
				    Function.ClosureTypeName == "fn(wstring)->wstring" ? TextFeature : IdentityInputFeature;
			}
			if (LocalCount && std::find(Kinds, Kinds + LocalCount, 4u) != Kinds + LocalCount &&
				VmIndexedExpressions::FCache::CanFuseInFunction(Module, Function, Kinds, LocalCount))
			{
				Features |= IndexedFeature;
			}
			return true;
		}
	}

	bool TryDescribeReachableFeatures(const FBytecodeModule& Module,
	    const FBytecodeFunction& Root, std::uint8_t& OutFeatures) noexcept
	{
		constexpr std::size_t InlineFunctions = 64u;
		constexpr std::size_t InlineLocals = 256u;
		constexpr std::size_t InlineInstructions = 8192u;
		if (Module.Functions.size() > MaxFunctions) return false;
		std::array<const FBytecodeFunction*, InlineFunctions> Functions;
		Functions[0] = &Root;
		std::size_t FunctionCount = 1u;
		std::size_t Instructions = 0u;
		std::uint8_t Result = 0u;
		// One pair is reused for every visited function; caller graphs and local
		// kind vectors are never allocated for the common small reachable closure.
		std::array<std::uint8_t, InlineLocals> Kinds, Stored;
		for (std::size_t Index = 0u; Index < FunctionCount; ++Index)
		{
			const auto& Function = *Functions[Index];
			if (Function.LocalCount > InlineLocals) return false;
			std::uint8_t Features = 0u;
			const auto VisitCall = [&](const std::uint16_t Target)
			{
				const auto* Callee = &Module.Functions[Target];
				if (std::find(Functions.begin(), Functions.begin() + FunctionCount, Callee) !=
					Functions.begin() + FunctionCount)
				{
					return true;
				}
				if (FunctionCount == Functions.size()) return false;
				Functions[FunctionCount++] = Callee;
				return true;
			};
			if (TryDescribeFeaturelessFunction(Module, Function, Instructions, InlineInstructions, VisitCall))
			{
				continue;
			}
			std::fill_n(Kinds.begin(), Function.LocalCount, std::uint8_t{0});
			std::fill_n(Stored.begin(), Function.LocalCount, std::uint8_t{0});
			if (!DescribeFunctionFeatures(Module, Function, Kinds.data(), Stored.data(),
					Instructions, InlineInstructions, VisitCall, Features))
			{
				return false;
			}
			// Reverse call-edge propagation is the union over the reachable closure.
			// A callee's dynamic identity-input flag never propagates to its caller.
			Result |= Features & PropagatedFeatures;
			if (Index == 0u)
			{
				Result |= Features & IdentityInputFeature;
			}
		}
		OutFeatures = Result;
		return true;
	}

	void FCache::Reset() noexcept
	{
		Entries.clear();
		InlineEntryCount = 0u;
		NextInlineEntry = 0u;
		LastFunction = nullptr;
		LastFeatures = 0u;
		bReady = false;
	}

	std::size_t FCache::GetRetainedBytes() const noexcept
	{
		return Entries.capacity() * sizeof(FEntry);
	}

	void FCache::Build(const FBytecodeModule& Module)
	{
		if (Module.Functions.size() > MaxFunctions) return;
		std::vector<std::uint8_t> Features(Module.Functions.size());
		std::vector<std::vector<std::size_t>> Callers(Features.size());
		std::vector<std::size_t> Queue;
		std::size_t Instructions = 0u;
		std::size_t CallEdges = 0u;
		for (std::size_t FunctionIndex = 0u; FunctionIndex < Features.size(); ++FunctionIndex)
		{
			const auto& Function = Module.Functions[FunctionIndex];
			std::vector<std::uint8_t> Kinds(Function.LocalCount), Stored(Function.LocalCount);
			const auto VisitCall = [&](const std::uint16_t Target)
			{
				if (++CallEdges > MaxCallEdges) return false;
				Callers[Target].push_back(FunctionIndex);
				return true;
			};
			if (!DescribeFunctionFeatures(Module, Function, Kinds.data(), Stored.data(),
					Instructions, MaxInstructions, VisitCall, Features[FunctionIndex]))
			{
				return;
			}
			if (Features[FunctionIndex] & PropagatedFeatures)
			{
				Queue.push_back(FunctionIndex);
			}
		}
		for (std::size_t Cursor = 0u; Cursor < Queue.size(); ++Cursor)
		{
			const auto Inherited = Features[Queue[Cursor]] & PropagatedFeatures;
			for (const auto Caller : Callers[Queue[Cursor]])
			{
				if ((Features[Caller] & Inherited) == Inherited)
				{
					continue;
				}
				Features[Caller] |= Inherited;
				Queue.push_back(Caller);
			}
		}
		std::vector<FEntry> CandidateEntries;
		for (std::size_t FunctionIndex = 0u; FunctionIndex < Features.size(); ++FunctionIndex)
		{
			if (Features[FunctionIndex])
			{
				CandidateEntries.push_back({&Module.Functions[FunctionIndex], Features[FunctionIndex]});
			}
		}
		std::sort(CandidateEntries.begin(), CandidateEntries.end(), [](const FEntry& Left, const FEntry& Right)
		{
			return std::less<const FBytecodeFunction*>{}(Left.Function, Right.Function);
		});
		Entries.swap(CandidateEntries);
	}

	std::uint8_t FCache::GetFunctionFeatures(const FBytecodeModule& Module,
	    const FBytecodeFunction& Function) noexcept
	{
		if (&Function == LastFunction) return LastFeatures;
		LastFunction = &Function;
		LastFeatures = 0u;
		if (!bReady)
		{
			for (std::size_t Index = 0u; Index < InlineEntryCount; ++Index)
			{
				if (InlineEntries[Index].Function == &Function)
				{
					LastFeatures = InlineEntries[Index].Features;
					return LastFeatures;
				}
			}
			if (TryDescribeReachableFeatures(Module, Function, LastFeatures))
			{
				InlineEntries[NextInlineEntry] = {&Function, LastFeatures};
				NextInlineEntry = (NextInlineEntry + 1u) % InlineEntries.size();
				InlineEntryCount = std::min(InlineEntryCount + 1u, InlineEntries.size());
				return LastFeatures;
			}
			bReady = true;
			try
			{
				Build(Module);
			}
			catch (const std::bad_alloc&)
			{
				// An optional optimization cannot turn memory pressure into a new
				// language failure, or retry the same allocation on every host call.
				Entries.clear();
			}
		}
		const auto Entry = std::lower_bound(Entries.begin(), Entries.end(), &Function,
		    [](const FEntry& Candidate, const FBytecodeFunction* Target)
		{
			return std::less<const FBytecodeFunction*>{}(Candidate.Function, Target);
		});
		if (Entry != Entries.end() && Entry->Function == &Function)
		{
			LastFeatures = Entry->Features;
		}
		return LastFeatures;
	}

	std::uint8_t FCache::GetFeatures(const FBytecodeModule& Module, const FVmExecutionView& State) noexcept
	{
		if (State.GetFrames().empty()) return 0u;
		const auto& Frame = State.GetFrames().back();
		// Initializers have no function entry and cannot select either feature.
		// Leave discovery until the actual entry function is known.
		if (!Frame.Function) return 0u;
		const auto Features = GetFunctionFeatures(Module, *Frame.Function);
		auto Result = Features & PropagatedFeatures;
		if ((Features & IdentityInputFeature) && !Frame.Locals.empty() &&
			(std::holds_alternative<std::string>(Frame.Locals[0]) || std::holds_alternative<std::u16string>(Frame.Locals[0])))
		{
			Result |= TextFeature;
		}
		return static_cast<std::uint8_t>(Result);
	}
}

#if defined(_MSC_VER) && defined(_M_X64)
#pragma code_seg(pop)
#endif
