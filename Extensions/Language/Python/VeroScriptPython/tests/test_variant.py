# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import unittest
import veroscript


class VariantTests(unittest.TestCase):
	def test_explicit_round_trip(self):
		with veroscript.Session() as session:
			value = session.variant("int", veroscript.Value(42, kind="int32"))
			self.assertEqual(value.kind, "variant")
			self.assertEqual(value.type_name, "variant")
			self.assertTrue(value.variant_has_value())
			self.assertEqual(value.variant_type_name(), "int")
			self.assertEqual(value.variant_get().value, 42)
			copy = value.clone()
		self.assertEqual(copy.variant_get().value, 42)

	def test_empty(self):
		value = veroscript.Value.empty_variant()
		self.assertFalse(value.variant_has_value())
		self.assertEqual(value.variant_type_name(), "")
		with self.assertRaises(veroscript.Error):
			value.variant_get()

	def test_canonical_box_copy(self):
		with veroscript.Session() as session:
			source = session.variant("int", veroscript.Value(42, kind="int32"))
			for name in ("variant", "std::variant", "std.variant", " variant "):
				with self.subTest(type_name=name):
					copy = session.variant(name, source)
					self.assertEqual(copy.variant_type_name(), "int")
					self.assertEqual(copy.variant_get().value, 42)
					empty = session.variant(name, veroscript.Value.empty_variant())
					self.assertFalse(empty.variant_has_value())

	def test_host_calls_with_variant_arguments(self):
		with veroscript.Session() as session:
			self.assertTrue(session.load_source(
				"int read(variant v){return cast<int>(v);} "
				"int select(variant v){return 42;} int select(int v){return 7;}").success)
			box = session.variant("int", veroscript.Value(42, kind="int32"))
			self.assertEqual(session.call("read", box), 42)
			self.assertEqual(session.call("select", box), 42)
			self.assertEqual(session.call("select", veroscript.Value(42, kind="int32")), 7)

	def test_callback_transfers_last_root(self):
		with veroscript.Session() as session:
			destroyed = []
			held = []

			def take():
				result = held[0].clone()
				held[0].set_variant_empty()
				return result

			session.register_function("take", take, return_type="variant", parameters=[])
			session.register_function("notify", lambda: destroyed.append(True), return_type="void", parameters=[])
			self.assertTrue(session.load_source(
				"class Item{public:~Item(){notify();}}; variant make(){return Item();} "
				"variant relay(){return take();} int ping(){return 1;}").success)
			held.append(session.call("make"))
			result = session.call("relay")
			self.assertTrue(result.variant_type_name())
			self.assertEqual(result.variant_get().kind, "object")
			self.assertEqual(destroyed, [])
			result.set_variant_empty()
			self.assertEqual(session.call("ping"), 1)
			self.assertEqual(destroyed, [True])

	def test_explicit_assignment(self):
		with veroscript.Session() as session:
			source = session.variant("int", veroscript.Value(7, kind="int32"))
			target = veroscript.Value.empty_variant()
			target.assign_variant(source)
			source.set_variant_empty()
			self.assertEqual(target.variant_get().value, 7)
			with self.assertRaises(TypeError):
				target.assign_variant(veroscript.Value(3))

	def test_retired_destructor_failure_is_reported(self):
		with veroscript.Session() as session:
			held = []
			session.register_function("drop", lambda: held[0].set_variant_empty(),
									  return_type="void", parameters=[])
			self.assertTrue(session.load_source(
				"class Item{public:~Item(){assert(false);}}; variant make(){return Item();} "
				"int action(){drop();return 5;}").success)
			held.append(session.call("make"))
			with self.assertRaises(veroscript.Error):
				session.call("action")

	def test_native_variant_signature(self):
		with veroscript.Session() as session:
			session.register_function("echo_variant", lambda value: value,
									  return_type="variant", parameters=["variant"])
			result = session.load_source("variant answer(){return echo_variant(42);}")
			self.assertTrue(result.success)
			box = session.call("answer")
			self.assertEqual(box.kind, "variant")
			self.assertEqual(box.variant_get().value, 42)

	def test_closed_module_box(self):
		with veroscript.Session() as session:
			self.assertTrue(session.load_source(
				"class Item{}; variant make(){return Item();}").success)
			box = session.call("make")
			self.assertTrue(box.variant_has_value())
		self.assertEqual(box.kind, "invalid")
		with self.assertRaises(veroscript.Error):
			box.variant_get()

	def test_explicit_payload(self):
		with veroscript.Session() as session:
			with self.assertRaises(TypeError):
				session.variant("int", 42)
			with self.assertRaises(veroscript.Error):
				session.variant("double", veroscript.Value(42, kind="int32"))

	def test_boxing_preserves_host_clone_exception(self):
		class Point:
			def __init__(self, x):
				self.x = x

		original = LookupError("variant host clone failed")
		failure = [False]

		def clone(point):
			if failure[0]:
				raise original
			return Point(point.x)

		with veroscript.Session(backend=veroscript.Backend.VM) as session:
			session.register_struct("Point", Point).constructor(parameters=("int",)).protocol(clone=clone)
			self.assertTrue(session.load_source(
				"Point make(){return Point(42);} int alive(){return 42;} void main(){}"
			).success)
			point = session.call("make")
			self.assertEqual(session.variant("Point", point).kind, "variant")
			failure[0] = True
			with self.assertRaises(veroscript.CallbackError) as raised:
				session.variant("Point", point)
			self.assertIs(raised.exception.args[0], original)
			failure[0] = False
			self.assertEqual(session.variant("Point", point).variant_get().kind, "host_object")
			self.assertEqual(session.call("alive"), 42)

	def test_nested_boxing_owns_its_host_clone_exception(self):
		class Point:
			def __init__(self, x):
				self.x = x

		for outer_failure in (False, True):
			with self.subTest(outer_failure=outer_failure):
				original = LookupError("nested variant host clone failed")
				earlier = ValueError("earlier print callback failed")
				failure = [False]
				held = []
				caught = []

				def clone(point):
					if failure[0]:
						raise original
					return Point(point.x)

				def print_callback(text):
					if outer_failure:
						raise earlier

				def nested():
					failure[0] = True
					try:
						with self.assertRaises(veroscript.CallbackError) as raised:
							session.variant("Point", held[0])
						caught.append(raised.exception.args[0])
					finally:
						failure[0] = False
					return 7

				with veroscript.Session(backend=veroscript.Backend.VM, print_callback=print_callback) as session:
					session.register_struct("Point", Point).constructor(parameters=("int",)).protocol(clone=clone)
					session.register_function("nested", nested, return_type="int")
					self.assertTrue(session.load_source(
						'Point make(){return Point(42);} int trigger(){print("first");return nested();} '
						'int alive(){return 42;} void main(){}'
					).success)
					held.append(session.call("make"))
					if outer_failure:
						with self.assertRaises(veroscript.CallbackError) as raised:
							session.call("trigger")
						self.assertIs(raised.exception.args[0], earlier)
					else:
						self.assertEqual(session.call("trigger"), 7)
					self.assertEqual(caught, [original])
					self.assertEqual(session.variant("Point", held[0]).kind, "variant")
					self.assertEqual(session.call("alive"), 42)

	def test_boxing_rejects_cross_owner_clone_closing_consumer(self):
		class Point:
			def __init__(self, x):
				self.x = x

		target = [None]
		rejected = []

		def clone(point):
			if target[0] is not None:
				with self.assertRaisesRegex(veroscript.Error, "active operation or callback"):
					target[0].close()
				rejected.append(True)
			return Point(point.x)

		with veroscript.Session(backend=veroscript.Backend.VM) as producer:
			producer.register_struct("Point", Point).constructor(parameters=("int",)).protocol(clone=clone)
			self.assertTrue(producer.load_source("Point make(){return Point(42);} void main(){}").success)
			point = producer.call("make")
			with veroscript.Session(backend=veroscript.Backend.VM) as consumer:
				consumer.register_struct("Point", Point).protocol(clone=clone)
				target[0] = consumer
				box = consumer.variant("Point", point)
				target[0] = None
				self.assertEqual(box.kind, "variant")
				self.assertEqual(rejected, [True])
				self.assertFalse(consumer.closed)
				self.assertEqual(consumer.variant("Point", point).kind, "variant")


if __name__ == "__main__":
	unittest.main()
