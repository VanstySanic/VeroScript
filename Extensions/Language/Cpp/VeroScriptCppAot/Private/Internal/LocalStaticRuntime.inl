// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			if (Flags.bNeedsLocalStatics)
			{
				Support << R"CPP(
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

class VsLocalStaticState
{
	struct Entry
	{
		bool Initializing = false;
		std::shared_ptr<void> Value;
	};
	std::unordered_map<std::type_index, Entry> Entries;
	std::vector<std::type_index> Completed;
	bool Destroying = false;
public:
	template<typename T, typename F> std::shared_ptr<T> Get(F&& Initialize)
	{
		const std::type_index Key(typeid(std::decay_t<F>));
		auto& Slot = Entries[Key];
		if (Slot.Value) return std::static_pointer_cast<T>(Slot.Value);
		if (Destroying) VsScriptFail("local static initialization during state destruction");
		if (Slot.Initializing) VsScriptFail("recursive local static initialization");
		Slot.Initializing = true;
		try
		{
			std::shared_ptr<T> Value(new T(Initialize()));
			Completed.push_back(Key);
			Slot.Value = Value;
			Slot.Initializing = false;
			return Value;
		}
		catch (...)
		{
			Slot.Initializing = false;
			throw;
		}
	}
	void Clear()
	{
		Destroying = true;
		while (!Completed.empty())
		{
			const auto Key = Completed.back();
			Completed.pop_back();
			Entries.at(Key).Value.reset();
		}
	}
	~VsLocalStaticState() { Clear(); }
};
inline VsLocalStaticState*& VsActiveLocalStaticState()
{
	static thread_local VsLocalStaticState* State = nullptr;
	return State;
}
class VsLocalStaticScope
{
	VsLocalStaticState State;
	VsLocalStaticState* Previous;
public:
	VsLocalStaticScope() : Previous(VsActiveLocalStaticState()) { VsActiveLocalStaticState() = &State; }
	~VsLocalStaticScope() { State.Clear(); VsActiveLocalStaticState() = Previous; }
	VsLocalStaticScope(const VsLocalStaticScope&) = delete;
	VsLocalStaticScope& operator=(const VsLocalStaticScope&) = delete;
};
inline VsLocalStaticState& VsDefaultLocalStaticState()
{
	static thread_local VsLocalStaticState State;
	return State;
}
template<typename T, typename F> std::shared_ptr<T> VsLocalStatic(F&& Initialize)
{
	if (auto* State = VsActiveLocalStaticState()) return State->Get<T>(std::forward<F>(Initialize));
	return VsDefaultLocalStaticState().Get<T>(std::forward<F>(Initialize));
}
)CPP";
			}
