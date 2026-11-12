// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

extern "C" uint32_t vero_web_memory_operation(VeroWebRuntimeHandle Runtime, uint32_t Op, uint32_t A,
	uint32_t B, int64_t X, int64_t Y, int64_t Z, uint64_t Number)
{
	using namespace VeroScript;
	if (!Runtime)
	{
		return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
	}
	if (IsInteractiveExecutionActive())
	{
		return static_cast<uint32_t>(EMemoryStatus::Busy);
	}
	auto& S = *static_cast<FWebRuntimeState*>(Runtime);
	std::fill(std::begin(S.MemoryResult), std::end(S.MemoryResult), 0);
	try
	{
		auto& C = S.MemoryContext;
		FMemoryResult R;
		FValue New;
		const auto Find = [&](uint32_t I) -> const FValue*
		{
			auto It = S.MemoryValues.find(I);
			return It == S.MemoryValues.end() ? nullptr : &It->second;
		};
		const auto* Left = Find(A);
		const auto* Right = Find(B);
		bool Creates = false, Same = false;
		int64_t Length = 0;
		if (((Op > 2 && Op < 15) || Op == 17 || Op == 18) && !Left)
		{
			return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
		}
		switch (Op)
		{
		case 1:
			{
				R = Memory::CreateBuffer(C, X, New);
				Creates = true;
				break;
			}
		case 2:
			{
				if (Number > 1)
				{
					return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
				}
				R = Memory::CreateEmptyView(Number != 0, New);
				Creates = true;
				break;
			}
		case 3:
			{
				if (Number > 1)
				{
					return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
				}
				R = Memory::Slice(C, *Left, X, Y, Number != 0, New);
				Creates = true;
				break;
			}
		case 4:
			{
				if (Number > 1)
				{
					return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
				}
				R = Memory::Resize(C, *Left, X, Number != 0);
				break;
			}
		case 5:
			{
				if (!Right)
				{
					return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
				}
				R = Memory::Copy(C, *Left, X, *Right, Y, Z);
				break;
			}
		case 6:
			{
				if (Number > 255)
				{
					return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
				}
				R = Memory::Fill(C, *Left, X, Y, static_cast<uint8_t>(Number));
				break;
			}
		case 7:
			{
				if (!Right)
				{
					return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
				}
				R = Memory::Equal(C, *Left, X, *Right, Y, Z, Same);
				S.MemoryResult[0] = Same;
				break;
			}
		case 8:
			{
				R = Memory::Clone(C, *Left, New);
				Creates = true;
				break;
			}
		case 9:
			{
				R = Memory::Size(C, *Left, Length);
				S.MemoryResult[0] = static_cast<uint64_t>(Length);
				break;
			}
		case 10:
			{
				R = Memory::Capacity(C, *Left, Length);
				S.MemoryResult[0] = static_cast<uint64_t>(Length);
				break;
			}
		case 11:
			{
				R = Memory::ObjectId(*Left, S.MemoryResult[0]);
				break;
			}
		case 12:
			{
				R = Memory::MutationVersion(C, *Left, S.MemoryResult[0]);
				break;
			}
		case 13:
			{
				if (Number > 1)
				{
					return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
				}
				FMemoryLease L;
				R = Memory::AcquireLease(C, *Left, Number != 0, L);
				if (R)
				{
					S.MemoryResult[1] = reinterpret_cast<uintptr_t>(L.Data);
					S.MemoryResult[2] = L.Size;
					S.MemoryResult[3] = L.Token.Runtime;
					S.MemoryResult[4] = L.Token.Serial;
				}
				break;
			}
		case 17:
		case 18:
			{
				static constexpr const char* Types[] = {"int8", "uint8", "int16", "uint16", "int32", "uint32",
					"int64", "uint64", "float", "double"};
				if (Y < 0 || Y >= 10 || Z < 0 || Z > 1)
				{
					return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
				}
				const auto Order = static_cast<EMemoryByteOrder>(Z);
				FValue Value;
				if (Op == 18)
				{
					if (Y == 8)
					{
						const uint32_t Bits = static_cast<uint32_t>(Number);
						float V;
						std::memcpy(&V, &Bits, 4);
						Value = V;
					}
					else if (Y == 9)
					{
						double V;
						std::memcpy(&V, &Number, 8);
						Value = V;
					}
					else if (Y % 2 == 0)
					{
						Value = Number <= static_cast<uint64_t>(INT64_MAX)
									? static_cast<int64_t>(Number)
									: -1 - static_cast<int64_t>(~Number);
					}
					else
					{
						Value = Number;
					}
					R = Memory::Write(C, *Left, X, Types[Y], Order, Value);
				}
				else
				{
					R = Memory::Read(C, *Left, X, Types[Y], Order, Value);
					if (R)
					{
						std::visit(
							[&](const auto& V)
							{
								using T = std::decay_t<decltype(V)>;
								if constexpr (std::is_integral_v<T>)
								{
									S.MemoryResult[0] = static_cast<uint64_t>(V);
								}
								else if constexpr (std::is_same_v<T, float>)
								{
									uint32_t Bits;
									std::memcpy(&Bits, &V, 4);
									S.MemoryResult[0] = Bits;
								}
								else if constexpr (std::is_same_v<T, double>)
								{
									std::memcpy(&S.MemoryResult[0], &V, 8);
								}
							},
							Value);
					}
				}
				break;
			}
		case 14:
			{
				S.MemoryValues.erase(A);
				break;
			}
		case 15:
			{
				R = C.SetLimits({Number, (static_cast<uint64_t>(A) << 32) | B});
				break;
			}
		case 16:
			{
				auto T = C.GetStats();
				S.MemoryResult[0] = T.OwnedBytes;
				S.MemoryResult[1] = T.BorrowedBytes;
				S.MemoryResult[2] = T.PeakOwnedBytes;
				break;
			}
		default:
			return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
		}
		if (R && Creates)
		{
			if (!S.NextMemoryHandle)
			{
				return static_cast<uint32_t>(EMemoryStatus::LimitExceeded);
			}
			const auto I = S.NextMemoryHandle;
			S.MemoryValues.emplace(I, std::move(New));
			++S.NextMemoryHandle;
			S.MemoryResult[0] = I;
		}
		return static_cast<uint32_t>(R.Status);
	}
	catch (const std::bad_alloc&)
	{
		return static_cast<uint32_t>(EMemoryStatus::OutOfMemory);
	}
	catch (...)
	{
		return static_cast<uint32_t>(EMemoryStatus::InvalidArgument);
	}
}
extern "C" uint64_t vero_web_memory_result(VeroWebRuntimeHandle Runtime, uint32_t Field)
{
	if (!Runtime || Field >= 8 || IsInteractiveExecutionActive())
	{
		return 0;
	}
	return static_cast<FWebRuntimeState*>(Runtime)->MemoryResult[Field];
}
extern "C" uint32_t vero_web_memory_release_lease(uint64_t Runtime, uint64_t Serial)
{
	try
	{
		return static_cast<uint32_t>(VeroScript::Memory::ReleaseLease({Runtime, Serial}).Status);
	}
	catch (...)
	{
		return static_cast<uint32_t>(VeroScript::EMemoryStatus::InvalidArgument);
	}
}
