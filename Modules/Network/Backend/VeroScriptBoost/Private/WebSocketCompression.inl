// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Included in FBoostWebSocketConnection. Configure and observe the actual handshake on the I/O executor.

Ws::permessage_deflate CompressionParameters(bool Enabled) const
{
	Ws::permessage_deflate Value;
	Value.client_enable = Value.server_enable = Enabled;
	Value.client_max_window_bits = Options.Compression.ClientMaxWindowBits;
	Value.server_max_window_bits = Options.Compression.ServerMaxWindowBits;
	Value.client_no_context_takeover = Options.Compression.bClientNoContextTakeover;
	Value.server_no_context_takeover = Options.Compression.bServerNoContextTakeover;
	Value.compLevel = Options.Compression.Level;
	Value.memLevel = Options.Compression.MemoryLevel;
	return Value;
}
template <class Header>
void CaptureCompression(const Header& Head)
{
	FCompressionInfo Value;
	const auto Field = Beast::http::field::sec_websocket_extensions;
	if (Head.count(Field) == 1)
	{
		const auto Text = Head[Field];
		Detail::ParseCompression(std::string_view(Text.data(), Text.size()), Value, false);
	}
	if (!Value.bEnabled)
	{
		CompressionMemory.reset();
	}
	std::lock_guard<std::mutex> Lock(InfoMutex);
	SelectedCompression = Value;
}
void ServerDecorator(std::string Selected)
{
	Stream.set_option(Ws::stream_base::decorator(
		[Weak = weak_from_this(), Selected = std::move(Selected)](Ws::response_type& Response)
		{
			if (!Selected.empty())
			{
				Response.set(Beast::http::field::sec_websocket_protocol, Selected);
			}
			if (auto Self = Weak.lock())
			{
				if (Self->Options.bCompression)
				{
					const auto Field = Beast::http::field::sec_websocket_extensions;
					Response.erase(Field);
					if (!Self->ServerCompressionHeader.empty())
					{
						Response.set(Field, Self->ServerCompressionHeader);
					}
				}
				Self->CaptureCompression(Response);
			}
		}));
}
template <class Request>
void SelectServerCompression(const Request& Head)
{
	if (!Options.bCompression)
	{
		return;
	}
	const auto Field = Beast::http::field::sec_websocket_extensions;
	ServerCompressionHeader.clear();
	for (const auto& Item : Head)
	{
		if (Item.name() != Field)
		{
			continue;
		}
		const auto Text = Item.value();
		ServerCompressionHeader =
			Detail::SelectCompressionOffer(std::string_view(Text.data(), Text.size()), Options.Compression);
		if (!ServerCompressionHeader.empty())
		{
			break;
		}
	}
}
