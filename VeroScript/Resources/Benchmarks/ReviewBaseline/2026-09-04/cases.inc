const FBenchCase Cases[] = {
			{
				"main_int_loop",
				"void main() {\n"
					"\tint s = 0;\n"
					"\tint i = 0;\n"
					"\twhile (i < 5000000) {\n"
					"\t\ts = s + 1;\n"
					"\t\ti = i + 1;\n"
					"\t}\n"
					"\tprint(s);\n"
					"}\n",
				"5000000\n",
				0.0,
				1.0,
			},
			{
				"leaf_int_loop",
				"int work() {\n"
					"\tint s = 0;\n"
					"\tint i = 0;\n"
					"\twhile (i < 5000000) {\n"
					"\t\ts = s + 1;\n"
					"\t\ti = i + 1;\n"
					"\t}\n"
					"\treturn s;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"5000000\n",
				0.0,
				1.0,
			},
			{
				"leaf_int_call",
				"int add(int a, int b) { return a + b; }\n"
					"int work() {\n"
					"\tint s = 0;\n"
					"\tint i = 0;\n"
					"\twhile (i < 2000000) {\n"
					"\t\ts = add(s, 1);\n"
					"\t\ti = i + 1;\n"
					"\t}\n"
					"\treturn s;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"2000000\n",
				0.0,
				1.0,
			},
			{
				"direct_native_free_call",
				"int transform_loop(int value) { for (int i = 0; i < 4; i++) { "
					"value = (value * 3 + 1) % 10007; } return value; }\n"
					"int work() { int total = 0; for (int i = 0; i < 100000; i++) { "
					"total = (total + transform_loop(i & 255)) % 1000003; } return total; }\n"
					"void main() { print(work()); }\n",
				"481283\n",
				0.0,
				1.0,
			},
			{
				"direct_native_f64_call",
				"double transform_loop_f64(double value) { "
					"for (double i = 0.0; i < 4.0; i += 1.0) { "
					"value = value * 1.25 + 0.5; } return value; }\n"
					"double work() { double total = 0.0; "
					"for (double i = 0.0; i < 10000.0; i += 1.0) { "
					"total += transform_loop_f64(i); } return total; }\n"
					"void main() { print(to_int(work())); }\n",
				"122086933\n",
				0.0,
				1.0,
			},
			{
				"leaf_int64_loop",
				"int64 work() {\n"
					"\tint64 total = 0i64;\n"
					"\tint64 i = 0i64;\n"
					"\twhile (i < 5000000i64) { total = total + 1i64; i = i + 1i64; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(to_int(work())); }\n",
				"5000000\n",
				0.0,
				1.0,
			},
			{
				"leaf_float_loop",
				"float work() {\n"
					"\tfloat total = 0.f;\n"
					"\tfloat i = 0.f;\n"
					"\twhile (i < 1000000.f) { total = total + 1.f; i = i + 1.f; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(to_int(work())); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"leaf_method",
				// Hot loop uses locals inside the method (NativeCfg ≈ leaf_int_loop).
				"class Counter {\n"
					"public:\n"
					"\tint n = 0;\n"
					"\tint run(int limit) {\n"
					"\t\tint s = n;\n"
					"\t\tint i = 0;\n"
					"\t\twhile (i < limit) {\n"
					"\t\t\ts = s + 1;\n"
					"\t\t\ti = i + 1;\n"
					"\t\t}\n"
					"\t\tn = s;\n"
					"\t\treturn s;\n"
					"\t}\n"
					"}\n"
					"int work() {\n"
					"\tCounter c = Counter();\n"
					"\treturn c.run(5000000);\n"
					"}\n"
					"void main() { print(work()); }\n",
				"5000000\n",
				0.0,
				1.0,
			},
			{
				"leaf_this_call",
				// Nested this.bump()/add still NativeCfg; measures method-call overhead on JIT.
				"class Acc {\n"
					"public:\n"
					"\tint n = 0;\n"
					"\tvoid add(int x) { n = n + x; }\n"
					"\tvoid bump() { this.add(1); }\n"
					"\tint get() { return n; }\n"
					"\tint run(int limit) {\n"
					"\t\tint i = 0;\n"
					"\t\twhile (i < limit) {\n"
					"\t\t\tthis.bump();\n"
					"\t\t\ti = i + 1;\n"
					"\t\t}\n"
					"\t\treturn this.get();\n"
					"\t}\n"
					"}\n"
					"int work() {\n"
					"\tAcc a = Acc();\n"
					"\treturn a.run(2000000);\n"
					"}\n"
					"void main() { print(work()); }\n",
				"2000000\n",
				0.0,
				1.0,
			},
			{
				"ordinary_instance_method",
				"class Increment { public: int apply(int value) { return value + 1; } };\n"
					"int work(int limit) { Increment operation = Increment(); int total = 0; "
					"for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
					"return total; }\n"
					"void main() { print(work(1000000)); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"nested_local_this_method",
				"class NestedCounter { public: int value = 0; "
					"void add(int delta) { value += delta; } "
					"void bump() { this.add(1); } }\n"
					"int work(int limit) { NestedCounter value = NestedCounter(); "
					"for (int i = 0; i < limit; i++) { value.bump(); } "
					"return value.value; }\n"
					"void main() { print(work(2000000)); }\n",
				"2000000\n",
				0.0,
				1.0,
			},
			{
				"ordinary_instance_method_uint",
				"class UIncrement { public: uint32 apply(uint32 value) { return value + 1u; } };\n"
					"uint32 work(int limit) { UIncrement operation = UIncrement(); uint32 total = 0u; "
					"for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
					"return total; }\n"
					"void main() { print(to_int(work(1000000))); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"ordinary_instance_method_double",
				"class DIncrement { public: double apply(double value) { return value + 1.0; } };\n"
					"double work(int limit) { DIncrement operation = DIncrement(); double total = 0.0; "
					"for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
					"return total; }\n"
					"void main() { print(to_int(work(1000000))); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"ordinary_instance_method_int64",
				"class I64Increment { public: int64 apply(int64 value) { return value + 1i64; } };\n"
					"int64 work(int limit) { I64Increment operation = I64Increment(); int64 total = 0i64; "
					"for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
					"return total; }\n"
					"void main() { print(to_int(work(1000000))); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"ordinary_instance_method_uint64",
				"class U64Increment { public: uint64 apply(uint64 value) { return value + 1u64; } };\n"
					"uint64 work(int limit) { U64Increment operation = U64Increment(); uint64 total = 0u64; "
					"for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
					"return total; }\n"
					"void main() { print(to_int(work(1000000))); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"ordinary_instance_method_float",
				"class FIncrement { public: float apply(float value) { return value + 1.f; } };\n"
					"float work(int limit) { FIncrement operation = FIncrement(); float total = 0.f; "
					"for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
					"return total; }\n"
					"void main() { print(to_int(work(1000000))); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"short_lived_object",
				"class Pair { public: int first = 1; int second = 2; "
					"int sum() { return first + second; } };\n"
					"int work(int limit) { int total = 0; for (int i = 0; i < limit; i++) { "
					"Pair value = Pair(); total += value.sum(); } return total; }\n"
					"void main() { print(work(100000)); }\n",
				"300000\n",
				0.0,
				1.0,
			},
			{
				"short_lived_object_double",
				"class DPair { public: double first = 1.0; double second = 2.0; "
					"double sum() { return first + second; } };\n"
					"double work(int limit) { double total = 0.0; for (int i = 0; i < limit; i++) { "
					"DPair value = DPair(); total += value.sum(); } return total; }\n"
					"void main() { print(to_int(work(100000))); }\n",
				"300000\n",
				0.0,
				1.0,
			},
			{
				"leaf_this_call_uint",
				"class UAcc {\n"
					"public:\n"
					"\tuint32 n = 0u;\n"
					"\tvoid add(uint32 x) { n = n + x; }\n"
					"\tvoid bump() { this.add(1u); }\n"
					"\tuint32 get() { return n; }\n"
					"\tuint32 run(uint32 limit) {\n"
					"\t\tuint32 i = 0u;\n"
					"\t\twhile (i < limit) {\n"
					"\t\t\tthis.bump();\n"
					"\t\t\ti = i + 1u;\n"
					"\t\t}\n"
					"\t\treturn this.get();\n"
					"\t}\n"
					"}\n"
					"uint32 work() {\n"
					"\tUAcc a = UAcc();\n"
					"\treturn a.run(2000000u);\n"
					"}\n"
					"void main() { print(work()); }\n",
				"2000000\n",
				0.0,
				1.0,
			},
			{
				"leaf_this_call_double",
				"class DAcc {\n"
					"public:\n"
					"\tdouble n = 0.0;\n"
					"\tvoid add(double x) { n = n + x; }\n"
					"\tvoid bump() { this.add(1.0); }\n"
					"\tdouble get() { return n; }\n"
					"\tdouble run(double limit) {\n"
					"\t\tdouble i = 0.0;\n"
					"\t\twhile (i < limit) {\n"
					"\t\t\tthis.bump();\n"
					"\t\t\ti = i + 1.0;\n"
					"\t\t}\n"
					"\t\treturn this.get();\n"
					"\t}\n"
					"}\n"
					"double work() {\n"
					"\tDAcc a = DAcc();\n"
					"\treturn a.run(1000000.0);\n"
					"}\n"
					"void main() { print(to_int(work())); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"leaf_this_call_int64",
				"class I64Acc {\n"
					"public:\n"
					"\tint64 n = 0i64;\n"
					"\tvoid add(int64 x) { n = n + x; }\n"
					"\tvoid bump() { this.add(1i64); }\n"
					"\tint64 get() { return n; }\n"
					"\tint64 run(int64 limit) {\n"
					"\t\tint64 i = 0i64;\n"
					"\t\twhile (i < limit) { this.bump(); i = i + 1i64; }\n"
					"\t\treturn this.get();\n"
					"\t}\n"
					"}\n"
					"int64 work() { I64Acc a = I64Acc(); return a.run(1000000i64); }\n"
					"void main() { print(to_int(work())); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"leaf_this_call_uint64",
				"class U64Acc {\n"
					"public:\n"
					"\tuint64 n = 0u64;\n"
					"\tvoid add(uint64 x) { n = n + x; }\n"
					"\tvoid bump() { this.add(1u64); }\n"
					"\tuint64 get() { return n; }\n"
					"\tuint64 run(uint64 limit) {\n"
					"\t\tuint64 i = 0u64;\n"
					"\t\twhile (i < limit) { this.bump(); i = i + 1u64; }\n"
					"\t\treturn this.get();\n"
					"\t}\n"
					"}\n"
					"uint64 work() { U64Acc a = U64Acc(); return a.run(1000000u64); }\n"
					"void main() { print(to_int(work())); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"leaf_this_call_float",
				"class FAcc {\n"
					"public:\n"
					"\tfloat n = 0.f;\n"
					"\tvoid add(float x) { n = n + x; }\n"
					"\tvoid bump() { this.add(1.f); }\n"
					"\tfloat get() { return n; }\n"
					"\tfloat run(float limit) {\n"
					"\t\tfloat i = 0.f;\n"
					"\t\twhile (i < limit) { this.bump(); i = i + 1.f; }\n"
					"\t\treturn this.get();\n"
					"\t}\n"
					"}\n"
					"float work() { FAcc a = FAcc(); return a.run(1000000.f); }\n"
					"void main() { print(to_int(work())); }\n",
				"1000000\n",
				0.0,
				1.0,
			},
			{
				"scalar_inline_branch",
				"int max_leaf(int a, int b) {\n"
					"\tif (a < b) { a = b; }\n"
					"\treturn a;\n"
					"}\n"
					"int work() {\n"
					"\tint s = 0;\n"
					"\tint i = 0;\n"
					"\twhile (i < 2000000) {\n"
					"\t\ts = max_leaf(s, i);\n"
					"\t\ti++;\n"
					"\t}\n"
					"\treturn s;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"1999999\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_dispatch",
				"interface I { virtual int value() = 0; }\n"
					"class A : I { int value() { return 1; } }\n"
					"class B : I { int value() { return 2; } }\n"
					"int work(I a, I b, int limit) {\n"
					"\tint total = 0; int i = 0;\n"
					"\twhile (i < limit) { total = total + a.value(); "
						"total = total + b.value(); i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { A a = A(); B b = B(); print(work(a, b, 100000)); }\n",
				"300000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_dispatch_args",
				"interface I { virtual int add(int x) = 0; }\n"
					"class A : I { int add(int x) { return x + 1; } }\n"
					"class B : I { int add(int x) { return x + 2; } }\n"
					"int work(I a, I b, int limit) {\n"
					"\tint total = 0; int i = 0; int step = 1;\n"
					"\twhile (i < limit) { total = total + a.add(step); "
						"total = total + b.add(step); i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { A a = A(); B b = B(); print(work(a, b, 100000)); }\n",
				"500000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_dispatch_four_types",
				"interface I { virtual int add(int x) = 0; }\n"
					"class A : I { int add(int x) { return x + 1; } }\n"
					"class B : I { int add(int x) { return x + 2; } }\n"
					"class C : I { int add(int x) { return x + 3; } }\n"
					"class D : I { int add(int x) { return x + 4; } }\n"
					"int work(I a, I b, I c, I d, int limit) {\n"
					"\tI current = a; int total = 0; int i = 0;\n"
					"\twhile (i < limit) { int choice = i % 4;\n"
					"\t\tif (choice == 0) current = a;\n"
					"\t\telse if (choice == 1) current = b;\n"
					"\t\telse if (choice == 2) current = c; else current = d;\n"
					"\t\ttotal = total + current.add(1); i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { A a = A(); B b = B(); C c = C(); D d = D(); "
					"print(work(a, b, c, d, 200000)); }\n",
				"700000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_dispatch_uint_args",
				"interface I { virtual unsigned int add(unsigned int x) = 0; }\n"
					"class A : I { unsigned int add(unsigned int x) { return x + 1u; } }\n"
					"class B : I { unsigned int add(unsigned int x) { return x + 2u; } }\n"
					"unsigned int work(I a, I b, int limit) {\n"
					"\tunsigned int total = 0u; int i = 0; unsigned int step = 1u;\n"
					"\twhile (i < limit) { total = total + a.add(step); "
						"total = total + b.add(step); i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { A a = A(); B b = B(); print(work(a, b, 100000)); }\n",
				"500000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_dispatch_int64_raw",
				"interface I64Op { virtual int64 add(int64 a, int64 b) = 0; }\n"
					"class I64Add : I64Op { int64 add(int64 a, int64 b) { return a + b; } }\n"
					"int64 work(I64Op op, int limit) {\n"
					"\tint64 total = 0i64; int i = 0;\n"
					"\twhile (i < limit) { total = op.add(total, 1i64); i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { I64Add op = I64Add(); print(work(op, 100000)); }\n",
				"100000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_dispatch_float_raw",
				"interface F32Op { virtual float add(float a, float b) = 0; }\n"
					"class F32Add : F32Op { float add(float a, float b) { return a + b; } }\n"
					"float work(F32Op op, int limit) {\n"
					"\tfloat total = 0.f; int i = 0;\n"
					"\twhile (i < limit) { total = op.add(total, 1.f); i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { F32Add op = F32Add(); print(to_int(work(op, 100000))); }\n",
				"100000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_fields",
				"class FieldRich { public:\n"
					"\tint f0 = 0; int f1 = 0; int f2 = 0; int f3 = 0;\n"
					"\tint f4 = 0; int f5 = 0; int f6 = 0; int f7 = 0;\n"
					"\tint f8 = 0; int f9 = 0; int f10 = 0; int f11 = 0;\n"
					"\tint f12 = 0; int f13 = 0; int f14 = 0; int f15 = 0;\n"
					"}\n"
					"int work(FieldRich value, int limit) {\n"
					"\tint i = 0;\n"
					"\twhile (i < limit) {\n"
					"\t\tvalue.f0 = value.f0 + 1; value.f1 = value.f1 + 2;\n"
					"\t\tvalue.f2 = value.f2 + 3; value.f3 = value.f3 + 4;\n"
					"\t\tvalue.f4 = value.f4 + 5; value.f5 = value.f5 + 6;\n"
					"\t\tvalue.f6 = value.f6 + 7; value.f7 = value.f7 + 8;\n"
					"\t\tvalue.f8 = value.f8 + 9; value.f9 = value.f9 + 10;\n"
					"\t\tvalue.f10 = value.f10 + 11; value.f11 = value.f11 + 12;\n"
					"\t\tvalue.f12 = value.f12 + 13; value.f13 = value.f13 + 14;\n"
					"\t\tvalue.f14 = value.f14 + 15; value.f15 = value.f15 + 16;\n"
					"\t\ti++;\n"
					"\t}\n"
					"\treturn value.f0 + value.f1 + value.f2 + value.f3\n"
					"\t\t+ value.f4 + value.f5 + value.f6 + value.f7\n"
					"\t\t+ value.f8 + value.f9 + value.f10 + value.f11\n"
					"\t\t+ value.f12 + value.f13 + value.f14 + value.f15;\n"
					"}\n"
					"void main() { FieldRich value = FieldRich(); print(work(value, 20000)); }\n",
				"2720000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_field_accumulate",
				"class FieldValue { public: int value = 3; }\n"
					"int work(FieldValue object, int limit) {\n"
					"\tint total = 0;\n"
					"\tfor (int i = 0; i < limit; i++) { total += object.value; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { FieldValue object = FieldValue(); "
					"print(work(object, 1000000)); }\n",
				"3000000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_field_accumulate_while",
				"class FieldValue { public: int value = 3; }\n"
					"int work(FieldValue object, int limit) {\n"
					"\tint total = 0; int i = 0;\n"
					"\twhile (i < limit) { total += object.value; i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { FieldValue object = FieldValue(); "
					"print(work(object, 1000000)); }\n",
				"3000000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_field_accumulate_double",
				"class FieldValue { public: double value = 3.0; }\n"
					"double work(FieldValue object, int limit) {\n"
					"\tdouble total = 0.0;\n"
					"\tfor (int i = 0; i < limit; i++) { total += object.value; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { FieldValue object = FieldValue(); "
					"print(to_int(work(object, 1000000))); }\n",
				"3000000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_field_accumulate_int64",
				"class FieldValue { public: int64 value = 3i64; }\n"
					"int64 work(FieldValue object, int limit) {\n"
					"\tint64 total = 0i64;\n"
					"\tfor (int i = 0; i < limit; i++) { total += object.value; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { FieldValue object = FieldValue(); "
					"print(to_int(work(object, 1000000))); }\n",
				"3000000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_object_fields_compound",
				"class FieldRich { public:\n"
					"\tint f0 = 0; int f1 = 0; int f2 = 0; int f3 = 0;\n"
					"\tint f4 = 0; int f5 = 0; int f6 = 0; int f7 = 0;\n"
					"\tint f8 = 0; int f9 = 0; int f10 = 0; int f11 = 0;\n"
					"\tint f12 = 0; int f13 = 0; int f14 = 0; int f15 = 0;\n"
					"}\n"
					"int work(FieldRich value, int limit) {\n"
					"\tint i = 0;\n"
					"\twhile (i < limit) {\n"
					"\t\tvalue.f0 += 1; value.f1 += 2; value.f2 += 3; value.f3 += 4;\n"
					"\t\tvalue.f4 += 5; value.f5 += 6; value.f6 += 7; value.f7 += 8;\n"
					"\t\tvalue.f8 += 9; value.f9 += 10; value.f10 += 11; value.f11 += 12;\n"
					"\t\tvalue.f12 += 13; value.f13 += 14; value.f14 += 15; value.f15 += 16;\n"
					"\t\ti++;\n"
					"\t}\n"
					"\treturn value.f0 + value.f1 + value.f2 + value.f3\n"
					"\t\t+ value.f4 + value.f5 + value.f6 + value.f7\n"
					"\t\t+ value.f8 + value.f9 + value.f10 + value.f11\n"
					"\t\t+ value.f12 + value.f13 + value.f14 + value.f15;\n"
					"}\n"
					"void main() { FieldRich value = FieldRich(); print(work(value, 20000)); }\n",
				"2720000\n",
				0.0,
				1.0,
			},
			{
				"scalar_array_storage",
				"int work() {\n"
					"\tarray<int> values = array<int>();\n"
					"\tvalues.reserve(50000);\n"
					"\tint i = 0;\n"
					"\twhile (i < 50000) { values.push(i); i++; }\n"
					"\tint total = 0; i = 0;\n"
					"\twhile (i < 50000) { total = total + values[i]; i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"1249975000\n",
				0.0,
				1.0,
			},
			{
				"scalar_array_expression_storage",
				"int work() {\n"
					"\tarray<int> values = array<int>(); values.reserve(50000);\n"
					"\tint i = 0; while (i < 50000) { values.push(i % 1024); i++; }\n"
					"\tint total = 0; i = 0;\n"
					"\twhile (i < 50000) { total += values[i]; i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"25500376\n",
				0.0,
				1.0,
			},
			{
				"typed_array_random_read_write",
				"int work() {\n"
					"\tarray<int> values = array<int>(); values.reserve(65536);\n"
					"\tint i = 0; while (i < 65536) { values.push(0); i++; }\n"
					"\tint state = 1; int total = 0; i = 0;\n"
					"\twhile (i < 1000000) {\n"
					"\t\tstate = (state * 25173 + 13849) % 65536;\n"
					"\t\tvalues[state] = values[state] + 1;\n"
					"\t\ttotal = (total + values[state]) % 1000000007;\n"
					"\t\ti++;\n"
					"\t}\n"
					"\treturn total + values[state];\n"
					"}\n"
					"void main() { print(work()); }\n",
				"8135696\n",
				0.0,
				1.0,
			},
			{
				"scalar_array_read_hot",
				"int work() {\n"
					"\tarray<int> values = array<int>(); values.push(7);\n"
					"\tint total = 0; int i = 0;\n"
					"\twhile (i < 1000000) { total += values[0]; i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"7000000\n",
				0.0,
				1.0,
			},
			{
				"scalar_array_write_hot",
				"int work() {\n"
					"\tint seed = 3; array<int> values = {seed};\n"
					"\tint i = 0;\n"
					"\twhile (i < 500000) { values[0] = i + seed; i++; }\n"
					"\treturn values[0];\n"
					"}\n"
					"void main() { print(work()); }\n",
				"500002\n",
				0.0,
				1.0,
			},
			{
				"scalar_set_add_hot",
				"int work() {\n"
					"\tset<int> values = set<int>(); values.reserve(500000);\n"
					"\tint i = 0;\n"
					"\twhile (i < 500000) { values.add(i); i++; }\n"
					"\treturn len(values);\n"
					"}\n"
					"void main() { print(work()); }\n",
				"500000\n",
				0.0,
				1.0,
			},
			{
				"scalar_set_has_hot",
				"int work() {\n"
					"\tset<int> values = {7};\n"
					"\tint total = 0; int i = 0;\n"
					"\twhile (i < 500000) { if (values.has(7)) { total++; } i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"500000\n",
				0.0,
				1.0,
			},
			{
				"scalar_set_expression_has_hot",
				"int work() {\n"
					"\tset<int> values = set<int>(); values.reserve(4096);\n"
					"\tint i = 0; while (i < 4096) { values.add(i); i++; }\n"
					"\tint total = 0; i = 0;\n"
					"\twhile (i < 500000) { if (values.has(i % 8192)) { total++; } i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"250144\n",
				0.0,
				1.0,
			},
			{
				"string_set_has_hot",
				"int work() {\n"
					"\tset<string> values = {\"needle\"};\n"
					"\tstring needle = \"needle\"; int total = 0; int i = 0;\n"
					"\twhile (i < 500000) { if (values.has(needle)) { total++; } i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"500000\n",
				0.0,
				1.0,
			},
			{
				"string_equal_hot",
				"int work() {\n"
					"\tstring a = \"same\"; string b = \"same\";\n"
					"\tint total = 0; int i = 0;\n"
					"\twhile (i < 500000) { if (a == b) { total++; } i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"500000\n",
				0.0,
				1.0,
			},
			{
				"string_conditional_equal_hot",
				"int work(int limit) {\n"
					"\tstring same_a = \"the same benchmark string\";\n"
					"\tstring same_b = \"the same benchmark string\";\n"
					"\tstring other = \"a different benchmark string\";\n"
					"\tint total = 0;\n"
					"\tfor (int i = 0; i < limit; i++) {\n"
					"\t\tif ((i % 8 == 0 ? other : same_a) == same_b) total++;\n"
					"\t}\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work(200000)); }\n",
				"175000\n",
				0.0,
				1.0,
			},
			{
				"exception_catch_hot",
				"int work() {\n"
					"\tint total = 0;\n"
					"\tfor (int i = 0; i < 50000; i++) {\n"
					"\t\ttry { throw i; } catch (int value) { total += value; }\n"
					"\t}\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"1249975000\n",
				0.0,
				1.0,
			},
			{
				"scalar_map_storage",
				"int work() {\n"
					"\tmap<int,int> values = map<int,int>();\n"
					"\tint i = 0;\n"
					"\twhile (i < 500000) {\n"
					"\t\tvalues[i] = i + 1;\n"
					"\t\ti++;\n"
					"\t}\n"
					"\treturn values[499999];\n"
					"}\n"
					"void main() { print(work()); }\n",
				"500000\n",
				0.0,
				1.0,
			},
			{
				"scalar_map_direct_storage",
				"int work() {\n"
					"\tmap<int,int> values = map<int,int>();\n"
					"\tint i = 0;\n"
					"\twhile (i < 500000) { values[i] = i; i++; }\n"
					"\treturn values[499999];\n"
					"}\n"
					"void main() { print(work()); }\n",
				"499999\n",
				0.0,
				1.0,
			},
			{
				"scalar_map_read_hot",
				"int work() {\n"
					"\tmap<int,int> values = map<int,int>();\n"
					"\tvalues[0] = 7;\n"
					"\tint total = 0; int i = 0;\n"
					"\twhile (i < 500000) { total += values[0]; i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"3500000\n",
				0.0,
				1.0,
			},
			{
				"string_map_read_hot",
				"int work() {\n"
					"\tmap<string,int> values = map<string,int>();\n"
					"\tstring key = \"needle\"; values[key] = 7;\n"
					"\tint total = 0; int i = 0;\n"
					"\twhile (i < 500000) { total += values[key]; i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"3500000\n",
				0.0,
				1.0,
			},
			{
				"scalar_map_expression_read_hot",
				"int work() {\n"
					"\tmap<int,int> values = map<int,int>(); values.reserve(4096);\n"
					"\tint i = 0; while (i < 4096) { values[i] = i % 1024; i++; }\n"
					"\tint total = 0; i = 0;\n"
					"\twhile (i < 500000) { "
					"total = (total + values[i % 4096]) % 1000000007; i++; }\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"255644016\n",
				0.0,
				1.0,
			},
			{
				"string_set_random_key_has",
				"int work() {\n"
					"\tset<string> values = {\"alpha\", \"beta\", \"gamma\", \"delta\"};\n"
					"\tarray<string> keys = {\"alpha\", \"beta\", \"gamma\", \"delta\", \"missing\"};\n"
					"\tint state = 1; int total = 0; int i = 0;\n"
					"\twhile (i < 1000000) {\n"
					"\t\tstate = (state * 25173 + 13849) % 65536;\n"
					"\t\tif (values.has(keys[state % 5])) { total++; }\n"
					"\t\ti++;\n"
					"\t}\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"800098\n",
				0.0,
				1.0,
			},
			{
				"typed_set_random_has",
				"int work() {\n"
					"\tset<int> values = set<int>(); values.reserve(32768);\n"
					"\tint i = 0; while (i < 65536) { if ((i % 2) == 0) values.add(i); i++; }\n"
					"\tint state = 1; int total = 0; i = 0;\n"
					"\twhile (i < 1000000) {\n"
					"\t\tstate = (state * 25173 + 13849) % 65536;\n"
					"\t\tif (values.has(state)) { total++; }\n"
					"\t\ti++;\n"
					"\t}\n"
					"\treturn total;\n"
					"}\n"
					"void main() { print(work()); }\n",
				"500000\n",
				0.0,
				1.0,
			},
			{
				"typed_map_random_read_write",
				"int work() {\n"
					"\tmap<int,int> values = map<int,int>(); values.reserve(65536);\n"
					"\tint i = 0; while (i < 65536) { values[i] = 0; i++; }\n"
					"\tint state = 1; int total = 0; i = 0;\n"
					"\twhile (i < 1000000) {\n"
					"\t\tstate = (state * 25173 + 13849) % 65536;\n"
					"\t\tvalues[state] = values[state] + 1;\n"
					"\t\ttotal = (total + values[state]) % 1000000007;\n"
					"\t\ti++;\n"
					"\t}\n"
					"\treturn total + values[state];\n"
					"}\n"
					"void main() { print(work()); }\n",
				"8135696\n",
				0.0,
				1.0,
			},
			{
				"typed_map_double_random_update",
				"double work() {\n"
					"\tmap<int,double> values = map<int,double>(); values.reserve(65536);\n"
					"\tint i = 0; while (i < 65536) { values[i] = 1.0; i++; }\n"
					"\tint state = 1; i = 0; while (i < 1000000) {\n"
					"\t\tstate = (state * 25173 + 13849) % 65536;\n"
					"\t\tvalues[state] = values[state] + 0.5; i++;\n"
					"\t}\n"
					"\treturn values[state];\n"
					"}\n"
					"void main() { print(work()); }\n",
				"9\n",
				0.0,
				1.0,
			},
			{
				"typed_map_uint_random_update",
				"uint32 work() {\n"
					"\tmap<int,uint32> values = map<int,uint32>(); values.reserve(65536);\n"
					"\tint i = 0; while (i < 65536) { values[i] = 0u; i++; }\n"
					"\tint state = 1; i = 0; while (i < 1000000) {\n"
					"\t\tstate = (state * 25173 + 13849) % 65536;\n"
					"\t\tvalues[state] = values[state] + 1u; i++;\n"
					"\t}\n"
					"\treturn values[state];\n"
					"}\n"
					"void main() { print(work()); }\n",
				"16\n",
				0.0,
				1.0,
			},
			{
				"bound_method_field_update",
				"class Counter { public: int value = 0; "
					"void add(int delta) { value = (value + delta) % 1000000007; } };\n"
					"int work() { Counter value = Counter(); int i = 0; "
					"while (i < 200000) { value.add(i % 97); i++; } "
					"return value.value; }\n"
					"void main() { print(work()); }\n",
				"9599419\n",
				0.0,
				1.0,
			},
			{
				"dynamic_transform_pair_loop",
				"interface IOperation { virtual int step(int value) = 0; };\n"
					"class AddThree : IOperation { public: int step(int value) { "
					"return (value + 3) % 1000000007; } };\n"
					"class AddFive : IOperation { public: int step(int value) { "
					"return (value + 5) % 1000000007; } };\n"
					"int pair_once(IOperation first, IOperation second) { int total = 0; "
					"total = first.step(total); total = second.step(total); return total; }\n"
					"int work(IOperation first, IOperation second, int limit) { int total = 0; "
					"for (int i = 0; i < limit; i++) { total = first.step(total); "
					"total = second.step(total); } return total; }\n"
					"void main() { AddThree first = AddThree(); AddFive second = AddFive(); "
					"print(pair_once(first, second)); print(work(first, second, 200000)); }\n",
				"8\n1600000\n",
				0.0,
				1.0,
			},
			{
				"dynamic_transform_replaced_receiver",
				"interface IOperation { virtual int step(int value) = 0; };\n"
					"class AddThree : IOperation { public: int step(int value) { "
					"return (value + 3) % 1000000007; } };\n"
					"class AddFive : IOperation { public: int step(int value) { "
					"return (value + 5) % 1000000007; } };\n"
					"int work(int limit) { IOperation operation = AddThree(); int total = 0; "
					"for (int i = 0; i < limit; i++) { "
					"if ((i % 2) == 0) { operation = AddThree(); } "
					"else { operation = AddFive(); } "
					"total = operation.step(total); } return total; }\n"
					"void main() { print(work(10000)); }\n",
				"40000\n",
				0.0,
				1.0,
			},
			{
				"scalar_map_storage_reserved",
				"int work() {\n"
					"\tmap<int,int> values = map<int,int>();\n"
					"\tvalues.reserve(500000);\n"
					"\tint i = 0;\n"
					"\twhile (i < 500000) {\n"
					"\t\tvalues[i] = i + 1;\n"
					"\t\ti++;\n"
					"\t}\n"
					"\treturn values[499999];\n"
					"}\n"
					"void main() { print(work()); }\n",
				"500000\n",
				0.0,
				1.0,
			},
			{
				"general_i32_mod_loop",
				"int work() { int total = 0; for (int i = 0; i < 20000000; i++) { "
					"total = (total + (i % 97)) % 1000000007; } return total; } "
					"void main() { print(work()); }\n",
				"959998845\n",
				0.0,
				1.0,
			},
			{
				"noncanonical_double_indexed_recurrence",
				"int work(int limit) { array<double> table = {0.125, -0.75, 1.5, "
					"0.0625, -1.25, 0.875, 0.375}; int state = 17; double value = 0.25; "
					"for (int i = 0; i < limit; i++) { state = "
					"(state * 25173 + 13849) % 65521; value = value * 0.99991 "
					"+ table[state % 7] * 0.00009; } return "
					"to_int((value + 4.0) * 1000000.0) + state; } "
					"void main() { print(work(200000)); }\n",
				"4199876\n",
				0.0,
				1.0,
			},
			{
				"noncanonical_indirect_array",
				"int work(int limit) { array<int> values = array<int>(); "
					"array<int> indices = array<int>(); for (int i = 0; i < 257; i++) { "
					"values.push((i * 17 + 11) % 101); indices.push((i * 37 + 19) % 257); } "
					"int state = 17; int total = 0; for (int i = 0; i < limit; i++) { "
					"state = (state * 25173 + 13849) % 65521; int position = "
					"(state + ((i % 257) * 7)) % 257; int index = indices[position]; "
					"int delta = ((state / 7) % 13) + 1; values[index] = "
					"(values[index] + delta) % 1000003; total = "
					"(total + values[index] + (position & 3)) % 1000000007; } "
					"return (total + state + values[indices[state % 257]]) % 1000000007; } "
					"void main() { print(work(100000)); }\n",
				"142060410\n",
				0.0,
				1.0,
			},
			{
				"noncanonical_short_object",
				"class Pair { public: int first = 0; int second = 0; "
					"int sum() { return first + second; } }; "
					"int work(int limit) { int state = 17; int total = 0; "
					"for (int i = 0; i < limit; i++) { state = "
					"(state * 25173 + 13849) % 65521; Pair value = Pair(); "
					"value.first = state % 127; value.second = (state / 7) % 127; "
					"total = (total + value.sum()) % 1000000007; } "
					"return total + state; } void main() { print(work(50000)); }\n",
				"6302055\n",
				0.0,
				1.0,
			},
			{
				"noncanonical_short_object_argument",
				"class Pair { public: int first = 0; int second = 0; "
					"int sum(int salt) { return first + second + (salt & 1); } }; "
					"int work(int limit) { int state = 17; int total = 0; "
					"for (int i = 0; i < limit; i++) { state = "
					"(state * 25173 + 13849) % 65521; Pair value = Pair(); "
					"value.first = state % 127; value.second = (state / 7) % 127; "
					"total = (total + value.sum(state)) % 1000000007; } "
					"return total + state; } void main() { print(work(50000)); }\n",
				"6327125\n",
				0.0,
				1.0,
			},
			{
				"noncanonical_xorshift_short_object",
				"uint32 next_state(uint32 value) { value = value ^ (value << 13u); "
					"value = value ^ (value >> 17u); return value ^ (value << 5u); } "
					"class Pair { public: int first = 0; int second = 0; "
					"int sum() { return first + second; } }; "
					"int work(int limit) { uint32 state = 1357911u; int total = 0; "
					"for (int i = 0; i < limit; i++) { state = next_state(state); "
					"Pair value = Pair(); value.first = to_int(state & 255u); "
					"value.second = to_int((state >> 8u) & 255u); "
					"total = (total + value.sum()) % 1000000007; } return total; } "
					"void main() { print(work(500000)); }\n",
				"127519895\n",
				0.0,
				1.0,
			},
			{
				"noncanonical_array_virtual_receiver",
				"interface IOperation { virtual int step(int value, int salt) = 0; }; "
					"class Add : IOperation { public: int step(int value, int salt) { "
					"return (value + 1 + (salt % 3)) % 1000000007; } }; "
					"class Xor : IOperation { public: int step(int value, int salt) { "
					"return ((value ^ (salt & 7)) + 7) % 1000000007; } }; "
					"int advance(int state) { return (state * 75 + 74) % 65521; } "
					"int work(int limit) { array<IOperation> operations = {Add(), Xor()}; "
					"array<int> order = {1,0,1,0}; int state = 17; int total = 0; "
					"for (int i=0; i<limit; i++) { state=advance(state); "
					"int index=order[(state+total)&3]; IOperation operation=operations[index]; "
					"total=operation.step(total,state); } return total+state; } "
					"void main(){ print(work(200000)); }\n",
				"881023\n",
				0.0,
				1.0,
			},
			{
				"noncanonical_map_f64_conditional_update",
				"int work(int limit, int seed) { int size=1021; "
					"map<int,double> values=map<int,double>(); values.reserve(size); "
					"for(int i=0;i<size;i++) values[i]=(i%31)*0.25+1.0; "
					"int state=seed; double total=0.0; for(int i=0;i<limit;i++) { "
					"state=(state*75+74)%65521; int key=(state+i*13)%size; "
					"double value=values[key]*1.000001+(state%7)*0.25; "
					"if(value>10000.0) value*=0.5; values[key]=value; total+=value; } "
					"return to_int(total)+state; } "
					"void main(){ print(work(100000,173)); }\n",
				"4280489\n",
				0.0,
				1.0,
			},
			{
				"noncanonical_dual_string_array_compare",
				"int work(int limit,int seed){ array<string> left={\"alpha-000\","
					"\"beta-111\",\"gamma-222\",\"delta-333\",\"epsilon-444\","
					"\"zeta-555\",\"eta-666\"}; array<string> right={\"alpha-000\","
					"\"beta-X11\",\"gamma-222\",\"delta-X33\",\"epsilon-444\","
					"\"zeta-X55\",\"eta-666\"}; int state=seed; int total=0; "
					"for(int i=0;i<limit;i++){ state=(state*75+74)%65521; "
					"int a=state%7; int b=(state+(i&3))%7; "
					"if(left[a]==right[b]) total++; } return total+state; } "
					"void main(){print(work(300000,173));}\n",
				"98646\n",
				0.0,
				1.0,
			},
			{
				"noncanonical_mixed_typed_containers",
				"int work(int limit) { array<int> values = {1,2,3,4}; "
					"array<int> indices = {3,1,0,2}; map<int,int> counts = map<int,int>(); "
					"set<int> selected = {0,2}; array<string> left = {\"aa\",\"bb\",\"cc\",\"dd\"}; "
					"array<string> right = {\"aa\",\"bx\",\"cc\",\"dx\"}; "
					"map<string,int> names = map<string,int>(); set<string> known = {\"aa\",\"cc\"}; "
					"names[\"aa\"]=5; names[\"bb\"]=7; names[\"cc\"]=11; names[\"dd\"]=13; "
					"int total=0; for (int i=0; i<limit; i++) { int slot=indices[(i*3+1)&3]; "
					"values[slot]=values[slot]+(i%5); counts[slot]=values[slot]; "
					"if(selected.has(slot)) total+=counts[slot]; int textSlot=(i*5+2)&3; "
					"if(left[textSlot]==right[textSlot]) total+=names[left[textSlot]]; "
					"if(known.has(left[textSlot])) total++; total%=1000000007; } "
					"return total+values[indices[limit&3]]; } "
					"void main(){ print(work(100000)); }\n",
				"250624997\n",
				0.0,
				1.0,
			},
			{
				"general_unary_free_call",
				"int add_one(int value) { return value + 1; } "
					"int work() { int total = 0; for (int i = 0; i < 10000000; i++) { "
					"total = add_one(total); } return total; } "
					"void main() { print(work()); }\n",
				"10000000\n",
				0.0,
				1.0,
			},
			{
				"general_double_recurrence",
				"double work() { double total = 0.0; for (int i = 0; i < 10000000; i++) { "
					"total += 1.0; } return total; } "
					"void main() { print(to_int(work())); }\n",
				"10000000\n",
				0.0,
				1.0,
			},
		};
