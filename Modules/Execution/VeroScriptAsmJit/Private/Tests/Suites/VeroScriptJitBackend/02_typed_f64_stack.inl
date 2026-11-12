// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// A floating value survives a conditional edge, Dup, and a spill beyond the
// two volatile F64 operand registers. Scratch history is deliberately private.
#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
	{
		using namespace VeroScript;
		using namespace VeroScript::AsmJit;
		const auto Bits=[](double Value){std::uint64_t Result;std::memcpy(&Result,&Value,8);return Result;};
		const auto NaN=[](std::uint64_t Value){return (Value&0x7ff0000000000000ull)==0x7ff0000000000000ull&&(Value&0xfffffffffffffull)!=0;};
		const auto Same=[&](std::uint64_t Actual,double Expected){return Actual==Bits(Expected)||(NaN(Actual)&&NaN(Bits(Expected)));};
		for (bool Fail : {false, true})
		{
			FBytecodeModule Module;
			FBytecodeConstant Zero;Zero.Kind=EConstantKind::I32;Zero.I32=0;
			FBytecodeConstant Quarter;Quarter.Kind=EConstantKind::F64;Quarter.F64=0.25;
			FBytecodeConstant NegativeZero;NegativeZero.Kind=EConstantKind::F64;NegativeZero.F64=-0.0;
			Module.Constants={Zero,Quarter,NegativeZero};
			FBytecodeFunction F;F.Name="fp_join";F.ClosureTypeName="fn(int,bool,double)->double";F.ParamCount=3;F.LocalCount=5;F.bReturnsVoid=false;
			auto Op=[&](EOpcode Code){F.Code.push_back(static_cast<std::uint8_t>(Code));};
			auto U16=[&](std::uint16_t V){F.Code.push_back(V&255u);F.Code.push_back(V>>8u);};
			auto Local=[&](EOpcode Code,std::uint16_t Slot){Op(Code);U16(Slot);};
			auto Jump=[&](EOpcode Code){Op(Code);auto At=F.Code.size();F.Code.insert(F.Code.end(),4,0);return At;};
			auto Patch = [&](std::size_t At, std::size_t Target)
			{
				auto V = static_cast<std::uint32_t>(Target - (At + 4u));
				for (int B = 0; B < 4; ++B)
				{
					F.Code[At + B] = V >> (B * 8) & 255u;
				}
			};
			Local(EOpcode::ConstI32,0);Local(EOpcode::StoreLocal,3);Local(EOpcode::LoadLocal,2);Local(EOpcode::StoreLocal,4);
			auto Header=F.Code.size();Local(EOpcode::LoadLocal,3);Local(EOpcode::LoadLocal,0);Op(EOpcode::Lt);auto Exit=Jump(EOpcode::JmpIfFalse);
			Local(EOpcode::LoadLocal,4);Local(EOpcode::LoadLocal,1);auto Else=Jump(EOpcode::JmpIfFalse);
			Local(EOpcode::LoadLocal,2);auto Join=Jump(EOpcode::Jmp);Patch(Else,F.Code.size());Local(EOpcode::ConstF64,1);Patch(Join,F.Code.size());
			Op(EOpcode::Dup);Op(EOpcode::Add);Op(EOpcode::Add);Local(EOpcode::StoreLocal,4);Local(EOpcode::IncLocal,3);
			auto Back=Jump(EOpcode::Jmp);Patch(Back,Header);Patch(Exit,F.Code.size());Local(EOpcode::LoadLocal,4);
			std::size_t FailureOffset = 0;
			if (Fail)
			{
				Local(EOpcode::ConstF64, 2);
				FailureOffset = F.Code.size();
				Op(EOpcode::Div);
			}
			Op(EOpcode::Return);
			Module.Functions.push_back(std::move(F));JitNative::FArena Arena;auto Kernel=Arena.EmitLinearMixedCfg(Module,Module.Functions[0]);
			if(!Expect(Kernel!=nullptr,"emit typed F64 nonempty join/Dup fixture"))return EXIT_FAILURE;
			for (std::uint64_t SeedBits : {0ull, 0x8000000000000000ull, 0x3ff4000000000000ull, 0x7ff0000000000000ull, 0x7ff8000000000042ull})
			{
				for (int Count : {0, 1, 7})
				{
					for (bool Choice : {false, true})
					{
						for (bool Alias : {false, true})
						{
							double Seed;std::memcpy(&Seed,&SeedBits,8);volatile double Expected=Seed;
							for (int Step = 0; Step < Count; ++Step)
							{
								volatile double Value = Choice ? Seed : 0.25;
								volatile double Twice = Value + Value;
								Expected = Expected + Twice;
							}
				constexpr std::uint64_t Sentinel=0x12345678abcdef00ull;
				std::uint64_t Locals[]{static_cast<std::uint32_t>(Count),Choice?1u:0u,SeedBits,Sentinel,Sentinel};
				std::uint64_t Scratch[8];std::fill_n(Scratch+3,5,Sentinel);std::uint64_t Out=Sentinel;
				auto Error=DecodeJitKernelFailure(Kernel(nullptr,Locals,Scratch,Alias?&Scratch[1]:&Out,nullptr));
				bool Result=Fail?(Alias||Out==Sentinel):Same(Alias?Scratch[1]:Out,Expected);
				if (!Fail && Count == 0)
				{
					Result = (Alias ? Scratch[1] : Out) == SeedBits;
				}
				if (!Expect(Result && Error.Status == (Fail ? EJitRuntimeStatus::DivisionByZero : EJitRuntimeStatus::Ok) && (!Fail || Error.InstructionOffset == FailureOffset) && Locals[2] == SeedBits && Locals[3] == static_cast<std::uint64_t>(Count) && Same(Locals[4], Expected) && Scratch[3] == Sentinel && Scratch[4] == Sentinel && Scratch[5] == Sentinel && Scratch[6] == Sentinel && Scratch[7] == Sentinel,
						"typed F64 live joins/Dup/deep spills/NaN/signed zero/division error/alias/guards"))
				{
					return EXIT_FAILURE;
				}
						}
					}
				}
			}
		}
	}
	{
		// Helper-requiring casts keep their original native coverage, while an
		// implicit mixed numeric expression exercises typed stack result storage.
		const auto Module = CompileSource(
			"double wide(int n,uint64 seed){double total=1.0;for(int i=0;i<n;++i){total+=cast<double>(seed);}return total;}"
			"double checked(int n,double seed){double total=0.0;for(int i=0;i<n;++i){int part=cast<int>(seed);total+=cast<double>(part);}return total;}"
			"double mixed(int n){double value=1.25,total=0.0;for(int i=0;i<n;++i){value=value*0.875+i;total+=value;}return total+value;}"
			"void main(){print(mixed(0));print(mixed(1));print(mixed(83));}");
		if (!Expect(static_cast<bool>(Module), "compile mixed F64 stack conversion coverage")) return EXIT_FAILURE;
		VeroScript::AsmJit::JitNative::FArena Arena;
		for (const char* Name : {"wide", "checked"})
		{
			const auto Index = FindFunctionIndex(*Module, Name);
			if (!Expect(Index < Module->Functions.size() && Arena.EmitLinearMixedCfg(*Module, Module->Functions[Index]) != nullptr,
					"helper-requiring conversion retains mixed native coverage"))
			{
				return EXIT_FAILURE;
			}
		}
		const auto Baseline = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
		if (!Expect(Baseline.bSuccess, "execute mixed F64 stack VM reference")) return EXIT_FAILURE;
		for (const auto Backend : {VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			const auto Actual = RunCapture(*Module, Backend);
			if (!Expect(Actual.bSuccess && Actual.Output == Baseline.Output,
					"implicit numeric promotion preserves mixed F64 stack results"))
			{
				return EXIT_FAILURE;
			}
		}
	}
#endif
