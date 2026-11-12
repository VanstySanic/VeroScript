// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in the Http namespace; result snapshots are independent of the selected transport.

	const Network::FOperation& FStreamingOperation::Operation() const noexcept
	{
		return Op;
	}
	std::shared_ptr<Network::FReadableStream> FStreamingOperation::Body() const noexcept
	{
		return Reader;
	}
	bool FStreamingOperation::TryHead(FResponse& Out, Network::FError& Error) const
	{
		Error = {};
		try
		{
			if (State)
			{
				std::lock_guard<std::mutex> Lock(State->Mutex);
				if (State->Head)
				{
					auto Copy = *State->Head;
					Out = std::move(Copy);
					return true;
				}
			}
			if (!Op.IsValid())
			{
				Error.Code = Network::EError::State;
			}
			else if (Op.IsComplete())
			{
				Error = Op.Error();
			}
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = Network::EError::Allocation;
		}
		return false;
	}
	const Network::FOperation& FResponseOperation::Operation() const noexcept
	{
		return Op;
	}
	EResponseState FResponseOperation::State() const noexcept
	{
		if (Op.IsComplete())
		{
			return Op.State() == Network::EOperationState::Succeeded ? EResponseState::Finished : EResponseState::Aborted;
		}
		return Progress ? Progress->State.load() : EResponseState::Aborted;
	}
	std::uint64_t FResponseOperation::BodyBytes() const noexcept
	{
		return Progress ? Progress->Bytes.load() : 0;
	}
