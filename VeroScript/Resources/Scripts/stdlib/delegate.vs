// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "callable.vs";

// delegate.vs - typed delegate (unicast) / multicast_delegate (multicast)
// Same simple name overloaded by type-parameter arity (0 / 1 / 2 / 3).
// broadcast snapshots handlers first. Field function values must be copied
// to a local before CallIndirect.

export class delegate<R>
{
public:
	void bind(fn() -> R h)
	{
		handler = h;
		bound = true;
	}

	void unbind()
	{
		delegate<R> empty = delegate<R>();
		handler = empty.handler;
		bound = false;
	}

	bool is_bound()
	{
		return bound;
	}

	R execute()
	{
		assert(bound);
		fn() -> R h = handler;
		return h();
	}

	bool execute_if_bound()
	{
		if (!bound)
		{
			return false;
		}
		fn() -> R h = handler;
		h();
		return true;
	}

private:
	fn() -> R handler;
	bool bound;
};

export class delegate<R, A>
{
public:
	void bind(fn(A) -> R h)
	{
		handler = h;
		bound = true;
	}

	void unbind()
	{
		delegate<R, A> empty = delegate<R, A>();
		handler = empty.handler;
		bound = false;
	}

	bool is_bound()
	{
		return bound;
	}

	R execute(A a)
	{
		assert(bound);
		fn(A) -> R h = handler;
		return h(a);
	}

	bool execute_if_bound(A a)
	{
		if (!bound)
		{
			return false;
		}
		fn(A) -> R h = handler;
		h(a);
		return true;
	}

private:
	fn(A) -> R handler;
	bool bound;
};

export class delegate<R, A, B>
{
public:
	void bind(fn(A, B) -> R h)
	{
		handler = h;
		bound = true;
	}

	void unbind()
	{
		delegate<R, A, B> empty = delegate<R, A, B>();
		handler = empty.handler;
		bound = false;
	}

	bool is_bound()
	{
		return bound;
	}

	R execute(A a, B b)
	{
		assert(bound);
		fn(A, B) -> R h = handler;
		return h(a, b);
	}

	bool execute_if_bound(A a, B b)
	{
		if (!bound)
		{
			return false;
		}
		fn(A, B) -> R h = handler;
		h(a, b);
		return true;
	}

private:
	fn(A, B) -> R handler;
	bool bound;
};

export class delegate<R, A, B, C>
{
public:
	void bind(fn(A, B, C) -> R h)
	{
		handler = h;
		bound = true;
	}

	void unbind()
	{
		delegate<R, A, B, C> empty = delegate<R, A, B, C>();
		handler = empty.handler;
		bound = false;
	}

	bool is_bound()
	{
		return bound;
	}

	R execute(A a, B b, C c)
	{
		assert(bound);
		fn(A, B, C) -> R h = handler;
		return h(a, b, c);
	}

	bool execute_if_bound(A a, B b, C c)
	{
		if (!bound)
		{
			return false;
		}
		fn(A, B, C) -> R h = handler;
		h(a, b, c);
		return true;
	}

private:
	fn(A, B, C) -> R handler;
	bool bound;
};

export class multicast_delegate
{
public:
	void add(fn() -> void h)
	{
		handlers.push(h);
	}

	void remove(fn() -> void h)
	{
		handlers.remove(h);
	}

	void broadcast()
	{
		array < fn() -> void > snapshot = array < fn() -> void > ();
		snapshot.reserve(len(handlers));
		for (fn() -> void h : handlers)
		{
			snapshot.push(h);
		}
		for (fn() -> void h : snapshot)
		{
			h();
		}
	}

	int64 count()
	{
		return len(handlers);
	}

	void clear()
	{
		handlers.clear();
	}

	array < fn() -> void > handlers;
};

export class multicast_delegate<A>
{
public:
	void add(fn(A) -> void h)
	{
		handlers.push(h);
	}

	void remove(fn(A) -> void h)
	{
		handlers.remove(h);
	}

	void broadcast(A value)
	{
		array < fn(A) -> void > snapshot = array < fn(A) -> void > ();
		snapshot.reserve(len(handlers));
		for (fn(A) -> void h : handlers)
		{
			snapshot.push(h);
		}
		for (fn(A) -> void h : snapshot)
		{
			h(value);
		}
	}

	int64 count()
	{
		return len(handlers);
	}

	void clear()
	{
		handlers.clear();
	}

	array < fn(A) -> void > handlers;
};

export class multicast_delegate<A, B>
{
public:
	void add(fn(A, B) -> void h)
	{
		handlers.push(h);
	}

	void remove(fn(A, B) -> void h)
	{
		handlers.remove(h);
	}

	void broadcast(A a, B b)
	{
		array < fn(A, B) -> void > snapshot = array < fn(A, B) -> void > ();
		snapshot.reserve(len(handlers));
		for (fn(A, B) -> void h : handlers)
		{
			snapshot.push(h);
		}
		for (fn(A, B) -> void h : snapshot)
		{
			h(a, b);
		}
	}

	int64 count()
	{
		return len(handlers);
	}

	void clear()
	{
		handlers.clear();
	}

	array < fn(A, B) -> void > handlers;
};

export class multicast_delegate<A, B, C>
{
public:
	void add(fn(A, B, C) -> void h)
	{
		handlers.push(h);
	}

	void remove(fn(A, B, C) -> void h)
	{
		handlers.remove(h);
	}

	void broadcast(A a, B b, C c)
	{
		array < fn(A, B, C) -> void > snapshot = array < fn(A, B, C) -> void > ();
		snapshot.reserve(len(handlers));
		for (fn(A, B, C) -> void h : handlers)
		{
			snapshot.push(h);
		}
		for (fn(A, B, C) -> void h : snapshot)
		{
			h(a, b, c);
		}
	}

	int64 count()
	{
		return len(handlers);
	}

	void clear()
	{
		handlers.clear();
	}

	array < fn(A, B, C) -> void > handlers;
};
