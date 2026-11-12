# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import threading
import unittest

import veroscript


class NumericTests(unittest.TestCase):
	def session(self):
		return veroscript.Session(backend=veroscript.Backend.VM)

	def test_capability_and_verified_kind(self):
		self.assertTrue(veroscript.CAPABILITIES & veroscript.RuntimeCapabilities.NUMERICS)
		self.assertEqual(veroscript.Value(1).numeric_kind, veroscript.NumericKind.NONE)
		with self.session() as session:
			value = session.numerics.bigint("18446744073709551616")
			self.assertEqual(value.kind, "host_object")
			self.assertEqual(value.numeric_kind, veroscript.NumericKind.BIGINT)
			self.assertEqual(session.numerics.to_text(value), "18446744073709551616")

	def test_rational_normalization(self):
		with self.session() as session:
			value = session.numerics.parse("-6/8", veroscript.NumericKind.RATIONAL)
			self.assertEqual(value.numeric_kind, veroscript.NumericKind.RATIONAL)
			self.assertEqual(session.numerics.to_text(value), "-3/4")

	def test_scalar_conversion_preserves_type_and_range(self):
		with self.session() as session:
			value = session.numerics.bigint("18446744073709551615")
			converted = session.numerics.convert(value, "uint64")
			self.assertEqual(converted.kind, "uint64")
			self.assertEqual(converted.value, 18446744073709551615)
			with self.assertRaises(veroscript.Error):
				session.numerics.convert(value, "int64")
			self.assertEqual(session.numerics.to_text(value), "18446744073709551615")
			for target in ("bool", "string", "variant", "int\0ignored"):
				with self.subTest(target=target), self.assertRaises(ValueError):
					session.numerics.convert(value, target)

	def test_float_conversion_precision(self):
		with self.session() as session:
			half = session.numerics.rational("1/2")
			self.assertEqual(session.numerics.convert(half, "float", exact_float=True).value, 0.5)
			third = session.numerics.rational("1/3")
			with self.assertRaises(veroscript.Error):
				session.numerics.convert(third, "double", exact_float=True)
			self.assertAlmostEqual(session.numerics.convert(third, "double").value, 1 / 3)

	def test_invalid_text_is_not_truncated(self):
		with self.session() as session:
			for text in ("not-a-number", "1\0ignored"):
				with self.subTest(text=text), self.assertRaises(veroscript.Error):
					session.numerics.bigint(text)
			with self.assertRaises(veroscript.Error):
				session.numerics.rational("1/0")
			with self.assertRaises(ValueError):
				session.numerics.parse("1", veroscript.NumericKind.NONE)
			with self.assertRaises(TypeError):
				session.numerics.to_text(42)
			with self.assertRaises(veroscript.Error):
				session.numerics.to_text(veroscript.Value(42))

	def test_limits_are_checked_and_preserved_by_clear(self):
		with self.session() as session:
			session.set_numeric_limits(max_value_bits=8)
			self.assertEqual(session.numerics.to_text(session.numerics.bigint("255")), "255")
			with self.assertRaises(veroscript.Error):
				session.numerics.bigint("256")
			with self.assertRaises(OverflowError):
				session.set_numeric_limits(max_value_bits=-1)
			with self.assertRaises(OverflowError):
				session.set_numeric_limits(max_input_bytes=1 << 64)
			session.clear()
			with self.assertRaises(veroscript.Error):
				session.numerics.bigint("256")
			session.set_numeric_limits()
			self.assertEqual(session.numerics.to_text(session.numerics.bigint("256")), "256")

	def test_independent_value_survives_producer_close(self):
		with self.session() as producer:
			value = producer.numerics.bigint("18446744073709551616")
		with self.session() as consumer:
			self.assertEqual(consumer.numerics.to_text(value.clone()), "18446744073709551616")

	def test_callback_exchange_uses_borrowed_context(self):
		seen = []

		def echo(value):
			self.assertIsInstance(value, veroscript.Value)
			numerics = veroscript.current_numerics()
			seen.append(numerics.to_text(value))
			return numerics.parse(seen[-1], veroscript.NumericKind(value.numeric_kind))

		with self.session() as session:
			session.register_function("echo_bigint", echo, return_type="std.bigint", parameters=["std.bigint"])
			session.register_function("echo_rational", echo, return_type="std.rational", parameters=["std.rational"])
			result = session.load_source(
				"std::bigint relay_bigint(std::bigint value){return echo_bigint(value);} "
				"std::rational relay_rational(std::rational value){return echo_rational(value);}")
			self.assertTrue(result.success, result)
			for name, value, text in (
				("relay_bigint", session.numerics.bigint("18446744073709551616"), "18446744073709551616"),
				("relay_rational", session.numerics.rational("6/18"), "1/3"),
			):
				with self.subTest(name=name):
					returned = session.call(name, value)
					self.assertIsInstance(returned, veroscript.Value)
					self.assertEqual(session.numerics.to_text(returned), text)
			self.assertEqual(seen, ["18446744073709551616", "1/3"])

	def test_numeric_argument_to_python_host_member(self):
		seen = []

		class NumericReader:
			@staticmethod
			def echo(value):
				seen.append(veroscript.current_numerics().to_text(value))
				return value

		with self.session() as session:
			session.register_class("NumericReader", NumericReader).static_method(
				"Echo", "echo", return_type="std.bigint", parameters=["std.bigint"])
			self.assertTrue(session.load_source(
				"std::bigint relay(std::bigint value){return NumericReader.Echo(value);}").success)
			result = session.call("relay", session.numerics.bigint("42"))
			self.assertEqual(session.numerics.to_text(result), "42")
			self.assertEqual(seen, ["42"])

	def test_python_payload_from_closed_session_keeps_subtype(self):
		class Animal:
			pass

		class Dog(Animal):
			pass

		with self.session() as producer:
			producer.register_class("NumericAnimal", Animal)
			producer.register_class("NumericDog", Dog).constructor()
			self.assertTrue(producer.load_source("NumericDog make(){return NumericDog();}").success)
			value = producer.call("make")
		with self.session() as consumer:
			# Only the base is registered here. Payload ownership, rather than
			# consumer type names/IDs, must identify an escaped Python object.
			consumer.register_class("NumericAnimal", Animal)
			seen = []

			def accept(item):
				seen.append(item)
				return 7

			consumer.register_function("accept", accept, return_type="int", parameters=["NumericAnimal"])
			self.assertTrue(consumer.load_source(
				"int inspect(NumericAnimal value){return accept(value);}").success)
			self.assertEqual(consumer.call("inspect", value), 7)
			self.assertIsInstance(seen[0], Dog)

	def test_native_signatures_accept_core_numeric_containers_and_enum(self):
		class NumericRelay:
			@staticmethod
			def echo(value):
				return value

		nested = "array<map<string,std.bigint>>"
		rounding = "enum<std.numeric.rounding_mode>"
		with self.session() as session:
			session.register_function("echo_nested", lambda value: value,
									  return_type=nested, parameters=[nested])
			session.register_function("echo_rounding", lambda value: value,
									  return_type=rounding, parameters=[rounding])
			session.register_function("echo_set", lambda value: value,
									  return_type="set<std.bigint>", parameters=["set<std.bigint>"])
			relay = session.register_class("NumericRelay", NumericRelay)
			relay.static_method("Nested", "echo", return_type=nested, parameters=[nested])
			relay.static_method("Rounding", "echo", return_type=rounding, parameters=[rounding])
			loaded = session.load_source(
				"array<map<string,std::bigint>> nested(){"
				'map<string,std::bigint> item={"answer":std::bigint(42)};'
				"array<map<string,std::bigint>> values={item};"
				"return NumericRelay.Nested(echo_nested(values));} "
				"std::numeric::rounding_mode rounding(){"
				"return NumericRelay.Rounding(echo_rounding(std::numeric::rounding_mode::half_even));} "
				"set<std::bigint> numbers(){set<std::bigint> values={};"
				"values.add(std::bigint(42));return echo_set(values);}"
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			number = session.call("nested")[0]["answer"]
			self.assertEqual(session.numerics.to_text(number), "42")
			self.assertEqual(session.call("rounding"), 4)
			numbers = list(session.call("numbers"))
			self.assertEqual(len(numbers), 1)
			self.assertEqual(session.numerics.to_text(numbers[0]), "42")

	def test_native_signatures_still_reject_unknown_host_types(self):
		class NumericRelay:
			@staticmethod
			def echo(value):
				return value

		with self.session() as session:
			relay = session.register_class("NumericRelay", NumericRelay)
			for type_name in ("std.unknown_numeric", "array<std.unknown_numeric>"):
				with self.subTest(type_name=type_name):
					with self.assertRaises(veroscript.Error):
						session.register_function("unknown", lambda value: value,
												  return_type=type_name, parameters=[type_name])
					with self.assertRaises(veroscript.Error):
						relay.static_method("Unknown", "echo", return_type=type_name,
											parameters=[type_name])

	def test_callback_scalar_conversion_and_context_expiry(self):
		saved = []

		def number():
			numerics = veroscript.current_numerics()
			saved.append(numerics)
			value = numerics.bigint("18446744073709551615")
			return numerics.convert(value, "uint64")

		with self.session() as session:
			session.register_function("number", number, return_type="uint64", parameters=[])
			self.assertTrue(session.load_source("uint64 answer(){return number();}").success)
			self.assertEqual(session.call("answer"), 18446744073709551615)
		with self.assertRaises(veroscript.Error):
			saved[0].bigint("1")

	def test_session_requires_owner_thread_and_open_handle(self):
		with self.session() as session:
			numerics = session.numerics
			errors = []

			def worker():
				try:
					numerics.bigint("1")
				except Exception as error:
					errors.append(error)

			thread = threading.Thread(target=worker)
			thread.start()
			thread.join()
			self.assertEqual(len(errors), 1)
			self.assertIsInstance(errors[0], veroscript.ThreadError)
		with self.assertRaises(veroscript.Error):
			numerics.bigint("1")
		with self.assertRaises(veroscript.Error):
			veroscript.current_numerics().bigint("1")

	def test_argument_conversion_revalidates_session(self):
		class ClosingIndex:
			def __init__(self, session):
				self.session = session

			def __index__(self):
				self.session.close()
				return 1

		class ClosingBool:
			def __init__(self, session):
				self.session = session

			def __bool__(self):
				self.session.close()
				return True

		with self.session() as session:
			with self.assertRaises(TypeError):
				session.set_numeric_limits(max_value_bits=ClosingIndex(session))
			self.assertFalse(session.closed)
			self.assertEqual(session.numerics.to_text(session.numerics.bigint("1")), "1")
		with self.session() as session:
			with self.assertRaises(TypeError):
				session.numerics.parse("1", ClosingIndex(session))
			self.assertFalse(session.closed)
			self.assertEqual(session.numerics.to_text(session.numerics.bigint("1")), "1")
		with self.session() as session:
			value = session.numerics.bigint("1")
			with self.assertRaises(veroscript.Error):
				session.numerics.convert(value, "double", exact_float=ClosingBool(session))


if __name__ == "__main__":
	unittest.main()
