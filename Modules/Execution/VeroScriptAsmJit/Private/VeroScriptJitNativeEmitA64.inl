// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included inside VeroScript::AsmJit::JitNative anonymous asmjit namespace (AArch64 / AAPCS64).

using namespace a64;

/**
 * Preserves the bridge context across AAPCS64 helper calls. The frame is
 * 80 bytes (four register pairs plus 16-byte scratch), preserving stack
 * alignment. x19..x24 hold Vm, locals, operand stack, out pointer, depth,
 * and the invocation-scoped recursion state.
 */
struct FAapcs64CfgCallFrame
{
	static void Enter(Assembler& A)
	{
		A.stp(x29, x30, ptr_pre(sp, -16));
		A.stp(x19, x20, ptr_pre(sp, -16));
		A.stp(x21, x22, ptr_pre(sp, -16));
		A.stp(x23, x24, ptr_pre(sp, -16));
		A.sub(sp, sp, 16);
		A.mov(x19, x0);
		A.mov(x20, x1);
		A.mov(x21, x2);
		A.mov(x22, x3);
		A.mov(w23, wzr);
		A.mov(x24, x4);
	}

	static void Leave(Assembler& A)
	{
		A.add(sp, sp, 16);
		A.ldp(x23, x24, ptr_post(sp, 16));
		A.ldp(x21, x22, ptr_post(sp, 16));
		A.ldp(x19, x20, ptr_post(sp, 16));
		A.ldp(x29, x30, ptr_post(sp, 16));
	}

	static void SaveLink(Assembler& A)
	{
		A.str(x30, ptr_pre(sp, -16));
	}

	static void RestoreLink(Assembler& A)
	{
		A.ldr(x30, ptr_post(sp, 16));
	}
};

FBinaryI32Kernel EmitAdd(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	Label Overflow = A.new_label();
	// w0=Left, w1=Right, x2=Out*
	A.adds(w8, w0, w1);
	A.b_vs(Overflow);
	A.str(w8, ptr(x2));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
}

FBinaryI32Kernel EmitSub(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	Label Overflow = A.new_label();
	A.subs(w8, w0, w1);
	A.b_vs(Overflow);
	A.str(w8, ptr(x2));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
}

FBinaryI32Kernel EmitMul(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	Label Overflow = A.new_label();
	A.smull(x8, w0, w1);
	A.sxtw(x9, w8);
	A.cmp(x8, x9);
	A.b_ne(Overflow);
	A.str(w8, ptr(x2));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
}

FBinaryI32Kernel EmitDivOrMod(JitRuntime& Runtime, const bool bDiv)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);

	Label DivByZero = A.new_label();
	Label Overflow = A.new_label();
	Label DoDiv = A.new_label();

	A.cbz(w1, DivByZero);
	A.mov(w8, static_cast<std::int32_t>(0x80000000u));
	A.cmp(w0, w8);
	A.b_ne(DoDiv);
	A.cmn(w1, 1);
	A.b_eq(Overflow);
	A.bind(DoDiv);
	A.sdiv(w8, w0, w1);
	if (!bDiv)
	{
		A.msub(w8, w8, w1, w0);
	}
	A.str(w8, ptr(x2));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(DivByZero);
	A.mov(w0, kStatusDivByZero);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);

	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
}

FBinaryI32Kernel EmitBit(JitRuntime& Runtime, const EBitOp Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	switch (Op)
	{
	case EBitOp::And:
		{
			A.and_(w8, w0, w1);
			break;
		}
	case EBitOp::Or:
		{
			A.orr(w8, w0, w1);
			break;
		}
	case EBitOp::Xor:
		{
			A.eor(w8, w0, w1);
			break;
		}
	default:
		{
			break;
		}
	}
	A.str(w8, ptr(x2));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
}

FBinaryI32Kernel EmitShift(JitRuntime& Runtime, const bool bShl)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	Label Bad = A.new_label();
	A.cmp(w1, 0);
	A.b_lt(Bad);
	A.cmp(w1, 31);
	A.b_gt(Bad);
	if (bShl)
	{
		A.lslv(w8, w0, w1);
	}
	else
	{
		A.asrv(w8, w0, w1);
	}
	A.str(w8, ptr(x2));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(Bad);
	A.mov(w0, kStatusShiftRange);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
}

FBinaryI32Kernel EmitCmp(JitRuntime& Runtime, const EOpcode Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	A.cmp(w0, w1);
	CondCode Cc;
	switch (Op)
	{
	case EOpcode::Eq:
		{
			Cc = CondCode::kEQ;
			break;
		}
	case EOpcode::Ne:
		{
			Cc = CondCode::kNE;
			break;
		}
	case EOpcode::Lt:
		{
			Cc = CondCode::kLT;
			break;
		}
	case EOpcode::Le:
		{
			Cc = CondCode::kLE;
			break;
		}
	case EOpcode::Gt:
		{
			Cc = CondCode::kGT;
			break;
		}
	case EOpcode::Ge:
		{
			Cc = CondCode::kGE;
			break;
		}
	default:
		return nullptr;
	}
	A.cset(w8, Cc);
	A.str(w8, ptr(x2));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
}

FUnaryI32Kernel EmitNeg(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	Label Overflow = A.new_label();
	// w0=Value, x1=Out*
	A.negs(w8, w0);
	A.b_vs(Overflow);
	A.str(w8, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
}

FUnaryI32Kernel EmitBitNot(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	A.orn(w8, wzr, w0);
	A.str(w8, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
}

FUnaryI32ImmKernel EmitUnaryAddSubImm(
	JitRuntime& Runtime,
	const std::int32_t Imm,
	const bool bAdd)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);

	Label Overflow = A.new_label();
	A.mov(w8, Imm);
	if (bAdd)
	{
		A.adds(w8, w0, w8);
	}
	else
	{
		A.subs(w8, w0, w8);
	}
	A.b_vs(Overflow);
	A.str(w8, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);

	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
}

FUnaryI32ImmKernel EmitMulImm(JitRuntime& Runtime, const std::int32_t Imm)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	Label Overflow = A.new_label();
	A.mov(w8, Imm);
	A.smull(x9, w0, w8);
	A.sxtw(x8, w9);
	A.cmp(x9, x8);
	A.b_ne(Overflow);
	A.str(w9, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
}

FUnaryI32ImmKernel EmitBitImm(JitRuntime& Runtime, const std::int32_t Imm, const EBitOp Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	A.mov(w8, Imm);
	switch (Op)
	{
	case EBitOp::And:
		{
			A.and_(w8, w0, w8);
			break;
		}
	case EBitOp::Or:
		{
			A.orr(w8, w0, w8);
			break;
		}
	case EBitOp::Xor:
		{
			A.eor(w8, w0, w8);
			break;
		}
	default:
		{
			break;
		}
	}
	A.str(w8, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
}

FUnaryI32ImmKernel EmitShiftImm(JitRuntime& Runtime, const std::int32_t Imm, const bool bShl)
{
	if (Imm < 0 || Imm > 31)
	{
		return nullptr;
	}
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	if (bShl)
	{
		A.lsl(w8, w0, Imm);
	}
	else
	{
		A.asr(w8, w0, Imm);
	}
	A.str(w8, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
}

FUnaryI32ImmKernel EmitImmMinusValue(JitRuntime& Runtime, const std::int32_t Imm)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);

	Label Overflow = A.new_label();
	A.mov(w8, Imm);
	A.subs(w8, w8, w0);
	A.b_vs(Overflow);
	A.str(w8, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);

	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
}

FUnaryI32ImmKernel EmitAlwaysStatus(JitRuntime& Runtime, const std::int32_t Status)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	A.mov(w0, Status);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
}

FUnaryI32ImmKernel EmitDivOrModImm(
	JitRuntime& Runtime,
	const std::int32_t Imm,
	const bool bDiv)
{
	if (Imm == 0)
	{
		return EmitAlwaysStatus(Runtime, kStatusDivByZero);
	}
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	Label Overflow = A.new_label();
	// Match x86: INT_MIN / -1 and INT_MIN % -1 both report overflow.
	if (Imm == -1)
	{
		A.mov(w8, static_cast<std::int32_t>(0x80000000u));
		A.cmp(w0, w8);
		A.b_eq(Overflow);
		if (bDiv)
		{
			A.neg(w8, w0);
		}
		else
		{
			A.mov(w8, wzr);
		}
	}
	else
	{
		A.mov(w9, Imm);
		A.sdiv(w8, w0, w9);
		if (!bDiv)
		{
			A.msub(w8, w8, w9, w0);
		}
	}
	A.str(w8, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
}

FUnaryI32ImmKernel EmitDivOrModFromImm(
	JitRuntime& Runtime,
	const std::int32_t Imm,
	const bool bDiv)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	Label DivByZero = A.new_label();
	Label Overflow = A.new_label();
	A.cbz(w0, DivByZero);
	if (Imm == static_cast<std::int32_t>(0x80000000u))
	{
		A.cmn(w0, 1);
		A.b_eq(Overflow);
	}
	A.mov(w8, Imm);
	A.sdiv(w9, w8, w0);
	if (!bDiv)
	{
		A.msub(w9, w9, w0, w8);
	}
	A.str(w9, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	A.bind(DivByZero);
	A.mov(w0, kStatusDivByZero);
	A.ret(x30);
	A.bind(Overflow);
	A.mov(w0, kStatusOverflow);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32ImmKernel>(Fn) : nullptr;
}

FBinaryI32Kernel EmitU32Arith(JitRuntime& Runtime, const EOpcode Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	if (Op == EOpcode::Add || Op == EOpcode::Sub || Op == EOpcode::Mul)
	{
		if (Op == EOpcode::Add)
		{
			A.add(w8, w0, w1);
		}
		else if (Op == EOpcode::Sub)
		{
			A.sub(w8, w0, w1);
		}
		else
		{
			A.umull(x8, w0, w1);
		}
		A.str(w8, ptr(x2));
		A.mov(w0, wzr);
		A.ret(x30);
	}
	else if (Op == EOpcode::Div || Op == EOpcode::Mod)
	{
		Label DivByZero = A.new_label();
		A.cbz(w1, DivByZero);
		A.udiv(w8, w0, w1);
		if (Op == EOpcode::Mod)
		{
			A.msub(w8, w8, w1, w0);
		}
		A.str(w8, ptr(x2));
		A.mov(w0, wzr);
		A.ret(x30);
		A.bind(DivByZero);
		A.mov(w0, kStatusDivByZero);
		A.ret(x30);
	}
	else
	{
		return nullptr;
	}
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
}

FBinaryI32Kernel EmitCmpU32Op(JitRuntime& Runtime, const EOpcode Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	A.cmp(w0, w1);
	CondCode Cc;
	switch (Op)
	{
	case EOpcode::Eq:
		{
			Cc = CondCode::kEQ;
			break;
		}
	case EOpcode::Ne:
		{
			Cc = CondCode::kNE;
			break;
		}
	case EOpcode::Lt:
		{
			Cc = CondCode::kLO;
			break;
		}
	case EOpcode::Le:
		{
			Cc = CondCode::kLS;
			break;
		}
	case EOpcode::Gt:
		{
			Cc = CondCode::kHI;
			break;
		}
	case EOpcode::Ge:
		{
			Cc = CondCode::kHS;
			break;
		}
	default:
		return nullptr;
	}
	A.cset(w8, Cc);
	A.str(w8, ptr(x2));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FBinaryI32Kernel>(Fn) : nullptr;
}

FUnaryI32Kernel EmitNegU32Kernel(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	A.neg(w8, w0);
	A.str(w8, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
}

FUnaryI32Kernel EmitNotBoolKernel(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	A.eor(w8, w0, 1);
	A.and_(w8, w8, 1);
	A.str(w8, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
}

FUnaryI32Kernel EmitIdentityI32Kernel(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	A.str(w0, ptr(x1));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryI32Kernel>(Fn) : nullptr;
}

FUnaryF64Kernel EmitIdentityF64Kernel(JitRuntime& Runtime)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	// d0=Value, x0=Out*
	A.str(d0, ptr(x0));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FUnaryF64Kernel>(Fn) : nullptr;
}

FBinaryF64Kernel EmitF64Arith(JitRuntime& Runtime, const EOpcode Op)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	// d0=Left, d1=Right, x0=Out*
	if (Op == EOpcode::Div)
	{
		Label DivByZero = A.new_label();
		// Exact +0.0/-0.0 only (NaN is unordered, not EQ, so division proceeds).
		A.fcmp(d1, 0);
		A.b_eq(DivByZero);
		A.fdiv(d0, d0, d1);
		A.str(d0, ptr(x0));
		A.mov(w0, wzr);
		A.ret(x30);
		A.bind(DivByZero);
		A.mov(w0, kStatusDivByZero);
		A.ret(x30);
	}
	else
	{
		if (Op == EOpcode::Add)
		{
			A.fadd(d0, d0, d1);
		}
		else if (Op == EOpcode::Sub)
		{
			A.fsub(d0, d0, d1);
		}
		else if (Op == EOpcode::Mul)
		{
			A.fmul(d0, d0, d1);
		}
		else
		{
			return nullptr;
		}
		A.str(d0, ptr(x0));
		A.mov(w0, wzr);
		A.ret(x30);
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
	Assembler A(&Code);
	// Match C++ double relational operators (unordered is false except for Ne).
	A.fcmp(d0, d1);
	CondCode Cc;
	switch (Op)
	{
	case EOpcode::Eq:
		{
			Cc = CondCode::kEQ;
			break;
		}
	case EOpcode::Ne:
		{
			Cc = CondCode::kNE;
			break;
		}
	case EOpcode::Lt:
		{
			Cc = CondCode::kLO;
			break;
		}
	case EOpcode::Le:
		{
			Cc = CondCode::kLS;
			break;
		}
	case EOpcode::Gt:
		{
			Cc = CondCode::kGT;
			break;
		}
	case EOpcode::Ge:
		{
			Cc = CondCode::kGE;
			break;
		}
	default:
		return nullptr;
	}
	A.cset(w8, Cc);
	A.str(w8, ptr(x0));
	A.mov(w0, wzr);
	A.ret(x30);
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
	Assembler A(&Code);
	A.fneg(d0, d0);
	A.str(d0, ptr(x0));
	A.mov(w0, wzr);
	A.ret(x30);
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
	Assembler A(&Code);
	std::uint64_t Bits = 0;
	std::memcpy(&Bits, &Imm, sizeof(Bits));
	A.mov(x8, static_cast<std::int64_t>(Bits));
	A.str(x8, ptr(x0));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FConstF64Kernel>(Fn) : nullptr;
}

/**
 * AAPCS64 homogeneous-double CFG subset. Scalar eligibility is shared with
 * x64; comparison predicates may only flow directly into their branch.
 */
FLinearF64Kernel EmitLinearF64CfgA64Kernel(
	JitRuntime& Runtime,
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function)
{
	if (!IsF64LinearCandidate(Module, Function)) return nullptr;
	const auto& Bytes = Function.Code;
	if (!Function.bReturnsVoid && Bytes.size() == 4
		&& static_cast<EOpcode>(Bytes[0]) == EOpcode::LoadGlobal
		&& static_cast<EOpcode>(Bytes[3]) == EOpcode::Return)
	{
		const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + 1);
		if (Slot >= Module.GlobalTypeNames.size() || Module.GlobalTypeNames[Slot] != "double")
		{
			return nullptr;
		}
		CodeHolder Code;
		if (!InitCode(Runtime, Code))
		{
			return nullptr;
		}
		Assembler A(&Code);
		// Bridge ABI: x0=Vm, x3=Out. Helper ABI: x0=Vm, w1=slot, x2=Out.
		A.mov(w1, Slot);
		A.mov(x2, x3);
		A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadGlobalF64));
		FAapcs64CfgCallFrame::SaveLink(A);
		A.blr(x16);
		FAapcs64CfgCallFrame::RestoreLink(A);
		A.ret(x30);
		void* Fn = nullptr;
		return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FLinearF64Kernel>(Fn) : nullptr;
	}
	if (Function.bReturnsVoid && Function.LocalCount != 0 && Bytes.size() == 7
		&& static_cast<EOpcode>(Bytes[0]) == EOpcode::LoadLocal
		&& static_cast<EOpcode>(Bytes[3]) == EOpcode::StoreGlobal
		&& static_cast<EOpcode>(Bytes[6]) == EOpcode::Return)
	{
		const std::uint16_t Local = Bytecode::ReadU16LE(Bytes.data() + 1);
		const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + 4);
		if (Local >= Function.LocalCount || Slot >= Module.GlobalTypeNames.size()
			|| Module.GlobalTypeNames[Slot] != "double")
		{
			return nullptr;
		}
		CodeHolder Code;
		if (!InitCode(Runtime, Code))
		{
			return nullptr;
		}
		Assembler A(&Code);
		A.ldr(d0, ptr(x1, static_cast<int>(Local * 8)));
		A.mov(w1, Slot);
		A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalF64));
		FAapcs64CfgCallFrame::SaveLink(A);
		A.blr(x16);
		FAapcs64CfgCallFrame::RestoreLink(A);
		A.ret(x30);
		void* Fn = nullptr;
		return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FLinearF64Kernel>(Fn) : nullptr;
	}
	if (!Function.bReturnsVoid && Bytes.size() >= 5
		&& static_cast<EOpcode>(Bytes[Bytes.size() - 5]) == EOpcode::Call
		&& static_cast<EOpcode>(Bytes.back()) == EOpcode::Return)
	{
		const std::size_t CallOffset = Bytes.size() - 5;
		const std::uint16_t Target = Bytecode::ReadU16LE(Bytes.data() + CallOffset + 1);
		const std::uint8_t ArgCount = Bytes[CallOffset + 3];
		const auto HasDoubleSignature = [](const FBytecodeFunction& Candidate)
		{
			JitIr::FSignature Signature;
			return JitIr::ParseSignature(Candidate.ClosureTypeName, Signature) &&
				Signature.ReturnType == JitIr::EValueType::F64 &&
				std::all_of(Signature.Parameters.begin(), Signature.Parameters.end(),
					[](const JitIr::EValueType Type)
					{
						return Type == JitIr::EValueType::F64;
					});
		};
		bool bShape = Target < Module.Functions.size()
			&& Module.Functions[Target].ParamCount == ArgCount
			&& !Module.Functions[Target].bReturnsVoid
			&& HasDoubleSignature(Function) && HasDoubleSignature(Module.Functions[Target])
			&& CallOffset == static_cast<std::size_t>(ArgCount) * 3;
		for (std::uint8_t I = 0; bShape && I < ArgCount; ++I)
		{
			bShape = static_cast<EOpcode>(Bytes[static_cast<std::size_t>(I) * 3]) == EOpcode::LoadLocal
				&& Bytecode::ReadU16LE(Bytes.data() + static_cast<std::size_t>(I) * 3 + 1) == I;
		}
		if (bShape)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			Assembler A(&Code);
			// Writing w3 also overwrites x3, which still holds the result pointer.
			A.mov(x4, x3);
			A.mov(x2, x1);
			A.mov(w1, Target);
			A.mov(w3, ArgCount);
			A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallF64));
			FAapcs64CfgCallFrame::SaveLink(A);
			A.blr(x16);
			FAapcs64CfgCallFrame::RestoreLink(A);
			A.ret(x30);
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FLinearF64Kernel>(Fn) : nullptr;
		}
	}

	JitLowering::FPlan Plan;
	std::string Error;
	if (!JitLowering::BuildScalarPlan(Module, Function, Plan, &Error)
		|| Plan.LocalCount > kMaxNativeCfgLocals
		|| Plan.MaxStackSlots > kMaxNativeCfgStackSlots)
	{
		return nullptr;
	}
	for (const JitIr::EValueType Type : Plan.Signature.Parameters)
	{
		if (Type != JitIr::EValueType::F64)
		{
			return nullptr;
		}
	}
	if (Plan.Signature.ReturnType != JitIr::EValueType::F64
		&& Plan.Signature.ReturnType != JitIr::EValueType::Void)
	{
		return nullptr;
	}
	const auto CanElideLocalStore = [&](const std::size_t Offset)
	{
		return Plan.bScalarEligible && std::binary_search(
			Plan.ElidedLocalStoreOffsets.begin(),
			Plan.ElidedLocalStoreOffsets.end(), Offset);
	};
	std::uint16_t SelfFunctionIndex = 0xFFFFu;
	for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
	{
		if (&Module.Functions[Index] == &Function)
		{
			SelfFunctionIndex = static_cast<std::uint16_t>(Index);
			break;
		}
	}
	std::vector<std::optional<FExternalDirectF64Callee>> ExternalDirectCallees(
		Module.Functions.size());
	const auto GetExternalDirectF64Callee = [&](const std::uint16_t CalleeIndex)
		-> const FExternalDirectF64Callee*
	{
		if (CalleeIndex >= Module.Functions.size()
			|| CalleeIndex == SelfFunctionIndex)
		{
			return nullptr;
		}
		auto& Cached = ExternalDirectCallees[CalleeIndex];
		if (!Cached)
		{
			Cached.emplace();
			(void)AnalyzeExternalDirectF64Callee(
				Module, SelfFunctionIndex, CalleeIndex, *Cached);
		}
		return Cached->bEligible ? &*Cached : nullptr;
	};

	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	std::vector<Label> At(Bytes.size());
	for (std::size_t Ip = 0; Ip < Bytes.size();)
	{
		At[Ip] = A.new_label();
		Ip += Bytecode::InstructionSize(static_cast<EOpcode>(Bytes[Ip]));
	}
	struct FF64EncodedFailureSite
	{
		Label Target;
		std::int32_t EncodedFailure = 0;
	};
	std::vector<FF64EncodedFailureSite> EncodedFailureSites;
	const auto NewF64FailureSite = [&](const EJitRuntimeStatus Status, const std::size_t Ip)
	{
		const Label Target = A.new_label();
		EncodedFailureSites.push_back(FF64EncodedFailureSite{
			Target,
			EncodeJitKernelFailure(Status, static_cast<std::uint32_t>(Ip))});
		return Target;
	};
	FAapcs64CfgCallFrame::Enter(A);
	for (std::size_t Ip = 0; Ip < Bytes.size();)
	{
		const EOpcode Op = static_cast<EOpcode>(Bytes[Ip]);
		const std::size_t Size = Bytecode::InstructionSize(Op);
		A.bind(At[Ip]);
		// d16/d17 are caller-saved under AAPCS64; d8-d15 are not.
		auto PushD16 = [&]()
		{
			A.str(d16, ptr(x21, x23, lsl(3)));
			A.add(w23, w23, 1);
		};
		auto PopD16 = [&]() -> bool
		{
			A.sub(w23, w23, 1);
			A.ldr(d16, ptr(x21, x23, lsl(3)));
			return true;
		};
		auto PopD17 = [&]() -> bool
		{
			A.sub(w23, w23, 1);
			A.ldr(d17, ptr(x21, x23, lsl(3)));
			return true;
		};
		switch (Op)
		{
		case EOpcode::Nop:
			{
				break;
			}
		case EOpcode::ConstI32:
			{
				if (!IsVoidReturnSentinelI32(Module, Function, Ip))
				{
					// The shared validator only admits a discarded print/eprint
					// expression sentinel here; preserve its stack slot as F64 zero.
					A.fmov(d16, xzr);
					PushD16();
				}
				break;
			}
		case EOpcode::ConstF64:
			{
				const std::uint16_t Index = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Index >= Module.Constants.size() || Module.Constants[Index].Kind != EConstantKind::F64)
				{
					return nullptr;
				}
				std::uint64_t Bits = 0;
				std::memcpy(&Bits, &Module.Constants[Index].F64, sizeof(Bits));
				A.mov(x8, static_cast<std::int64_t>(Bits));
				A.fmov(d16, x8);
				PushD16();
				break;
			}
		case EOpcode::LoadLocal:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Function.LocalCount)
				{
					return nullptr;
				}
				A.ldr(d16, ptr(x20, static_cast<int>(Slot * 8)));
				PushD16();
				break;
			}
		case EOpcode::StoreLocal:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Function.LocalCount || !PopD16())
				{
					return nullptr;
				}
				if (!CanElideLocalStore(Ip))
				{
					A.str(d16, ptr(x20, static_cast<int>(Slot * 8)));
				}
				break;
			}
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Function.LocalCount)
				{
					return nullptr;
				}
				const std::int32_t Imm = Op == EOpcode::IncLocal
					? 1
					: Bytecode::ReadI32LE(Bytes.data() + Ip + 3);
				A.ldr(d16, ptr(x20, static_cast<int>(Slot * 8)));
				A.mov(w8, Imm);
				A.scvtf(d17, w8);
				A.fadd(d16, d16, d17);
				A.str(d16, ptr(x20, static_cast<int>(Slot * 8)));
				break;
			}
		case EOpcode::AddLocalLocal:
			{
				const std::uint16_t Destination = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::uint16_t Source = Bytecode::ReadU16LE(Bytes.data() + Ip + 3);
				if (Destination >= Function.LocalCount || Source >= Function.LocalCount)
				{
					return nullptr;
				}
				A.ldr(d16, ptr(x20, static_cast<int>(Destination * 8)));
				A.ldr(d17, ptr(x20, static_cast<int>(Source * 8)));
				A.fadd(d16, d16, d17);
				A.str(d16, ptr(x20, static_cast<int>(Destination * 8)));
				break;
			}
		case EOpcode::LoadGlobal:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Module.GlobalTypeNames.size() || Module.GlobalTypeNames[Slot] != "double")
				{
					return nullptr;
				}
				A.mov(x0, x19);
				A.mov(w1, Slot);
				A.add(x2, x21, x23, lsl(3));
				A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadGlobalF64));
				A.blr(x16);
				A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				A.add(w23, w23, 1);
				break;
			}
		case EOpcode::StoreGlobal:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Module.GlobalTypeNames.size() || Module.GlobalTypeNames[Slot] != "double" || !PopD16())
				{
					return nullptr;
				}
				A.mov(x0, x19);
				A.mov(w1, Slot);
				A.fmov(d0, d16);
				A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalF64));
				A.blr(x16);
				A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				break;
			}
		case EOpcode::LoadThisField:
			{
				const std::uint16_t Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x0, x19);
				A.mov(w1, Name);
				A.add(x2, x21, x23, lsl(3));
				A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadThisFieldF64));
				A.blr(x16);
				A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				A.add(w23, w23, 1);
				break;
			}
		case EOpcode::StoreThisField:
		case EOpcode::InitializeThisField:
			{
				const std::uint16_t Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (!PopD16())
				{
					return nullptr;
				}
				A.mov(x0, x19);
				A.mov(w1, Name);
				A.fmov(d0, d16);
				A.mov(x16, reinterpret_cast<std::uint64_t>(Op == EOpcode::InitializeThisField
						? &VeroScript::AsmJit::JitNative::LinearHelpers::InitializeThisFieldF64
						: &VeroScript::AsmJit::JitNative::LinearHelpers::StoreThisFieldF64));
				A.blr(x16);
				A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				break;
			}
		case EOpcode::IncField:
			{
				const std::uint16_t Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x0, x19);
				A.mov(w1, Name);
				A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::IncThisFieldF64));
				A.blr(x16);
				A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				break;
			}
		case EOpcode::Call:
			{
				const std::uint16_t Target = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::uint8_t ArgCount = Bytes[Ip + 3];
				if (Target >= Module.Functions.size() || Module.Functions[Target].ParamCount != ArgCount)
				{
					return nullptr;
				}
				A.sub(w23, w23, ArgCount);
				const FExternalDirectF64Callee* const ExternalCallee =
					GetExternalDirectF64Callee(Target);
				if (ExternalCallee)
				{
					const std::size_t OutOffset = 0u;
					const std::size_t StatusOffset = 8u;
					const std::size_t LocalsOffset = 16u;
					const std::size_t StackOffset = LocalsOffset
						+ ExternalCallee->LocalCount * sizeof(double);
					const std::size_t FrameBytes =
						(StackOffset + ExternalCallee->StackSlots * sizeof(double) + 15u)
						& ~15u;
					Label FallbackCall = A.new_label();
					Label LanguageOverflow = A.new_label();
					Label FailureOwnerKnown = A.new_label();
					Label DirectSuccess = A.new_label();
					Label CallDone = A.new_label();

					A.cbz(x24, FallbackCall);
					A.ldr(x10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectF64TargetCount))));
					// Function indices span the full uint16 range, while AArch64 CMP's
					// immediate form cannot encode every 16-bit value. Materialize the
					// index so large modules keep the same direct-call eligibility.
					A.mov(w11, Target);
					A.cmp(x10, x11);
					A.b_ls(FallbackCall);
					A.ldr(x16, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectF64Targets))));
					A.cbz(x16, FallbackCall);
					A.mov(w10, Target);
					A.ldr(x16, ptr(x16, x10, lsl(3)));
					A.cbz(x16, FallbackCall);
					A.ldr(x10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, ActiveDepth))));
					A.ldr(x11, ptr(x10));
					A.ldr(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, LanguageDepthLimit))));
					A.cmp(x11, x12);
					A.b_hs(LanguageOverflow);
					A.ldr(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, NativeDepthLimit))));
					A.cmp(x11, x12);
					A.b_hs(FallbackCall);
					A.add(x11, x11, 1);
					A.str(x11, ptr(x10));
					A.ldr(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectCalls))));
					A.add(x12, x12, 1);
					A.str(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectCalls))));

					A.add(x10, x21, x23, lsl(3));
					A.sub(sp, sp, static_cast<std::uint32_t>(FrameBytes));
					for (std::size_t Arg = 0; Arg < ArgCount; ++Arg)
					{
						A.ldr(d16, ptr(x10, static_cast<int>(Arg * sizeof(double))));
						A.str(d16, ptr(sp, static_cast<int>(
							LocalsOffset + Arg * sizeof(double))));
					}
					// AArch64 FMOV floating immediates cannot encode zero. Move the
					// architectural zero register instead to produce exact +0.0 bits.
					A.fmov(d16, xzr);
					for (std::size_t Local = ArgCount;
						Local < ExternalCallee->LocalCount; ++Local)
					{
						A.str(d16, ptr(sp, static_cast<int>(
							LocalsOffset + Local * sizeof(double))));
					}
					A.mov(x0, x19);
					A.add(x1, sp, static_cast<std::uint32_t>(LocalsOffset));
					A.add(x2, sp, static_cast<std::uint32_t>(StackOffset));
					A.add(x3, sp, static_cast<std::uint32_t>(OutOffset));
					A.mov(x4, x24);
					A.blr(x16);
					A.str(w0, ptr(sp, static_cast<int>(StatusOffset)));
					A.cbz(w0, FailureOwnerKnown);
					A.ldrh(w10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, FailureFunctionIndex))));
					A.mov(w11, 0xFFFFu);
					A.cmp(w10, w11);
					A.b_ne(FailureOwnerKnown);
					A.mov(w10, Target);
					A.strh(w10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, FailureFunctionIndex))));
					A.bind(FailureOwnerKnown);
					A.ldr(x10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, ActiveDepth))));
					A.ldr(x11, ptr(x10));
					A.sub(x11, x11, 1);
					A.str(x11, ptr(x10));
					A.ldr(w9, ptr(sp, static_cast<int>(StatusOffset)));
					if (!Module.Functions[Target].bReturnsVoid)
					{
						A.ldr(d16, ptr(sp, static_cast<int>(OutOffset)));
					}
					else
					{
						A.fmov(d16, xzr);
					}
					A.add(sp, sp, static_cast<std::uint32_t>(FrameBytes));
					A.cbz(w9, DirectSuccess);
					A.mov(w0, w9);
					FAapcs64CfgCallFrame::Leave(A);
					A.ret(x30);
					A.bind(DirectSuccess);
					PushD16();
					A.b(CallDone);

					A.bind(LanguageOverflow);
					A.mov(x0, x19);
					A.mov(w1, static_cast<std::uint32_t>(EJitScalarValueKind::F64));
					A.mov(x16, reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
					A.blr(x16);
					A.b(NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));

					A.bind(FallbackCall);
					A.mov(x0, x19);
					A.mov(w1, Target);
					A.add(x2, x21, x23, lsl(3));
					A.mov(w3, ArgCount);
					A.mov(x4, sp);
					A.mov(x16, reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallF64));
					A.blr(x16);
					A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
					if (!Module.Functions[Target].bReturnsVoid)
					{
						A.ldr(d16, ptr(sp));
					}
					else
					{
						A.fmov(d16, xzr);
					}
					PushD16();
					A.bind(CallDone);
					break;
				}
				A.mov(x0, x19);
				A.mov(w1, Target);
				A.add(x2, x21, x23, lsl(3));
				A.mov(w3, ArgCount);
				A.mov(x4, sp);
				A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallF64));
				A.blr(x16);
				A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				A.ldr(d16, ptr(sp));
				PushD16();
				break;
			}
		case EOpcode::LoadThis:
			{
				break;
			}
		case EOpcode::CallBoundMethod:
		case EOpcode::CallMethod:
			{
				const std::uint16_t TargetOrName = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::uint8_t ArgCount = Bytes[Ip + 3];
				if ((Op == EOpcode::CallBoundMethod && TargetOrName >= Module.Functions.size())
					|| (Op == EOpcode::CallMethod && TargetOrName >= Module.StringPool.size()))
				{
					return nullptr;
				}
				A.sub(w23, w23, ArgCount);
				A.mov(x0, x19);
				A.mov(w1, TargetOrName);
				A.add(x2, x21, x23, lsl(3));
				A.mov(w3, ArgCount);
				A.mov(x4, sp);
				A.mov(x16, Op == EOpcode::CallBoundMethod
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisF64)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::CallMethodFromThisF64));
				A.blr(x16);
				A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				A.ldr(d16, ptr(sp));
				PushD16();
				break;
			}
		case EOpcode::CallBuiltin:
			{
				const EBuiltinId Builtin = static_cast<EBuiltinId>(Bytes[Ip + 1]);
				if (Builtin == EBuiltinId::Now || Builtin == EBuiltinId::SteadyNow)
				{
					A.mov(x0, x19);
					A.mov(w1, static_cast<std::uint32_t>(Builtin));
					A.mov(x2, sp);
					A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::ClockNowF64));
					A.blr(x16);
					A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
					A.ldr(d16, ptr(sp));
					PushD16();
					break;
				}
				if (Builtin == EBuiltinId::Print || Builtin == EBuiltinId::Eprint)
				{
					PopD16();
					A.mov(x0, x19);
					A.mov(w1, static_cast<std::uint32_t>(Builtin));
					A.fmov(d0, d16);
					A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::OutputF64Builtin));
					A.blr(x16);
					A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
					break;
				}
				if (Builtin == EBuiltinId::Min || Builtin == EBuiltinId::Max)
				{
					if (!PopD17() || !PopD16())
					{
						return nullptr;
					}
					// The language defines min/max as `left < right ? left : right`
					// (or `>` for max), so equality and unordered comparisons must
					// preserve the right operand's exact bits. ARM FMIN/FMAX instead
					// have different NaN and signed-zero rules. FCSEL keeps AArch64
					// aligned with the VM and x64 backends.
					A.fcmp(d16, d17);
					A.fcsel(
						d16,
						d16,
						d17,
						Builtin == EBuiltinId::Min ? CondCode::kLO : CondCode::kGT);
					PushD16();
					break;
				}
				if (Builtin != EBuiltinId::Abs && Builtin != EBuiltinId::Sqrt
					&& Builtin != EBuiltinId::Sin && Builtin != EBuiltinId::Cos
					&& Builtin != EBuiltinId::Tan && Builtin != EBuiltinId::Floor
					&& Builtin != EBuiltinId::Ceil && Builtin != EBuiltinId::Round
					&& Builtin != EBuiltinId::Trunc && Builtin != EBuiltinId::Exp
					&& Builtin != EBuiltinId::Log && Builtin != EBuiltinId::Log2
					&& Builtin != EBuiltinId::Asin && Builtin != EBuiltinId::Acos)
				{
					return nullptr;
				}
				A.sub(w8, w23, 1);
				A.add(x2, x21, x8, lsl(3));
				A.mov(x0, x19);
				A.mov(w1, static_cast<std::uint32_t>(Builtin));
				A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::MathUnaryF64));
				A.blr(x16);
				A.cbnz(w0, NewF64FailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				break;
			}
		case EOpcode::Neg:
			{
				if (!PopD16())
				{
					return nullptr;
				}
				A.fneg(d16, d16);
				PushD16();
				break;
			}
		case EOpcode::Dup:
			{
				A.sub(w10, w23, 1);
				A.ldr(d16, ptr(x21, w10, lsl(3)));
				PushD16();
				break;
			}
		case EOpcode::Pop:
			{
				if (!PopD16())
				{
					return nullptr;
				}
				break;
			}
		case EOpcode::Add:
		case EOpcode::Sub:
		case EOpcode::Mul:
		case EOpcode::Div:
			{
				if (!PopD17() || !PopD16())
				{
					return nullptr;
				}
				if (Op == EOpcode::Add)
				{
					A.fadd(d16, d16, d17);
				}
				else if (Op == EOpcode::Sub)
				{
					A.fsub(d16, d16, d17);
				}
				else if (Op == EOpcode::Mul)
				{
					A.fmul(d16, d16, d17);
				}
				else
				{
					A.fcmp(d17, 0);
					A.b_eq(NewF64FailureSite(
						EJitRuntimeStatus::DivisionByZero, Ip));
					A.fdiv(d16, d16, d17);
				}
				PushD16();
				break;
			}
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
			{
				if (!PopD17() || !PopD16())
				{
					return nullptr;
				}
				A.fcmp(d16, d17);
				A.cset(w8, Op == EOpcode::Eq ? CondCode::kEQ
					: Op == EOpcode::Ne ? CondCode::kNE
					: Op == EOpcode::Lt ? CondCode::kLO
					: Op == EOpcode::Le ? CondCode::kLS
					: Op == EOpcode::Gt ? CondCode::kGT
																				 : CondCode::kGE);
				A.scvtf(d16, w8);
				PushD16();
				break;
			}
		case EOpcode::Jmp:
			{
				const std::int32_t Offset = Bytecode::ReadI32LE(Bytes.data() + Ip + 1);
				const std::size_t Target = static_cast<std::size_t>(static_cast<std::int64_t>(Ip + Size) + Offset);
				if (Target >= At.size())
				{
					return nullptr;
				}
				A.b(At[Target]);
				break;
			}
		case EOpcode::JmpIfFalse:
			{
				if (!PopD16())
				{
					return nullptr;
				}
				const std::int32_t Offset = Bytecode::ReadI32LE(Bytes.data() + Ip + 1);
				const std::size_t Target = static_cast<std::size_t>(static_cast<std::int64_t>(Ip + Size) + Offset);
				if (Target >= At.size())
				{
					return nullptr;
				}
				A.fcmp(d16, 0);
				A.b_eq(At[Target]);
				break;
			}
		case EOpcode::JmpIfLocalGeImm:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::int32_t Imm = Bytecode::ReadI32LE(Bytes.data() + Ip + 3);
				const std::int32_t Offset = Bytecode::ReadI32LE(Bytes.data() + Ip + 7);
				const std::int64_t TargetOffset = static_cast<std::int64_t>(Ip + Size) + Offset;
				if (Slot >= Function.LocalCount || TargetOffset < 0
					|| static_cast<std::size_t>(TargetOffset) >= At.size())
				{
					return nullptr;
				}
				A.ldr(d16, ptr(x20, static_cast<int>(Slot * 8)));
				A.mov(w8, Imm);
				A.scvtf(d17, w8);
				// Language semantics are !(local < immediate).  Using CSET+CBZ
				// deliberately takes the jump for unordered (NaN), matching x64
				// and the VM rather than treating it as an ordered >= comparison.
				A.fcmp(d16, d17);
				A.cset(w8, CondCode::kLO);
				A.cbz(w8, At[static_cast<std::size_t>(TargetOffset)]);
				break;
			}
		case EOpcode::Return:
			{
				if (!Function.bReturnsVoid && !PopD16())
				{
					return nullptr;
				}
				if (!Function.bReturnsVoid)
				{
					Label NoOut = A.new_label();
					A.cbz(x22, NoOut);
					A.str(d16, ptr(x22));
					A.bind(NoOut);
				}
				A.mov(w0, wzr);
				FAapcs64CfgCallFrame::Leave(A);
				A.ret(x30);
				break;
			}
		default:
			return nullptr;
		}
		Ip += Size;
	}
	for (const FF64EncodedFailureSite& FailureSite : EncodedFailureSites)
	{
		A.bind(FailureSite.Target);
		A.mov(w0, static_cast<std::uint32_t>(FailureSite.EncodedFailure));
		FAapcs64CfgCallFrame::Leave(A);
		A.ret(x30);
	}
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FLinearF64Kernel>(Fn) : nullptr;
}

FConstI32Kernel EmitConst(JitRuntime& Runtime, const std::int32_t Imm)
{
	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	// AAPCS64: Out in x0
	A.mov(w8, Imm);
	A.str(w8, ptr(x0));
	A.mov(w0, wzr);
	A.ret(x30);
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FConstI32Kernel>(Fn) : nullptr;
}

/**
 * AAPCS64 homogeneous scalar CFG emitter. The bridge supplies:
 * x0=VmContext, x1=Locals, x2=OpStack, x3=OutResult.
 * Scalar calls/globals/fields use the preserved helper frame below. Mixed
 * scalar and boxed object values retain the linear/VM fallback.
 */
FLinearI32Kernel EmitLinearI32CfgA64Kernel(
	JitRuntime& Runtime,
	const FBytecodeModule& Module,
	const FBytecodeFunction& Function)
{
	// First AAPCS64 helper-backed CFG shape: direct global load.
	// Helper exits right after the call, so no caller-saved CFG registers
	// need to survive before the general helper-call frame exists.
	const auto& Bytes = Function.Code;
	if (!Function.bReturnsVoid && Bytes.size() == 4
		&& static_cast<EOpcode>(Bytes[0]) == EOpcode::LoadGlobal
		&& static_cast<EOpcode>(Bytes[3]) == EOpcode::Return)
	{
		const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + 1);
		if (Slot >= Module.GlobalTypeNames.size()
			|| (Module.GlobalTypeNames[Slot] != "int" && Module.GlobalTypeNames[Slot] != "bool"))
		{
			return nullptr;
		}
		CodeHolder Code;
		if (!InitCode(Runtime, Code))
		{
			return nullptr;
		}
		Assembler A(&Code);
		// Bridge ABI: x0=Vm, x3=Out. Helper ABI: x0=Vm, w1=slot, x2=Out.
		A.mov(w1, Slot);
		A.mov(x2, x3);
		A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadGlobalI32));
		FAapcs64CfgCallFrame::SaveLink(A);
		A.blr(x16);
		FAapcs64CfgCallFrame::RestoreLink(A);
		A.ret(x30);
		void* Fn = nullptr;
		return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FLinearI32Kernel>(Fn) : nullptr;
	}
	if (Function.bReturnsVoid && Bytes.size() == 7
		&& static_cast<EOpcode>(Bytes[0]) == EOpcode::ConstI32
		&& static_cast<EOpcode>(Bytes[3]) == EOpcode::StoreGlobal
		&& static_cast<EOpcode>(Bytes[6]) == EOpcode::Return)
	{
		const std::uint16_t Constant = Bytecode::ReadU16LE(Bytes.data() + 1);
		const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + 4);
		if (Constant >= Module.Constants.size() || Module.Constants[Constant].Kind != EConstantKind::I32
			|| Slot >= Module.GlobalTypeNames.size()
			|| (Module.GlobalTypeNames[Slot] != "int" && Module.GlobalTypeNames[Slot] != "bool"))
		{
			return nullptr;
		}
		CodeHolder Code;
		if (!InitCode(Runtime, Code))
		{
			return nullptr;
		}
		Assembler A(&Code);
		A.mov(w2, Module.Constants[Constant].I32);
		A.mov(w1, Slot);
		A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalI32));
		FAapcs64CfgCallFrame::SaveLink(A);
		A.blr(x16);
		FAapcs64CfgCallFrame::RestoreLink(A);
		A.ret(x30);
		void* Fn = nullptr;
		return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FLinearI32Kernel>(Fn) : nullptr;
	}
	if (Function.bReturnsVoid && Function.LocalCount != 0 && Bytes.size() == 7
		&& static_cast<EOpcode>(Bytes[0]) == EOpcode::LoadLocal
		&& static_cast<EOpcode>(Bytes[3]) == EOpcode::StoreGlobal
		&& static_cast<EOpcode>(Bytes[6]) == EOpcode::Return)
	{
		const std::uint16_t Local = Bytecode::ReadU16LE(Bytes.data() + 1);
		const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + 4);
		if (Local >= Function.LocalCount || Slot >= Module.GlobalTypeNames.size()
			|| (Module.GlobalTypeNames[Slot] != "int" && Module.GlobalTypeNames[Slot] != "bool"))
		{
			return nullptr;
		}
		CodeHolder Code;
		if (!InitCode(Runtime, Code))
		{
			return nullptr;
		}
		Assembler A(&Code);
		A.ldr(w2, ptr(x1, static_cast<int>(Local * 4)));
		A.mov(w1, Slot);
		A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalI32));
		FAapcs64CfgCallFrame::SaveLink(A);
		A.blr(x16);
		FAapcs64CfgCallFrame::RestoreLink(A);
		A.ret(x30);
		void* Fn = nullptr;
		return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FLinearI32Kernel>(Fn) : nullptr;
	}
	// Load each parameter in declaration order, make one direct scalar call,
	// then return it. Locals already form the helper's contiguous args buffer.
	if (!Function.bReturnsVoid && Bytes.size() >= 5
		&& static_cast<EOpcode>(Bytes[Bytes.size() - 5]) == EOpcode::Call
		&& static_cast<EOpcode>(Bytes.back()) == EOpcode::Return)
	{
		const std::size_t CallOffset = Bytes.size() - 5;
		const std::uint16_t Target = Bytecode::ReadU16LE(Bytes.data() + CallOffset + 1);
		const std::uint8_t ArgCount = Bytes[CallOffset + 3];
		bool bShape = Target < Module.Functions.size()
			&& Module.Functions[Target].ParamCount == ArgCount
			&& !Module.Functions[Target].bReturnsVoid
			&& ClosureTypeMatchesLocal(Function.ClosureTypeName, false)
			&& ClosureTypeMatchesLocal(Module.Functions[Target].ClosureTypeName, false)
			&& CallOffset == static_cast<std::size_t>(ArgCount) * 3;
		for (std::uint8_t I = 0; bShape && I < ArgCount; ++I)
		{
			bShape = static_cast<EOpcode>(Bytes[static_cast<std::size_t>(I) * 3]) == EOpcode::LoadLocal
				&& Bytecode::ReadU16LE(Bytes.data() + static_cast<std::size_t>(I) * 3 + 1) == I;
		}
		if (bShape)
		{
			CodeHolder Code;
			if (!InitCode(Runtime, Code))
			{
				return nullptr;
			}
			Assembler A(&Code);
			// x0=Vm, x1=target, x2=locals args, w3=count, x4=out.
			// Writing w3 also overwrites x3, which still holds the result pointer.
			A.mov(x4, x3);
			A.mov(x2, x1);
			A.mov(w1, Target);
			A.mov(w3, ArgCount);
			A.mov(x16, reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCall));
			FAapcs64CfgCallFrame::SaveLink(A);
			A.blr(x16);
			FAapcs64CfgCallFrame::RestoreLink(A);
			A.ret(x30);
			void* Fn = nullptr;
			return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FLinearI32Kernel>(Fn) : nullptr;
		}
	}

	JitLowering::FPlan Plan;
	std::string Error;
	if (!JitLowering::BuildScalarPlan(Module, Function, Plan, &Error)
		|| Plan.LocalCount > kMaxNativeCfgLocals
		|| Plan.MaxStackSlots > kMaxNativeCfgStackSlots)
	{
		return nullptr;
	}
	const bool bUnsigned = InferI32FunctionModeLocal(Module, Function)
		== EI32FunctionModeLocal::Unsigned;
	for (const JitIr::EValueType Type : Plan.Signature.Parameters)
	{
		if ((bUnsigned && Type != JitIr::EValueType::U32)
			|| (!bUnsigned && Type != JitIr::EValueType::I32 && Type != JitIr::EValueType::Bool))
		{
			return nullptr;
		}
	}
	if ((bUnsigned && Plan.Signature.ReturnType != JitIr::EValueType::U32
			&& Plan.Signature.ReturnType != JitIr::EValueType::Void)
			|| (!bUnsigned && Plan.Signature.ReturnType != JitIr::EValueType::I32
				&& Plan.Signature.ReturnType != JitIr::EValueType::Bool
				&& Plan.Signature.ReturnType != JitIr::EValueType::Void))
	{
		return nullptr;
	}
	const auto CanElideLocalStore = [&](const std::size_t Offset)
	{
		return Plan.bScalarEligible && std::binary_search(
			Plan.ElidedLocalStoreOffsets.begin(),
			Plan.ElidedLocalStoreOffsets.end(), Offset);
	};
	std::vector<std::optional<FI32CfgTagState>> Entries;
	// This dataflow validates every supported opcode and pending receiver. The
	// generic plan also counts receiver/void builtin slots as boxed, although
	// this I32 emitter handles those without storing boxed values.
	if (!AnalyzeI32BoolCfgTags(Module, Function, Entries))
	{
		return nullptr;
	}

	CodeHolder Code;
	if (!InitCode(Runtime, Code))
	{
		return nullptr;
	}
	Assembler A(&Code);
	std::vector<Label> At(Bytes.size());
	for (std::size_t Ip = 0; Ip < Bytes.size();)
	{
		At[Ip] = A.new_label();
		Ip += Bytecode::InstructionSize(static_cast<EOpcode>(Bytes[Ip]));
	}
	Label NativeEntry = A.new_label();
	struct FEncodedFailureSite
	{
		Label Target;
		std::int32_t EncodedFailure = 0;
	};
	std::vector<FEncodedFailureSite> EncodedFailureSites;
	const auto NewFailureSite = [&](const EJitRuntimeStatus Status, const std::size_t Ip)
	{
		const Label Target = A.new_label();
		EncodedFailureSites.push_back(FEncodedFailureSite{
			Target,
			EncodeJitKernelFailure(Status, static_cast<std::uint32_t>(Ip))});
		return Target;
	};
	A.bind(NativeEntry);

	std::uint16_t SelfFunctionIndex = 0xFFFFu;
	for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
	{
		if (&Module.Functions[Index] == &Function)
		{
			SelfFunctionIndex = static_cast<std::uint16_t>(Index);
			break;
		}
	}
	const std::size_t SelfStackSlots =
		std::max<std::size_t>(1u, Plan.MaxStackSlots);
	const bool bCanDirectSelfRecurse = SelfFunctionIndex != 0xFFFFu
		&& Function.LocalCount <= 64u && SelfStackSlots <= 64u;
	std::vector<std::optional<FExternalDirectI32Callee>> ExternalDirectCallees(
		Module.Functions.size());
	const auto GetExternalDirectCallee = [&](const std::uint16_t CalleeIndex)
		-> const FExternalDirectI32Callee*
	{
		if (CalleeIndex >= Module.Functions.size()
			|| CalleeIndex == SelfFunctionIndex)
		{
			return nullptr;
		}
		auto& Cached = ExternalDirectCallees[CalleeIndex];
		if (!Cached)
		{
			Cached.emplace();
			(void)AnalyzeExternalDirectI32Callee(
				Module, SelfFunctionIndex, CalleeIndex, *Cached);
		}
		return Cached->bEligible ? &*Cached : nullptr;
	};
	FAapcs64CfgCallFrame::Enter(A);
	for (std::size_t Ip = 0; Ip < Bytes.size();)
	{
		if (!Entries[Ip])
		{
			Ip += Bytecode::InstructionSize(static_cast<EOpcode>(Bytes[Ip]));
			continue;
		}
		const EOpcode Op = static_cast<EOpcode>(Bytes[Ip]);
		const std::size_t Size = Bytecode::InstructionSize(Op);
		A.bind(At[Ip]);
		const auto& StackTags = Entries[Ip]->Stack;
		auto PushW8 = [&]()
		{
			A.str(w8, ptr(x21, x23, lsl(2)));
			A.add(w23, w23, 1);
		};
		auto PopW8 = [&]()
		{
			A.sub(w23, w23, 1);
			A.ldr(w8, ptr(x21, x23, lsl(2)));
		};
		auto PopW9 = [&]() -> bool
		{
			A.sub(w23, w23, 1);
			A.ldr(w9, ptr(x21, x23, lsl(2)));
			return true;
		};
		switch (Op)
		{
		case EOpcode::Nop:
			{
				break;
			}
		case EOpcode::ConstI32:
			{
				const std::uint16_t Index = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Index >= Module.Constants.size())
				{
					return nullptr;
				}
				const auto& Constant = Module.Constants[Index];
				if (!bUnsigned && Constant.Kind == EConstantKind::I32)
				{
					A.mov(w8, Constant.I32);
				}
				else if (bUnsigned && (Constant.Kind == EConstantKind::U32 || Constant.Kind == EConstantKind::I32))
				{
					A.mov(w8, Constant.Kind == EConstantKind::U32 ? static_cast<std::int32_t>(Constant.U32) : Constant.I32);
				}
				else
				{
					return nullptr;
				}
				PushW8();
				break;
			}
		case EOpcode::ConstBool:
			{
				A.mov(w8, Bytes[Ip + 1] ? 1 : 0);
				PushW8();
				break;
			}
		case EOpcode::LoadLocal:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Function.LocalCount)
				{
					return nullptr;
				}
				A.ldr(w8, ptr(x20, static_cast<int>(Slot * 4)));
				PushW8();
				break;
			}
		case EOpcode::StoreLocal:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Function.LocalCount)
				{
					return nullptr;
				}
				PopW8();
				if (!CanElideLocalStore(Ip))
				{
					A.str(w8, ptr(x20, static_cast<int>(Slot * 4)));
				}
				break;
			}
		case EOpcode::LoadGlobal:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Module.GlobalTypeNames.size()
					|| (bUnsigned ? Module.GlobalTypeNames[Slot] != "uint"
					: (Module.GlobalTypeNames[Slot] != "int" && Module.GlobalTypeNames[Slot] != "bool")))
				{
					return nullptr;
				}
				A.mov(x0, x19);
				A.mov(w1, Slot);
				A.add(x2, x21, x23, lsl(2));
				A.mov(x16, bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadGlobalU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadGlobalI32));
				A.blr(x16);
				A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				A.add(w23, w23, 1);
				break;
			}
		case EOpcode::StoreGlobal:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				if (Slot >= Module.GlobalTypeNames.size()
					|| (bUnsigned ? Module.GlobalTypeNames[Slot] != "uint"
					: (Module.GlobalTypeNames[Slot] != "int" && Module.GlobalTypeNames[Slot] != "bool")))
				{
					return nullptr;
				}
				PopW8();
				A.mov(x0, x19);
				A.mov(w1, Slot);
				A.mov(w2, w8);
				A.mov(x16, bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::StoreGlobalI32));
				A.blr(x16);
				A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				break;
			}
		case EOpcode::LoadThisField:
			{
				const std::uint16_t Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x0, x19);
				A.mov(w1, Name);
				A.add(x2, x21, x23, lsl(2));
				A.mov(x16, bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadThisFieldU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::LoadThisFieldI32));
				A.blr(x16);
				A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				A.add(w23, w23, 1);
				break;
			}
		case EOpcode::StoreThisField:
		case EOpcode::InitializeThisField:
			{
				const std::uint16_t Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				PopW8();
				A.mov(x0, x19);
				A.mov(w1, Name);
				A.mov(w2, w8);
				A.mov(x16, bUnsigned
					? reinterpret_cast<std::uint64_t>(Op == EOpcode::InitializeThisField
						? &VeroScript::AsmJit::JitNative::LinearHelpers::InitializeThisFieldU32
						: &VeroScript::AsmJit::JitNative::LinearHelpers::StoreThisFieldU32)
					: reinterpret_cast<std::uint64_t>(Op == EOpcode::InitializeThisField
						? &VeroScript::AsmJit::JitNative::LinearHelpers::InitializeThisFieldI32
						: &VeroScript::AsmJit::JitNative::LinearHelpers::StoreThisFieldI32));
				A.blr(x16);
				A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				break;
			}
		case EOpcode::IncField:
			{
				const std::uint16_t Name = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				A.mov(x0, x19);
				A.mov(w1, Name);
				A.mov(x16, bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::IncThisFieldU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::IncThisFieldI32));
				A.blr(x16);
				A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				break;
			}
		case EOpcode::Call:
			{
				const std::uint16_t Target = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::uint8_t ArgCount = Bytes[Ip + 3];
				if (Target >= Module.Functions.size() || Module.Functions[Target].ParamCount != ArgCount)
				{
					return nullptr;
				}
				if (StackTags.size() < ArgCount)
				{
					return nullptr;
				}
				A.sub(w23, w23, ArgCount);
				const FExternalDirectI32Callee* const ExternalCallee =
					GetExternalDirectCallee(Target);
				if (ExternalCallee && ExternalCallee->bUnsigned == bUnsigned)
				{
					// AAPCS64 counterpart of the x64 generation-owned direct-call path.
					// It deliberately shares the same architecture-neutral eligibility
					// proof, frame limits, recursion accounting and failure-owner protocol.
					const std::size_t OutOffset = 0u;
					const std::size_t StatusOffset = 4u;
					const std::size_t LocalsOffset = 16u;
					const std::size_t StackOffset = LocalsOffset
						+ ExternalCallee->LocalCount * 4u;
					const std::size_t FrameBytes =
						(StackOffset + ExternalCallee->StackSlots * 4u + 15u) & ~15u;
					Label FallbackCall = A.new_label();
					Label LanguageOverflow = A.new_label();
					Label DirectSuccess = A.new_label();
					Label FailureOwnerKnown = A.new_label();
					Label CallDone = A.new_label();

					A.cbz(x24, FallbackCall);
					A.ldr(x10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectI32TargetCount))));
					A.mov(w11, Target);
					A.cmp(x10, x11);
					A.b_ls(FallbackCall);
					A.ldr(x16, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectI32Targets))));
					A.cbz(x16, FallbackCall);
					A.mov(w10, Target);
					A.ldr(x16, ptr(x16, x10, lsl(3)));
					A.cbz(x16, FallbackCall);

					A.ldr(x10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, ActiveDepth))));
					A.ldr(x11, ptr(x10));
					A.ldr(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, LanguageDepthLimit))));
					A.cmp(x11, x12);
					A.b_hs(LanguageOverflow);
					A.ldr(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, NativeDepthLimit))));
					A.cmp(x11, x12);
					A.b_hs(FallbackCall);
					A.add(x11, x11, 1);
					A.str(x11, ptr(x10));
					A.ldr(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectCalls))));
					A.add(x12, x12, 1);
					A.str(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectCalls))));

					A.add(x10, x21, x23, lsl(2));
					A.sub(sp, sp, static_cast<std::uint32_t>(FrameBytes));
					for (std::size_t Arg = 0; Arg < ArgCount; ++Arg)
					{
						A.ldr(w8, ptr(x10, static_cast<int>(Arg * 4u)));
						A.str(w8, ptr(sp, static_cast<int>(LocalsOffset + Arg * 4u)));
					}
					A.mov(w8, wzr);
					for (std::size_t Local = ArgCount;
						Local < ExternalCallee->LocalCount; ++Local)
					{
						A.str(w8, ptr(sp, static_cast<int>(LocalsOffset + Local * 4u)));
					}
					A.mov(x0, x19);
					A.add(x1, sp, static_cast<std::uint32_t>(LocalsOffset));
					A.add(x2, sp, static_cast<std::uint32_t>(StackOffset));
					A.add(x3, sp, static_cast<std::uint32_t>(OutOffset));
					A.mov(x4, x24);
					A.blr(x16);
					A.str(w0, ptr(sp, static_cast<int>(StatusOffset)));
					A.cbz(w0, FailureOwnerKnown);
					A.ldrh(w10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, FailureFunctionIndex))));
					A.mov(w11, 0xFFFFu);
					A.cmp(w10, w11);
					A.b_ne(FailureOwnerKnown);
					A.mov(w10, Target);
					A.strh(w10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, FailureFunctionIndex))));
					A.bind(FailureOwnerKnown);
					A.ldr(x10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, ActiveDepth))));
					A.ldr(x11, ptr(x10));
					A.sub(x11, x11, 1);
					A.str(x11, ptr(x10));
					A.ldr(w9, ptr(sp, static_cast<int>(StatusOffset)));
					if (!Module.Functions[Target].bReturnsVoid)
					{
						A.ldr(w8, ptr(sp, static_cast<int>(OutOffset)));
					}
					else
					{
						A.mov(w8, wzr);
					}
					A.add(sp, sp, static_cast<std::uint32_t>(FrameBytes));
					A.cbz(w9, DirectSuccess);
					A.mov(w0, w9);
					FAapcs64CfgCallFrame::Leave(A);
					A.ret(x30);
					A.bind(DirectSuccess);
					PushW8();
					A.b(CallDone);

					A.bind(LanguageOverflow);
					A.mov(x0, x19);
					A.mov(w1, static_cast<std::uint32_t>(bUnsigned
						? EJitScalarValueKind::U32 : EJitScalarValueKind::I32));
					A.mov(x16, reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
					A.blr(x16);
					A.b(NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));

					A.bind(FallbackCall);
					A.mov(x0, x19);
					A.mov(w1, Target);
					A.add(x2, x21, x23, lsl(2));
					A.mov(w3, ArgCount);
					A.mov(x4, sp);
					A.mov(x16, bUnsigned
						? reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallU32)
						: reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCall));
					A.blr(x16);
					A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
					A.ldr(w8, ptr(sp));
					if (Module.Functions[Target].bReturnsVoid)
					{
						A.mov(w8, wzr);
					}
					PushW8();
					A.bind(CallDone);
					break;
				}
				if (bCanDirectSelfRecurse && Target == SelfFunctionIndex)
				{
					const std::size_t OutOffset = 0u;
					const std::size_t LocalsOffset = 16u;
					const std::size_t StackOffset = LocalsOffset + Function.LocalCount * 4u;
					const std::size_t FrameBytes = (StackOffset + SelfStackSlots * 4u + 15u) & ~15u;
					Label DirectCall = A.new_label();
					Label FallbackCall = A.new_label();
					Label LanguageOverflow = A.new_label();
					Label CallDone = A.new_label();

					A.cbz(x24, FallbackCall);
					A.ldr(x10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, ActiveDepth))));
					A.ldr(x11, ptr(x10));
					A.ldr(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, LanguageDepthLimit))));
					A.cmp(x11, x12);
					A.b_hs(LanguageOverflow);
					A.ldr(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, NativeDepthLimit))));
					A.cmp(x11, x12);
					A.b_hs(FallbackCall);
					A.add(x11, x11, 1);
					A.str(x11, ptr(x10));
					A.ldr(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectCalls))));
					A.add(x12, x12, 1);
					A.str(x12, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, DirectCalls))));
					A.b(DirectCall);

					A.bind(LanguageOverflow);
					A.mov(x0, x19);
					A.mov(w1, static_cast<std::uint32_t>(bUnsigned
						? EJitScalarValueKind::U32 : EJitScalarValueKind::I32));
					A.mov(x16, reinterpret_cast<std::uint64_t>(
						&VeroScript::AsmJit::JitNative::LinearHelpers::EnterDirectScalarRecursion));
					A.blr(x16);
					A.b(NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));

					A.bind(DirectCall);
					A.add(x10, x21, x23, lsl(2));
					A.sub(sp, sp, static_cast<std::uint32_t>(FrameBytes));
					for (std::size_t Arg = 0; Arg < ArgCount; ++Arg)
					{
						A.ldr(w8, ptr(x10, static_cast<int>(Arg * 4u)));
						A.str(w8, ptr(sp, static_cast<int>(LocalsOffset + Arg * 4u)));
					}
					A.mov(w8, wzr);
					for (std::size_t Local = ArgCount; Local < Function.LocalCount; ++Local)
					{
						A.str(w8, ptr(sp, static_cast<int>(LocalsOffset + Local * 4u)));
					}
					A.mov(x0, x19);
					A.add(x1, sp, static_cast<std::uint32_t>(LocalsOffset));
					A.add(x2, sp, static_cast<std::uint32_t>(StackOffset));
					A.add(x3, sp, static_cast<std::uint32_t>(OutOffset));
					A.mov(x4, x24);
					A.bl(NativeEntry);
					A.mov(w9, w0);
					A.ldr(x10, ptr(x24, static_cast<int>(
						offsetof(FNativeRecursionState, ActiveDepth))));
					A.ldr(x11, ptr(x10));
					A.sub(x11, x11, 1);
					A.str(x11, ptr(x10));
					if (!Function.bReturnsVoid)
					{
						A.ldr(w8, ptr(sp, static_cast<int>(OutOffset)));
					}
					else
					{
						A.mov(w8, wzr);
					}
					A.add(sp, sp, static_cast<std::uint32_t>(FrameBytes));
					Label DirectSuccess = A.new_label();
					A.cbz(w9, DirectSuccess);
					A.mov(w0, w9);
					FAapcs64CfgCallFrame::Leave(A);
					A.ret(x30);
					A.bind(DirectSuccess);
					PushW8();
					A.b(CallDone);

					A.bind(FallbackCall);
					A.mov(x0, x19);
					A.mov(w1, Target);
					A.add(x2, x21, x23, lsl(2));
					A.mov(w3, ArgCount);
					A.mov(x4, sp);
					A.mov(x16, bUnsigned
						? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallU32)
						: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCall));
					A.blr(x16);
					A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
					A.ldr(w8, ptr(sp));
					if (Function.bReturnsVoid)
					{
						A.mov(w8, wzr);
					}
					PushW8();
					A.bind(CallDone);
					break;
				}
				A.mov(x0, x19);
				A.mov(w1, Target);
				A.add(x2, x21, x23, lsl(2));
				A.mov(w3, ArgCount);
				A.mov(x4, sp);
				A.mov(x16, bUnsigned
					? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCallU32)
					: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeCall));
				A.blr(x16);
				A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				A.ldr(w8, ptr(sp));
				if (!Module.Functions[Target].bReturnsVoid)
				{
					PushW8();
				}
				else
				{
					A.mov(w8, 0);
					PushW8();
				}
				break;
			}
		case EOpcode::LoadThis:
			{
				break;
			}
		case EOpcode::CallBoundMethod:
		case EOpcode::CallMethod:
			{
				const std::uint16_t TargetOrName = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::uint8_t ArgCount = Bytes[Ip + 3];
				if ((Op == EOpcode::CallBoundMethod && TargetOrName >= Module.Functions.size())
					|| (Op == EOpcode::CallMethod && TargetOrName >= Module.StringPool.size()))
				{
					return nullptr;
				}
				if (StackTags.size() < ArgCount)
				{
					return nullptr;
				}
				A.sub(w23, w23, ArgCount);
				A.mov(x0, x19);
				A.mov(w1, TargetOrName);
				A.add(x2, x21, x23, lsl(2));
				A.mov(w3, ArgCount);
				A.mov(x4, sp);
				bool bCallVoid = false;
				bool bSaw = false;
				bool bAmbiguous = false;
				std::uint16_t ResolvedFnIdx = TargetOrName;
				if (Op == EOpcode::CallBoundMethod && TargetOrName < Module.Functions.size())
				{
					bCallVoid = Module.Functions[TargetOrName].bReturnsVoid;
					bSaw = true;
				}
				else if (Op == EOpcode::CallMethod)
				{
					const std::string& LeafName = Module.StringPool[TargetOrName];
					std::uint16_t CandidateIndex = 0;
					for (const auto& Candidate : Module.Functions)
					{
						const std::uint16_t CurrentCandidateIndex = CandidateIndex++;
						if (!Candidate.bIsMethod || Candidate.ParamCount != ArgCount)
						{
							continue;
						}
						if (!MethodLinkageLeafMatches(Candidate.Name, LeafName))
						{
							continue;
						}
						if (!ClosureTypeMatchesLocal(Candidate.ClosureTypeName, bUnsigned))
						{
							continue;
						}
						if (bSaw)
						{
							bAmbiguous = true;
						}
						else
						{
							ResolvedFnIdx = CurrentCandidateIndex;
							bCallVoid = Candidate.bReturnsVoid;
							bSaw = true;
						}
					}
					if (!bAmbiguous && bSaw)
					{
						A.mov(w1, ResolvedFnIdx);
					}
				}
				A.mov(x16, (Op == EOpcode::CallBoundMethod || (!bAmbiguous && bSaw && Op == EOpcode::CallMethod))
					? (bUnsigned
						? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisU32)
						: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::InvokeBoundMethodFromThisI32))
						: (bUnsigned
							? reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::CallMethodFromThisU32)
							: reinterpret_cast<std::uint64_t>(&VeroScript::AsmJit::JitNative::LinearHelpers::CallMethodFromThisI32)));
				A.blr(x16);
				A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
				A.ldr(w8, ptr(sp));
				if (bSaw && !bCallVoid)
				{
					PushW8();
				}
				else
				{
					A.mov(w8, 0);
					PushW8();
				}
				break;
			}
		case EOpcode::CallBuiltin:
			{
				const EBuiltinId Builtin = static_cast<EBuiltinId>(Bytes[Ip + 1]);
				const auto ArgCount = Bytes[Ip + 2];
				if (Builtin == EBuiltinId::Print || Builtin == EBuiltinId::Eprint)
				{
					if (ArgCount != 1 || StackTags.empty())
					{
						return nullptr;
					}
					const std::uint8_t ArgTag = StackTags.back();
					PopW8();
					A.mov(w8, w8); // zero-extend into x8 for the 8-byte helper slot
					A.str(x8, ptr(sp));
					A.mov(x0, x19);
					A.mov(w1, static_cast<std::uint32_t>(Builtin));
					A.mov(w2, static_cast<std::uint32_t>(ArgTag));
					A.mov(x3, sp);
					A.mov(w4, 1);
					A.mov(x16, reinterpret_cast<std::uint64_t>(
							&VeroScript::AsmJit::JitNative::LinearHelpers::BuiltinMixed));
					FAapcs64CfgCallFrame::SaveLink(A);
					A.blr(x16);
					FAapcs64CfgCallFrame::RestoreLink(A);
					A.cbnz(w0, NewFailureSite(EJitRuntimeStatus::GenericFailure, Ip));
					break;
				}
				if (Builtin == EBuiltinId::Abs)
				{
					if (bUnsigned)
					{
						break;
					}
					PopW8();
					A.mov(w10, static_cast<std::int32_t>(0x80000000u));
					A.cmp(w8, w10);
					A.b_eq(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
					Label NonNegative = A.new_label();
					A.cmp(w8, 0);
					A.b_ge(NonNegative);
					A.neg(w8, w8);
					A.bind(NonNegative);
					PushW8();
					break;
				}
				if (Builtin == EBuiltinId::Min || Builtin == EBuiltinId::Max)
				{
					if (!PopW9())
					{
						return nullptr;
					}
					PopW8();
					A.cmp(w8, w9);
					A.csel(w8, w8, w9, Builtin == EBuiltinId::Min
						? (bUnsigned ? CondCode::kLS : CondCode::kLE)
						: (bUnsigned ? CondCode::kHS : CondCode::kGE));
					PushW8();
					break;
				}
				if (Builtin == EBuiltinId::ToInt)
				{
					// Peek stack top; value stays in place (tag retargeted by CFG analysis).
					A.sub(w10, w23, 1);
					A.ldr(w8, ptr(x21, w10, lsl(2)));
					if (bUnsigned)
					{
						A.mov(w11, static_cast<std::int32_t>(0x80000000u));
						A.cmp(w8, w11);
						A.b_hs(NewFailureSite(EJitRuntimeStatus::ToIntOverflow, Ip));
					}
					break;
				}
				if (Builtin == EBuiltinId::ToUnsigned)
				{
					A.sub(w10, w23, 1);
					A.ldr(w8, ptr(x21, w10, lsl(2)));
					if (!bUnsigned)
					{
						A.tbnz(w8, 31, NewFailureSite(EJitRuntimeStatus::ToUnsignedOverflow, Ip));
					}
					break;
				}
				return nullptr;
			}
		case EOpcode::IncLocal:
		case EOpcode::AddLocalImm:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::int32_t Imm = Op == EOpcode::IncLocal
					? 1
					: Bytecode::ReadI32LE(Bytes.data() + Ip + 3);
				if (Slot >= Function.LocalCount)
				{
					return nullptr;
				}
				A.ldr(w8, ptr(x20, static_cast<int>(Slot * 4)));
				// AddLocalImm carries an arbitrary int32 payload. AArch64 ADD/SUB
				// immediates only encode a small unsigned subset, so keep the complete
				// language range by materializing the two's-complement value.
				A.mov(w9, Imm);
				if (bUnsigned)
				{
					A.add(w8, w8, w9);
				}
				else
				{
					A.adds(w8, w8, w9);
					A.b_vs(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
				}
				A.str(w8, ptr(x20, static_cast<int>(Slot * 4)));
				break;
			}
		case EOpcode::AddLocalLocal:
			{
				const std::uint16_t Dst = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::uint16_t Src = Bytecode::ReadU16LE(Bytes.data() + Ip + 3);
				if (Dst >= Function.LocalCount || Src >= Function.LocalCount)
				{
					return nullptr;
				}
				A.ldr(w8, ptr(x20, static_cast<int>(Dst * 4)));
				A.ldr(w9, ptr(x20, static_cast<int>(Src * 4)));
				if (bUnsigned)
				{
					A.add(w8, w8, w9);
				}
				else
				{
					A.adds(w8, w8, w9);
					A.b_vs(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
				}
				A.str(w8, ptr(x20, static_cast<int>(Dst * 4)));
				break;
			}
		case EOpcode::Add:
		case EOpcode::Sub:
		case EOpcode::Mul:
		case EOpcode::Div:
		case EOpcode::Mod:
		case EOpcode::BitAnd:
		case EOpcode::BitOr:
		case EOpcode::BitXor:
		case EOpcode::Shl:
		case EOpcode::Shr:
		case EOpcode::Eq:
		case EOpcode::Ne:
		case EOpcode::Lt:
		case EOpcode::Le:
		case EOpcode::Gt:
		case EOpcode::Ge:
			{
				if (!PopW9())
				{
					return nullptr;
				}
				(void)PopW8();
				if (Op == EOpcode::Add)
				{
					if (bUnsigned)
					{
						A.add(w8, w8, w9);
					}
					else
					{
						A.adds(w8, w8, w9);
						A.b_vs(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
					}
				}
				else if (Op == EOpcode::Sub)
				{
					if (bUnsigned)
					{
						A.sub(w8, w8, w9);
					}
					else
					{
						A.subs(w8, w8, w9);
						A.b_vs(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
					}
				}
				else if (Op == EOpcode::Mul)
				{
					if (bUnsigned)
					{
						A.mul(w8, w8, w9);
					}
					else
					{
						A.smull(x10, w8, w9);
						A.sxtw(x11, w10);
						A.cmp(x10, x11);
						A.b_ne(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
						A.mov(w8, w10);
					}
				}
				else if (Op == EOpcode::Div || Op == EOpcode::Mod)
				{
					A.cbz(w9, NewFailureSite(EJitRuntimeStatus::DivisionByZero, Ip));
					Label DoDiv = A.new_label();
					if (!bUnsigned)
					{
						A.mov(w10, static_cast<std::int32_t>(0x80000000u));
						A.cmp(w8, w10);
						A.b_ne(DoDiv);
						A.cmn(w9, 1);
						A.b_eq(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
					}
					A.bind(DoDiv);
					if (bUnsigned)
					{
						A.udiv(w10, w8, w9);
					}
					else
					{
						A.sdiv(w10, w8, w9);
					}
					if (Op == EOpcode::Div)
					{
						A.mov(w8, w10);
					}
					else
					{
						A.msub(w8, w10, w9, w8);
					}
				}
				else if (Op == EOpcode::BitAnd)
				{
					A.and_(w8, w8, w9);
				}
				else if (Op == EOpcode::BitOr)
				{
					A.orr(w8, w8, w9);
				}
				else if (Op == EOpcode::BitXor)
				{
					A.eor(w8, w8, w9);
				}
				else if (Op == EOpcode::Shl || Op == EOpcode::Shr)
				{
					A.cmp(w9, 32);
					A.b_hs(NewFailureSite(EJitRuntimeStatus::ShiftOutOfRange, Ip));
					if (Op == EOpcode::Shl)
					{
						A.lslv(w8, w8, w9);
					}
					else if (bUnsigned)
					{
						A.lsrv(w8, w8, w9);
					}
					else
					{
						A.asrv(w8, w8, w9);
					}
				}
				else
				{
					A.cmp(w8, w9);
					CondCode Cc = CondCode::kEQ;
					switch (Op)
					{
					case EOpcode::Eq:
						{
							Cc = CondCode::kEQ;
							break;
						}
					case EOpcode::Ne:
						{
							Cc = CondCode::kNE;
							break;
						}
					case EOpcode::Lt:
						{
							Cc = bUnsigned ? CondCode::kLO : CondCode::kLT;
							break;
						}
					case EOpcode::Le:
						{
							Cc = bUnsigned ? CondCode::kLS : CondCode::kLE;
							break;
						}
					case EOpcode::Gt:
						{
							Cc = bUnsigned ? CondCode::kHI : CondCode::kGT;
							break;
						}
					case EOpcode::Ge:
						{
							Cc = bUnsigned ? CondCode::kHS : CondCode::kGE;
							break;
						}
					default:
						{
							break;
						}
					}
					A.cset(w8, Cc);
					PushW8();
					break;
				}
				PushW8();
				break;
			}
		case EOpcode::Neg:
			{
				(void)PopW8();
				if (!bUnsigned)
				{
					A.mov(w10, static_cast<std::int32_t>(0x80000000u));
					A.cmp(w8, w10);
					A.b_eq(NewFailureSite(EJitRuntimeStatus::IntegerOverflow, Ip));
				}
				A.neg(w8, w8);
				PushW8();
				break;
			}
		case EOpcode::BitNot:
			{
				(void)PopW8();
				// MSVC ARM64 headers also define mvn as an intrinsic macro.
				(A.mvn)(w8, w8);
				PushW8();
				break;
			}
		case EOpcode::Not:
			{
				(void)PopW8();
				A.cmp(w8, 0);
				A.cset(w8, CondCode::kEQ);
				PushW8();
				break;
			}
		case EOpcode::Dup:
			{
				A.sub(w10, w23, 1);
				A.ldr(w8, ptr(x21, w10, lsl(2)));
				PushW8();
				break;
			}
		case EOpcode::Pop:
			{
				PopW8();
				break;
			}
		case EOpcode::Jmp:
			{
				const std::int32_t Offset = Bytecode::ReadI32LE(Bytes.data() + Ip + 1);
				const std::size_t Target = static_cast<std::size_t>(static_cast<std::int64_t>(Ip + Size) + Offset);
				if (Target >= At.size())
				{
					return nullptr;
				}
				A.b(At[Target]);
				break;
			}
		case EOpcode::JmpIfFalse:
			{
				PopW8();
				const std::int32_t Offset = Bytecode::ReadI32LE(Bytes.data() + Ip + 1);
				const std::size_t Target = static_cast<std::size_t>(static_cast<std::int64_t>(Ip + Size) + Offset);
				if (Target >= At.size())
				{
					return nullptr;
				}
				A.cbz(w8, At[Target]);
				break;
			}
		case EOpcode::JmpIfLocalGeImm:
			{
				const std::uint16_t Slot = Bytecode::ReadU16LE(Bytes.data() + Ip + 1);
				const std::int32_t Imm = Bytecode::ReadI32LE(Bytes.data() + Ip + 3);
				const std::int32_t Offset = Bytecode::ReadI32LE(Bytes.data() + Ip + 7);
				const std::size_t Target = static_cast<std::size_t>(static_cast<std::int64_t>(Ip + Size) + Offset);
				if (Slot >= Function.LocalCount || Target >= At.size())
				{
					return nullptr;
				}
				A.ldr(w8, ptr(x20, static_cast<int>(Slot * 4)));
				A.mov(w9, Imm);
				A.cmp(w8, w9);
				if (bUnsigned)
				{
					A.b_hs(At[Target]);
				}
				else
				{
					A.b_ge(At[Target]);
				}
				break;
			}
		case EOpcode::Return:
			{
				if (!Function.bReturnsVoid)
				{
					PopW8();
				}
				Label NoOut = A.new_label();
				if (!Function.bReturnsVoid)
				{
					A.cbz(x22, NoOut);
					A.str(w8, ptr(x22));
					A.bind(NoOut);
				}
				A.mov(w0, wzr);
				FAapcs64CfgCallFrame::Leave(A);
				A.ret(x30);
				break;
			}
		default:
			return nullptr;
		}
		Ip += Size;
	}
	// Void bodies may fall off the end with no trailing Return.
	if (Function.bReturnsVoid)
	{
		A.mov(w0, wzr);
		FAapcs64CfgCallFrame::Leave(A);
		A.ret(x30);
	}
	for (const FEncodedFailureSite& FailureSite : EncodedFailureSites)
	{
		A.bind(FailureSite.Target);
		A.mov(w0, static_cast<std::uint32_t>(FailureSite.EncodedFailure));
		FAapcs64CfgCallFrame::Leave(A);
		A.ret(x30);
	}
	void* Fn = nullptr;
	return FinalizeKernel(Runtime, Code, &Fn) ? reinterpret_cast<FLinearI32Kernel>(Fn) : nullptr;
}
