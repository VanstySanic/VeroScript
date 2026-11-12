// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptM3Tests / standard library and runtime suite. Included by the owning translation unit.

	if (!ExpectOutput(
			"struct Box<T> {\n"
			"    T value;\n"
			"    T get() { return this.value; }\n"
			"    void set(T v) { this.value = v; }\n"
			"}\n"
			"void main() {\n"
			"    Box<int> b = Box<int>();\n"
			"    b.set(9);\n"
			"    print(b.get());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9\n",
			"generic struct methods"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Box<T> { T value; }\n"
			"void main() { Box b = Box(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "bare generic struct should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Point { int x; }\n"
			"void main() { Point<int> p = Point<int>(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "non-generic struct type args should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.27: class generics (monomorphization, handle semantics)
	if (!ExpectOutput(
			"class Box<T> {\n"
			"    public:\n"
			"    T value;\n"
			"    void set(T v) { this.value = v; }\n"
			"    T get() { return this.value; }\n"
			"}\n"
			"void main() {\n"
			"    Box<int> a = Box<int>();\n"
			"    Box<int> b = a;\n"
			"    a.set(3);\n"
			"    print(b.get());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"generic class Box share and methods"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class Box<T> { public: T value; }\n"
			"void main() { Box b = Box(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "bare generic class should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// Regression: open generic must not construct without type args (even if T only in methods)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Box<T> { int n; void set(T v) { this.n = 1; } }\n"
			"void main() { Box(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "open generic ctor Box() should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class Box<T> { public: int n; void set(T v) { this.n = 1; } }\n"
			"void main() { auto b = Box(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "open generic auto Box() should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// Nested generic method return array<T>
	if (!ExpectOutput(
			"struct Box<T> {\n"
			"    array<T> xs;\n"
			"    array<T> get() { return this.xs; }\n"
			"}\n"
			"void main() {\n"
			"    Box<int> b = Box<int>();\n"
			"    b.xs = array<int>();\n"
			"    b.xs.push(5);\n"
			"    array<int> a = b.get();\n"
			"    print(a[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"generic method returns array<T>"))
	{
		return EXIT_FAILURE;
	}

	// v1.28: multi-parameter struct/class generics
	if (!ExpectOutput(
			"struct Pair<T,U> { T a; U b; }\n"
			"void main() {\n"
			"    Pair<int,string> p = Pair<int,string>();\n"
			"    p.a = 7;\n"
			"    p.b = \"ok\";\n"
			"    print(p.a);\n"
			"    print(p.b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\nok\n",
			"multi-parameter generic struct fields"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"class Pair<T,U> { public: T a; U b; }\n"
			"void main() {\n"
			"    Pair<int,string> a = Pair<int,string>();\n"
			"    Pair<int,string> b = a;\n"
			"    a.a = 9;\n"
			"    a.b = \"shared\";\n"
			"    print(b.a);\n"
			"    print(b.b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9\nshared\n",
			"multi-parameter generic class share"))
	{
		return EXIT_FAILURE;
	}

	// v1.28.1: nested owner mangling and method substitutions
	if (!ExpectOutput(
			"struct Box<T> { T value; }\n"
			"struct Pair<T,U> {\n"
			"    T a; U b;\n"
			"    void setB(U v) { this.b = v; }\n"
			"    U getB() { return this.b; }\n"
			"    T getA() { return this.a; }\n"
			"}\n"
			"void main() {\n"
			"    Pair<Box<int>,string> p = Pair<Box<int>,string>();\n"
			"    p.a = Box<int>();\n"
			"    p.a.value = 7;\n"
			"    p.setB(\"hi\");\n"
			"    print(p.getA().value);\n"
			"    print(p.getB());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\nhi\n",
			"nested generic owner methods"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Pair<T,U> {\n"
			"    T a; U b;\n"
			"    void run() {\n"
			"        U x = this.b;\n"
			"        array<U> xs = array<U>();\n"
			"        xs.push(this.b);\n"
			"        print(x);\n"
			"        print(xs[0]);\n"
			"    }\n"
			"}\n"
			"void main() {\n"
			"    Pair<int,string> p = Pair<int,string>();\n"
			"    p.b = \"hi\";\n"
			"    p.run();\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"hi\nhi\n",
			"generic method local type substitution"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Pair<T,U> {\n"
			"    T a; U b;\n"
			"    void setArr(array<U> v) { this.b = v[0]; }\n"
			"}\n"
			"void main() {\n"
			"    Pair<int,string> p = Pair<int,string>();\n"
			"    array<string> a = array<string>();\n"
			"    a.push(\"z\");\n"
			"    p.setArr(a);\n"
			"    print(p.b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"z\n",
			"generic method array parameter substitution"))
	{
		return EXIT_FAILURE;
	}

	for (const char* Source : {
			 "struct Pair<T,U> { T a; U b; }\nvoid main() { Pair<int> p = Pair<int>(); }\n",
	"struct Pair<T,U> { T a; U b; }\nvoid main() { Pair<int,string,float> p = Pair<int,string,float>(); }\n"})
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "wrong generic type arity should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("wrong number of type arguments for Pair") != std::string::npos,
				"wrong generic type arity diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Pair<T,U> { T a; U b; }\n"
			"void main() { Pair(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "bare multi-parameter generic constructor should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct Pair<T,U> { T a; U b; }\n"
			"void main() {\n"
			"    Pair<map<int,string>,int> p = Pair<map<int,string>,int>();\n"
			"    p.a = map<int,string>();\n"
			"    p.a[1] = \"nested\";\n"
			"    p.b = 4;\n"
			"    print(p.a[1]);\n"
			"    print(p.b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"nested\n4\n",
			"nested type argument in generic struct"))
	{
		return EXIT_FAILURE;
	}

	// v1.30: length-prefixed nested generic mangling
	if (!ExpectOutput(
			"struct Pair<T,U> {\n"
			"    T a; U b;\n"
			"    U getB() { return this.b; }\n"
			"}\n"
			"void main() {\n"
			"    Pair<Pair<int,string>,bool> p = Pair<Pair<int,string>,bool>();\n"
			"    p.b = true;\n"
			"    print(p.getB());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n",
			"nested multi-parameter generic method"))
	{
		return EXIT_FAILURE;
	}

	// v1.30: nested function type-parameter inference and explicit specialization
	if (!ExpectOutput(
			"T first(array<T> xs) { return xs[0]; }\n"
			"array<T> same<T>(array<T> xs) { return xs; }\n"
			"void main() {\n"
			"    array<int> a = array<int>();\n"
			"    a.push(7);\n"
			"    print(first(a));\n"
			"    print(first<int>(a));\n"
			"    array<int> b = same(a);\n"
			"    print(b[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n7\n7\n",
			"generic function array inference"))
	{
		return EXIT_FAILURE;
	}

	// v1.30: multiple function type parameters
	if (!ExpectOutput(
			"U second<T,U>(T a, U b) { return b; }\n"
			"T first<T,U>(T a, U b) { return a; }\n"
			"void main() {\n"
			"    print(second(1, \"inferred\"));\n"
			"    print(second<int,string>(2, \"explicit\"));\n"
			"    print(first(3, \"x\"));\n"
			"    print(first<int,string>(4, \"y\"));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"inferred\nexplicit\n3\n4\n",
			"multi-parameter generic functions"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Pair<A,B> { A a; B b; }\n"
			"T left<T,U>(Pair<T,U> p) { return p.a; }\n"
			"void main() {\n"
			"    Pair<int,string> p = Pair<int,string>();\n"
			"    p.a = 9;\n"
			"    print(left(p));\n"
			"    print(left<int,string>(p));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9\n9\n",
			"user generic function inference"))
	{
		return EXIT_FAILURE;
	}

	// v1.31: open map/set/ordered_map keys + return-only type params
	if (!ExpectOutput(
			"U get<T,U>(map<T,U> m, T k) { return m[k]; }\n"
			"void main() {\n"
			"    map<int,string> m = map<int,string>();\n"
			"    m[1] = \"hi\";\n"
			"    print(get(m, 1));\n"
			"    print(get<int,string>(m, 1));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"hi\nhi\n",
			"generic map key and value inference"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"bool has<T>(set<T> s, T v) { return s.has(v); }\n"
			"void main() {\n"
			"    set<int> s = set<int>();\n"
			"    s.add(3);\n"
			"    print(has(s, 3));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n",
			"generic set element type param"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"U get<T,U>(ordered_map<T,U> m, T k) { return m[k]; }\n"
			"void main() {\n"
			"    ordered_map<string,int> m = ordered_map<string,int>();\n"
			"    m[\"a\"] = 8;\n"
			"    print(get(m, \"a\"));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"8\n",
			"generic ordered_map key type param"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"T zero<T>() { return 0; }\n"
			"void main() { print(zero<int>()); }\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"return-only type param with explicit args"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"T zero<T>() { return 0; }\n"
			"void main() { print(zero()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "return-only generic without explicit args should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { map<float,int> m = map<float,int>(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "map with float key should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.32: generic field defaults + target-typed bare ctors
	if (!ExpectOutput(
			"struct Box<T> { T value = 0; }\n"
			"void main() {\n"
			"    Box<int> b = Box();\n"
			"    print(b.value);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"generic field default and inferred ctor"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Pair<T,U> { T a; U b; }\n"
			"Pair<int,string> make() { return Pair(); }\n"
			"void main() {\n"
			"    Pair<int,string> p = make();\n"
			"    p.a = 1;\n"
			"    p.b = \"x\";\n"
			"    print(p.a);\n"
			"    print(p.b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\nx\n",
			"return-site inferred generic ctor"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"class Box<T> { public: T value = 0; void set(T v) { this.value = v; } T get() { return this.value; } }\n"
			"void main() {\n"
			"    Box<int> a = Box();\n"
			"    Box<int> b = a;\n"
			"    a.set(4);\n"
			"    print(b.get());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n",
			"class inferred ctor and field default"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Pair<T,U> { T a; U b; }\n"
			"void main() { auto p = Pair(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "bare generic ctor without target type should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.33: field-default scope isolation + in operator
	if (!ExpectOutput(
			"struct Bag<T,U> { map<T,U> m = {}; }\n"
			"void main() {\n"
			"    Bag<int,string> b = Bag();\n"
			"    b.m[1] = \"z\";\n"
			"    print(b.m[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"z\n",
			"generic map field empty default after use"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    set<int> s = set<int>();\n"
			"    s.add(3);\n"
			"    print(3 in s);\n"
			"    print(4 in s);\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m[\"a\"] = 1;\n"
			"    print(\"a\" in m);\n"
			"    print(\"b\" in m);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\nfalse\ntrue\nfalse\n",
			"in operator for set and map"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { array<int> a = array<int>(); print(1 in a); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "in on array should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.34: const cannot be passed to mutable ref; in evaluates left-to-right; nested brace literals
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void write(ref int x) { x = 9; }\n"
			"void main() { const int n = 1; write(ref n); print(n); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "mutable ref of const local should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void write(ref int x) { x = 9; }\n"
			"void forward(const ref int x) { write(ref x); }\n"
			"void main() { int n = 1; forward(ref n); print(n); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "forwarding const ref to mutable ref should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"int n = 1;\n"
			"set<int> make() { n = 2; set<int> s = {1}; return s; }\n"
			"void main() { print(n++ in make()); print(n); }\n",
			VeroScript::ERunMode::Script,
			"true\n2\n",
			"in evaluates left then right"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<array<int>> xs = { {1, 2}, {3} };\n"
			"    print(xs[0][1]);\n"
			"    print(xs[1][0]);\n"
			"    map<string,array<int>> m = { \"a\": {4, 5} };\n"
			"    print(m[\"a\"][1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n3\n5\n",
			"nested brace literals for array and map"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Nest { array<array<int>> xs = { {7} }; }\n"
			"void main() {\n"
			"    Nest n = Nest();\n"
			"    print(n.xs[0][0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"nested brace field default"))
	{
		return EXIT_FAILURE;
	}

	// v1.35: ref arg type check + bare generic ctor in typed brace / field defaults
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void write(ref int x) { x = 9; }\n"
			"void main() { string s = \"bad\"; write(ref s); print(s); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "ref type mismatch should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct Box<T> { T value = 0; }\n"
			"void main() {\n"
			"    array<Box<int>> xs = { Box() };\n"
			"    print(xs[0].value);\n"
			"    map<string,Box<int>> m = { \"a\": Box() };\n"
			"    print(m[\"a\"].value);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n0\n",
			"bare generic ctor in typed brace literals"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Box<T> { T value = 0; }\n"
			"struct Holder { Box<int> b = Box(); array<Box<int>> xs = { Box() }; }\n"
			"void main() {\n"
			"    Holder h = Holder();\n"
			"    print(h.b.value);\n"
			"    print(h.xs[0].value);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n0\n",
			"bare generic ctor in field defaults"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Box<T> { T value = 0; }\n"
			"void main() { array<Box<int>> xs = { Box<string>() }; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "mismatched bare ctor in brace should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct Box<T> { T value = 0; }\n"
			"void main() {\n"
			"    array<array<Box<int>>> xs = { { Box() } };\n"
			"    print(xs[0][0].value);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"nested brace bare generic ctor"))
	{
		return EXIT_FAILURE;
	}

	// Application entry points: void means success; int is the process exit code.
	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
		VeroScript::EExecutionBackend::Mixed})
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int main() { return 37; }\n",
			"<int-main>",
			Output,
			Errors,
			VeroScript::ERunMode::Script,
			{},
			{},
			nullptr,
			{},
			{},
			Backend);
		if (!Expect(
			Result.bSuccess && !Result.bExitRequested && Result.ExitCode == 37,
			"int main return should propagate on every execution backend"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"using Args = string[];\n"
			"int32 main(Args args) { return len(args); }\n",
			"<int-main-args>",
			Output,
			Errors,
			VeroScript::ERunMode::Script,
			{},
			{"one", "two", "three"});
		if (!Expect(Result.bSuccess && Result.ExitCode == 3,
			"aliased int main(aliased string[] args) should run and return an exit code"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int main() {}\n",
			"<implicit-int-main>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess && Result.ExitCode == 0,
			"falling off int main should return zero"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int main() { exit(9); return 3; }\n",
			"<int-main-exit-precedence>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(
			Result.bSuccess && Result.bExitRequested && Result.ExitCode == 9,
			"exit(n) should take precedence over an int main return value"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"double main() { return 1.0; }\n",
			"<invalid-main-return>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess
			&& Errors.str().find("main must return void or int") != std::string::npos,
			"main should reject non-int non-void return types"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int main(ref string[] args) { return 0; }\n",
			"<invalid-main-ref-args>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess
			&& Errors.str().find("main parameter must be string[]") != std::string::npos,
			"main string[] argument must be passed by value"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int main<T>(T value) { return 0; }\n",
			"<invalid-generic-main>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess
			&& Errors.str().find("main cannot be generic") != std::string::npos,
			"main should reject generic entry declarations"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	// v1.36: script usability — exit, getenv, mkdir, string+scalar, assert, read_line
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { exit(7); print(1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "exit should succeed without runtime error"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 7, "exit(7) should set ExitCode 7"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str().empty(), "exit should stop before further prints"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().empty(), "exit should not emit diagnostics"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(std::environment::getenv(\"VERO_SCRIPT_USABILITY_MISSING_ENV\")); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "getenv missing should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "\n", "getenv missing yields empty string"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	#if defined(_WIN32)
	_putenv_s("VERO_SCRIPT_USABILITY_ENV", "usable");
	#else
	setenv("VERO_SCRIPT_USABILITY_ENV", "usable", 1);
	#endif
	if (!ExpectOutput(
			"void main() { print(std::environment::getenv(\"VERO_SCRIPT_USABILITY_ENV\")); }\n",
			VeroScript::ERunMode::Script,
			"usable\n",
			"getenv set value"))
	{
		return EXIT_FAILURE;
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-mkdir-test" / "nested" / "dir";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot.parent_path().parent_path(), RemoveError);
		const std::string Path = TempRoot.generic_string();
		std::ostringstream Output;
		std::ostringstream Errors;
		const std::string Source =
			"import \"io.vs\";\n"
			"void main() {\n"
			"    std::filesystem::mkdir(\"" +
			Path + "\");\n"
			"    print(std::filesystem::exists(\"" +
			Path + "\"));\n"
			"}\n";
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		const bool bExists = std::filesystem::is_directory(TempRoot);
		std::filesystem::remove_all(TempRoot.parent_path().parent_path(), RemoveError);
		if (!Expect(Result.bSuccess, "mkdir stdlib should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "true\n" && bExists, "mkdir creates nested directories"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() { print(\"n=\" + 3); print(true + \"!\"); print(1 + \"x\" + 2); }\n",
			VeroScript::ERunMode::Script,
			"n=3\ntrue!\n1x2\n",
			"string + scalar concatenation"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() { assert(true); assert(true, \"unused message\"); print(1); }\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"assert true continues with or without a message"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { assert(1 == 2, \"score must be positive\"); print(1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(
				!Result.bSuccess && Result.ExitCode == 1
					&& Errors.str().find("score must be positive") != std::string::npos,
				"assert false should report its message"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { assert(true, 7); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(
				!Result.bSuccess
					&& Errors.str().find("assert expects a string message") != std::string::npos,
				"assert message must be string"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { assert(true, \"one\", \"two\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(
				!Result.bSuccess
					&& Errors.str().find("assert expects between 1 and 2 argument(s)")
						!= std::string::npos,
				"assert rejects more than one message"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::istringstream Input("hello line\n");
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"io.vs\";\n"
			"void main() { print(std::io::read_line()); print(std::io::read_line()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script,
			{},
			{},
				&Input);
		if (!Expect(Result.bSuccess, "read_line should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "hello line\n\n", "read_line then EOF empty"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::istringstream Input("discarded by pause\nafter pause\n");
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"io.vs\";\n"
			"void main() { std::io::pause(); print(std::io::read_line()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script,
			{},
			{},
			&Input);
		if (!Expect(Result.bSuccess, "pause should wait for and discard one input line"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "after pause\n", "pause consumes exactly one line"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::istringstream Input;
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"io.vs\";\n"
			"void wait_here() { std::io::pause(); }\n"
			"void main() { wait_here(); print(\"continued\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script,
			{},
			{},
			&Input);
		if (!Expect(Result.bSuccess, "pause should return cleanly at EOF from any function"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "continued\n", "execution continues after pause at EOF"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { exit(1U); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "exit(unsigned) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.37: eprint + append_file
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { eprint(\"warn\"); print(\"out\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "eprint should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "out\n", "print goes to stdout"))
		{
			std::cerr << "got out:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str() == "warn\n", "eprint goes to stderr stream"))
		{
			std::cerr << "got err:\n"
					  << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-append-file-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::string Path = (TempRoot / "log.txt").generic_string();
		std::ostringstream Output;
		std::ostringstream Errors;
		const std::string Source =
			"import \"io.vs\";\n"
			"void main() {\n"
			"    std::io::write_file(\"" +
			Path + "\", \"a\");\n"
			"    std::io::append_file(\"" +
			Path + "\", \"b\");\n"
			"    print(std::io::read_file(\"" +
			Path + "\"));\n"
			"}\n";
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "append_file should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "ab\n", "append_file concatenates"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	// v1.38: Session InputStream + sleep + format
	{
		struct FSessionCaptureHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				DiagnosticText.append(Line);
				DiagnosticText.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;
			std::string DiagnosticText;

		};

		FSessionCaptureHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		VeroScript::FEngineConfig Config;
		std::istringstream Input("session-line\n42\n");
		Config.InputStream = &Input;
		Session.ApplyConfig(Config);
		const VeroScript::FRunResult LoadResult = Session.LoadSource(
			"import \"io.vs\";\n"
			"void main() { print(std::io::read_line()); print(std::io::read_int()); }\n",
			"<session-stdin>");
		if (!Expect(LoadResult.bSuccess, "session InputStream LoadSource should succeed"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}
		const VeroScript::FRunResult RunResult = Session.Run();
		if (!Expect(RunResult.bSuccess, "session InputStream Run should succeed"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}
		if (!Expect(
				Host.PrintText == "session-line\n42\n",
				"session should honor Config.InputStream through std::io"))
		{
			std::cerr << "got:\n"
					  << Host.PrintText;
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { std::this_thread::sleep(0); print(1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "sleep(0) should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "1\n", "sleep(0) should continue"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { std::this_thread::sleep(-1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "sleep(-1) should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { std::this_thread::sleep(1U); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "sleep(unsigned) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"string.vs\";\n"
			"void main() {\n"
			"    array<string> args = {\"1\", \"2\"};\n"
			"    print(format(\"a={} b={}\", args));\n"
			"    array<string> empty = array<string>();\n"
			"    print(format(\"brace {{}} done\", empty));\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "format should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "a=1 b=2\nbrace {} done\n", "format replaces and escapes"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"string.vs\";\n"
			"void main() {\n"
			"    array<string> empty = array<string>();\n"
			"    print(format(\"need {}\", empty));\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "format missing arg should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.39: setenv + eprint(wstring) via ErrorStream
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    std::environment::setenv(\"VERO_SCRIPT_SETENV_TEST\", \"alpha\");\n"
			"    print(std::environment::getenv(\"VERO_SCRIPT_SETENV_TEST\"));\n"
			"    std::environment::setenv(\"VERO_SCRIPT_SETENV_TEST\", \"\");\n"
			"    print(std::environment::getenv(\"VERO_SCRIPT_SETENV_TEST\"));\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "setenv/getenv roundtrip should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "alpha\n\n", "setenv then empty unsets"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { std::environment::setenv(\"\", \"x\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "setenv empty name should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { eprint(L\"wide-err\"); print(\"out\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "eprint(wstring) should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "out\n", "print still on stdout"))
		{
			std::cerr << "got out:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str() == "wide-err\n", "eprint(wstring) to error stream"))
		{
			std::cerr << "got err:\n"
					  << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { std::this_thread::sleep(0 - 1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "sleep(0-1) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(std::environment::getenv(\"\")); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "getenv empty name should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.40: range / cwd / chdir / now — v1 script closeout
	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> xs = range(3);\n"
			"    print(len(xs));\n"
			"    print(xs[0]);\n"
			"    print(xs[2]);\n"
			"    print(len(range(0)));\n"
			"    print(len(range(-1)));\n"
			"    for (int x : range(2)) { print(x); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n0\n2\n0\n0\n0\n1\n",
			"range + for-range"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(range(1000001)); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "range too large should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path OriginalCwd = std::filesystem::current_path();
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-cwd-chdir-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::string TempGeneric = TempRoot.generic_string();
		const std::string ExpectedCwd = std::filesystem::canonical(TempRoot).generic_string();
		std::ostringstream Output;
		std::ostringstream Errors;
		const std::string Source =
			"import \"io.vs\";\n"
			"void main() {\n"
			"    std::filesystem::chdir(\"" +
			TempGeneric + "\");\n"
			"    print(std::filesystem::cwd());\n"
			"}\n";
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		const std::string PrintedCwd = Output.str();
		const bool HasNewline = !PrintedCwd.empty() && PrintedCwd.back() == '\n';
		const auto ReportedCwd = std::filesystem::u8path(
			HasNewline ? PrintedCwd.substr(0, PrintedCwd.size() - 1) : PrintedCwd);
		std::error_code CwdError;
		// Windows short names and POSIX system aliases can spell the same directory differently.
		const bool CwdMatches = HasNewline && ReportedCwd.is_absolute() &&
			std::filesystem::equivalent(ReportedCwd, TempRoot, CwdError) && !CwdError &&
			std::filesystem::equivalent(std::filesystem::current_path(), TempRoot, CwdError) && !CwdError;
		std::filesystem::current_path(OriginalCwd, RemoveError);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "cwd/chdir should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(CwdMatches, "chdir then cwd matches"))
		{
			std::cerr << "got:\n"
					  << Output.str() << "expected:\n"
					  << ExpectedCwd << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    double t = std::chrono::now();\n"
			"    print(t > 1700000000.0);\n"
			"    print(t < 4102444800.0);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "now should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "true\ntrue\n", "now returns plausible epoch seconds"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	// Regression coverage: method arity, non-numeric min, and wstring map print size.
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class C { public: void f(int x) {} }\n"
			"void main() { C c = new C(); c.f(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "method wrong arity should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(min(\"a\", \"b\")); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "min(string,string) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    map<wstring,int> m = map<wstring,int>();\n"
			"    m[L\"a\"] = 1;\n"
			"    m[L\"b\"] = 2;\n"
			"    print(m);\n"
			"    print(len(m));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"map<wstring,int>[2]\n2\n",
			"print wstring-keyed map size matches len"))
	{
		return EXIT_FAILURE;
	}

	// Regression coverage: container arity, len/abs types, exit(0), and owned input.
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    array<int> a = array<int>();\n"
			"    a.push(1, 2);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "array.push wrong arity should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m.clear(1);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "map.clear wrong arity should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(len(true)); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "len(bool) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(abs(\"x\")); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "abs(string) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { exit(0); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess && Result.bExitRequested && Result.ExitCode == 0,
				"exit(0) should succeed with bExitRequested"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		struct FOwnedInputHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				DiagnosticText.append(Line);
				DiagnosticText.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;
			std::string DiagnosticText;

		};

		FOwnedInputHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetInputFromString("owned-line\n");
		const VeroScript::FRunResult LoadResult = Session.LoadSource(
			"import \"io.vs\";\n"
			"void main() { print(std::io::read_line()); }\n",
			"<owned-stdin>");
		if (!Expect(LoadResult.bSuccess, "SetInputFromString LoadSource should succeed"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}
		const VeroScript::FRunResult RunResult = Session.Run();
		if (!Expect(RunResult.bSuccess, "SetInputFromString Run should succeed"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}
		if (!Expect(Host.PrintText == "owned-line\n", "session should honor SetInputFromString"))
		{
			std::cerr << "got:\n"
					  << Host.PrintText;
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"/* never closed\n"
			"void main() { print(1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unterminated block comment should fail compile"))
		{
			return EXIT_FAILURE;
		}
	}

	// Regression coverage: preserve hard type-parse failures and one module identity.
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(array<>); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "array<> in expression should fail with type error"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { int x = identity<; }\n"
			"T identity<T>(T v) { return v; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "incomplete generic type args should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// Regression coverage: entry-module void main and Check/Run consistency.
	{
		namespace fs = std::filesystem;
		const fs::path TempRoot = fs::temp_directory_path() / "VeroScript-entry-main-test";
		std::error_code Ec;
		fs::remove_all(TempRoot, Ec);
		fs::create_directories(TempRoot, Ec);
		std::ofstream(TempRoot / "lib.vs") << "void main() { print(\"lib\"); }\n";
		std::ofstream(TempRoot / "app.vs")
			<< "import \"lib.vs\";\n"
			<< "void main() { print(\"app\"); }\n";
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "app.vs").string(),
			Output,
			Errors);
		fs::remove_all(TempRoot, Ec);
		if (!Expect(Result.bSuccess && Result.ExitCode == 0, "entry main should win over imported main"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "app\n", "should run entry module main, not imported main"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		namespace fs = std::filesystem;
		const fs::path TempRoot = fs::temp_directory_path() / "VeroScript-lib-main-only-test";
		std::error_code Ec;
		fs::remove_all(TempRoot, Ec);
		fs::create_directories(TempRoot, Ec);
		std::ofstream(TempRoot / "lib.vs") << "void main() { print(\"lib-only\"); }\n";
		std::ofstream(TempRoot / "app.vs") << "import \"lib.vs\";\nint x = 1;\n";
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "app.vs").string(),
			Output,
			Errors);
		fs::remove_all(TempRoot, Ec);
		if (!Expect(Result.bSuccess && Result.ExitCode == 0, "entry without main should not run imported main"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str().empty(), "imported main must not become entry"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::CheckSource(
			"int main() { return 0; }\n",
			"<test>",
			Errors);
		if (!Expect(Result.bSuccess && Result.ExitCode == 0, "int main should pass check/compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int main() { return 0; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess && Result.ExitCode == 0, "int main zero should run successfully"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int also() { return; }\n"
			"void main() { print(also()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "bare return in non-void function should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("non-void function must return a value") != std::string::npos,
				"bare return diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { int huge[2147483647]; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "huge native array should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("native array size exceeds limit") != std::string::npos,
				"huge native array diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::string Source = "void main() { int value";
		for (std::size_t Dimension = 0; Dimension < 65; ++Dimension)
		{
			Source += "[1]";
		}
		Source += "; }\n";

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "excessively deep native array should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
			Errors.str().find("native array dimension count exceeds limit") != std::string::npos,
			"deep native array diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::string Source = "void consume(int";
		for (std::size_t Dimension = 0; Dimension < 65; ++Dimension)
		{
			Source += "[1]";
		}
		Source += " value) {}\nvoid main() {}\n";

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "excessively deep native-array parameter should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
			Errors.str().find("native array dimension count exceeds limit") != std::string::npos,
			"deep native-array parameter diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    int x = 2147483647;\n"
			"    print(x + 1);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "signed add overflow should fail at runtime"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("integer overflow") != std::string::npos,
				"signed add overflow diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int no_return() { }\n"
			"void main() { print(no_return()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "missing return path should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("missing return in function") != std::string::npos,
				"missing return compile diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    int x = 2147483647;\n"
			"    x++;\n"
			"    print(x);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "signed inc overflow should fail at runtime"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("integer overflow") != std::string::npos,
				"signed inc overflow diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int pick(int x) {\n"
			"    switch (x) {\n"
			"        case 1: return 10;\n"
			"        default: return 0;\n"
			"    }\n"
			"}\n"
			"void main() { print(pick(1)); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess && Output.str() == "10\n", "switch with default return paths"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int pick(int x) {\n"
			"    switch (x) {\n"
			"        case 1: return 10;\n"
			"    }\n"
			"}\n"
			"void main() { print(pick(1)); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "switch without default should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("missing return in function") != std::string::npos,
				"switch without default missing return diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    optional<int> empty = optional<int>();\n"
			"    assert(!empty.has_value());\n"
			"    assert(empty.value_or(9) == 9);\n"
			"    optional<int> some = optional<int>(42);\n"
			"    assert(some.has_value());\n"
			"    assert(some.get() == 42);\n"
			"    some.reset();\n"
			"    assert(!some.has_value());\n"
			"    type<int> ti = type<int>();\n"
			"    type<string> ts = type<string>();\n"
			"    assert(ti.name == \"int\");\n"
			"    assert(ts.name == \"string\");\n"
			"}\n",
			"<soft-value-types>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "optional<T> and type<T> soft builtins"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class WeakNode {\n"
			"public:\n"
			"    int value;\n"
			"    WeakNode(int v) { value = v; }\n"
			"}\n"
			"weak<WeakNode> observe_temporary() {\n"
			"    WeakNode temporary = WeakNode(9);\n"
			"    return weak<WeakNode>(temporary);\n"
			"}\n"
			"void main() {\n"
			"    WeakNode owner = WeakNode(7);\n"
			"    weak<WeakNode> observer = weak<WeakNode>(owner);\n"
			"    assert(!observer.expired());\n"
			"    WeakNode locked = observer.lock();\n"
			"    assert(locked.value == 7);\n"
			"    weak<WeakNode> copied = observer;\n"
			"    observer.reset();\n"
			"    assert(observer.expired());\n"
			"    assert(!copied.expired());\n"
			"    assert(copied.lock().value == 7);\n"
			"    copied.reset();\n"
			"    weak<WeakNode> gone = observe_temporary();\n"
			"    assert(gone.expired());\n"
			"}\n",
			"<weak-reference>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "weak<T> non-owning class lifecycle"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class OptionalNode {}\n"
			"void main() {\n"
			"    OptionalNode owner = OptionalNode();\n"
			"    optional<OptionalNode> holder = optional<OptionalNode>(owner);\n"
			"    assert(holder.has_value());\n"
			"    holder.reset();\n"
			"    assert(!holder.has_value());\n"
			"    assert(holder.value_or(nullptr) == nullptr);\n"
			"}\n",
			"<optional-release> ",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "optional<T>.reset releases retained class value"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { weak<int> invalid = weak<int>(); }\n",
			"<invalid-weak-target>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "weak<T> rejects value types"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct optional<T,U> { public: T custom; }\n"
			"void main() {}\n",
			"<reserved-soft-builtin>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "reserved generic builtin names cannot be redeclared"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"using weak = int;\n"
			"void main() {}\n",
			"<reserved-soft-builtin-alias>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "reserved builtin names also reject type aliases"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    multimap<string,int> grouped();\n"
			"    grouped.add(\"a\", 1).add(\"a\", 2).add(\"b\", 3);\n"
			"    assert(len(grouped) == 2);\n"
			"    assert(len(grouped[\"a\"]) == 2 && grouped[\"a\"][1] == 2);\n"
			"    ordered_multimap<string,int> ordered();\n"
			"    ordered.add(\"second\", 2).add(\"first\", 1).add(\"second\", 3);\n"
			"    assert(ordered.keys()[0] == \"second\");\n"
			"    assert(ordered[\"second\"][1] == 3);\n"
			"}\n",
			"<multimap-regression>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "multimap grouped values and ordered key order"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    multiset<int> bag = {2, 1, 2, 2};\n"
			"    assert(len(bag) == 4 && bag.count(2) == 3);\n"
			"    assert(bag.remove(2) && bag.count(2) == 2);\n"
			"    ordered_multiset<string> ordered = {\"b\", \"a\", \"b\"};\n"
			"    assert(ordered.values()[0] == \"b\");\n"
			"    assert(ordered.values()[1] == \"b\");\n"
			"    assert(ordered.values()[2] == \"a\");\n"
			"}\n",
			"<multiset-regression>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "multiset counts and ordered expansion"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    priority_queue<int> q = {3, 1, 5, 2};\n"
			"    assert(q.top() == 5 && q.pop() == 5);\n"
			"    q.push(9).push(4);\n"
			"    assert(q.pop() == 9 && q.pop() == 4 && q.top() == 3);\n"
			"}\n",
			"<priority-queue-regression>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "priority_queue maximum-first ordering"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { priority_queue<double> invalid(); }\n",
			"<priority-queue-invalid-element>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "priority_queue rejects non-total float ordering"))
		{
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
