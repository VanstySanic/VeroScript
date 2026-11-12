// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included inside VeroScript::AsmJit::JitNative anonymous asmjit namespace.
// x86 32-bit cdecl: arguments are at [esp+4...], caller owns stack cleanup.

template <typename TKernel>
TKernel FinalizeX86Kernel(JitRuntime& Runtime, CodeHolder& Code)
{
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<TKernel>(Fn) : nullptr;
}

FBinaryI32Kernel EmitX86I32Binary(
	JitRuntime& Runtime,
	const EOpcode Op,
	const bool bUnsigned)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	Label Error = A.new_label();
	Label Store = A.new_label();
	A.mov(x86::eax, x86::dword_ptr(x86::esp, 4));

	switch (Op)
	{
	case EOpcode::Add:
		{
			A.add(x86::eax, x86::dword_ptr(x86::esp, 8));
			if (!bUnsigned)
			{
				A.jo(Error);
			}
			break;
		}
	case EOpcode::Sub:
		{
			A.sub(x86::eax, x86::dword_ptr(x86::esp, 8));
			if (!bUnsigned)
			{
				A.jo(Error);
			}
			break;
		}
	case EOpcode::Mul:
		{
			A.imul(x86::eax, x86::dword_ptr(x86::esp, 8));
			if (!bUnsigned)
			{
				A.jo(Error);
			}
			break;
		}
	case EOpcode::Div:
	case EOpcode::Mod:
		{
			Label Divide = A.new_label();
			Label DivZero = A.new_label();
			A.mov(x86::ecx, x86::dword_ptr(x86::esp, 8));
			A.test(x86::ecx, x86::ecx);
			A.jz(DivZero);
			if (!bUnsigned)
			{
				A.cmp(x86::eax, static_cast<std::int32_t>(0x80000000u));
				A.jne(Divide);
				A.cmp(x86::ecx, -1);
				A.je(Error);
			}
			A.bind(Divide);
			if (bUnsigned)
			{
				A.xor_(x86::edx, x86::edx);
				A.div(x86::ecx);
			}
			else
			{
				A.cdq();
				A.idiv(x86::ecx);
			}
			if (Op == EOpcode::Mod)
			{
				A.mov(x86::eax, x86::edx);
			}
			A.jmp(Store);
			A.bind(DivZero);
			A.mov(x86::eax, kStatusDivByZero);
			A.ret();
			break;
		}
	default:
		return nullptr;
	}

	A.bind(Store);
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 12));
	A.mov(x86::dword_ptr(x86::ecx), x86::eax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	A.bind(Error);
	A.mov(x86::eax, kStatusOverflow);
	A.ret();
	return FinalizeX86Kernel<FBinaryI32Kernel>(Runtime, Code);
}

FBinaryI32Kernel EmitAdd(JitRuntime& Runtime)
{
	return EmitX86I32Binary(Runtime, EOpcode::Add, false);
}
FBinaryI32Kernel EmitSub(JitRuntime& Runtime)
{
	return EmitX86I32Binary(Runtime, EOpcode::Sub, false);
}
FBinaryI32Kernel EmitMul(JitRuntime& Runtime)
{
	return EmitX86I32Binary(Runtime, EOpcode::Mul, false);
}
FBinaryI32Kernel EmitDivOrMod(JitRuntime& Runtime, const bool bDiv)
{
	return EmitX86I32Binary(Runtime, bDiv ? EOpcode::Div : EOpcode::Mod, false);
}

FBinaryI32Kernel EmitBit(JitRuntime& Runtime, const EBitOp Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	A.mov(x86::eax, x86::dword_ptr(x86::esp, 4));
	if (Op == EBitOp::And)
	{
		A.and_(x86::eax, x86::dword_ptr(x86::esp, 8));
	}
	else if (Op == EBitOp::Or)
	{
		A.or_(x86::eax, x86::dword_ptr(x86::esp, 8));
	}
	else
	{
		A.xor_(x86::eax, x86::dword_ptr(x86::esp, 8));
	}
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 12));
	A.mov(x86::dword_ptr(x86::ecx), x86::eax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	return FinalizeX86Kernel<FBinaryI32Kernel>(Runtime, Code);
}

FBinaryI32Kernel EmitShift(JitRuntime& Runtime, const bool bShl)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	Label RangeError = A.new_label();
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 8));
	A.cmp(x86::ecx, 31);
	A.ja(RangeError);
	A.mov(x86::eax, x86::dword_ptr(x86::esp, 4));
	if (bShl)
	{
		A.shl(x86::eax, x86::cl);
	}
	else
	{
		A.sar(x86::eax, x86::cl);
	}
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 12));
	A.mov(x86::dword_ptr(x86::ecx), x86::eax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	A.bind(RangeError);
	A.mov(x86::eax, kStatusShiftRange);
	A.ret();
	return FinalizeX86Kernel<FBinaryI32Kernel>(Runtime, Code);
}

FBinaryI32Kernel EmitX86Cmp(JitRuntime& Runtime, const EOpcode Op, const bool bUnsigned)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	A.mov(x86::eax, x86::dword_ptr(x86::esp, 4));
	A.cmp(x86::eax, x86::dword_ptr(x86::esp, 8));
	switch (Op)
	{
	case EOpcode::Eq:
		{
			A.sete(x86::al);
			break;
		}
	case EOpcode::Ne:
		{
			A.setne(x86::al);
			break;
		}
	case EOpcode::Lt:
		{
			bUnsigned ? A.setb(x86::al) : A.setl(x86::al);
			break;
		}
	case EOpcode::Le:
		{
			bUnsigned ? A.setbe(x86::al) : A.setle(x86::al);
			break;
		}
	case EOpcode::Gt:
		{
			bUnsigned ? A.seta(x86::al) : A.setg(x86::al);
			break;
		}
	case EOpcode::Ge:
		{
			bUnsigned ? A.setae(x86::al) : A.setge(x86::al);
			break;
		}
	default:
		return nullptr;
	}
	A.movzx(x86::eax, x86::al);
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 12));
	A.mov(x86::dword_ptr(x86::ecx), x86::eax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	return FinalizeX86Kernel<FBinaryI32Kernel>(Runtime, Code);
}

FBinaryI32Kernel EmitCmp(JitRuntime& Runtime, const EOpcode Op)
{
	return EmitX86Cmp(Runtime, Op, false);
}
FBinaryI32Kernel EmitCmpU32Op(JitRuntime& Runtime, const EOpcode Op)
{
	return EmitX86Cmp(Runtime, Op, true);
}
FBinaryI32Kernel EmitU32Arith(JitRuntime& Runtime, const EOpcode Op)
{
	return EmitX86I32Binary(Runtime, Op, true);
}

FUnaryI32Kernel EmitX86UnaryI32(JitRuntime& Runtime, const EOpcode Op, const bool bUnsigned)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	Label Overflow = A.new_label();
	A.mov(x86::eax, x86::dword_ptr(x86::esp, 4));
	if (Op == EOpcode::Neg)
	{
		A.neg(x86::eax);
		if (!bUnsigned)
		{
			A.jo(Overflow);
		}
	}
	else if (Op == EOpcode::BitNot)
	{
		A.not_(x86::eax);
	}
	else if (Op == EOpcode::Not)
	{
		A.test(x86::eax, x86::eax);
		A.sete(x86::al);
		A.movzx(x86::eax, x86::al);
	}
	else if (Op != EOpcode::Nop)
	{
		return nullptr;
	}
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 8));
	A.mov(x86::dword_ptr(x86::ecx), x86::eax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	A.bind(Overflow);
	A.mov(x86::eax, kStatusOverflow);
	A.ret();
	return FinalizeX86Kernel<FUnaryI32Kernel>(Runtime, Code);
}

FUnaryI32Kernel EmitNeg(JitRuntime& Runtime)
{
	return EmitX86UnaryI32(Runtime, EOpcode::Neg, false);
}
FUnaryI32Kernel EmitBitNot(JitRuntime& Runtime)
{
	return EmitX86UnaryI32(Runtime, EOpcode::BitNot, false);
}
FUnaryI32Kernel EmitNegU32Kernel(JitRuntime& Runtime)
{
	return EmitX86UnaryI32(Runtime, EOpcode::Neg, true);
}
FUnaryI32Kernel EmitNotBoolKernel(JitRuntime& Runtime)
{
	return EmitX86UnaryI32(Runtime, EOpcode::Not, false);
}
FUnaryI32Kernel EmitIdentityI32Kernel(JitRuntime& Runtime)
{
	return EmitX86UnaryI32(Runtime, EOpcode::Nop, false);
}

FUnaryI32ImmKernel EmitX86Imm(
	JitRuntime& Runtime,
	const std::int32_t Imm,
	const EOpcode Op,
	const bool bImmFirst)
{
	if ((Op == EOpcode::Shl || Op == EOpcode::Shr) && (Imm < 0 || Imm > 31))
	{
		return nullptr;
	}
	if ((Op == EOpcode::Div || Op == EOpcode::Mod) && !bImmFirst && Imm == 0)
	{
		return nullptr;
	}
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	Label Overflow = A.new_label();
	Label DivZero = A.new_label();
	A.mov(x86::eax, bImmFirst ? Imm : 0);
	if (!bImmFirst)
	{
		A.mov(x86::eax, x86::dword_ptr(x86::esp, 4));
	}

	switch (Op)
	{
	case EOpcode::Add:
		{
			A.add(x86::eax, Imm);
			A.jo(Overflow);
			break;
		}
	case EOpcode::Sub:
		{
			if (bImmFirst)
			{
				A.sub(x86::eax, x86::dword_ptr(x86::esp, 4));
			}
			else
			{
				A.sub(x86::eax, Imm);
			}
			A.jo(Overflow);
			break;
		}
	case EOpcode::Mul:
		{
			A.imul(x86::eax, x86::eax, Imm);
			A.jo(Overflow);
			break;
		}
	case EOpcode::BitAnd:
		{
			A.and_(x86::eax, Imm);
			break;
		}
	case EOpcode::BitOr:
		{
			A.or_(x86::eax, Imm);
			break;
		}
	case EOpcode::BitXor:
		{
			A.xor_(x86::eax, Imm);
			break;
		}
	case EOpcode::Shl:
		{
			A.shl(x86::eax, Imm);
			break;
		}
	case EOpcode::Shr:
		{
			A.sar(x86::eax, Imm);
			break;
		}

	case EOpcode::Div:
	case EOpcode::Mod:
		{
			A.mov(x86::ecx, bImmFirst ? x86::dword_ptr(x86::esp, 4) : x86::dword_ptr(x86::esp, 4));
			if (!bImmFirst)
			{
				A.mov(x86::ecx, Imm);
			}
			else
			{
				A.test(x86::ecx, x86::ecx);
				A.jz(DivZero);
			}
			A.cmp(x86::eax, static_cast<std::int32_t>(0x80000000u));
			Label Divide = A.new_label();
			A.jne(Divide);
			A.cmp(x86::ecx, -1);
			A.je(Overflow);
			A.bind(Divide);
			A.cdq();
			A.idiv(x86::ecx);
			if (Op == EOpcode::Mod)
			{
				A.mov(x86::eax, x86::edx);
			}
			break;
		}
	default:
		return nullptr;
	}

	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 8));
	A.mov(x86::dword_ptr(x86::ecx), x86::eax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	A.bind(DivZero);
	A.mov(x86::eax, kStatusDivByZero);
	A.ret();
	A.bind(Overflow);
	A.mov(x86::eax, kStatusOverflow);
	A.ret();
	return FinalizeX86Kernel<FUnaryI32ImmKernel>(Runtime, Code);
}

FUnaryI32ImmKernel EmitUnaryAddSubImm(JitRuntime& Runtime, const std::int32_t Imm, const bool bAdd)
{
	return EmitX86Imm(Runtime, Imm, bAdd ? EOpcode::Add : EOpcode::Sub, false);
}
FUnaryI32ImmKernel EmitMulImm(JitRuntime& Runtime, const std::int32_t Imm)
{
	return EmitX86Imm(Runtime, Imm, EOpcode::Mul, false);
}
FUnaryI32ImmKernel EmitDivOrModImm(JitRuntime& Runtime, const std::int32_t Imm, const bool bDiv)
{
	return EmitX86Imm(Runtime, Imm, bDiv ? EOpcode::Div : EOpcode::Mod, false);
}
FUnaryI32ImmKernel EmitBitImm(JitRuntime& Runtime, const std::int32_t Imm, const EBitOp Op)
{
	return EmitX86Imm(Runtime, Imm,
		Op == EBitOp::And ? EOpcode::BitAnd : Op == EBitOp::Or ? EOpcode::BitOr
															   : EOpcode::BitXor,
		false);
}
FUnaryI32ImmKernel EmitShiftImm(JitRuntime& Runtime, const std::int32_t Imm, const bool bShl)
{
	return EmitX86Imm(Runtime, Imm, bShl ? EOpcode::Shl : EOpcode::Shr, false);
}
FUnaryI32ImmKernel EmitImmMinusValue(JitRuntime& Runtime, const std::int32_t Imm)
{
	return EmitX86Imm(Runtime, Imm, EOpcode::Sub, true);
}
FUnaryI32ImmKernel EmitDivOrModFromImm(JitRuntime& Runtime, const std::int32_t Imm, const bool bDiv)
{
	return EmitX86Imm(Runtime, Imm, bDiv ? EOpcode::Div : EOpcode::Mod, true);
}

FConstI32Kernel EmitConst(JitRuntime& Runtime, const std::int32_t Imm)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	A.mov(x86::eax, x86::dword_ptr(x86::esp, 4));
	A.mov(x86::dword_ptr(x86::eax), Imm);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	return FinalizeX86Kernel<FConstI32Kernel>(Runtime, Code);
}

FUnaryF64Kernel EmitIdentityF64Kernel(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 12));
	A.movsd(x86::xmm0, x86::qword_ptr(x86::esp, 4));
	A.movsd(x86::qword_ptr(x86::ecx), x86::xmm0);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	return FinalizeX86Kernel<FUnaryF64Kernel>(Runtime, Code);
}

FBinaryF64Kernel EmitF64Arith(JitRuntime& Runtime, const EOpcode Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	Label DivZero = A.new_label();
	Label Calculate = A.new_label();
	A.movsd(x86::xmm0, x86::qword_ptr(x86::esp, 4));
	A.movsd(x86::xmm1, x86::qword_ptr(x86::esp, 12));
	if (Op == EOpcode::Div)
	{
		A.xorpd(x86::xmm2, x86::xmm2);
		A.ucomisd(x86::xmm1, x86::xmm2);
		A.jp(Calculate);
		A.je(DivZero);
	}
	A.bind(Calculate);
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
	else if (Op == EOpcode::Div)
	{
		A.divsd(x86::xmm0, x86::xmm1);
	}
	else
	{
		return nullptr;
	}
	A.mov(x86::eax, x86::dword_ptr(x86::esp, 20));
	A.movsd(x86::qword_ptr(x86::eax), x86::xmm0);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	A.bind(DivZero);
	A.mov(x86::eax, kStatusDivByZero);
	A.ret();
	return FinalizeX86Kernel<FBinaryF64Kernel>(Runtime, Code);
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
	A.movsd(x86::xmm0, x86::qword_ptr(x86::esp, 4));
	A.ucomisd(x86::xmm0, x86::qword_ptr(x86::esp, 12));
	if (Op == EOpcode::Ne)
	{
		A.mov(x86::eax, 1);
		A.jp(Store);
		A.setne(x86::al);
		A.movzx(x86::eax, x86::al);
	}
	else
	{
		A.xor_(x86::eax, x86::eax);
		A.jp(Store);
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
		A.movzx(x86::eax, x86::al);
	}
	A.bind(Store);
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 20));
	A.mov(x86::dword_ptr(x86::ecx), x86::eax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	return FinalizeX86Kernel<FCmpF64Kernel>(Runtime, Code);
}

FUnaryF64Kernel EmitNegF64Kernel(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	x86::Assembler A(&Code);
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 12));
	A.mov(x86::eax, x86::dword_ptr(x86::esp, 4));
	A.mov(x86::dword_ptr(x86::ecx), x86::eax);
	A.mov(x86::eax, x86::dword_ptr(x86::esp, 8));
	A.xor_(x86::eax, static_cast<std::int32_t>(0x80000000u));
	A.mov(x86::dword_ptr(x86::ecx, 4), x86::eax);
	A.xor_(x86::eax, x86::eax);
	A.ret();
	return FinalizeX86Kernel<FUnaryF64Kernel>(Runtime, Code);
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
	A.mov(x86::ecx, x86::dword_ptr(x86::esp, 4));
	A.mov(x86::dword_ptr(x86::ecx), static_cast<std::uint32_t>(Bits));
	A.mov(x86::dword_ptr(x86::ecx, 4), static_cast<std::uint32_t>(Bits >> 32));
	A.xor_(x86::eax, x86::eax);
	A.ret();
	return FinalizeX86Kernel<FConstF64Kernel>(Runtime, Code);
}
