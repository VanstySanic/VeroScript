// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

if (Flags.bNeedsAttribute)
{
	Support << R"VSCPP(
template <typename T>
class vs_Attribute {
	struct State {
		std::optional<T> fixed;
		std::shared_ptr<std::function<T()>> getter;
		std::shared_ptr<std::function<vs_Optional<T>()>> reader;
	};
	std::shared_ptr<State> state;
	void publish(std::shared_ptr<State> next) {
		auto old = std::move(state);
		state = std::move(next);
	}
public:
	vs_Attribute() = default;
	vs_Attribute(T value) { set(std::move(value)); }
	vs_Attribute(const vs_Attribute&) = default;
	vs_Attribute(vs_Attribute&&) = default;
	vs_Attribute& operator=(const vs_Attribute& other) {
		if (this != &other) publish(other.state);
		return *this;
	}
	vs_Attribute& operator=(vs_Attribute&& other) {
		if (this != &other) publish(std::move(other.state));
		return *this;
	}
	void set(T value) {
		auto next = std::make_shared<State>(); next->fixed.emplace(std::move(value));
		publish(std::move(next));
	}
	void bind(std::function<T()> getter) {
		auto next = std::make_shared<State>();
		next->getter = std::make_shared<std::function<T()>>(std::move(getter));
		publish(std::move(next));
	}
	void bind_optional(std::function<vs_Optional<T>()> getter) {
		auto next = std::make_shared<State>();
		next->reader = std::make_shared<std::function<vs_Optional<T>()>>(std::move(getter));
		publish(std::move(next));
	}
	vs_Optional<T> try_get() const {
		auto snapshot = state;
		if (!snapshot) return {};
		if (snapshot->fixed) return vs_Optional<T>(*snapshot->fixed);
		auto getter = snapshot->getter; auto reader = snapshot->reader;
		snapshot.reset();
		if (getter) { if (!*getter) VsScriptFail("unbound attribute getter"); return vs_Optional<T>((*getter)()); }
		if (reader) { if (!*reader) VsScriptFail("unbound attribute getter"); return (*reader)(); }
		return {};
	}
	bool try_get(T& output) const {
		auto result = try_get();
		if (!result.has_value()) return false;
		output = result.get();
		return true;
	}
	bool has_same_configuration(const vs_Attribute& other) const { return state == other.state; }
	T get() const {
		auto result = try_get();
		if (!result.has_value()) VsScriptFail("std::attribute has no value");
		return result.get();
	}
	T get_or(T fallback) const { return try_get().value_or(std::move(fallback)); }
	operator T() const { return get(); }
	T operator()() const { return get(); }
	bool is_set() const { return static_cast<bool>(state); }
	bool is_bound() const { return state && (state->getter || state->reader); }
	void reset() { publish(nullptr); }
};
template <typename T, typename F>
static vs_Attribute<T> VsAttributeFromGetter(F getter) {
	vs_Attribute<T> result; result.bind(std::move(getter)); return result;
}
template <typename T, typename F>
static vs_Attribute<T> VsAttributeFromOptionalGetter(F getter) {
	vs_Attribute<T> result; result.bind_optional(std::move(getter)); return result;
}
template <typename T, typename U, typename F>
static vs_Attribute<T> VsWeakAttribute(U observer, F getter) {
	vs_Attribute<T> result;
	result.bind_optional([observer = std::move(observer), getter = std::move(getter)]() mutable -> vs_Optional<T> {
		auto locked = observer.lock();
		if (!locked) return {};
		return vs_Optional<T>(getter(locked));
	});
	return result;
}
)VSCPP";
}
