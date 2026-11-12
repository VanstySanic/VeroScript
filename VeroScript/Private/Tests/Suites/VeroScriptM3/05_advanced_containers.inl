// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptM3Tests / advanced containers suite. Included by the owning translation unit.

	if (!ExpectOutput(
			"void main() {\n"
			"    map<wstring,int> m = map<wstring,int>();\n"
			"    m[L\"a\"] = 1;\n"
			"    m[L\"b\"] = 2;\n"
			"    print(m[L\"a\"]);\n"
			"    print(m.has(L\"b\"));\n"
			"    print(m.remove(L\"a\"));\n"
			"    print(m.has(L\"a\"));\n"
			"    print(len(m));\n"
			"    array<wstring> k = m.keys();\n"
			"    print(len(k));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\ntrue\ntrue\nfalse\n1\n1\n",
			"map<wstring,int> get set has remove keys len"))
	{
		return EXIT_FAILURE;
	}

	// v1.18: set<wstring>
	if (!ExpectOutput(
			"void main() {\n"
			"    set<wstring> s = set<wstring>();\n"
			"    s.add(L\"x\");\n"
			"    s.add(L\"y\");\n"
			"    s.add(L\"x\");\n"
			"    print(s.has(L\"x\"));\n"
			"    print(s.remove(L\"y\"));\n"
			"    print(s.has(L\"y\"));\n"
			"    print(len(s));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\ntrue\nfalse\n1\n",
			"set<wstring> add has remove len"))
	{
		return EXIT_FAILURE;
	}

	// v1.18: ordered_map<wstring,int> insertion order
	if (!ExpectOutput(
			"void main() {\n"
			"    ordered_map<wstring,int> m = ordered_map<wstring,int>();\n"
			"    m[L\"a\"] = 1;\n"
			"    m[L\"b\"] = 2;\n"
			"    m[L\"c\"] = 3;\n"
			"    m[L\"b\"] = 20;\n"
			"    m.remove(L\"a\");\n"
			"    array<wstring> k = m.keys();\n"
			"    print(len(k));\n"
			"    print(k[0]);\n"
			"    print(k[1]);\n"
			"    print(m[L\"b\"]);\n"
			"    print(m[L\"c\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\nb\nc\n20\n3\n",
			"ordered_map<wstring,int> insertion order"))
	{
		return EXIT_FAILURE;
	}

	// v1.18: reject string key on wstring map
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<wstring,int> m = map<wstring,int>();\n"
			"    m[\"a\"] = 1;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "map<wstring> with string key should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.18: deque subscript read/write
	if (!ExpectOutput(
			"void main() {\n"
			"    deque<int> d = deque<int>();\n"
			"    d.push_back(10);\n"
			"    d.push_back(20);\n"
			"    d.push_back(30);\n"
			"    print(d[0]);\n"
			"    print(d[2]);\n"
			"    d[1] = 99;\n"
			"    print(d[1]);\n"
			"    print(d.front());\n"
			"    print(d.back());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"10\n30\n99\n10\n30\n",
			"deque subscript read write"))
	{
		return EXIT_FAILURE;
	}

	// v1.18: deque index out of bounds
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    deque<int> d = deque<int>();\n"
			"    d.push_back(1);\n"
			"    print(d[1]);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "deque index out of bounds should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("deque index out of bounds") != std::string::npos,
				"deque index out of bounds message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	// v1.19: range-for deque
	if (!ExpectOutput(
			"void main() {\n"
			"    deque<int> d = deque<int>();\n"
			"    d.push_back(1);\n"
			"    d.push_back(2);\n"
			"    d.push_back(3);\n"
			"    int sum = 0;\n"
			"    for (int x : d) { sum = sum + x; }\n"
			"    print(sum);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"6\n",
			"range-for deque"))
	{
		return EXIT_FAILURE;
	}

	// v1.19: deque foreach snapshots — shrink during loop must not OOB
	if (!ExpectOutput(
			"void main() {\n"
			"    deque<int> d = deque<int>();\n"
			"    d.push_back(1);\n"
			"    d.push_back(2);\n"
			"    d.push_back(3);\n"
			"    int n = 0;\n"
			"    for (int x : d) {\n"
			"        n = n + 1;\n"
			"        d.pop_back();\n"
			"    }\n"
			"    print(n);\n"
			"    print(len(d));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n0\n",
			"range-for deque shrink safe snapshot"))
	{
		return EXIT_FAILURE;
	}

	// v1.19: deque.values()
	if (!ExpectOutput(
			"void main() {\n"
			"    deque<int> d = deque<int>();\n"
			"    d.push_back(7);\n"
			"    d.push_front(3);\n"
			"    array<int> v = d.values();\n"
			"    print(v[0]);\n"
			"    print(v[1]);\n"
			"    print(len(v));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n7\n2\n",
			"deque values method"))
	{
		return EXIT_FAILURE;
	}

	// v1.19: range-for set
	if (!ExpectOutput(
			"void main() {\n"
			"    set<int> s = set<int>();\n"
			"    s.add(1);\n"
			"    s.add(2);\n"
			"    s.add(1);\n"
			"    int sum = 0;\n"
			"    for (int x : s) { sum = sum + x; }\n"
			"    print(sum);\n"
			"    print(len(s));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n2\n",
			"range-for set"))
	{
		return EXIT_FAILURE;
	}

	// v1.19: range-for queue / stack
	if (!ExpectOutput(
			"void main() {\n"
			"    queue<int> q = queue<int>();\n"
			"    q.push(10);\n"
			"    q.push(20);\n"
			"    int qs = 0;\n"
			"    for (int x : q) { qs = qs + x; }\n"
			"    print(qs);\n"
			"    print(len(q));\n"
			"    stack<int> st = stack<int>();\n"
			"    st.push(3);\n"
			"    st.push(4);\n"
			"    int ss = 0;\n"
			"    for (int x : st) { ss = ss + x; }\n"
			"    print(ss);\n"
			"    print(len(st));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"30\n2\n7\n2\n",
			"range-for queue and stack snapshot"))
	{
		return EXIT_FAILURE;
	}

	// v1.19: ordered_map values() insertion order + range-for via values
	if (!ExpectOutput(
			"void main() {\n"
			"    ordered_map<string,int> m = ordered_map<string,int>();\n"
			"    m[\"a\"] = 1;\n"
			"    m[\"b\"] = 2;\n"
			"    m[\"c\"] = 3;\n"
			"    array<int> vs = m.values();\n"
			"    print(vs[0]);\n"
			"    print(vs[1]);\n"
			"    print(vs[2]);\n"
			"    int sum = 0;\n"
			"    for (int v : m.values()) { sum = sum + v; }\n"
			"    print(sum);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n2\n3\n6\n",
			"ordered_map values and range-for"))
	{
		return EXIT_FAILURE;
	}

	// v1.19: map values() + set.values()
	if (!ExpectOutput(
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m[\"x\"] = 5;\n"
			"    array<int> vs = m.values();\n"
			"    print(len(vs));\n"
			"    print(vs[0]);\n"
			"    set<string> s = set<string>();\n"
			"    s.add(\"a\");\n"
			"    array<string> elems = s.values();\n"
			"    print(len(elems));\n"
			"    print(elems[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n5\n1\na\n",
			"map and set values method"))
	{
		return EXIT_FAILURE;
	}

	// v2.38: direct range-for on map iterates values (by-value copy)
	if (!ExpectOutput(
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m[\"a\"] = 1;\n"
			"    m[\"b\"] = 2;\n"
			"    int sum = 0;\n"
			"    for (int x : m) { sum = sum + x; }\n"
			"    print(sum);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"range-for on map iterates values"))
	{
		return EXIT_FAILURE;
	}

	// v1.20: array / set / deque brace literals
	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> a = {1, 2, 3};\n"
			"    print(len(a));\n"
			"    print(a[0]);\n"
			"    print(a[2]);\n"
			"    set<int> s = {1, 2, 2};\n"
			"    print(len(s));\n"
			"    print(s.has(1));\n"
			"    deque<int> d = {10, 20};\n"
			"    print(d[0]);\n"
			"    print(d[1]);\n"
			"    print(len(d));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n1\n3\n2\ntrue\n10\n20\n2\n",
			"container list brace literals"))
	{
		return EXIT_FAILURE;
	}

	// v1.20: map / ordered_map brace literals
	if (!ExpectOutput(
			"void main() {\n"
			"    map<string,int> m = {\"a\": 1, \"b\": 2};\n"
			"    print(m[\"a\"]);\n"
			"    print(m[\"b\"]);\n"
			"    print(len(m));\n"
			"    ordered_map<string,int> om = {\"x\": 9, \"y\": 8};\n"
			"    array<string> k = om.keys();\n"
			"    print(k[0]);\n"
			"    print(k[1]);\n"
			"    print(om[\"x\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n2\n2\nx\ny\n9\n",
			"map and ordered_map brace literals"))
	{
		return EXIT_FAILURE;
	}

	// v1.20: empty brace literals
	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> a = {};\n"
			"    map<string,int> m = {};\n"
			"    set<string> s = {};\n"
			"    print(len(a));\n"
			"    print(len(m));\n"
			"    print(len(s));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n0\n0\n",
			"empty container brace literals"))
	{
		return EXIT_FAILURE;
	}

	// v1.20: queue/stack brace + wstring map
	if (!ExpectOutput(
			"void main() {\n"
			"    queue<int> q = {1, 2};\n"
			"    print(q.front());\n"
			"    print(len(q));\n"
			"    stack<int> st = {3, 4};\n"
			"    print(st.top());\n"
			"    map<wstring,int> wm = {L\"k\": 5};\n"
			"    print(wm[L\"k\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n2\n4\n5\n",
			"queue stack and wstring map brace literals"))
	{
		return EXIT_FAILURE;
	}

	// v1.20: list brace on map should fail
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = {1, 2};\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "list brace on map should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.21: nested array + nested map
	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> row0 = {1, 2};\n"
			"    array<int> row1 = {3};\n"
			"    array<array<int>> aa = array<array<int>>();\n"
			"    aa.push(row0);\n"
			"    aa.push(row1);\n"
			"    print(aa[0][1]);\n"
			"    print(aa[1][0]);\n"
			"    map<string,map<string,int>> mm = {};\n"
			"    map<string,int> inner = {\"x\": 9};\n"
			"    mm[\"a\"] = inner;\n"
			"    print(mm[\"a\"][\"x\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n3\n9\n",
			"nested array and map containers"))
	{
		return EXIT_FAILURE;
	}

	// v1.22: clear() on containers + shared handle
	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> a = {1, 2, 3};\n"
			"    array<int> b = a;\n"
			"    a.clear();\n"
			"    print(len(a));\n"
			"    print(len(b));\n"
			"    map<string,int> m = {\"k\": 1};\n"
			"    m.clear();\n"
			"    print(len(m));\n"
			"    ordered_map<int,int> om = {1: 10, 2: 20};\n"
			"    om.clear();\n"
			"    print(len(om));\n"
			"    print(len(om.keys()));\n"
			"    set<int> s = {1, 2};\n"
			"    s.clear();\n"
			"    print(len(s));\n"
			"    queue<int> q = {1, 2};\n"
			"    q.clear();\n"
			"    print(len(q));\n"
			"    stack<int> st = {1, 2};\n"
			"    st.clear();\n"
			"    print(len(st));\n"
			"    deque<int> d = {1, 2};\n"
			"    d.clear();\n"
			"    print(len(d));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n0\n0\n0\n0\n0\n0\n0\n0\n",
			"container clear and shared handle"))
	{
		return EXIT_FAILURE;
	}

	// v1.23: const local / param / field / container mutator
	if (!ExpectOutput(
			"struct Point { const int x = 1; int y = 2; }\n"
			"int read_const(const int n) { return n; }\n"
			"void main() {\n"
			"    const int a = 3;\n"
			"    print(a);\n"
			"    print(read_const(a));\n"
			"    Point p = Point();\n"
			"    print(p.x);\n"
			"    p.y = 9;\n"
			"    print(p.y);\n"
			"    const array<int> arr = {1, 2};\n"
			"    print(len(arr));\n"
			"    print(arr[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n3\n1\n9\n2\n1\n",
			"const read paths"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Payload {\n"
			"    int sum = 0;\n"
			"    Payload(int left, int right) { sum = left + right; }\n"
			"}\n"
			"class BaseValue {\n"
			"public:\n"
			"    int base_value = 0;\n"
			"    BaseValue(int input) { base_value = input; }\n"
			"}\n"
			"class ConstHolder : BaseValue {\n"
			"public:\n"
			"    const int fixed = 1;\n"
			"    const int value;\n"
			"    const Payload payload;\n"
			"    ConstHolder(int input) : BaseValue(input + 1), payload(input, 2), value(input) {}\n"
			"    int total() const { return fixed + value + payload.sum + base_value; }\n"
			"}\n"
			"struct External { const int value; External(int input); }\n"
			"External::External(int input) : value(input) {}\n"
			"void main() {\n"
			"    ConstHolder holder = ConstHolder(5);\n"
			"    External external = External(9);\n"
			"    print(holder.total());\n"
			"    print(external.value);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"19\n9\n",
			"constructor initializer list initializes direct const fields"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"int constructor_init_order = 0;\n"
			"int mark_constructor_init(int digit) {\n"
			"    constructor_init_order = constructor_init_order * 10 + digit;\n"
			"    return digit;\n"
			"}\n"
			"class InitOrderBase {\n"
			"public:\n"
			"    InitOrderBase(int ignored) {}\n"
			"}\n"
			"class InitOrderDerived : InitOrderBase {\n"
			"public:\n"
			"    int first;\n"
			"    int second;\n"
			"    InitOrderDerived() : second(mark_constructor_init(2)), InitOrderBase(mark_constructor_init(9)), first(mark_constructor_init(1)) {}\n"
			"}\n"
			"void main() {\n"
			"    InitOrderDerived value = InitOrderDerived();\n"
			"    print(constructor_init_order);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"912\n",
			"constructor initializer execution order"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct DefaultToken {\n"
			"    int value = 3;\n"
			"    DefaultToken() { value = 4; }\n"
			"}\n"
			"struct GenericFieldBox<T> {\n"
			"    const T value;\n"
			"    GenericFieldBox(T input) : value(input) {}\n"
			"}\n"
			"struct FieldInitDefaults {\n"
			"    const int explicit_zero;\n"
			"    const int implicit_zero;\n"
			"    const string explicit_text;\n"
			"    int mutable_value;\n"
			"    const DefaultToken token;\n"
			"    FieldInitDefaults(int input) : token(), mutable_value(input), explicit_text(), explicit_zero() {}\n"
			"}\n"
			"void main() {\n"
			"    GenericFieldBox<int> box = GenericFieldBox<int>(7);\n"
			"    FieldInitDefaults defaults = FieldInitDefaults(9);\n"
			"    print(box.value);\n"
			"    print(defaults.explicit_zero);\n"
			"    print(defaults.implicit_zero);\n"
			"    print(len(defaults.explicit_text));\n"
			"    print(defaults.mutable_value);\n"
			"    print(defaults.token.value);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n0\n0\n0\n9\n4\n",
			"constructor field initializer generic and default semantics"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct DeclarationInit {\n"
			"    int value;\n"
			"    DeclarationInit() : value(1);\n"
			"}\n"
			"DeclarationInit::DeclarationInit() {}\n"
			"void main() {}\n",
			"<constructor-initializer-on-declaration>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "constructor initializer list on a declaration should fail")
			|| !Expect(
				Errors.str().find("requires a constructor body") != std::string::npos,
				"constructor declaration initializer list should explain that a body is required"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct DeletedInit {\n"
			"    int value;\n"
			"    DeletedInit() : value(1) = delete;\n"
			"}\n"
			"void main() {}\n",
			"<constructor-initializer-on-deleted-constructor>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "deleted constructor with an initializer list should fail")
			|| !Expect(
				Errors.str().find("requires a constructor body") != std::string::npos,
				"deleted constructor initializer list should explain that a body is required"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct DuplicateSource {\n"
			"    const int value = 1;\n"
			"    DuplicateSource() : value(2) {}\n"
			"}\n"
			"void main() {}\n",
			"<const-field-declaration-and-constructor-init>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "field declaration and constructor initializer should conflict")
			|| !Expect(
				Errors.str().find("already has a declaration initializer") != std::string::npos,
				"field initializer conflict should identify the declaration initializer"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct DuplicateFieldInit {\n"
			"    const int value;\n"
			"    DuplicateFieldInit() : value(1), value(2) {}\n"
			"}\n"
			"void main() {}\n",
			"<duplicate-const-field-constructor-init>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "duplicate field constructor initializer should fail")
			|| !Expect(
				Errors.str().find("duplicate constructor initializer") != std::string::npos,
				"duplicate field initializer should report its target"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct ScalarFieldInit {\n"
			"    const int value;\n"
			"    ScalarFieldInit() : value(1, 2) {}\n"
			"}\n"
			"void main() {}\n",
			"<scalar-field-constructor-init-arity>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "scalar field constructor initializer with two arguments should fail")
			|| !Expect(
				Errors.str().find("expects at most one argument") != std::string::npos,
				"scalar field initializer should report its argument limit"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct StaticFieldInit {\n"
			"    static int value;\n"
			"    StaticFieldInit() : value(1) {}\n"
			"}\n"
			"void main() {}\n",
			"<static-field-constructor-init>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "static field constructor initializer should fail")
			|| !Expect(
				Errors.str().find("cannot target static field") != std::string::npos,
				"static field initializer should report its target"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    const int a = 1;\n"
			"    a = 2;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const local assign should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot assign to const") != std::string::npos,
				"const local assign should report cannot assign to const"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void bump(const int n) { n = n + 1; }\n"
			"void main() { bump(1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const param assign should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Point { const int x = 1; }\n"
			"void main() {\n"
			"    Point p = Point();\n"
			"    p.x = 2;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const field assign should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    const array<int> a = {1};\n"
			"    a.push(2);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const array push should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    const array<int> a = {1};\n"
			"    a[0] = 2;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const array index assign should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.23 must-fix: mutable local shadowing const field must still assign
	if (!ExpectOutput(
			"void main() { int mutable = 7; print(mutable); }\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"mutable remains a valid identifier outside a field modifier"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectOutput(
			"struct mutable { int value; };\n"
			"struct Holder { mutable item; };\n"
			"void main() { Holder h = Holder(); h.item.value = 9; print(h.item.value); }\n",
			VeroScript::ERunMode::Script,
			"9\n",
			"contextual mutable preserves an existing user type spelling"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct RootType {\n"
			"  int value = 0;\n"
			"  static int Marker = 50;\n"
			"  RootType(int input) { value = input; }\n"
			"};\n"
			"using RootAlias = RootType;\n"
			"struct RootBox<T> { T value; RootBox(T input) { value = input; } };\n"
			"int RootValue = 10;\n"
			"int choose() { return 20; }\n"
			"enum class RootFlag { Ready = 30 };\n"
			"namespace RootNs {\n"
			"  int choose() { return 40; }\n"
			"  int imported() { return 41; }\n"
			"}\n"
			"namespace Inner {\n"
			"  struct RootType { int value = -1; };\n"
			"  struct RootBox<T> { T value; };\n"
			"  int choose() { return -20; }\n"
			"  enum class RootFlag { Ready = -30 };\n"
			"  namespace RootNs { int choose() { return -40; } }\n"
			"  using namespace ::RootNs;\n"
			"  void run() {\n"
			"    using RootAlias = RootType;\n"
			"    int RootValue = -10;\n"
			"    ::RootType item(1);\n"
			"    ::RootAlias aliased(2);\n"
			"    ::RootBox<int> box(60);\n"
			"    print(item.value);\n"
			"    print(aliased.value);\n"
			"    print(::RootValue);\n"
			"    print(::choose());\n"
			"    print(cast<int>(::RootFlag::Ready));\n"
			"    print(::RootType::Marker);\n"
			"    print(::RootNs::choose());\n"
			"    print(imported());\n"
			"    print(box.value);\n"
			"  }\n"
			"}\n"
			"void main() { Inner::run(); }\n",
			VeroScript::ERunMode::Script,
			"1\n2\n10\n20\n30\n50\n40\n41\n60\n",
			"leading global scope qualifier bypasses lexical and using lookup"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void bump(ref int value) { value += 1; }\n"
			"struct Cache { int value = 0; }\n"
			"struct Counter {\n"
			"    mutable Cache cache = Cache();\n"
			"    mutable int reads = 0;\n"
			"    int read() const { cache.value += 1; bump(ref reads); return cache.value + reads; }\n"
			"}\n"
			"void main() {\n"
			"    const Counter counter = Counter();\n"
			"    print(counter.read());\n"
			"    print(counter.read());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n4\n",
			"mutable field may be written through a const receiver"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Counter {\n"
			"    int value = 0;\n"
			"    void write() const { value = 1; }\n"
			"}\n"
			"void main() {}\n",
			"<mutable-nonmutable-field>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "non-mutable field write in const method should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Invalid { static mutable int value = 0; }\n",
			"<mutable-static-field>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "static mutable field should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct S {\n"
			"    const int x = 1;\n"
			"    int m() {\n"
			"        int x = 0;\n"
			"        x = 2;\n"
			"        return x;\n"
			"    }\n"
			"}\n"
			"void main() {\n"
			"    S s = S();\n"
			"    print(s.m());\n"
			"    print(s.x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n1\n",
			"mutable local shadows const field"))
	{
		return EXIT_FAILURE;
	}

	// v1.23 must-fix: mutable this-field must win over const global same name
	if (!ExpectOutput(
			"const int x = 1;\n"
			"struct T {\n"
			"    int x = 0;\n"
			"    void m() { x = 2; }\n"
			"}\n"
			"void main() {\n"
			"    T t = T();\n"
			"    t.m();\n"
			"    print(t.x);\n"
			"    print(x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n1\n",
			"mutable this-field over const global"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"const int g = 1;\n"
			"void main() { g = 2; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const global assign should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.24: ref parameter aliases caller local
	if (!ExpectOutput(
			"void bump(ref int x) { x = x + 1; }\n"
			"void main() {\n"
			"    int n = 10;\n"
			"    bump(ref n);\n"
			"    print(n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n",
			"ref param mutates caller"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Point { int x = 0; }\n"
			"void move(ref Point p) { p.x = 7; }\n"
			"void main() {\n"
			"    Point p = Point();\n"
			"    move(ref p);\n"
			"    print(p.x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"ref struct param"))
	{
		return EXIT_FAILURE;
	}

	// Range-for ref: mutate struct elements in place (no CloneStruct per iteration).
	if (!ExpectOutput(
			"struct Point { int x = 0; }\n"
			"void main() {\n"
			"    array<Point> pts = array<Point>();\n"
			"    pts.push(Point());\n"
			"    pts.push(Point());\n"
			"    for (ref Point p : pts) { p.x = p.x + 3; }\n"
			"    for (Point p : pts) { print(p.x); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n3\n",
			"ref range-for mutates struct elements"))
	{
		return EXIT_FAILURE;
	}

	// Native T[N] is a value type: ref foreach must alias the local, not a CloneStruct copy.
	if (!ExpectOutput(
			"void main() {\n"
			"    int xs[2] = {1, 2};\n"
			"    for (ref int x : xs) { x = x + 10; }\n"
			"    print(xs[0]);\n"
			"    print(xs[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n12\n",
			"ref range-for mutates native array"))
	{
		return EXIT_FAILURE;
	}

	// Native multi-dim T[N][M]: nested FNativeArray, IndexRef chains, row foreach.
	if (!ExpectOutput(
			"void bump(ref int v) { v = v + 1; }\n"
			"void main() {\n"
			"    int grid[2][3] = { {1, 2, 3}, {4, 5, 6} };\n"
			"    print(len(grid));\n"
			"    print(len(grid[0]));\n"
			"    print(grid[1][2]);\n"
			"    grid[0][1] = 9;\n"
			"    bump(ref grid[1][0]);\n"
			"    print(grid[0][1]);\n"
			"    print(grid[1][0]);\n"
			"    for (ref int row[3] : grid) { row[2] = row[2] + 10; }\n"
			"    print(grid[0][2]);\n"
			"    print(grid[1][2]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n3\n6\n9\n5\n13\n16\n",
			"native multi-dim T[N][M]"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectOutput(
			"void fill(ref int m[2][2]) {\n"
			"    m[0][0] = 1; m[0][1] = 2;\n"
			"    m[1][0] = 3; m[1][1] = 4;\n"
			"}\n"
			"void main() {\n"
			"    int m[2][2];\n"
			"    fill(ref m);\n"
			"    print(m[1][1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n",
			"native multi-dim ref parameter"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectOutput(
			"void main() {\n"
			"    int cube[2][2][2] = {\n"
			"        { {1, 2}, {3, 4} },\n"
			"        { {5, 6}, {7, 8} }\n"
			"    };\n"
			"    print(cube[1][0][1]);\n"
			"    cube[0][1][0] = 9;\n"
			"    print(cube[0][1][0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"6\n9\n",
			"native 3D T[N][M][P]"))
	{
		return EXIT_FAILURE;
	}
	// Struct/class field multi-dim: public MakeFieldRef seeds GetIndexRef into object field storage.
	if (!ExpectOutput(
			"struct S { int cells[2][2] = { {1, 2}, {3, 4} }; }\n"
			"void bump(ref int v) { v = v + 1; }\n"
			"void main() {\n"
			"    S s = S();\n"
			"    s.cells[1][0] = 9;\n"
			"    bump(ref s.cells[0][1]);\n"
			"    print(s.cells[1][0]);\n"
			"    print(s.cells[0][1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9\n3\n",
			"struct field native multi-dim assign and ref"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectOutput(
			"int g[2][2] = { {1, 2}, {3, 4} };\n"
			"void bump(ref int v) { v = v + 10; }\n"
			"void main() {\n"
			"    bump(ref g[1][0]);\n"
			"    g[0][1] += 5;\n"
			"    print(g[1][0]);\n"
			"    print(g[0][1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"13\n7\n",
			"global native multi-dim IndexRef"))
	{
		return EXIT_FAILURE;
	}

	// ref xs[i] call argument
	if (!ExpectOutput(
			"struct Point { int x = 0; }\n"
			"void bump(ref Point p) { p.x = p.x + 1; }\n"
			"void main() {\n"
			"    array<Point> pts = array<Point>();\n"
			"    pts.push(Point());\n"
			"    bump(ref pts[0]);\n"
			"    print(pts[0].x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"ref index argument"))
	{
		return EXIT_FAILURE;
	}

	// GetIndexRef must accept unsigned indices like GetIndexCopy.

	// Chained GetIndexRef: ref a[i][j] and nested assign write-through.
	if (!ExpectOutput(
			"void bump(ref int v) { v = v + 1; }\n"
			"void main() {\n"
			"    array<array<int>> xs = { {1, 2}, {3, 4} };\n"
			"    bump(ref xs[1][0]);\n"
			"    xs[0][1] = 9;\n"
			"    print(xs[1][0]);\n"
			"    print(xs[0][1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n9\n",
			"chained ref index and nested assign"))
	{
		return EXIT_FAILURE;
	}

	// Global / global-nested GetIndexRef (MakeGlobalRef seed + BindLocal).
	if (!ExpectOutput(
			"array<array<int>> g = { {1, 2}, {3, 4} };\n"
			"void bump(ref int v) { v = v + 1; }\n"
			"void main() {\n"
			"    bump(ref g[1][0]);\n"
			"    g[0][1] += 5;\n"
			"    print(g[1][0]);\n"
			"    print(g[0][1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n7\n",
			"global chained ref index and nested compound"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectOutput(
			"int g[2] = {1, 2};\n"
			"void bump(ref int v) { v = v + 10; }\n"
			"void main() {\n"
			"    bump(ref g[0]);\n"
			"    print(g[0]);\n"
			"    print(g[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n2\n",
			"global native T[N] ref index"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectOutput(
			"void bump(ref int v) { v = v + 1; }\n"
			"class Holder {\n"
			"    public:\n"
			"    array<array<int>> xs = { {1, 2}, {3, 4} };\n"
			"    void run() {\n"
			"        bump(ref xs[1][0]);\n"
			"        print(xs[1][0]);\n"
			"    }\n"
			"}\n"
			"void main() {\n"
			"    Holder h = Holder();\n"
			"    h.run();\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n",
			"this-field chained ref index"))
	{
		return EXIT_FAILURE;
	}

	// Native T[N] row as value type: nested IndexRef write-back via local row alias.
	if (!ExpectOutput(
			"void bump(ref int v) { v = v + 10; }\n"
			"void main() {\n"
			"    int row[2] = {1, 2};\n"
			"    bump(ref row[0]);\n"
			"    print(row[0]);\n"
			"    print(row[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n2\n",
			"native T[N] ref index still works"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectOutput(
			"void bump(ref int x) { x = x + 1; }\n"
			"void main() {\n"
			"    array<int> xs = array<int>();\n"
			"    xs.push(10);\n"
			"    unsigned u = 0;\n"
			"    bump(ref xs[u]);\n"
			"    print(xs[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n",
			"ref index with unsigned"))
	{
		return EXIT_FAILURE;
	}

	// `&` is a synonym for `ref` (prefix, trailing Type&, and call-site &x).
	if (!ExpectOutput(
			"void bump(& int x) { x = x + 1; }\n"
			"void bump2(int& y) { y = y + 10; }\n"
			"int read(const int& value) { return value; }\n"
			"void main() {\n"
			"    int n = 1;\n"
			"    bump(&n);\n"
			"    bump2(&n);\n"
			"    print(n);\n"
			"    print(read(&n));\n"
			"    int xs[2] = {1, 2};\n"
			"    for (int& v : xs) { v = v + 1; }\n"
			"    print(xs[0]);\n"
			"    print(xs[1]);\n"
			"    print(3 & 1);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"12\n12\n2\n3\n1\n",
			"ampersand as ref synonym; bitand still works"))
	{
		return EXIT_FAILURE;
	}

	// By-value range-for still copies (does not mutate source).
	if (!ExpectOutput(
			"struct Point { int x = 0; }\n"
			"void main() {\n"
			"    array<Point> pts = array<Point>();\n"
			"    pts.push(Point());\n"
			"    for (Point p : pts) { p.x = 9; }\n"
			"    print(pts[0].x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"value range-for copies struct"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    set<int> s = set<int>();\n"
			"    s.add(1);\n"
			"    for (ref int x : s) { x = 2; }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "ref range-for on set should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v2.38: map/ordered_map MapEntryRef + value ref foreach; deque ref foreach
	if (!ExpectOutput(
			"void bump(ref int x) { x = x + 1; }\n"
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m[\"a\"] = 40;\n"
			"    bump(ref m[\"a\"]);\n"
			"    bump(&m[\"a\"]);\n"
			"    print(m[\"a\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"42\n",
			"ref / & map entry write-through"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    ordered_map<string,int> m = ordered_map<string,int>();\n"
			"    m[\"x\"] = 10;\n"
			"    m[\"y\"] = 20;\n"
			"    for (ref int v : m) { v = v + 1; }\n"
			"    print(m[\"x\"]);\n"
			"    print(m[\"y\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n21\n",
			"ref range-for on ordered_map writes values"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void bump_nested(ref int x) { x = x + 10; }\n"
			"void main() {\n"
			"    map<string,array<int>> m = map<string,array<int>>();\n"
			"    array<int> row = array<int>();\n"
			"    row.push(1);\n"
			"    row.push(2);\n"
			"    m[\"r\"] = row;\n"
			"    bump_nested(ref m[\"r\"][1]);\n"
			"    print(m[\"r\"][1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"12\n",
			"nested ref m[k][i] MapEntryRef + IndexRef"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    deque<int> d = deque<int>();\n"
			"    d.push_back(1);\n"
			"    d.push_back(2);\n"
			"    for (ref int x : d) { x = x + 10; }\n"
			"    print(d[0]);\n"
			"    print(d[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n12\n",
			"ref range-for on deque write-through"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = {\"a\": 1};\n"
			"    for (ref int v : m.values()) { v = 2; }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "ref range-for on map.values() snapshot should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v2.39: trailing const methods + builtin pair + map.entries()
	if (!ExpectOutput(
			"class Box {\n"
			"    public:\n"
			"    int n = 7;\n"
			"    int get() const { return n; }\n"
			"    void set(int v) { n = v; }\n"
			"}\n"
			"void main() {\n"
			"    const Box b = Box();\n"
			"    print(b.get());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"const object can call const method"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct ReadonlyCounter {\n"
			"    int value = 1;\n"
			"    void set_value(int v) { value = v; }\n"
			"}\n"
			"struct ReadonlyRequired {\n"
			"    int value;\n"
			"    ReadonlyRequired(int v) { value = v; }\n"
			"}\n"
			"class ReadonlyBox {\n"
			"    public:\n"
			"    int value = 1;\n"
			"    int get_value() const { return value; }\n"
			"    void set_value(int v) { value = v; }\n"
			"    void force_value(int v) const { const_cast<ReadonlyBox>(this).set_value(v); }\n"
			"}\n"
			"const array<int> SharedLocked = {5};\n"
			"const array<int>& shared_locked() { return SharedLocked; }\n"
			"void main() {\n"
			"    ReadonlyBox box = ReadonlyBox();\n"
			"    readonly<ReadonlyBox> view = readonly<ReadonlyBox>(box);\n"
			"    print(view.get().get_value());\n"
			"    std::readonly<ReadonlyBox> std_view = std::readonly<ReadonlyBox>(box);\n"
			"    print(std_view.get().get_value());\n"
			"    readonly<int> implicit_value = 41;\n"
			"    print(implicit_value.get());\n"
			"    readonly<ReadonlyRequired> required = ReadonlyRequired(13);\n"
			"    print(required.get().value);\n"
			"    const array<readonly<int>> locked_values = {implicit_value};\n"
			"    const_cast<array<readonly<int>>>(locked_values).push(readonly<int>(42));\n"
			"    print(len(locked_values));\n"
			"    const_cast<ReadonlyBox>(view.get()).set_value(7);\n"
			"    print(view.get().get_value());\n"
			"    box.force_value(9);\n"
			"    print(view.get().get_value());\n"
			"    array<readonly<ReadonlyBox>> views = {view};\n"
			"    print(views[0].get().get_value());\n"
			"    const array<int> locked = {1};\n"
			"    const_cast<array<int>>(locked).push(2);\n"
			"    print(len(locked));\n"
			"    readonly<ReadonlyCounter> counter = readonly<ReadonlyCounter>(ReadonlyCounter());\n"
			"    const_cast<ReadonlyCounter>(counter.get()).set_value(12);\n"
			"    print(counter.get().value);\n"
			"    const_cast<array<int>>(shared_locked()).push(6);\n"
			"    print(len(SharedLocked));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n1\n41\n13\n2\n7\n9\n9\n2\n12\n2\n",
			"readonly wrapper and explicit const_cast"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { readonly<int> value; print(value.get()); }\n",
			"<readonly-requires-value>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "readonly<T> requires an initial value"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class Box { public: void mutate() {} }\n"
			"void main() { Box box = Box(); readonly<Box> view = readonly<Box>(box); "
			"view.get().mutate(); }\n",
			"<readonly-mutation>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "readonly<T>.get() rejects mutating methods"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { const int value = 1; double bad = const_cast<double>(value); }\n",
			"<const-cast-type-change>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const_cast cannot change the value type"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { array<int> values = {1}; const_cast<array<int>>(values).clear(); }\n",
			"<const-cast-non-const>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const_cast requires a const source"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { const array<int> values = {1}; cast<array<int>>(values).clear(); }\n",
			"<ordinary-cast-const-escape>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "ordinary cast must preserve const access"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class Box {\n"
			"    public:\n"
			"    int n = 0;\n"
			"    int get() const { return n; }\n"
			"    void set(int v) { n = v; }\n"
			"}\n"
			"void main() {\n"
			"    const Box b = Box();\n"
			"    b.set(1);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const object cannot call non-const method"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class Box {\n"
			"    public:\n"
			"    int n = 0;\n"
			"    void bump() const { n = n + 1; }\n"
			"}\n"
			"void main() { Box b = Box(); b.bump(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const method cannot write fields"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"class Box {\n"
			"    public:\n"
			"    int n = 1;\n"
			"    int tag() const { return 10; }\n"
			"    int tag() { return 20; }\n"
			"}\n"
			"void main() {\n"
			"    Box m = Box();\n"
			"    const Box c = Box();\n"
			"    print(m.tag());\n"
			"    print(c.tag());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"20\n10\n",
			"const/non-const method overload selection"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"interface Reader {\n"
			"    virtual int read() const = 0;\n"
			"}\n"
			"class Counter : public Reader{\n"
			"    public:\n"
			"    int n = 3;\n"
			"    int read() const { return n; }\n"
			"}\n"
			"void main() {\n"
			"    const Counter c = Counter();\n"
			"    print(c.read());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"interface const method + implement"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"class Base {\n"
			"    public:\n"
			"    int tag() const { return 10; }\n"
			"    int tag() { return 20; }\n"
			"}\n"
			"class Derived : public Base {}\n"
			"void main() {\n"
			"    Derived m = Derived();\n"
			"    const Derived c = Derived();\n"
			"    print(m.tag());\n"
			"    print(c.tag());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"20\n10\n",
			"inherited const/non-const overload with const receiver"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"class Base {\n"
			"    public:\n"
			"    int n = 1;\n"
			"    virtual int value() const { return n; }\n"
			"    virtual void set(int v) { n = v; }\n"
			"}\n"
			"class Child : public Base{\n"
			"    public:\n"
			"    int value() const override { return n + 100; }\n"
			"}\n"
			"void main() {\n"
			"    const Child c = Child();\n"
			"    print(c.value());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"101\n",
			"virtual const method CallMethod leaf with c suffix"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"class Base {\n"
			"    public:\n"
			"    int n = 0;\n"
			"    virtual int get() const { return n; }\n"
			"    virtual int get() { return n + 1; }\n"
			"}\n"
			"class Child : public Base {}\n"
			"void main() {\n"
			"    Child m = Child();\n"
			"    const Child c = Child();\n"
			"    print(m.get());\n"
			"    print(c.get());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n0\n",
			"const/non-const virtual pair is not an incompatible override"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    const map<string,int> m = {\"a\": 7};\n"
			"    print(m.has(\"a\"));\n"
			"    array<string> ks = m.keys();\n"
			"    print(ks[0]);\n"
			"    array<pair<string,int>> es = m.entries();\n"
			"    print(es[0].second);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\na\n7\n",
			"const map allows keys/has/entries"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    const map<string,int> m = {\"a\": 1};\n"
			"    m.clear();\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const map cannot call clear"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    const array<int> xs = array<int>();\n"
			"    for (ref int x : xs) { x = 1; }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "mutable ref range-for into const collection should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void bump(ref int x) { x = x + 1; }\n"
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m[\"a\"] = 1;\n"
			"    for (pair<string,int> p : m.entries()) {\n"
			"        print(p.first);\n"
			"        print(p.second);\n"
			"        bump(ref m[p.first]);\n"
			"    }\n"
			"    print(m[\"a\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"a\n1\n2\n",
			"map.entries() + pair + ref m[p.first]"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    ordered_map<string,int> m = ordered_map<string,int>();\n"
			"    m[\"x\"] = 10;\n"
			"    m[\"y\"] = 20;\n"
			"    for (pair<string,int> p : m) {\n"
			"        print(p.first);\n"
			"        print(p.second);\n"
			"    }\n"
			"    for (int v : m) { print(v); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"x\n10\ny\n20\n10\n20\n",
			"ordered_map for (Pair : m) entries sugar + value foreach"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void bump(ref int x) { x = x + 1; }\n"
			"void main() {\n"
			"    ordered_map<string,int> m = ordered_map<string,int>();\n"
			"    m[\"a\"] = 10;\n"
			"    m[\"b\"] = 20;\n"
			"    for (ref pair<string,int> p : m) {\n"
			"        bump(ref p.second);\n"
			"        print(p.first);\n"
			"        print(p.second);\n"
			"    }\n"
			"    print(m[\"a\"]);\n"
			"    print(m[\"b\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"a\n11\nb\n21\n11\n21\n",
			"for (ref Pair : ordered_map) entry view write-through"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m[\"k\"] = 5;\n"
			"    for (pair<string,int> p : m) { p.second = 99; }\n"
			"    print(m[\"k\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"for (Pair : m) value snapshot does not write back"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    map<string,array<int>> m = map<string,array<int>>();\n"
			"    array<int> row = array<int>();\n"
			"    row.push(1);\n"
			"    row.push(2);\n"
			"    m[\"r\"] = row;\n"
			"    for (ref pair<string,array<int>> p : m) {\n"
			"        p.second[1] = 42;\n"
			"    }\n"
			"    print(m[\"r\"][1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"42\n",
			"for (ref Pair) nested p.second[i] write-through"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Point { int x = 0; int y = 0; }\n"
			"void bump(ref int v) { v = v + 1; }\n"
			"void main() {\n"
			"    map<string,Point> m = map<string,Point>();\n"
			"    m[\"o\"] = Point();\n"
			"    for (ref pair<string,Point> p : m) {\n"
			"        p.second.x = 3;\n"
			"        p.second.y += 10;\n"
			"        bump(ref p.second.y);\n"
			"    }\n"
			"    print(m[\"o\"].x);\n"
			"    print(m[\"o\"].y);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n11\n",
			"for (ref Pair) nested p.second.field write-through"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Inner { int n = 0; }\n"
			"struct Outer { Inner inner; }\n"
			"void main() {\n"
			"    map<string,Outer> m = map<string,Outer>();\n"
			"    m[\"o\"] = Outer();\n"
			"    for (ref pair<string,Outer> p : m) {\n"
			"        p.second.inner.n = 7;\n"
			"    }\n"
			"    print(m[\"o\"].inner.n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"for (ref Pair) nested p.second.a.b write-through"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Cell { int cells[2] = {1, 2}; }\n"
			"void main() {\n"
			"    map<string,Cell> m = map<string,Cell>();\n"
			"    m[\"c\"] = Cell();\n"
			"    for (ref pair<string,Cell> p : m) {\n"
			"        p.second.cells[1] = 9;\n"
			"    }\n"
			"    print(m[\"c\"].cells[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9\n",
			"for (ref Pair) nested p.second.field[i] write-through"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    ordered_map<string,int> m = ordered_map<string,int>();\n"
			"    m[\"a\"] = 10;\n"
			"    m[\"b\"] = 20;\n"
			"    for ((string k, int& v) : m) {\n"
			"        print(k);\n"
			"        v = v + 1;\n"
			"    }\n"
			"    print(m[\"a\"]);\n"
			"    print(m[\"b\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"a\nb\n11\n21\n",
			"for ((K, V&) : m) typed destructure write-through"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m[\"x\"] = 5;\n"
			"    for (auto& [k, v] : m) { v = v + 2; print(k); }\n"
			"    print(m[\"x\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"x\n7\n",
			"for (auto& [k, v] : m) structured binding write-through"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m[\"x\"] = 5;\n"
			"    for (auto [k, v] : m) { v = 99; }\n"
			"    print(m[\"x\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"for (auto [k, v] : m) value destructure does not write back"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = {\"a\": 1};\n"
			"    for ((ref string k, ref int v) : m) { v = 2; }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "map destructure key cannot be ref"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    array<int> a = array<int>();\n"
			"    a.push(1);\n"
			"    for ((int k, ref int v) : a) { v = 2; }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "map destructure requires map"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = {\"a\": 1};\n"
			"    for (ref pair<string,int> p : m) { p.first = \"b\"; }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "cannot assign map entry view key"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = {\"a\": 1};\n"
			"    for (ref pair<string,int> p : m) { p = pair<string,int>(); }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "cannot assign whole map entry view"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = {\"a\": 1};\n"
			"    for (ref pair<string,int> p : m) { p += pair<string,int>(); }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "cannot compound-assign whole map entry view"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = {\"a\": 1};\n"
			"    for (pair<string,int> p : m.values()) { print(p.first); }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "for (Pair : m.values()) should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = {\"a\": 1};\n"
			"    for (ref pair<string,int> p : m.entries()) { print(p.first); }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "ref range-for on map.entries() snapshot should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v2.41: soft builtin handle<T> — class handle over struct payload (shared ownership)
	if (!ExpectOutput(
			"struct Point { int x = 0; int y = 0; }\n"
			"void bump(handle<Point> h) { h.value.x = h.value.x + 1; }\n"
			"void main() {\n"
			"    handle<Point> a = handle<Point>(Point());\n"
			"    a.value.x = 1;\n"
			"    handle<Point> b = a;\n"
			"    b.value.x = 2;\n"
			"    print(a.value.x);\n"
			"    bump(a);\n"
			"    print(b.value.x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n3\n",
			"handle<T> shares struct payload across assign and call"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Point { int x = 0; }\n"
			"void main() {\n"
			"    Point p = Point();\n"
			"    p.x = 7;\n"
			"    handle<Point> h = handle(p);\n"
			"    print(h.value.x);\n"
			"    p.x = 9;\n"
			"    print(h.value.x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n7\n",
			"handle(T) copies value once; later struct assign does not update handle"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"interface IBump { virtual void bump() = 0; }\n"
			"struct S : IBump { int n = 0; void bump() { n = n + 1; } }\n"
			"void main() {\n"
			"    handle<S> h = handle<S>(S());\n"
			"    IBump i = h.value;\n"
			"    i.bump();\n"
			"    print(h.value.n);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "handle struct→interface still allowed"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "0\n", "struct→interface via handle.value is value-copy box"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"class handle<T> { public: T value; int tag = 1; }\n"
			"struct Point { int x = 0; }\n"
			"void main() {\n"
			"    handle<Point> h = handle<Point>();\n"
			"    h.value.x = 4;\n"
			"    print(h.value.x);\n"
			"    print(h.tag);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n1\n",
			"user arity-1 class handle soft-yields builtin"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Point { int x = 0; }\n"
			"void main() {\n"
			"    handle<Point> a = handle<Point>();\n"
			"    a.value.x = 3;\n"
			"    handle<Point> b = nullptr;\n"
			"    print(a.value.x);\n"
			"    print(b == nullptr);\n"
			"    handle<Point> c = handle<Point>(Point());\n"
			"    print(c.value.x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\ntrue\n0\n",
			"handle default ctor + nullptr + value field"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { handle<int,int> h = handle<int,int>(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "handle wrong arity should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct SharedPoint { int x = 0; }\n"
			"shared<T> keep_shared<T>(handle<T> value) { return value; }\n"
			"shared<U> forward_shared<U>(handle<U> value) { return keep_shared<U>(value); }\n"
			"shared<T> forward_same_shared<T>(handle<T> value) { return keep_shared<T>(value); }\n"
			"int count_shared<T>(shared<T>... values) { return len(values); }\n"
			"shared<T> default_shared<T>() { return shared<T>(); }\n"
			"handle<T> construct_shared<T>(T value) { return shared<T>(value); }\n"
			"class SharedNode { public: int n; SharedNode(int value) { n = value; } }\n"
			"void main() {\n"
			"    SharedPoint source = SharedPoint(); source.x = 7;\n"
			"    shared<SharedPoint> first = shared(source);\n"
			"    handle<SharedPoint> second = keep_shared(first);\n"
			"    assert(forward_shared(first) == first && forward_same_shared(first) == first);\n"
			"    assert(count_shared<SharedPoint>(nullptr, first) == 2);\n"
			"    assert(first.keep_shared() == first && first.keep_shared<SharedPoint>() == first);\n"
			"    std::shared<SharedPoint> third = second;\n"
			"    std::handle<SharedPoint> fourth = std::shared(source);\n"
			"    source.x = 99;\n"
			"    assert(first == second && second == third);\n"
			"    assert(first.value.x == 7 && fourth.value.x == 7);\n"
			"    second.value.x += 1; assert(third.value.x == 8);\n"
			"    weak<shared<SharedPoint>> observer = weak<handle<SharedPoint>>(first);\n"
			"    weak<handle<SharedPoint>> copied = observer;\n"
			"    observer.reset(); assert(observer.expired() && !copied.expired());\n"
			"    handle<SharedPoint> locked = copied.lock();\n"
			"    first = nullptr; second = nullptr; third = nullptr;\n"
			"    assert(!copied.expired()); assert(locked.value.x == 8);\n"
			"    locked = nullptr; assert(copied.expired()); assert(copied.lock() == nullptr);\n"
			"    shared<handle<int>> nested = handle<shared<int>>(shared<int>(4));\n"
			"    assert(nested.value.value == 4);\n"
			"    array<shared<SharedPoint>> items = {fourth};\n"
			"    array<handle<SharedPoint>> same_items = items;\n"
			"    same_items[0].value.x = 12; assert(fourth.value.x == 12);\n"
			"    shared<SharedPoint> fresh = default_shared<SharedPoint>();\n"
			"    assert(fresh != nullptr && fresh.value.x == 0);\n"
			"    handle<int> constructed = construct_shared(6); assert(constructed.value == 6);\n"
			"    SharedNode node = SharedNode(9); weak<SharedNode> node_observer = weak<SharedNode>(node);\n"
			"    node = nullptr; assert(node_observer.expired());\n"
			"    shared<SharedNode> implicit_owner = SharedNode(5);\n"
			"    weak<SharedNode> payload_observer = weak<SharedNode>(implicit_owner.value);\n"
			"    implicit_owner.value = nullptr; assert(payload_observer.expired());\n"
			"    auto inferred_owner = shared(7); auto inferred_copy = handle(inferred_owner.value);\n"
			"    assert(inferred_owner.value == 7 && inferred_copy.value == 7 && inferred_owner != inferred_copy);\n"
			"    shared<double> widened = shared(7); assert(widened.value == 7.0);\n"
			"    auto inferred_point = shared(source); source.x = 100; assert(inferred_point.value.x == 99);\n"
			"    auto inferred_nested = shared(shared(2)); assert(inferred_nested.value.value == 2);\n"
			"    shared<shared<int>> contextual_nested = shared(shared(3)); assert(contextual_nested.value.value == 3);\n"
			"    print(\"shared-handle-ok\");\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"shared-handle-ok\n",
			"shared/handle aliases preserve identity, inference, nesting and weak lifetime"))
	{
		return EXIT_FAILURE;
	}

	for (const std::string& Kind : {std::string("class"), std::string("struct")})
	{
		if (!ExpectOutput(
				Kind + " handle { public: int value; handle(int n) { value = n + 1; } }\n"
				"void main() { auto local = handle(2); assert(local.value == 3);\n"
				"    shared<int> owner = shared<int>(7); handle<int> alias = owner;\n"
				"    assert(alias == owner && owner.value == 7);\n"
				"    print(\"coexist-ok\"); }\n",
				VeroScript::ERunMode::Script, "coexist-ok\n",
				"non-generic handle coexists with shared/handle builtin arity 1"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const char* InvalidSource : {
			"void main() { shared<int,int> bad = shared<int,int>(); }",
			"void main() { Handle<int> old = Handle<int>(); }",
			"class shared<T> { public: T value; } void main() {}",
			"using shared = int; void main() {}"})
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const auto Result = VeroScript::Test::FEngine::RunSource(
			InvalidSource, "<invalid-shared-handle>", Output, Errors, VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "invalid shared arity, old Handle name and shared redeclarations fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct Counter {\n"
			"    int n = 0;\n"
			"    Counter(int v) { n = v; }\n"
			"    Counter operator++() { n = n + 1; return Counter(n); }\n"
			"}\n"
			"void main() {\n"
			"    map<string,Counter> m = map<string,Counter>();\n"
			"    m[\"a\"] = Counter(0);\n"
			"    for (ref pair<string,Counter> p : m) { ++p.second; }\n"
			"    print(m[\"a\"].n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"entry view overloaded ++p.second write-through"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"interface Reader {\n"
			"    virtual int read() = 0;\n"
			"    virtual int read() const = 0;\n"
			"}\n"
			"class Box : public Reader{\n"
			"    public:\n"
			"    int n = 7;\n"
			"    int read() { return n + 100; }\n"
			"    int read() const { return n; }\n"
			"}\n"
			"void main() {\n"
			"    Box b = Box();\n"
			"    const Reader r = b;\n"
			"    print(r.read());\n"
			"    print(b.read());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n107\n",
			"const interface receiver prefers const overload"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,map<string,int>> m = map<string,map<string,int>>();\n"
			"    map<string,int> inner = map<string,int>();\n"
			"    inner[\"k\"] = 1;\n"
			"    m[\"o\"] = inner;\n"
			"    for (ref pair<string,map<string,int>> p : m) { p.second[\"k\"] = 2; }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "entry view nested map index assign should fail (not silent)"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"int read(const ref int x) { return x; }\n"
			"void main() {\n"
			"    int n = 5;\n"
			"    print(read(ref n));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"const ref read"))
	{
		return EXIT_FAILURE;
	}

	// Mutable `ref T` must match `const ref T` even when a value overload also exists.
	if (!ExpectOutput(
			"void f(const ref int x) { print(x); }\n"
			"void f(int x) { print(x + 100); }\n"
			"void main() {\n"
			"    int n = 5;\n"
			"    f(ref n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"ref arg selects const-ref overload over by-value"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void f(ref int x) { print(1); }\n"
			"void f(const ref int x) { print(2); }\n"
			"void main() {\n"
			"    int n = 5;\n"
			"    f(ref n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"mutable ref overload preferred over const-ref"))
	{
		return EXIT_FAILURE;
	}

	// Const operand must score as "const ref T" so mutable-ref overloads lose.
	if (!ExpectOutput(
			"void f(ref int x) { print(1); }\n"
			"void f(const ref int x) { print(2); }\n"
			"void main() {\n"
			"    const int n = 5;\n"
			"    f(ref n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"const operand selects const-ref overload"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void g(ref int x) { print(1); }\n"
			"void g(const ref int x) { print(2); }\n"
			"void forward(const ref int x) { g(ref x); }\n"
			"void main() {\n"
			"    int n = 5;\n"
			"    forward(ref n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"forwarded const-ref selects const-ref overload"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    array<int> xs = array<int>();\n"
			"    xs.push(1);\n"
			"    for (const ref int x : xs) { x = 2; }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const ref range-for element assign should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void bump(ref int x) { x = 1; }\n"
			"void main() { int n = 0; bump(n); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "missing ref on argument should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void bump(int x) { x = 1; }\n"
			"void main() { int n = 0; bump(ref n); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "ref on non-ref param should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void write(const ref int x) { x = 1; }\n"
			"void main() { int n = 0; write(ref n); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "const ref write should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.25: function generics (monomorphization)
	if (!ExpectOutput(
			"T identity(T x) { return x; }\n"
			"void main() {\n"
			"    print(identity(42));\n"
			"    print(identity(\"hi\"));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"42\nhi\n",
			"generic identity int and string"))
	{
		return EXIT_FAILURE;
	}

	// v1.29: explicit generic function type arguments
	if (!ExpectOutput(
			"T identity(T x) { return x; }\n"
			"void main() {\n"
			"    print(identity<int>(42));\n"
			"    print(identity<string>(\"hi\"));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"42\nhi\n",
			"explicit generic function type arguments"))
	{
		return EXIT_FAILURE;
	}

	for (const char* Source : {
			 "T identity(T x) { return x; }\nvoid main() { print(identity<int,string>(42)); }\n",
	"T identity(T x) { return x; }\nvoid main() { print(identity<int>(\"wrong\")); }\n"})
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "invalid explicit generic function call should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"T pick(T a, T b) { return a; }\n"
			"void main() {\n"
			"    print(pick(1, 2));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"generic two params same T"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"T pick(T a, T b) { return a; }\n"
			"void main() { print(pick(1, \"x\")); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "conflicting generic arg types should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int orphan<T>() { return 1; }\n"
			"void main() { print(orphan<int>()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess && Output.str() == "1\n", "unused generic type parameter remains valid"))
		{
			return EXIT_FAILURE;
		}
	}

	// v1.26: struct generics (monomorphization)
	if (!ExpectOutput(
			"struct Box<T> { T value; }\n"
			"void main() {\n"
			"    Box<int> a = Box<int>();\n"
			"    a.value = 7;\n"
			"    print(a.value);\n"
			"    Box<string> b = Box<string>();\n"
			"    b.value = \"ok\";\n"
			"    print(b.value);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\nok\n",
			"generic struct Box int and string"))
	{
		return EXIT_FAILURE;
	}
