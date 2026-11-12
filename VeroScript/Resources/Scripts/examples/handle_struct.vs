// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// handle_struct.vs — shared<T> and handle<T> share the same struct wrapper
// Run from this directory: veroscript handle_struct.vs
//
// `I = h.value` (struct → interface) still boxes a value copy — shared mutability
// through an interface needs `class : public I`, not handle alone. Share via `h.value` / handle copy.

struct Point
{
	int x = 0;
	int y = 0;
};

void bump_x(handle<Point> h)
{
	h.value.x = h.value.x + 1;
}

void main()
{
	shared<Point> a = shared<Point>(Point());
	a.value.x = 10;
	a.value.y = 20;

	handle<Point> empty = handle<Point>();
	empty = nullptr;
	print("null handle: {}", empty == nullptr);

	handle<Point> b = a;
	print("shared before bump: a=({}, {}) b=({}, {})", a.value.x, a.value.y, b.value.x, b.value.y);

	bump_x(b);
	print("after bump via b: a.x={} b.x={}", a.value.x, b.value.x);

	// handle(T) copies the struct once into the shared object.
	Point local = Point();
	local.x = 99;
	auto c = shared(local);
	local.x = 0;
	print("shared(local) then mutate local: c.x={} local.x={}", c.value.x, local.x);

	// weak observes the shared wrapper. lock() returns the same class identity.
	weak<shared<Point>> observer = weak<handle<Point>>(a);
	handle<Point> locked = observer.lock();
	if (locked != nullptr)
	{
		print("locked same object: {}", locked == a);
		locked.value.y += 1;
	}

	a = nullptr;
	b = nullptr;
	print("alive while locked: {}", !observer.expired());
	locked = nullptr;
	print("expired after last owner: {}", observer.expired());
}
