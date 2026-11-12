// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "BoostNetworkBackend.h"

namespace VeroScript::Network
{
	namespace N = Detail;
	using FUdp = N::Asio::ip::udp;

	struct FBoostUdpSocket : Backend::IUdpSocket, std::enable_shared_from_this<FBoostUdpSocket>
	{
		explicit FBoostUdpSocket(std::shared_ptr<N::FContext> C) : Context(std::move(C)), Socket(Detail::Io(Context))
		{
		}
		~FBoostUdpSocket()
		{
			Detail::RemoveResource(Context, ResourceId);
		}
		void Close()
		{
			Detail::RemoveResource(Context, ResourceId);
			N::FSystemError Ignore;
			Socket.close(Ignore);
		}
		std::shared_ptr<N::FContext> Context;
		FUdp::socket Socket;
		std::uint16_t LocalPort = 0;
		std::uint64_t ResourceId = 0;
		bool bReading = false;

		void Dispose() noexcept override;
		std::uint16_t Port() const noexcept override;
		FOperation SendTo(FBytes Bytes, FAddress Peer, FOperationOptions Options) override;
		FOperation Receive(std::uint64_t MaxBytes, FOperationOptions Options) override;
		FOperation SetBroadcast(bool bEnabled, FOperationOptions Options) override;
		FOperation JoinMulticast(FMulticastGroup Group, FOperationOptions Options);
		FOperation LeaveMulticast(FMulticastGroup Group, FOperationOptions Options);
		void RequestClose() override;
		FOperation Multicast(FMulticastGroup Group, bool bJoin, FOperationOptions Options) override;
	};

	std::shared_ptr<FUdpSocket> BoostBindUdp(
		FContext& Context, std::string Address, std::uint16_t Port, FError& Error)
	{
		Error = {};
		if (Address.empty() || Address.find('\0') != std::string::npos)
		{
			Error = {EError::InvalidArgument, "Invalid UDP bind address", 0};
			return {};
		}
		auto P = std::make_shared<FBoostUdpSocket>(N::FAccess::Context(Context));
		N::FSystemError Ec;
		auto Ip = N::Asio::ip::make_address(Address, Ec);
		if (!Ec)
		{
			FUdp::endpoint Ep(Ip, Port);
			P->Socket.open(Ep.protocol(), Ec);
			if (!Ec)
			{
				P->Socket.bind(Ep, Ec);
			}
		}
		if (Ec)
		{
			Error = {EError::Connect, Ec.message(), Ec.value()};
			return {};
		}
		P->LocalPort = P->Socket.local_endpoint().port();
		P->ResourceId = Detail::AddResource(P->Context,
			[Weak = std::weak_ptr<FBoostUdpSocket>(P)]
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

	void FBoostUdpSocket::Dispose() noexcept
	{
		N::OnDestroy(Context,
			[this]
			{
				RequestClose();
			});
	}

	std::uint16_t FBoostUdpSocket::Port() const noexcept
	{
		return LocalPort;
	}

	FOperation FBoostUdpSocket::SendTo(FBytes Bytes, FAddress Peer, FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (N::Terminal(Detail::State(Op)))
		{
			return N::FAccess::Wrap(Op);
		}
		N::FSystemError Ec;
		auto Address = N::Asio::ip::make_address(Peer.Address, Ec);
		if (Ec || Peer.Address.find('\0') != std::string::npos || Peer.Port == 0 || Bytes.size() > 65507 ||
			Bytes.size() > Detail::Options(P->Context).MaxBufferBytes)
		{
			N::Fail(Op, EError::InvalidArgument, "Invalid UDP peer or datagram exceeds 65507 bytes");
			return N::FAccess::Wrap(Op);
		}
		auto Retention = N::ReserveBytes(P->Context, Bytes.capacity(), Op);
		if (!Retention)
		{
			return N::FAccess::Wrap(Op);
		}
		Detail::Post(P->Context,
			[P,
				Op,
				Peer = FUdp::endpoint(Address, Peer.Port),
				Retention,
				Bytes = std::make_shared<FBytes>(std::move(Bytes))]
			{
				if (N::Terminal(Detail::State(Op)))
				{
					return;
				}
				N::SetAbort(Op,
					[Weak = std::weak_ptr<FBoostUdpSocket>(P)]
					{
						if (auto Self = Weak.lock())
						{
							Self->Close();
						}
					});
				if (N::Terminal(Detail::State(Op)))
				{
					return;
				}
				N::Running(Op);
				P->Socket.async_send_to(N::Asio::buffer(*Bytes),
					Peer,
					[P, Op, Retention, Bytes](N::FSystemError Error, std::size_t Count)
					{
						if (Error)
						{
							N::Fail(Op, EError::Write, Error);
							return;
						}
						N::Complete(Op, static_cast<std::uint64_t>(Count));
					});
			});
		return N::FAccess::Wrap(Op);
	}

	FOperation FBoostUdpSocket::Receive(std::uint64_t MaxBytes, FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (N::Terminal(Detail::State(Op)))
		{
			return N::FAccess::Wrap(Op);
		}
		if (MaxBytes > 65536 || 65536 > Detail::Options(P->Context).MaxBufferBytes)
		{
			N::Fail(Op, EError::Limit, "UDP receive requires a 65536-byte datagram buffer");
			return N::FAccess::Wrap(Op);
		}
		auto Retention = N::ReserveBytes(P->Context, 65536, Op);
		if (!Retention)
		{
			return N::FAccess::Wrap(Op);
		}
		Detail::Post(P->Context,
			[P, Op, MaxBytes, Retention]
			{
				if (N::Terminal(Detail::State(Op)))
				{
					return;
				}
				if (P->bReading)
				{
					N::Fail(Op, EError::State, "A datagram receive is already active");
					return;
				}
				N::SetAbort(Op,
					[Weak = std::weak_ptr<FBoostUdpSocket>(P)]
					{
						if (auto Self = Weak.lock())
						{
							Self->Close();
						}
					});
				if (N::Terminal(Detail::State(Op)))
				{
					return;
				}
				P->bReading = true;
				N::Running(Op);
				auto Bytes = std::make_shared<FBytes>(65536);
				auto Peer = std::make_shared<FUdp::endpoint>();
				P->Socket.async_receive_from(N::Asio::buffer(*Bytes),
					*Peer,
					[P, Op, Retention, Bytes, Peer, MaxBytes](N::FSystemError Error, std::size_t Count)
					{
						P->bReading = false;
						if (Error)
						{
							N::Fail(Op, EError::Read, Error);
							return;
						}
						Bytes->resize(static_cast<std::size_t>(std::min<std::uint64_t>(Count, MaxBytes)));
						FDatagram Result;
						Result.Peer = {Peer->address().to_string(), Peer->port(), Peer->address().is_v6()};
						Result.OriginalBytes = Count;
						Result.bTruncated = Count > MaxBytes;
						Result.Bytes = std::move(*Bytes);
						N::Complete(Op, std::move(Result), Retention);
					});
			});
		return N::FAccess::Wrap(Op);
	}

	FOperation FBoostUdpSocket::SetBroadcast(bool bEnabled, FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (!N::Terminal(Detail::State(Op)))
		{
			Detail::Post(P->Context,
				[P, Op, bEnabled]
				{
					if (N::Terminal(Detail::State(Op)))
					{
						return;
					}
					N::FSystemError Ec;
					P->Socket.set_option(N::Asio::socket_base::broadcast(bEnabled), Ec);
					if (Ec)
					{
						N::Fail(Op, EError::State, Ec);
					}
					else
					{
						N::Complete(Op, true);
					}
				});
		}
		return N::FAccess::Wrap(Op);
	}

	FOperation FBoostUdpSocket::JoinMulticast(FMulticastGroup Group, FOperationOptions Options)
	{
		return Multicast(std::move(Group), true, Options);
	}

	FOperation FBoostUdpSocket::LeaveMulticast(FMulticastGroup Group, FOperationOptions Options)
	{
		return Multicast(std::move(Group), false, Options);
	}

	void FBoostUdpSocket::RequestClose()
	{
		auto P = shared_from_this();
		Detail::Post(P->Context,
			[P]
			{
				P->Close();
			});
	}



	FOperation FBoostUdpSocket::Multicast(FMulticastGroup Group, bool bJoin, FOperationOptions Options)
	{
		auto P = shared_from_this();
		auto Op = N::Begin(P->Context, Options);
		if (N::Terminal(Detail::State(Op)))
		{
			return N::FAccess::Wrap(Op);
		}
		N::FSystemError Ec;
		auto Address = N::Asio::ip::make_address(Group.Group, Ec);
		N::Asio::ip::address_v4 Interface;
		if (!Ec && !Group.InterfaceAddress.empty())
		{
			Interface = N::Asio::ip::make_address_v4(Group.InterfaceAddress, Ec);
		}
		if (Ec || Group.Group.find('\0') != std::string::npos ||
			Group.InterfaceAddress.find('\0') != std::string::npos || !Address.is_multicast() ||
			(Address.is_v4() && Group.InterfaceIndex != 0) || (Address.is_v6() && !Group.InterfaceAddress.empty()))
		{
			N::Fail(Op, EError::InvalidArgument, "Invalid multicast group or interface");
			return N::FAccess::Wrap(Op);
		}
		Detail::Post(P->Context,
			[P, Op, Address, Interface, Index = Group.InterfaceIndex, bJoin]
			{
				if (N::Terminal(Detail::State(Op)))
				{
					return;
				}
				N::FSystemError Error;
				if (Address.is_v4())
				{
					if (bJoin)
					{
						P->Socket.set_option(N::Asio::ip::multicast::join_group(Address.to_v4(), Interface), Error);
					}
					else
					{
						P->Socket.set_option(N::Asio::ip::multicast::leave_group(Address.to_v4(), Interface), Error);
					}
				}
				else
				{
					if (bJoin)
					{
						P->Socket.set_option(N::Asio::ip::multicast::join_group(Address.to_v6(), Index), Error);
					}
					else
					{
						P->Socket.set_option(N::Asio::ip::multicast::leave_group(Address.to_v6(), Index), Error);
					}
				}
				if (Error)
				{
					N::Fail(Op, EError::State, Error);
				}
				else
				{
					N::Complete(Op, true);
				}
			});
		return N::FAccess::Wrap(Op);
	}

} // namespace VeroScript::Network
