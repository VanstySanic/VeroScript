// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "BoostNetworkBackend.h"
#include <algorithm>

namespace VeroScript::Network
{
	FOperation BoostResolve(FContext& Context, std::string Host, std::uint16_t Port, FOperationOptions Options)
	{
		auto C = Detail::FAccess::Context(Context);
		auto Op = Detail::Begin(C, Options);
		if (!Detail::Terminal(Detail::State(Op)) && (Host.empty() || Host.find('\0') != std::string::npos))
		{
			Detail::Fail(Op, EError::InvalidArgument, "Invalid resolver host");
		}
		if (!Detail::Terminal(Detail::State(Op)))
		{
			// Destruction cancels without allocating a replacement reusable resolver token.
			auto Resolver = std::make_shared<std::optional<Detail::FTcp::resolver>>(std::in_place, Detail::Io(C));
			Detail::SetAbort(Op,
				[Weak = std::weak_ptr<std::optional<Detail::FTcp::resolver>>(Resolver)]
				{
					if (auto R = Weak.lock())
					{
						R->reset();
					}
				});
			Detail::Post(C,
				[Op, Resolver, Host = std::move(Host), Port]
				{
					if (Detail::Terminal(Detail::State(Op)))
					{
						return;
					}
					Detail::Running(Op);
					(*Resolver)->async_resolve(Host,
						std::to_string(Port),
						[Op, Resolver](Detail::FSystemError Error, auto Results)
						{
							if (Detail::Terminal(Detail::State(Op)))
							{
								return;
							}
							if (Error)
							{
								Detail::Fail(Op, EError::Resolve, Error);
								return;
							}
							std::vector<FAddress> Addresses;
							for (const auto& Entry : Results)
							{
								auto Ep = Entry.endpoint();
								Addresses.push_back({Ep.address().to_string(), Ep.port(), Ep.address().is_v6()});
							}
							Detail::Complete(Op, std::move(Addresses));
						});
				});
		}
		return Detail::FAccess::Wrap(Op);
	}

	struct FBoostTcpSocket : Backend::ITcpSocket, std::enable_shared_from_this<FBoostTcpSocket>
	{
		struct FWrite
		{
			std::shared_ptr<Detail::FByteLease> Retention;
			std::shared_ptr<Detail::FOperation> Operation;
			FBytes Bytes;
		};
		explicit FBoostTcpSocket(std::shared_ptr<Detail::FContext> C)
			: Context(std::move(C)), Socket(Detail::Io(Context)), Resolver(std::in_place, Detail::Io(Context))
		{
		}
		~FBoostTcpSocket()
		{
			Detail::RemoveResource(Context, ResourceId);
		}
		void Close()
		{
			bClosed = true;
			Detail::RemoveResource(Context, ResourceId);
			Detail::FSystemError Ignore;
			Socket.cancel(Ignore);
			Socket.close(Ignore);
			Resolver.reset();
			for (auto& Write : Writes)
			{
				Detail::Fail(Write->Operation, EError::Closed, "TCP socket closed");
			}
			Writes.clear();
			QueuedBytes = 0;
		}
		void StartWrite()
		{
			if (bWriting || bClosed)
			{
				return;
			}
			while (!Writes.empty() && Detail::Terminal(Detail::State(Writes.front()->Operation)))
			{
				QueuedBytes -= Writes.front()->Bytes.size();
				Writes.pop_front();
			}
			if (Writes.empty())
			{
				return;
			}
			auto Write = Writes.front();
			bWriting = true;
			Detail::Running(Write->Operation);
			auto Self = shared_from_this();
			Detail::Asio::async_write(Socket,
				Detail::Asio::buffer(Write->Bytes),
				[Self, Write](Detail::FSystemError Ec, std::size_t Count)
				{
					Self->bWriting = false;
					if (!Self->Writes.empty() && Self->Writes.front() == Write)
					{
						Self->QueuedBytes -= Write->Bytes.size();
						Self->Writes.pop_front();
					}
					if (Ec)
					{
						Detail::Fail(Write->Operation, EError::Write, Ec);
						Self->Close();
						return;
					}
					Detail::Complete(Write->Operation, static_cast<std::uint64_t>(Count));
					Self->StartWrite();
				});
		}
		std::shared_ptr<Detail::FContext> Context;
		Detail::FTcp::socket Socket;
		std::optional<Detail::FTcp::resolver> Resolver;
		bool bReading = false;
		bool bWriting = false;
		bool bClosed = false;
		std::deque<std::shared_ptr<FWrite>> Writes;
		std::uint64_t QueuedBytes = 0;
		std::uint64_t ResourceId = 0;

		void Dispose() noexcept override;
		FOperation Read(std::uint64_t MaxBytes, FOperationOptions Options) override;
		FOperation Write(FBytes Bytes, FOperationOptions Options) override;
		void RequestClose() override;
	};



	FOperation BoostConnectTcp(FContext& Context, std::string Host, std::uint16_t Port, FOperationOptions Options)
	{
		auto C = Detail::FAccess::Context(Context);
		auto Op = Detail::Begin(C, Options);
		if (!Detail::Terminal(Detail::State(Op)) && (Host.empty() || Host.find('\0') != std::string::npos || Port == 0))
		{
			Detail::Fail(Op, EError::InvalidArgument, "Invalid TCP host or destination port");
		}
		if (Detail::Terminal(Detail::State(Op)))
		{
			return Detail::FAccess::Wrap(Op);
		}
		auto P = std::make_shared<FBoostTcpSocket>(C);
		P->ResourceId = Detail::AddResource(C,
			[Weak = std::weak_ptr<FBoostTcpSocket>(P)]
			{
				if (auto Locked = Weak.lock())
				{
					Locked->Close();
				}
			});
		if (!P->ResourceId)
		{
			Detail::Fail(Op, EError::Limit, "Connection limit exceeded");
			return Detail::FAccess::Wrap(Op);
		}
		Detail::SetAbort(Op,
			[Weak = std::weak_ptr<FBoostTcpSocket>(P)]
			{
				if (auto Locked = Weak.lock())
				{
					Locked->Close();
				}
			});
		Detail::Post(C,
			[P, Op, Host = std::move(Host), Port]
			{
				if (Detail::Terminal(Detail::State(Op)))
				{
					return;
				}
				Detail::Running(Op);
				P->Resolver->async_resolve(Host,
					std::to_string(Port),
					[P, Op](Detail::FSystemError Error, auto Addresses)
					{
						if (Detail::Terminal(Detail::State(Op)))
						{
							return;
						}
						if (Error)
						{
							Detail::Fail(Op, EError::Resolve, Error);
							return;
						}
						Detail::Asio::async_connect(P->Socket,
							Addresses,
							[P, Op](Detail::FSystemError Ec, auto)
							{
								if (Ec)
								{
									Detail::Fail(Op, EError::Connect, Ec);
									return;
								}
								Detail::Complete(Op, Backend::FAccess::Wrap(P->Context, P));
							});
					});
			});
		return Detail::FAccess::Wrap(Op);
	}

	void FBoostTcpSocket::Dispose() noexcept
	{
		Detail::OnDestroy(Context,
			[this]
			{
				RequestClose();
			});
	}

	FOperation FBoostTcpSocket::Read(std::uint64_t MaxBytes, FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = Detail::Begin(P->Context, Options);
		if (Detail::Terminal(Detail::State(Op)))
		{
			return Detail::FAccess::Wrap(Op);
		}
		if (MaxBytes == 0 || MaxBytes > Detail::Options(P->Context).MaxBufferBytes || MaxBytes > SIZE_MAX)
		{
			Detail::Fail(Op, EError::Limit, "Read buffer exceeds its limit");
			return Detail::FAccess::Wrap(Op);
		}
		auto Retention = Detail::ReserveBytes(P->Context, MaxBytes, Op);
		if (!Retention)
		{
			return Detail::FAccess::Wrap(Op);
		}
		Detail::Post(P->Context,
			[P, Op, MaxBytes, Retention]
			{
				if (Detail::Terminal(Detail::State(Op)))
				{
					return;
				}
				if (P->bReading)
				{
					Detail::Fail(Op, EError::State, "A read is already active");
					return;
				}
				Detail::SetAbort(Op,
					[Weak = std::weak_ptr<FBoostTcpSocket>(P)]
					{
						if (auto Locked = Weak.lock())
						{
							Locked->Close();
						}
					});
				if (Detail::Terminal(Detail::State(Op)))
				{
					return;
				}
				P->bReading = true;
				Detail::Running(Op);
				auto Bytes = std::make_shared<FBytes>(static_cast<std::size_t>(MaxBytes));
				P->Socket.async_read_some(Detail::Asio::buffer(*Bytes),
					[P, Op, Retention, Bytes](Detail::FSystemError Ec, std::size_t Count)
					{
						P->bReading = false;
						if (Ec && Ec != Detail::Asio::error::eof)
						{
							Detail::Fail(Op, EError::Read, Ec);
							return;
						}
						Bytes->resize(Count);
						Detail::Complete(Op, FReadResult{std::move(*Bytes), Ec == Detail::Asio::error::eof}, Retention);
					});
			});
		return Detail::FAccess::Wrap(Op);
	}

	FOperation FBoostTcpSocket::Write(FBytes Bytes, FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = Detail::Begin(P->Context, Options);
		if (Detail::Terminal(Detail::State(Op)))
		{
			return Detail::FAccess::Wrap(Op);
		}
		if (Bytes.size() > Detail::Options(P->Context).MaxBufferBytes)
		{
			Detail::Fail(Op, EError::Limit, "Write buffer exceeds its limit");
			return Detail::FAccess::Wrap(Op);
		}
		auto Retention = Detail::ReserveBytes(P->Context, Bytes.capacity(), Op);
		if (!Retention)
		{
			return Detail::FAccess::Wrap(Op);
		}
		Detail::Post(P->Context,
			[P, Op, Retention, Bytes = std::move(Bytes)]() mutable
			{
				if (Detail::Terminal(Detail::State(Op)))
				{
					return;
				}
				if (P->bClosed)
				{
					Detail::Fail(Op, EError::Closed, "TCP socket closed");
					return;
				}
				if (P->Writes.size() >= Detail::Options(P->Context).MaxQueuedWrites ||
					Bytes.size() > Detail::Options(P->Context).MaxBufferBytes - P->QueuedBytes)
				{
					Detail::Fail(Op, EError::QueueFull, "TCP write queue is full");
					return;
				}
				auto Write = std::make_shared<FBoostTcpSocket::FWrite>();
				Write->Retention = Retention;
				Write->Operation = Op;
				Write->Bytes = std::move(Bytes);
				P->QueuedBytes += Write->Bytes.size();
				P->Writes.push_back(Write);
				Detail::SetAbort(Op,
					[Weak = std::weak_ptr<FBoostTcpSocket>(P), Pending = std::weak_ptr<FBoostTcpSocket::FWrite>(Write)]
					{
						auto Locked = Weak.lock();
						auto Item = Pending.lock();
						if (!Locked || !Item)
						{
							return;
						}
						auto It = std::find(Locked->Writes.begin(), Locked->Writes.end(), Item);
						if (It == Locked->Writes.end())
						{
							return;
						}
						if (It == Locked->Writes.begin() && Locked->bWriting)
						{
							Locked->Close();
							return;
						}
						Locked->QueuedBytes -= Item->Bytes.size();
						Locked->Writes.erase(It);
						Locked->StartWrite();
					});
				P->StartWrite();
			});
		return Detail::FAccess::Wrap(Op);
	}

	void FBoostTcpSocket::RequestClose()
	{
		auto P = shared_from_this();
		Detail::Post(P->Context,
			[P]
			{
				P->Close();
			});
	}



	struct FBoostTcpListener : Backend::ITcpListener, std::enable_shared_from_this<FBoostTcpListener>
	{
		explicit FBoostTcpListener(std::shared_ptr<Detail::FContext> C) : Context(std::move(C)), Acceptor(Detail::Io(Context))
		{
		}
		~FBoostTcpListener()
		{
			Detail::RemoveResource(Context, ResourceId);
		}
		void Close()
		{
			Detail::RemoveResource(Context, ResourceId);
			Detail::FSystemError Ignore;
			Acceptor.close(Ignore);
		}
		std::shared_ptr<Detail::FContext> Context;
		Detail::FTcp::acceptor Acceptor;
		std::uint64_t ResourceId = 0;
		std::uint16_t LocalPort = 0;
		std::weak_ptr<Detail::FOperation> Pending;
		std::weak_ptr<FBoostTcpSocket> PendingSocket;

		void Dispose() noexcept override;
		std::uint16_t Port() const noexcept override;
		FOperation Accept(FOperationOptions Options) override;
		void RequestClose() override;
	};

	std::shared_ptr<FTcpListener> BoostListenTcp(
		FContext& Context, std::string Address, std::uint16_t Port, FError& Error, int Backlog)
	{
		Error = {};
		if (Backlog <= 0 || Address.empty() || Address.find('\0') != std::string::npos)
		{
			Error = {EError::InvalidArgument, "Invalid TCP listener address or backlog", 0};
			return {};
		}
		auto P = std::make_shared<FBoostTcpListener>(Detail::FAccess::Context(Context));
		Detail::FSystemError Ec;
		auto Ip = Detail::Asio::ip::make_address(Address, Ec);
		if (!Ec)
		{
			Detail::FTcp::endpoint Ep(Ip, Port);
			P->Acceptor.open(Ep.protocol(), Ec);
			if (!Ec)
			{
				P->Acceptor.set_option(Detail::Asio::socket_base::reuse_address(true), Ec);
			}
			if (!Ec)
			{
				P->Acceptor.bind(Ep, Ec);
			}
			if (!Ec)
			{
				P->Acceptor.listen(Backlog, Ec);
			}
			if (!Ec)
			{
				P->LocalPort = P->Acceptor.local_endpoint(Ec).port();
			}
		}
		if (Ec)
		{
			Error = {EError::Connect, Ec.message(), Ec.value()};
			return {};
		}
		P->ResourceId = Detail::AddResource(P->Context,
			[Weak = std::weak_ptr<FBoostTcpListener>(P)]
			{
				if (auto Self = Weak.lock())
				{
					Self->Close();
				}
			});
		if (!P->ResourceId)
		{
			Error = {EError::Limit, "Context closed or connection limit exceeded", 0};
			return {};
		}
		auto ContextOwner = P->Context;
		return Backend::FAccess::Wrap(ContextOwner, std::move(P));
	}

	void FBoostTcpListener::Dispose() noexcept
	{
		Detail::OnDestroy(Context,
			[this]
			{
				RequestClose();
			});
	}
	std::uint16_t FBoostTcpListener::Port() const noexcept
	{
		return LocalPort;
	}

	FOperation FBoostTcpListener::Accept(FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = Detail::Begin(P->Context, Options);
		if (!Detail::Terminal(Detail::State(Op)))
		{
			Detail::Post(P->Context,
				[P, Op]
				{
					if (Detail::Terminal(Detail::State(Op)))
					{
						return;
					}
					if (auto Existing = P->Pending.lock())
					{
						if (!Detail::Terminal(Detail::State(Existing)))
						{
							Detail::Fail(Op, EError::State, "A TCP accept is already active");
							return;
						}
						// Logical cancellation is visible before its cleanup post executes.
						// Cancel the old native wait before replacing Pending, so it cannot
						// consume the next connection while its identity guard skips cleanup.
						Detail::FSystemError Ec;
						P->Acceptor.cancel(Ec);
						if (auto Previous = P->PendingSocket.lock())
						{
							Previous->Close();
						}
						if (Ec)
						{
							Detail::Fail(Op, EError::Connect, Ec);
							return;
						}
					}
					auto Socket = std::make_shared<FBoostTcpSocket>(P->Context);
					Socket->ResourceId = Detail::AddResource(P->Context,
						[Weak = std::weak_ptr<FBoostTcpSocket>(Socket)]
						{
							if (auto Self = Weak.lock())
							{
								Self->Close();
							}
						});
					if (!Socket->ResourceId)
					{
						Detail::Fail(Op, EError::Limit, "Connection limit exceeded");
						return;
					}
					P->Pending = Op;
					P->PendingSocket = Socket;
					Detail::SetAbort(Op,
						[Weak = std::weak_ptr<FBoostTcpListener>(P),
							Pending = std::weak_ptr<Detail::FOperation>(Op),
							CancelledSocket = std::weak_ptr<FBoostTcpSocket>(Socket)]
						{
							auto Self = Weak.lock();
							if (Self && Self->Pending.lock() == Pending.lock())
							{
								Detail::FSystemError Ignore;
								Self->Acceptor.cancel(Ignore);
							}
							if (auto Value = CancelledSocket.lock())
							{
								Value->Close();
							}
						});
					Detail::Running(Op);
					P->Acceptor.async_accept(Socket->Socket,
						[P, Op, Socket](Detail::FSystemError Ec)
						{
							if (P->Pending.lock() == Op)
							{
								P->Pending.reset();
								P->PendingSocket.reset();
							}
							if (Detail::Terminal(Detail::State(Op)))
							{
								Socket->Close();
								return;
							}
							if (Ec)
							{
								Detail::Fail(Op, EError::Connect, Ec);
								return;
							}
							Detail::Complete(Op, Backend::FAccess::Wrap(Socket->Context, Socket));
						});
				});
		}
		return Detail::FAccess::Wrap(Op);
	}

	void FBoostTcpListener::RequestClose()
	{
		auto P = shared_from_this();
		Detail::Post(P->Context,
			[P]
			{
				P->Close();
			});
	}



} // namespace VeroScript::Network
