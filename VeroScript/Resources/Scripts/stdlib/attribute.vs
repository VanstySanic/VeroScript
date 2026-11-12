// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "callable.vs";

// Publish one complete configuration before releasing the previous payload.
class __std_attribute_storage<T>
{
public:
	// 0: empty, 1: value, 2: getter, 3: optional getter.
	int kind;
	optional<T> stored_value;
	fn() -> T getter;
	fn() -> optional<T> optional_reader;
};

@ __std_attribute_library export struct attribute<T>
{
public:
	// Comparing configurations never evaluates a getter.
	bool has_same_configuration(const ref attribute<T> other) const
	{
		return configuration == other.configuration;
	}
	attribute()
	{
	}
	attribute(T value)
	{
		set(value);
	}

	void set(T value)
	{
		__std_attribute_storage<T> next = __std_attribute_storage<T>();
		next.stored_value = optional<T>(value);
		next.kind = 1;
		configuration = next;
	}

	void bind(fn() -> T getter)
	{
		__std_attribute_storage<T> next = __std_attribute_storage<T>();
		next.getter = getter;
		next.kind = 2;
		configuration = next;
	}

	void bind_optional(fn() -> optional<T> getter)
	{
		__std_attribute_storage<T> next = __std_attribute_storage<T>();
		next.optional_reader = getter;
		next.kind = 3;
		configuration = next;
	}

	optional<T> try_get() const
	{
		if (configuration == nullptr)
		{
			return optional<T>();
		}
		if (configuration.kind == 1)
		{
			return configuration.stored_value;
		}
		if (configuration.kind == 2)
		{
			fn() -> T read = configuration.getter;
			return optional<T>(read());
		}
		if (configuration.kind == 3)
		{
			fn() -> optional<T> read = configuration.optional_reader;
			return read();
		}
		return optional<T>();
	}

	// Leave output unchanged when the optional reader declines a value.
	bool try_get(ref T output) const
	{
		if (configuration == nullptr)
		{
			return false;
		}
		if (configuration.kind == 1)
		{
			output = configuration.stored_value.get();
			return true;
		}
		if (configuration.kind == 2)
		{
			fn() -> T read = configuration.getter;
			output = read();
			return true;
		}
		if (configuration.kind == 3)
		{
			fn() -> optional<T> read = configuration.optional_reader;
			optional<T> result = read();
			if (result.has_value())
			{
				output = result.get();
				return true;
			}
		}
		return false;
	}

	T get() const
	{
		optional<T> result = try_get();
		assert(result.has_value(), "std::attribute has no value");
		return result.get();
	}

	T get_or(T fallback) const
	{
		optional<T> result = try_get();
		return result.value_or(fallback);
	}

	T operator T() const
	{
		return get();
	}
	T operator()() const
	{
		return get();
	}

	bool is_set() const
	{
		return configuration != nullptr && configuration.kind != 0;
	}

	bool is_bound() const
	{
		return configuration != nullptr && configuration.kind > 1;
	}

	void reset()
	{
		configuration = nullptr;
	}

private:
	__std_attribute_storage<T> configuration;
};

export attribute<T> __std_make_weak_attribute<T, U>(weak<U> target, fn(U) -> T getter)
{
	weak<U> observer = target;
	fn(U) -> T read = getter;
	attribute<T> result = attribute<T>();
	result.bind_optional([observer, read]() -> optional<T>
	{
		U locked = observer.lock(); if (locked == nullptr)
		{
			return optional<T>();
		}
		return optional<T>(read(locked));
	});
	return result;
}

@ __std_method_binding_factory export attribute<T> __std_make_attribute_by_name<T, U>(U target, string method_name);
@ __std_method_binding_factory export attribute<T> __std_make_weak_attribute_by_name<T, U>(weak<U> target, string method_name);
