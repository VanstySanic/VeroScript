// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included inside VeroScript::AsmJit::JitNative anonymous asmjit namespace.

FBinaryF64Kernel EmitF64Arith(JitRuntime& Runtime, const EOpcode Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	// Win64: Left=xmm0, Right=xmm1, Out=r8
	if (Op == EOpcode::Div)
	{
		Label DivByZero = A.new_label();
		Label DoDiv = A.new_label();
		A.xorpd(x86::xmm2, x86::xmm2);
		A.ucomisd(x86::xmm1, x86::xmm2);
		// NaN divisor: C++ (Right == 0.0) is false → perform the divide.
		A.jp(DoDiv);
		A.je(DivByZero);
		A.bind(DoDiv);
		A.divsd(x86::xmm0, x86::xmm1);
		A.movsd(x86::ptr(x86::r8), x86::xmm0);
		A.xor_(x86::eax, x86::eax);
		A.ret();
		A.bind(DivByZero);
		A.mov(x86::eax, kStatusDivByZero);
		A.ret();
	}
	else
	{
		if (Op == EOpcode::Add)
		{
			A.addsd(x86::xmm0, x86::xmm1);
		}
		else if (Op == EOpcode::Sub)
		{
			A.subsd(x86::xmm0, x86::xmm1);
		}
		else if (Op == EOpcode::Mul)
		{
			A.mulsd(x86::xmm0, x86::xmm1);
		}
		else
		{
			return nullptr;
		}
		A.movsd(x86::ptr(x86::r8), x86::xmm0);
		A.xor_(x86::eax, x86::eax);
		A.ret();
	}
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryF64Kernel>(Fn) : nullptr;
}

FCmpF64Kernel EmitF64Cmp(JitRuntime& Runtime, const EOpcode Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	Label Store = A.new_label();
	// Match C++ double relational operators.
	A.ucomisd(x86::xmm0, x86::xmm1);
	if (Op == EOpcode::Ne)
	{
		A.mov(x86::eax, 1);
		A.jp(Store); // unordered → true
		A.mov(x86::eax, 0);
		A.setne(x86::al);
	}
	else
	{
		A.xor_(x86::eax, x86::eax);
		A.jp(Store); // unordered → false for Eq/Lt/Le/Gt/Ge
		switch (Op)
		{
		case EOpcode::Eq:
			{
				A.sete(x86::al);
				break;
			}
		case EOpcode::Lt:
			{
				A.setb(x86::al);
				break;
			}
		case EOpcode::Le:
			{
				A.setbe(x86::al);
				break;
			}
		case EOpcode::Gt:
			{
				A.seta(x86::al);
				break;
			}
		case EOpcode::Ge:
			{
				A.setae(x86::al);
				break;
			}
		default:
			return nullptr;
		}
	}
	A.bind(Store);
	A.mov(x86::dword_ptr(x86::r8), x86::eax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FCmpF64Kernel>(Fn) : nullptr;
}

FUnaryF64Kernel EmitNegF64Kernel(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	A.mov(x86::rax, static_cast<std::int64_t>(0x8000000000000000ULL));
	A.movq(x86::xmm1, x86::rax);
	A.xorpd(x86::xmm0, x86::xmm1);
	A.movsd(x86::ptr(x86::rdx), x86::xmm0);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryF64Kernel>(Fn) : nullptr;
}

FConstF64Kernel EmitConstF64Kernel(JitRuntime& Runtime, const double Imm)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	std::uint64_t Bits = 0;
	std::memcpy(&Bits, &Imm, sizeof(Bits));
	A.mov(x86::rax, static_cast<std::int64_t>(Bits));
	A.mov(x86::qword_ptr(x86::rcx), x86::rax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FConstF64Kernel>(Fn) : nullptr;
}
