// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAudio.h"
#include <deque>
#include <map>

namespace VeroScript::Sdl3::AudioDetail
{
	using FDevices = std::map<std::pair<Audio::EDirection, std::uint64_t>, Audio::FDeviceInfo>;
	// Snapshot reconciliation is separate from native discovery so transition/overflow semantics are testable.
	class FDeviceChanges
	{
	public:
		void Initialize(FDevices Devices)
		{
			Known = std::move(Devices);
		}
		void Observe(FDevices Current)
		{
			try
			{
				for (const auto& Entry : Known)
				{
					if (!Current.count(Entry.first))
					{
						Emit({Audio::EEventType::DeviceRemoved, Entry.second});
					}
				}
				for (const auto& Entry : Current)
				{
					const auto Old = Known.find(Entry.first);
					if (Old == Known.end())
					{
						Emit({Audio::EEventType::DeviceAdded, Entry.second});
					}
					else if (Old->second.Name != Entry.second.Name ||
							 Old->second.PreferredFormat.SampleRate != Entry.second.PreferredFormat.SampleRate ||
							 Old->second.PreferredFormat.Channels != Entry.second.PreferredFormat.Channels)
					{
						Emit({Audio::EEventType::DeviceChanged, Entry.second});
					}
				}
			}
			catch (...)
			{
				bResync = true;
			}
			Known = std::move(Current);
		}
		bool Pop(Audio::FEvent& Result)
		{
			Audio::FEvent Event;
			if (bResync)
			{
				Events.clear();
				bResync = false;
				Event.Type = Audio::EEventType::TopologyReset;
			}
			else if (!Events.empty())
			{
				Event = std::move(Events.front());
				Events.pop_front();
			}
			const bool Available = Event.Type != Audio::EEventType::None;
			Result = std::move(Event);
			return Available;
		}
		const FDevices& GetDevices() const noexcept
		{
			return Known;
		}

	private:
		void Emit(Audio::FEvent Event)
		{
			if (Events.size() >= 4096)
			{
				bResync = true;
			}
			if (!bResync)
			{
				Events.push_back(std::move(Event));
			}
		}
		FDevices Known;
		std::deque<Audio::FEvent> Events;
		bool bResync = false;
	};
}
