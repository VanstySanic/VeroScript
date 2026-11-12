// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJitControlled.h"
#include "VeroScriptJitAbi.h"
#include "VeroScriptMethodKey.h"
#include <algorithm>
#include <array>

#if VEROSCRIPT_HAS_ASMJIT
#if VEROSCRIPT_ASMJIT_X64
#include <asmjit/x86.h>
#elif VEROSCRIPT_ASMJIT_A64
#include <asmjit/a64.h>
#endif
#endif

namespace VeroScript::AsmJit
{
	#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
	namespace
	{
		enum class EOwnedLoopScan : std::uint8_t { None, Loop, Stopped };

		bool IsRelativeBranch(const EOpcode Opcode) noexcept
		{
			return Opcode == EOpcode::Jmp || Opcode == EOpcode::JmpIfFalse ||
			    Opcode == EOpcode::JmpIfLocalGeImm || Opcode == EOpcode::BeginLocalStatic;
		}

		// This is only a cost hint for a Core-owned immutable program. It proves
		// instruction structure and branch boundaries, never language semantics.
		EOwnedLoopScan ScanUnsupportedOwnedLoop(const FJitCompileRequest& Request,
		    const FBytecodeFunction& Function)
		{
			if (!Request.RequiredGovernance || !Request.Program.Owner ||
				Request.Program.Owner.get() != Request.Program.Module ||
				Function.Code.empty() || Function.Code.size() > 65536u)
			{
				return EOwnedLoopScan::None;
			}
			const auto& Code = Function.Code;
			std::vector<std::uint8_t> Boundaries(Code.size() + 1u, 0);
			std::size_t Instructions = 0;
			for (std::size_t Offset = 0; Offset < Code.size();)
			{
				if ((Instructions++ & 255u) == 0 && !Request.ExecutionCheck.Poll()) return EOwnedLoopScan::Stopped;
				if (Code[Offset] > static_cast<std::uint8_t>(EOpcode::GetIndexPathRef)) return EOwnedLoopScan::None;
				const auto Size = Bytecode::InstructionSize(static_cast<EOpcode>(Code[Offset]));
				if (!Size || Size > Code.size() - Offset) return EOwnedLoopScan::None;
				Boundaries[Offset] = 1;
				Offset += Size;
			}
			Boundaries.back() = 1;
			bool HasLoop = false;
			for (std::size_t Offset = 0; Offset < Code.size();)
			{
				if ((Instructions++ & 255u) == 0 && !Request.ExecutionCheck.Poll()) return EOwnedLoopScan::Stopped;
				const auto Opcode = static_cast<EOpcode>(Code[Offset]);
				const auto Size = Bytecode::InstructionSize(Opcode);
				if (IsRelativeBranch(Opcode))
				{
					// All four relative branches encode their displacement last.
					const auto Target = static_cast<std::int64_t>(Offset + Size) +
					    Bytecode::ReadI32LE(Code.data() + Offset + Size - 4u);
					if (Target < 0 || static_cast<std::uint64_t>(Target) > Code.size() ||
						!Boundaries[static_cast<std::size_t>(Target)])
					{
						return EOwnedLoopScan::None;
					}
					HasLoop |= static_cast<std::uint64_t>(Target) <= Offset;
				}
				Offset += Size;
			}
			if (!Request.ExecutionCheck.Poll()) return EOwnedLoopScan::Stopped;
			return HasLoop ? EOwnedLoopScan::Loop : EOwnedLoopScan::None;
		}

		bool IsPrimitiveType(const std::string_view Type) noexcept
		{
			return Type == "void" || Type == "bool" || Type == "int" || Type == "uint" ||
			    Type == "int8" || Type == "uint8" || Type == "int16" || Type == "uint16" ||
			    Type == "int64" || Type == "uint64" || Type == "float" || Type == "double";
		}

		bool HasPrimitiveSignature(const FBytecodeFunction& Function) noexcept
		{
			const std::string_view Signature(Function.ClosureTypeName);
			const auto End = Signature.find(')');
			if (Signature.substr(0, 3) != "fn(" || End == Signature.npos || Signature.substr(End, 3) != ")->") return false;
			for (std::size_t Begin = 3; Begin < End;)
			{
				const auto Comma = Signature.find(',', Begin);
				const auto Stop = Comma == Signature.npos || Comma > End ? End : Comma;
				if (!IsPrimitiveType(Signature.substr(Begin, Stop - Begin))) return false;
				Begin = Stop + 1;
			}
			return IsPrimitiveType(Signature.substr(End + 3));
		}

		bool IsShortControlledLeaf(const FBytecodeModule& Module, const FBytecodeFunction& Function,
		    std::size_t& Remaining, const unsigned Depth = 0)
		{
			if (Depth == 4 || Remaining == 0 || Function.LocalCount > 8 || Function.Code.size() > 96 ||
				Function.bUsesLocalStaticStorage || !Function.Captures.empty() || !Function.ExceptionRegions.empty() ||
				BytecodeFunctionHasRefParams(Function) || !HasPrimitiveSignature(Function))
			{
				return false;
			}
			// A short VM-only field initializer can be cheaper too. This is a cost
			// walk, not a native plan or a second semantic validator.
			std::size_t Count = 0;
			for (std::size_t Offset = 0; Offset < Function.Code.size();)
			{
				if (Remaining == 0 || ++Count > 24) return false;
				--Remaining;
				const auto Opcode = static_cast<EOpcode>(Function.Code[Offset]);
				const auto Size = Bytecode::InstructionSize(Opcode);
				if (!Size || Size > Function.Code.size() - Offset) return false;
				switch (Opcode)
				{
				case EOpcode::Nop:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
				case EOpcode::ConstBool:
				case EOpcode::ConstNull:
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
				case EOpcode::LoadField:
				case EOpcode::LoadFieldKeep:
				case EOpcode::StoreField:
				case EOpcode::LoadThis:
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::IncField:
				case EOpcode::MakeLocalRef:
				case EOpcode::MakeGlobalRef:
				case EOpcode::MakeThisFieldRef:
				case EOpcode::MakeFieldRef:
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
				case EOpcode::Eq:
				case EOpcode::Ne:
				case EOpcode::Lt:
				case EOpcode::Le:
				case EOpcode::Gt:
				case EOpcode::Ge:
				case EOpcode::Not:
				case EOpcode::Neg:
				case EOpcode::BitNot:
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
				case EOpcode::Shl:
				case EOpcode::Shr:
				case EOpcode::Dup:
				case EOpcode::Pop:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::AddLocalLocal:
					{
						break;
					}
				case EOpcode::InitializeThisField:
					{
						if (!Function.bIsMethod || Function.OwnerTypeIndex >= Module.Types.size()) return false;
						const auto Name = Bytecode::ReadU16LE(Function.Code.data() + Offset + 1u);
						if (Name >= Module.StringPool.size()) return false;
						bool Matched = false;
						for (const auto &Field : Module.Types[Function.OwnerTypeIndex].Fields)
						{
							if (Remaining == 0) return false;
							--Remaining;
							if (Field.Name == Module.StringPool[Name])
							{
								Matched = IsPrimitiveType(Field.TypeName);
								break;
							}
						}
						if (!Matched) return false;
						break;
					}
				case EOpcode::Call:
				case EOpcode::CallBoundMethod:
					{
						const auto Target = Bytecode::ReadU16LE(Function.Code.data() + Offset + 1u);
						if (Target >= Module.Functions.size() || !IsShortControlledLeaf(Module, Module.Functions[Target], Remaining, Depth + 1)) return false;
						break;
					}
				case EOpcode::LoadGlobal:
				case EOpcode::StoreGlobal:
					{
						const auto Slot = Bytecode::ReadU16LE(Function.Code.data() + Offset + 1u);
						if (Slot >= Module.GlobalTypeNames.size() || !IsPrimitiveType(Module.GlobalTypeNames[Slot])) return false;
						break;
					}
				case EOpcode::Return:
					return Offset + Size == Function.Code.size();
				default:
					return false;
				}
				Offset += Size;
			}
			return false;
		}

		// Suppression is stronger than choosing the VM for the parent: it affects
		// every descendant. Keep it only for a bounded primitive leaf closure. A
		// heavy, recursive, indirect or host-dependent callee keeps its own dispatch.
		bool CanSuppressNestedControlledCalls(const FJitCompileRequest& Request,
		    const FBytecodeFunction& Function)
		{
			const auto& Module = *Request.Program.Module;
			if (Function.bIsMethod || !Function.Captures.empty() ||
				BytecodeFunctionHasRefParams(Function) || !HasPrimitiveSignature(Function))
			{
				return false;
			}
			std::size_t Remaining = 2048;
			std::array<std::uint16_t, 128> CreatedTypes;
			std::size_t CreatedTypeCount = 0;
			const auto AddType = [&](const std::uint16_t Type)
			{
				if (Type >= Module.Types.size()) return false;
				if (std::find(CreatedTypes.begin(), CreatedTypes.begin() + CreatedTypeCount, Type) !=
					CreatedTypes.begin() + CreatedTypeCount)
				{
					return true;
				}
				if (CreatedTypeCount == CreatedTypes.size()) return false;
				CreatedTypes[CreatedTypeCount++] = Type;
				return true;
			};
			std::size_t Checked = 0;
			for (std::size_t Offset = 0; Offset < Function.Code.size();)
			{
				if ((Checked++ & 255u) == 0 && !Request.ExecutionCheck.Poll()) return false;
				if (Remaining == 0) return false;
				--Remaining;
				const auto Opcode = static_cast<EOpcode>(Function.Code[Offset]);
				const auto Size = Bytecode::InstructionSize(Opcode);
				if (!Size || Size > Function.Code.size() - Offset) return false;
				if (Opcode == EOpcode::Call || Opcode == EOpcode::CallBoundMethod)
				{
					const auto Target = Bytecode::ReadU16LE(Function.Code.data() + Offset + 1u);
					if (Target >= Module.Functions.size() || !IsShortControlledLeaf(Module, Module.Functions[Target], Remaining)) return false;
				}
				else if (Opcode == EOpcode::CallMethod)
				{
					const auto Name = Bytecode::ReadU16LE(Function.Code.data() + Offset + 1u);
					if (Name >= Module.StringPool.size()) return false;
					const auto& Wanted = Module.StringPool[Name];
					bool Matched = false;
					for (const auto& Method : Module.Functions)
					{
						if ((Checked++ & 255u) == 0 && !Request.ExecutionCheck.Poll()) return false;
						if (Remaining == 0) return false;
						--Remaining;
						if (!Method.bIsMethod || !MethodLinkageLeafMatches(Method.Name, Wanted))
						{
							continue;
						}
						Matched = true;
						if (!(Method.bIsAbstract && Method.Code.empty()) && !IsShortControlledLeaf(Module, Method, Remaining)) return false;
					}
					// Container push is a Core operation; matching script overrides
					// were still proved above. Other unresolved receivers are unknown.
					if (!Matched && Wanted != "push") return false;
				}
				else if (Opcode == EOpcode::CallBuiltin)
				{
					const auto Id = static_cast<EBuiltinId>(Function.Code[Offset + 1u]);
					if (Id != EBuiltinId::Len && Id != EBuiltinId::ToInt && Id != EBuiltinId::Print &&
						Id != EBuiltinId::Eprint && Id != EBuiltinId::Assert)
					{
						return false;
					}
				}
				// Fixed-array default factories can call constructors/destructors
				// without a NewObject in this body; their closure is not proved here.
				else if (Opcode == EOpcode::CallNative || Opcode == EOpcode::CallIndirect ||
						 Opcode == EOpcode::CallMemory || Opcode == EOpcode::MakeClosure ||
						 Opcode == EOpcode::CloneStruct || Opcode == EOpcode::DefaultCopyConstruct ||
						 Opcode == EOpcode::NewNativeArray || Opcode == EOpcode::NewUninitializedNativeArray)
				{
					return false;
				}
				else if (Opcode == EOpcode::CloneValueOfType)
				{
					const auto Name = Bytecode::ReadU16LE(Function.Code.data() + Offset + 1u);
					if (Name >= Module.StringPool.size()) return false;
					const auto& Wanted = Module.StringPool[Name];
					if (!IsPrimitiveType(Wanted))
					{
						bool Matched = false;
						for (std::size_t Type = 0; Type < Module.Types.size(); ++Type)
						{
							if (Remaining == 0) return false;
							--Remaining;
							if (Module.Types[Type].Name != Wanted)
							{
								continue;
							}
							Matched = Module.Types[Type].Kind == EScriptObjectKind::Class && AddType(static_cast<std::uint16_t>(Type));
							break;
						}
						if (!Matched) return false;
					}
				}
				else if (Opcode == EOpcode::NewObject || Opcode == EOpcode::NewUninitializedObject)
				{
					if (!AddType(Bytecode::ReadU16LE(Function.Code.data() + Offset + 1u))) return false;
				}
				else if (Opcode == EOpcode::LoadGlobal || Opcode == EOpcode::StoreGlobal)
				{
					const auto Slot = Bytecode::ReadU16LE(Function.Code.data() + Offset + 1u);
					if (Slot >= Module.GlobalTypeNames.size() || !IsPrimitiveType(Module.GlobalTypeNames[Slot])) return false;
				}
				Offset += Size;
			}
			// Only actually constructed types can introduce hidden field/default,
			// base, copy or destructor work. Unrelated declarations are not callees.
			for (std::size_t Index = 0; Index < CreatedTypeCount; ++Index)
			{
				if (!Request.ExecutionCheck.Poll()) return false;
				const auto& Type = Module.Types[CreatedTypes[Index]];
				if (!Type.HostBaseTypeName.empty()) return false;
				for (const auto Base : Type.BaseTypeIndices)
				{
					if (Remaining == 0 || !AddType(Base)) return false;
					--Remaining;
				}
				for (const auto& Field : Type.Fields)
				{
					if (Remaining == 0 || Field.bHasInitCode || !IsPrimitiveType(Field.TypeName)) return false;
					--Remaining;
				}
				for (const auto Target : {Type.CopyConstructorIndex, Type.DestructorFunctionIndex})
				{
					if (Target != 0xFFFF && (Target >= Module.Functions.size() ||
												!IsShortControlledLeaf(Module, Module.Functions[Target], Remaining)))
					{
						return false;
					}
				}
			}
			return true;
		}
	}
	#endif

	struct FControlledCodeCache::FImpl
	{
		FJitStats Stats;
		std::size_t Executables = 0;
		std::size_t ExecutableMetadata = 0;
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		static asmjit::JitAllocator::CreateParams Parameters()
		{
			asmjit::JitAllocator::CreateParams Result{};
			Result.options = asmjit::JitAllocatorOptions::kUseDualMapping;
			return Result;
		}
		asmjit::JitRuntime Runtime{Parameters()};
		#endif
	};

	FControlledCodeCache::FControlledCodeCache() : Impl(std::make_shared<FImpl>())
	{
	}

	FControlledCodeCache::~FControlledCodeCache() = default;

	EJitCompilationResult FControlledCodeCache::Compile(const FJitCompileRequest& Request, FJitCompileResponse& Response)
	{
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		using namespace asmjit;
		const auto PreferVm = [&]()
		{
			const bool SuppressNested = Request.RequiredGovernance == 0 ||
			    CanSuppressNestedControlledCalls(Request, Request.Program.Module->Functions[Request.FunctionIndex]);
			if (!Request.ExecutionCheck.Poll())
			{
				Response.Reason = "controlled compilation stopped by host";
				return EJitCompilationResult::Failed;
			}
			class FVmPreferred final : public IJitExecutable
			{
			public:
				//~ Begin IJitExecutable Interface
				~FVmPreferred() override
				{
					State->ExecutableMetadata -= sizeof(*this);
				}
				EJitInvocationResult Invoke(FJitCallContext&) const override
				{
					return EJitInvocationResult::NotEntered;
				}
				//~ End IJitExecutable Interface

				explicit FVmPreferred(std::shared_ptr<FImpl> InState) : State(std::move(InState))
				{
					State->ExecutableMetadata += sizeof(*this);
				}

			private:
				std::shared_ptr<FImpl> State;
			};
			auto Artifact = std::make_shared<FJitCompiledFunction>();
			Artifact->Identity = Request.Program.Identity;
			Artifact->ProgramOwner = Request.Program.Owner;
			Artifact->FunctionIndex = Request.FunctionIndex;
			Artifact->GovernanceCapabilities = ControlledExecutionCapabilities | ExecutionObservationCapability;
			Artifact->bPreferVmForExecution = true;
			Artifact->bSuppressNestedBackendOnFallback = SuppressNested;
			Artifact->Executable = std::make_shared<FVmPreferred>(Impl);
			Response.Function = std::move(Artifact);
			Response.Reason.clear();
			return EJitCompilationResult::Ready;
		};
		std::vector<FCompiledInstruction> Plan;
		const auto& Function = Request.Program.Module->Functions[Request.FunctionIndex];
		if (!BuildCompiledFunctionPlan(Function, Plan, Response.Reason, Request.ExecutionCheck))
		{
			if (!Request.ExecutionCheck.Poll()) return EJitCompilationResult::Failed;
			const auto Loop = ScanUnsupportedOwnedLoop(Request, Function);
			if (Loop == EOwnedLoopScan::Stopped)
			{
				Response.Reason = "controlled compilation stopped by host";
				return EJitCompilationResult::Failed;
			}
			return Loop == EOwnedLoopScan::Loop ? PreferVm() : EJitCompilationResult::Unsupported;
		}
		const bool HasLoop = std::any_of(Plan.begin(), Plan.end(), [](const FCompiledInstruction& Instruction)
		{
			return (Instruction.Opcode == EOpcode::Jmp || Instruction.Opcode == EOpcode::JmpIfFalse || Instruction.Opcode == EOpcode::JmpIfLocalGeImm || Instruction.Opcode == EOpcode::BeginLocalStatic) && Instruction.Target <= Instruction.Offset;
		});
		const bool ScalarLoop = HasLoop && Function.LocalCount <= 32u && Plan.size() <= 128u && std::all_of(Plan.begin(), Plan.end(), [&](const FCompiledInstruction& I)
		{
			switch (I.Opcode)
			{
			case EOpcode::Nop:
			case EOpcode::ConstBool:
			case EOpcode::LoadLocal:
			case EOpcode::StoreLocal:
			case EOpcode::Add:
			case EOpcode::Sub:
			case EOpcode::Mul:
			case EOpcode::Div:
			case EOpcode::Mod:
			case EOpcode::BitAnd:
			case EOpcode::BitOr:
			case EOpcode::BitXor:
			case EOpcode::Eq:
			case EOpcode::Ne:
			case EOpcode::Lt:
			case EOpcode::Le:
			case EOpcode::Gt:
			case EOpcode::Ge:
			case EOpcode::Jmp:
			case EOpcode::JmpIfFalse:
			case EOpcode::Return:
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
			case EOpcode::JmpIfLocalGeImm:
				return true;
			case EOpcode::ConstI32:
				{
					const auto Index = Bytecode::ReadU16LE(Function.Code.data() + I.Offset + 1u);
					return Index < Request.Program.Module->Constants.size() && Request.Program.Module->Constants[Index].Kind == EConstantKind::I32;
				}
			case EOpcode::CallBuiltin:
				{
					const auto Id = static_cast<EBuiltinId>(Function.Code[I.Offset + 1u]);
					return Function.Code[I.Offset + 2u] == 1u && Id == EBuiltinId::ToInt;
				}
			default:
				return false;
			}
		});
		const bool VmBatchedGovernance = Request.RequiredGovernance != 0 &&
		    (Request.RequiredGovernance & ExecutionPollingCapability) == 0;
		if (HasLoop && (!ScalarLoop || VmBatchedGovernance))
		{
			// The controlled emitter currently executes each opcode through the Core
			// instruction service. A loop adds native/block transitions to the VM's
			// existing per-op checks without lowering arithmetic. Keep the canonical
			// VM loop (including its callees) until there is a profitable controlled
			// lowering. Quota/permission-only arithmetic can additionally use the
			// VM's exact batching, which this opcode-service emitter cannot match.
			// Do not allocate executable memory for either cost decision.
			return PreferVm();
		}
		if (!(Request.AvailableServices & CompiledInstructionServiceCapability))
		{
			Response.Reason = "Core compiled instruction services are unavailable";
			return EJitCompilationResult::Unsupported;
		}
		if (!Request.AvailableResources.ExecutableBytes)
		{
			Response.Reason = "controlled code budget exhausted";
			return EJitCompilationResult::Deferred;
		}
		const auto Instructions = std::make_shared<const std::vector<FCompiledInstruction>>(std::move(Plan));
		const auto& Blocks = *Instructions;
		CodeHolder Code;
		if (Code.init(Impl->Runtime.environment(), Impl->Runtime.cpu_features()) != Error::kOk)
		{
			Response.Reason = "controlled code initialization failed";
			return EJitCompilationResult::Failed;
		}
		struct FErrors final : ErrorHandler
		{
			bool Failed = false;
			void handle_error(Error, const char*, BaseEmitter*) override
			{
				Failed = true;
			}
		} Errors;
		Code.set_error_handler(&Errors);
		#if VEROSCRIPT_ASMJIT_X64
		x86::Assembler A(&Code);
		#else
		a64::Assembler A(&Code);
		#endif
		std::vector<Label> Labels;
		Labels.reserve(Blocks.size());
		for (std::size_t I = 0; I < Blocks.size(); ++I)
		{
			if ((I & 255u) == 0 && !Request.ExecutionCheck.Poll())
			{
				Response.Reason = "controlled compilation stopped by host";
				return EJitCompilationResult::Failed;
			}
			Labels.push_back(A.new_label());
		}
		const auto Done = A.new_label();
		const auto Step = +[](ICompiledFunctionServices* Services, const FCompiledInstruction* Begin, std::size_t Count) noexcept
		{
			return Services->ExecuteBlock(Begin, Count);
		};
		#if VEROSCRIPT_ASMJIT_X64
		using namespace x86;
		#if defined(_WIN32)
		const auto Arg0 = rcx, Arg1 = rdx, Arg2 = r8;
		constexpr int Shadow = 32;
		#else
		const auto Arg0 = rdi, Arg1 = rsi, Arg2 = rdx;
		constexpr int Shadow = 0;
		#endif
		A.push(rbx);
		A.mov(rbx, Arg0);
		if (Shadow)
		{
			A.sub(rsp, Shadow);
		}
		#else
		using namespace a64;
		// BTI c is a harmless hint on older CPUs and an indirect-call landing pad.
		A.embed_uint32(0xd503245fu);
		A.stp(x29, x30, ptr_pre(sp, -16));
		A.stp(x19, x20, ptr_pre(sp, -16));
		A.add(x29, sp, 16);
		A.mov(x19, x0);
		#endif
		std::vector<std::uint32_t> Targets;
		for (const auto& Instruction : Blocks)
		{
			if (Instruction.Opcode == EOpcode::Jmp || Instruction.Opcode == EOpcode::JmpIfFalse || Instruction.Opcode == EOpcode::JmpIfLocalGeImm || Instruction.Opcode == EOpcode::BeginLocalStatic)
			{
				Targets.push_back(Instruction.Target);
			}
		}
		std::sort(Targets.begin(), Targets.end());
		std::size_t NextCheck = 0;
		for (std::size_t I = 0; I < Blocks.size();)
		{
			if (I >= NextCheck)
			{
				if (!Request.ExecutionCheck.Poll())
				{
					Response.Reason = "controlled compilation stopped by host";
					return EJitCompilationResult::Failed;
				}
				NextCheck = I + 256u;
			}
			A.bind(Labels[I]);
			const std::size_t Begin = I;
			while (I + 1u < Blocks.size() && I - Begin + 1u < 32u && Blocks[I].Opcode != EOpcode::Return && Blocks[I].Opcode != EOpcode::Jmp && Blocks[I].Opcode != EOpcode::JmpIfFalse && Blocks[I].Opcode != EOpcode::JmpIfLocalGeImm && Blocks[I].Opcode != EOpcode::BeginLocalStatic && !std::binary_search(Targets.begin(), Targets.end(), Blocks[I + 1u].Offset))
			{
				++I;
			}
			const auto& Instruction = Blocks[I];
			#if VEROSCRIPT_ASMJIT_X64
			A.mov(Arg0, rbx);
			A.mov(Arg1, reinterpret_cast<std::uintptr_t>(Blocks.data() + Begin));
			A.mov(Arg2, I - Begin + 1u);
			A.mov(rax, reinterpret_cast<std::uintptr_t>(Step));
			A.call(rax);
			A.test(eax, eax);
			A.js(Done);
			#else
			A.mov(x0, x19);
			A.mov(x1, reinterpret_cast<std::uintptr_t>(Blocks.data() + Begin));
			A.mov(x2, I - Begin + 1u);
			A.mov(x16, reinterpret_cast<std::uintptr_t>(Step));
			A.blr(x16);
			const auto Continue = A.new_label();
			A.tbz(w0, 31, Continue);
			A.b(Done);
			A.bind(Continue);
			#endif
			if (Instruction.Opcode == EOpcode::Return)
			{
				#if VEROSCRIPT_ASMJIT_X64
				A.jmp(Done);
				#else
				A.b(Done);
				#endif
			}
			else if (Instruction.Opcode == EOpcode::Jmp || Instruction.Opcode == EOpcode::JmpIfFalse || Instruction.Opcode == EOpcode::JmpIfLocalGeImm || Instruction.Opcode == EOpcode::BeginLocalStatic)
			{
				const auto Target = std::lower_bound(Blocks.begin(), Blocks.end(), Instruction.Target,
				                        [](const auto& Item, auto Offset)
				{
					return Item.Offset < Offset;
				}) - Blocks.begin();
				#if VEROSCRIPT_ASMJIT_X64
				A.test(eax, eax);
				A.jnz(Labels[Target]);
				#else
				const auto Fallthrough = A.new_label();
				A.cbz(w0, Fallthrough);
				A.b(Labels[Target]);
				A.bind(Fallthrough);
				#endif
			}
			++I;
		}
		A.bind(Done);
		#if VEROSCRIPT_ASMJIT_X64
		if (Shadow)
		{
			A.add(rsp, Shadow);
		}
		A.pop(rbx);
		A.ret();
		#else
		A.ldp(x19, x20, ptr_post(sp, 16));
		A.ldp(x29, x30, ptr_post(sp, 16));
		A.ret(x30);
		#endif
		FCompiledFunctionEntry Entry = nullptr;
		if (Code.code_size() > Request.AvailableResources.ExecutableBytes)
		{
			Response.Reason = "controlled code exceeds the remaining executable budget";
			return EJitCompilationResult::Deferred;
		}
		if (Errors.Failed || !Request.ExecutionCheck.Poll() || Impl->Runtime.add(&Entry, &Code) != Error::kOk)
		{
			Response.Reason = "controlled executable memory allocation or emission failed";
			return EJitCompilationResult::Failed;
		}
		class FExecutable final : public IJitExecutable
		{
		public:
			FExecutable(std::shared_ptr<FImpl> State, FCompiledFunctionEntry Code,
			    std::shared_ptr<const std::vector<FCompiledInstruction>> Instructions, const bool Scalar)
			    : State(std::move(State)), Code(Code), Instructions(std::move(Instructions)), Scalar(Scalar)
			{
				++this->State->Executables;
				this->State->ExecutableMetadata += MetadataBytes();
			}
			~FExecutable() override
			{
				State->Runtime.release(Code);
				--State->Executables;
				State->ExecutableMetadata -= MetadataBytes();
			}
			EJitInvocationResult Invoke(FJitCallContext& Context) const override
			{
				if (Scalar)
				{
					if (Context.ArgumentCount && !Context.Arguments) return EJitInvocationResult::NotEntered;
					for (std::size_t Index = 0; Index < Context.ArgumentCount; ++Index)
					{
						if (!std::holds_alternative<std::int32_t>(Context.Arguments[Index])) return EJitInvocationResult::NotEntered;
					}
					auto* Services = Context.Services ? Context.Services->GetCompiledFunctionServices() : nullptr;
					if (!Services || !Services->EnableScalarBlocks()) return EJitInvocationResult::NotEntered;
				}
				return InvokeCompiledFunction(Context, Code, &State->Stats.ControlledNativeExecutions, EJitExecutionKind::ControlledNative);
			}

		private:
			std::size_t MetadataBytes() const noexcept
			{
				return sizeof(*this) + sizeof(*Instructions) + 2u * sizeof(void*) + Instructions->capacity() * sizeof(FCompiledInstruction);
			}
			std::shared_ptr<FImpl> State;
			FCompiledFunctionEntry Code;
			std::shared_ptr<const std::vector<FCompiledInstruction>> Instructions;
			bool Scalar = false;
		};
		// Establish the code owner before any further allocation can throw.
		std::shared_ptr<FExecutable> Executable;
		try
		{
			Executable = std::make_shared<FExecutable>(Impl, Entry, Instructions, ScalarLoop);
		}
		catch (...)
		{
			Impl->Runtime.release(Entry);
			throw;
		}
		auto Artifact = std::make_shared<FJitCompiledFunction>();
		Artifact->Identity = Request.Program.Identity;
		Artifact->ProgramOwner = Request.Program.Owner;
		Artifact->FunctionIndex = Request.FunctionIndex;
		Artifact->ExecutionKind = EJitExecutionKind::ControlledNative;
		Artifact->GovernanceCapabilities = ControlledExecutionCapabilities | ExecutionObservationCapability;
		// A short stateful leaf is cheaper to interpret when its caller is
		// already in the VM. Root and governed calls retain native execution.
		Artifact->bPreferVmForNestedExecution = Function.bUsesLocalStaticStorage && !HasLoop && Instructions->size() <= 32u;
		if (Artifact->bPreferVmForNestedExecution)
		{
			Artifact->FallbackReason = EJitFallbackReason::CostModel;
			Response.Reason = "cost model: short local-static access in an interpreter caller";
		}
		Artifact->RequiredServices = CompiledInstructionServiceCapability;
		Artifact->Executable = std::move(Executable);
		Response.Function = std::move(Artifact);
		return EJitCompilationResult::Ready;
		#else
		(void)Request;
		Response.Reason = "controlled native emission is unavailable for this target";
		return EJitCompilationResult::Unsupported;
		#endif
	}

	FJitResourceUsage FControlledCodeCache::GetResourceUsage() const noexcept
	{
		FJitResourceUsage Result;
		Result.RetainedBytes = sizeof(FImpl) + Impl->ExecutableMetadata;
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		const auto Stats = Impl->Runtime.allocator().statistics();
		Result.ExecutableBytes = Stats.reserved_size();
		Result.RetainedBytes += Stats.overhead_size();
		#endif
		return Result;
	}

	FJitStats FControlledCodeCache::GetStatistics() const noexcept
	{
		auto Result = Impl->Stats;
		Result.NativeCfgEntries = Impl->Executables;
		return Result;
	}

	void FControlledCodeCache::ResetStatistics() noexcept
	{
		Impl->Stats = {};
	}
}
