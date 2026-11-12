// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include "VeroScriptAst.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Message << '\n';
		}
		return Condition;
	}

	bool Run(
		const std::string& Source,
		std::string& OutOutput,
		std::string* OutError = nullptr,
		const bool bCompileOnly = false)
	{
		OutOutput.clear();
		if (OutError)
		{
			OutError->clear();
		}
		VeroScript::VeroScriptLexer Lexer(Source, "op.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (OutError)
			{
				*OutError = "lex";
			}
			return false;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "op.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			if (OutError)
			{
				*OutError = Parser.GetError().Message;
			}
			return false;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			if (OutError)
			{
				*OutError = Compiler.GetError().Message;
			}
			return false;
		}
		if (bCompileOnly) return true;
		std::ostringstream Output;
		std::istringstream In;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, In, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(*Module, VeroScript::ERunMode::Script))
		{
			if (OutError)
			{
				*OutError = Vm.GetError().Message;
			}
			return false;
		}
		OutOutput = Output.str();
		return true;
	}
}

int main()
{
	std::string Output;
	std::string Error;

	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	Vec operator+(Vec other) { return Vec(x + other.x); }\n"
				"	Vec operator-(Vec other) { return Vec(x - other.x); }\n"
				"	bool operator==(Vec other) { return x == other.x; }\n"
				"	Vec operator-() { return Vec(0 - x); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Vec a = Vec(3);\n"
				"	Vec b = Vec(4);\n"
				"	print((a + b).x);\n"
				"	print((a - b).x);\n"
				"	print(a == Vec(3));\n"
				"	print((-a).x);\n"
				"}\n",
				Output,
					&Error),
			"basic operator overload")
				|| !Expect(Output == "7\n-1\ntrue\n-3\n", "operator overload output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Base\n"
				"{\n"
				"\tpublic:\n"

				"	virtual int operator+(Base other) { return 1; }\n"
				"}\n"
				"class Derived : public Base\n"
				"{\n"
				"\tpublic:\n"

				"	int operator+(Base other) override { return 2; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Base b = Derived();\n"
				"	print(b + Base());\n"
				"}\n",
				Output,
					&Error),
			"virtual operator+ dispatch")
				|| !Expect(Output == "2\n", "virtual operator output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Box\n"
				"{\n"
				"\tpublic:\n"

				"	int n;\n"
				"	Box() { n = 1; }\n"
				"}\n"
				"class Box<T>\n"
				"{\n"
				"\tpublic:\n"

				"	T v;\n"
				"	Box(T x) { v = x; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Box<> plain = Box();\n"
				"	Box<int> n = Box<int>(5);\n"
				"	print(plain.n);\n"
				"	print(n.v);\n"
				"}\n",
				Output,
					&Error),
			"Foo<> arity-0 sugar")
				|| !Expect(Output == "1\n5\n", "Foo<> output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"void main()\n"
				"{\n"
				"	print(1 + 2);\n"
				"	print(\"a\" + \"b\");\n"
				"}\n",
				Output,
					&Error),
			"builtin operators unchanged")
				|| !Expect(Output == "3\nab\n", "builtin output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	void set(Vec other) { x = other.x; }\n"
				"	Vec operator+(Vec other) { return Vec(x + other.x); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Vec a = Vec(3);\n"
				"	a.set(Vec(4));\n"
				"	print(a.x);\n"
				"	print((a + Vec(5)).x);\n"
				"}\n",
				Output,
					&Error),
			"ctor temporary under method/operator")
				|| !Expect(Output == "4\n9\n", "ctor temporary output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"struct Point\n"
				"{\n"
				"	int x;\n"
				"	Point(int v) { x = v; }\n"
				"	bool operator!=(Point other) { return x != other.x; }\n"
				"	bool operator<(Point other) { return x < other.x; }\n"
				"	bool operator!() { return x == 0; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Point a = Point(1);\n"
				"	Point b = Point(2);\n"
				"	print(a != b);\n"
				"	print(a < b);\n"
				"	print(!Point(0));\n"
				"}\n",
				Output,
					&Error),
			"struct != / < / operator!")
				|| !Expect(Output == "true\ntrue\ntrue\n", "struct compare/not output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"interface IAdd\n"
				"{\n"
				"	virtual int operator+(IAdd other) = 0;\n"
				"}\n"
				"class Num : public IAdd\n"
				"{\n"
				"\tpublic:\n"

				"	int v;\n"
				"	Num(int x) { v = x; }\n"
				"	int operator+(IAdd other) { return v + 1; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	IAdd a = Num(3);\n"
				"	print(a + Num(0));\n"
				"}\n",
				Output,
					&Error),
			"interface static type operator+")
				|| !Expect(Output == "4\n", "interface operator output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	Vec operator+(Vec other) { return Vec(x + other.x); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Vec a = Vec(3);\n"
				"	a += Vec(4);\n"
				"	print(a.x);\n"
				"}\n",
				Output,
					&Error),
			"compound += via operator+")
				|| !Expect(Output == "7\n", "+= output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"}\n"
				"Vec operator+(int n, Vec v) { return Vec(n + v.x); }\n"
				"Vec operator-(Vec v) { return Vec(0 - v.x); }\n"
				"void main()\n"
				"{\n"
				"	print((1 + Vec(4)).x);\n"
				"	print((-Vec(5)).x);\n"
				"}\n",
				Output,
					&Error),
			"free-function operator + right-side dispatch")
				|| !Expect(Output == "5\n-5\n", "free operator output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			Run(
				"int seq = 0;\n"
				"int L() { seq = seq + 1; return seq; }\n"
				"int R() { seq = seq + 10; return seq; }\n"
				"class Wrap\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Wrap(int v) { x = v; }\n"
				"	Wrap operator+(Wrap other) { return Wrap(x + other.x); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Wrap a = Wrap(L()) + Wrap(R());\n"
				"	print(a.x);\n"
				"	print(seq);\n"
				"}\n",
				Output,
					&Error),
			"LHS-before-RHS evaluation order")
				|| !Expect(Output == "12\n11\n", "eval order output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Member arity match must not steal typed free-function operator+(Vec,int).
	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	Vec operator+(Vec other) { return Vec(x + other.x); }\n"
				"}\n"
				"Vec operator+(Vec v, int n) { return Vec(v.x + n); }\n"
				"void main()\n"
				"{\n"
				"	print((Vec(3) + 4).x);\n"
				"}\n",
				Output,
					&Error),
			"typed free op preferred over member type-mismatch")
				|| !Expect(Output == "7\n", "typed free op output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// += must reject operator+ whose return is not assignable to LHS.
	if (!Expect(
			!Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	int operator+(Vec other) { return x + other.x; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Vec a = Vec(1);\n"
				"	a += Vec(2);\n"
				"}\n",
				Output,
					&Error),
			"compound += rejects non-assignable operator+ return"))
	{
		std::cerr << "expected compile/runtime failure, got: [" << Output << "] err=[" << Error
			<< "]\n";
		return EXIT_FAILURE;
	}

	// Operator overloads cannot use default parameters.
	if (!Expect(
			!Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"}\n"
				"Vec operator+(Vec a, Vec b = Vec(0)) { return Vec(a.x + b.x); }\n"
				"void main() {}\n",
				Output,
					&Error),
			"reject default parameters on free operator"))
	{
		std::cerr << "expected parse failure for defaulted operator, got: [" << Output << "] err=["
			<< Error << "]\n";
		return EXIT_FAILURE;
	}

	// Field += must also reject non-assignable operator+ return.
	if (!Expect(
			!Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	int operator+(Vec other) { return x + other.x; }\n"
				"}\n"
				"class Holder\n"
				"{\n"
				"\tpublic:\n"

				"	Vec v;\n"
				"	Holder(int n) { v = Vec(n); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Holder h = Holder(1);\n"
				"	h.v += Vec(2);\n"
				"}\n",
				Output,
					&Error),
			"field += rejects non-assignable operator+ return"))
	{
		std::cerr << "expected failure for field +=, got: [" << Output << "] err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	// Multi-member operator overloads: type resolve must match emit scoring.
	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	int operator+(int n) { return x + n; }\n"
				"	Vec operator+(Vec other) { return Vec(x + other.x); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	int a = Vec(3) + 4;\n"
				"	print(a);\n"
				"	print((Vec(1) + Vec(2)).x);\n"
				"}\n",
				Output,
					&Error),
			"multi-member operator overload scoring")
				|| !Expect(Output == "7\n3\n", "multi-member op output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Struct operand coerced to interface parameter (boxing).
	if (!Expect(
			Run(
				"interface IVal\n"
				"{\n"
				"	virtual int get() = 0;\n"
				"}\n"
				"struct Num : IVal\n"
				"{\n"
				"	int v;\n"
				"	Num(int x) { v = x; }\n"
				"	int get() { return v; }\n"
				"	int operator+(IVal other) { return v + other.get(); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	print(Num(3) + Num(4));\n"
				"}\n",
				Output,
					&Error),
			"struct→interface operator operand coercion")
				|| !Expect(Output == "7\n", "struct→interface op output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(Run(
		"class Vec { public: int x; Vec(int n) { x = n; } }\n"
		"int operator+(const Vec& a, Vec& b) { b.x += a.x; return b.x; }\n"
		"void main() { Vec a = Vec(3); Vec b = Vec(4); print(a + b); print(b.x); }\n",
		Output, &Error), "free operator reference parameters")
		|| !Expect(Output == "7\n7\n", "reference operator updates the original"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	// True operator+= preferred over desugared operator+.
	if (!Expect(
			Run(
				"int path = 0;\n"
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	Vec operator+(Vec other) { path = 1; return Vec(x + other.x + 100); }\n"
				"	Vec operator+=(Vec other) { path = 2; x += other.x; return Vec(x); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Vec a = Vec(3);\n"
				"	a += Vec(4);\n"
				"	print(a.x);\n"
				"	print(path);\n"
				"}\n",
				Output,
					&Error),
			"true operator+=")
				|| !Expect(Output == "7\n2\n", "true operator+= output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// != synthesizes from overloaded ==.
	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	bool operator==(Vec other) { return x == other.x; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	print(Vec(1) != Vec(2));\n"
				"	print(Vec(3) != Vec(3));\n"
				"}\n",
				Output,
					&Error),
			"!= synthesizes from ==")
				|| !Expect(Output == "true\nfalse\n", "synthesized != output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Index += rejects non-assignable operator+ return (same as identifier/field).
	if (!Expect(
			!Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	int operator+(Vec other) { return x + other.x; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	array<Vec> xs = array<Vec>();\n"
				"	xs.push(Vec(1));\n"
				"	xs[0] += Vec(2);\n"
				"}\n",
				Output,
					&Error),
			"index += rejects non-assignable operator+ return"))
	{
		std::cerr << "expected failure for index +=, got: [" << Output << "] err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	// A declared operator+= may return any type; it is not replaced by operator+.
	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	int operator+=(Vec other) { return x + other.x; }\n"
				"	Vec operator+(Vec other) { return Vec(x + other.x); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Vec a = Vec(3);\n"
				"	a += Vec(4);\n"
				"	print(a.x);\n"
				"}\n",
				Output,
					&Error),
			"declared += return type does not trigger a fallback")
				|| !Expect(Output == "3\n", "+= executes exactly the declared body"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// A matching free += executes its declared body and return semantics.
	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	Vec operator+(Vec other) { return Vec(x + other.x); }\n"
				"}\n"
				"int operator+=(Vec a, Vec b) { return a.x + b.x; }\n"
				"void main()\n"
				"{\n"
				"	Vec a = Vec(3);\n"
				"	a += Vec(4);\n"
				"	print(a.x);\n"
				"}\n",
				Output,
					&Error),
			"free int += executes its own body")
				|| !Expect(Output == "3\n", "free += leaves target unchanged"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// void in-place operator+= (1A).
	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	void operator+=(Vec other) { x = x + other.x; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Vec a = Vec(3);\n"
				"	a += Vec(4);\n"
				"	print(a.x);\n"
				"	a += Vec(1); print(a.x);\n"
				"}\n",
				Output,
					&Error),
			"void in-place operator+=")
				|| !Expect(Output == "7\n8\n", "void += output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Relational operators must return bool.
	if (!Expect(
			!Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	int operator<(Vec other) { return x - other.x; }\n"
				"}\n"
				"void main() {}\n",
				Output,
					&Error),
			"reject non-bool operator<"))
	{
		std::cerr << "expected parse failure for non-bool <, got: [" << Output << "] err=["
			<< Error << "]\n";
		return EXIT_FAILURE;
	}

	// Comparison mirror: > from <.
	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	bool operator<(Vec other) { return x < other.x; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	print(Vec(3) > Vec(1));\n"
				"	print(Vec(1) > Vec(3));\n"
				"}\n",
				Output,
					&Error),
			"mirror > from <")
				|| !Expect(Output == "true\nfalse\n", "mirror > output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Right-member swap: int has no +, use right Vec.operator+(int).
	if (!Expect(
			Run(
				"class Vec\n"
				"{\n"
				"\tpublic:\n"

				"	int x;\n"
				"	Vec(int v) { x = v; }\n"
				"	Vec operator+(int n) { return Vec(x + n); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	print((1 + Vec(2)).x);\n"
				"}\n",
				Output,
					&Error),
			"right-member swap")
				|| !Expect(Output == "3\n", "right-member output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Bitwise + ~.
	if (!Expect(
			Run(
				"class Flags\n"
				"{\n"
				"\tpublic:\n"

				"	int bits;\n"
				"	Flags(int b) { bits = b; }\n"
				"	Flags operator&(Flags o) { return Flags(bits & o.bits); }\n"
				"	Flags operator~() { return Flags(0 - bits - 1); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	print((Flags(6) & Flags(3)).bits);\n"
				"	print((~Flags(0)).bits);\n"
				"}\n",
				Output,
					&Error),
			"bitwise overload")
				|| !Expect(Output == "2\n-1\n", "bitwise output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// && / || non-short-circuit (2A).
	if (!Expect(
			Run(
				"int hits = 0;\n"
				"class Gate\n"
				"{\n"
				"\tpublic:\n"

				"	bool v;\n"
				"	Gate(bool b) { v = b; }\n"
				"	bool operator&&(Gate o) { hits = hits + 1; return v && o.v; }\n"
				"}\n"
				"bool side()\n"
				"{\n"
				"	hits = hits + 10;\n"
				"	return false;\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	bool r = Gate(false) && Gate(side());\n"
				"	print(r);\n"
				"	print(hits);\n"
				"}\n",
				Output,
					&Error),
			"logical && non-short-circuit")
				|| !Expect(Output == "false\n11\n", "&& hits output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// operator++/-- (struct so postfix can snapshot via CloneStruct; class handles share identity).
	if (!Expect(
			Run(
				"struct Counter\n"
				"{\n"
				"	int n;\n"
				"	Counter(int v) { n = v; }\n"
				"	Counter operator++() { n = n + 1; return Counter(n); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Counter c = Counter(1);\n"
				"	print((++c).n);\n"
				"	print(c.n);\n"
				"	Counter d = Counter(5);\n"
				"	print((d++).n);\n"
				"	print(d.n);\n"
				"}\n",
				Output,
					&Error),
			"operator++")
				|| !Expect(Output == "2\n2\n5\n6\n", "++ output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Class postfix ++ rejected (shared identity cannot snapshot).
	if (!Expect(
			!Run(
				"class Counter\n"
				"{\n"
				"\tpublic:\n"

				"	int n;\n"
				"	Counter(int v) { n = v; }\n"
				"	Counter operator++() { n = n + 1; return Counter(n); }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Counter d = Counter(5);\n"
				"	d++;\n"
				"}\n",
				Output,
					&Error),
			"reject class postfix ++"))
	{
		std::cerr << "expected failure for class postfix ++, got: public [" << Output << "] err=["
			<< Error << "]\n";
		return EXIT_FAILURE;
	}

	// operator[] rvalue.
	if (!Expect(
			Run(
				"class Bag\n"
				"{\n"
				"\tpublic:\n"

				"	int a;\n"
				"	int b;\n"
				"	Bag(int x, int y) { a = x; b = y; }\n"
				"	int operator[](int i) { if (i == 0) return a; return b; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Bag g = Bag(4, 9);\n"
				"	print(g[0]);\n"
				"	print(g[1]);\n"
				"}\n",
				Output,
					&Error),
			"operator[]")
				|| !Expect(Output == "4\n9\n", "[] output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// operator[] assign rejected when [] exists.
	if (!Expect(
			!Run(
				"class Bag\n"
				"{\n"
				"\tpublic:\n"

				"	int a;\n"
				"	Bag(int x) { a = x; }\n"
				"	int operator[](int i) { return a; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Bag g = Bag(1);\n"
				"	g[0] = 2;\n"
				"}\n",
				Output,
					&Error),
			"reject assign through operator[]"))
	{
		std::cerr << "expected failure for [] assign, got: [" << Output << "] err=[" << Error
			<< "]\n";
		return EXIT_FAILURE;
	}

	// Index assign rejected on overloadable type even without operator[].
	if (!Expect(
			!Run(
				"class Bag\n"
				"{\n"
				"\tpublic:\n"

				"	int a;\n"
				"	Bag(int x) { a = x; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Bag g = Bag(1);\n"
				"	g[0] = 2;\n"
				"}\n",
				Output,
					&Error),
			"reject index assign without operator[]"))
	{
		std::cerr << "expected failure for index assign without [], got: [" << Output
			<< "] err=[" << Error << "]\n";
		return EXIT_FAILURE;
	}

	// operator().
	if (!Expect(
			Run(
				"class Adder\n"
				"{\n"
				"\tpublic:\n"

				"	int base;\n"
				"	Adder(int b) { base = b; }\n"
				"	int operator()(int n) { return base + n; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Adder f = Adder(10);\n"
				"	print(f(3));\n"
				"}\n",
				Output,
					&Error),
			"operator()")
				|| !Expect(Output == "13\n", "() output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Multi-arg operator() must CloneStruct each by-value struct arg (no shared identity).
	if (!Expect(
			Run(
				"struct S { int n = 0; }\n"
				"class Mut\n"
				"{\n"
				"\tpublic:\n"

				"	Mut() {}\n"
				"	void operator()(S a, S b) { a.n = 1; b.n = 2; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	S x = S();\n"
				"	S y = S();\n"
				"	Mut m = Mut();\n"
				"	m(x, y);\n"
				"	print(x.n);\n"
				"	print(y.n);\n"
				"}\n",
				Output,
					&Error),
			"multi-arg operator() clones struct args")
				|| !Expect(Output == "0\n0\n", "() struct args stayed unmodified"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Conversion operator bool / int.
	if (!Expect(
			Run(
				"class Flag\n"
				"{\n"
				"\tpublic:\n"

				"	int v;\n"
				"	Flag(int n) { v = n; }\n"
				"	bool operator bool() { return v != 0; }\n"
				"	int operator int() { return v; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Flag f = Flag(7);\n"
				"	bool b = f;\n"
				"	int n = f;\n"
				"	print(b);\n"
				"	print(n);\n"
				"}\n",
				Output,
					&Error),
			"conversion operators")
				|| !Expect(Output == "true\n7\n", "conversion output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Operator declarations may be defined later in the same source file.
	if (!Expect(
			Run(
				"class ExternalOps\n"
				"{\n"
				"public:\n"
				"    int value;\n"
				"    ExternalOps(int n);\n"
				"    ExternalOps operator+(ExternalOps other);\n"
				"    int operator()(int delta);\n"
				"    bool operator bool();\n"
				"}\n"
				"ExternalOps::ExternalOps(int n) { this.value = n; }\n"
				"ExternalOps ExternalOps::operator+(ExternalOps other) {\n"
				"    return ExternalOps(this.value + other.value);\n"
				"}\n"
				"int ExternalOps::operator()(int delta) { return this.value + delta; }\n"
				"bool ExternalOps::operator bool() { return this.value != 0; }\n"
				"void main() {\n"
				"    ExternalOps sum = ExternalOps(2) + ExternalOps(3);\n"
				"    print(sum(4));\n"
				"    bool valid = sum;\n"
				"    print(valid);\n"
				"}\n",
				Output,
				&Error),
			"same-file out-of-class operator definitions")
		|| !Expect(Output == "9\ntrue\n", "out-of-class operator output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	// Free generic operator.
	if (!Expect(
			Run(
				"interface IPair\n"
				"{\n"
				"	virtual int getA() = 0;\n"
				"	virtual int getB() = 0;\n"
				"}\n"
				"class Pair : public IPair\n"
				"{\n"
				"\tpublic:\n"

				"	int a;\n"
				"	int b;\n"
				"	Pair(int x, int y) { a = x; b = y; }\n"
				"	int getA() { return a; }\n"
				"	int getB() { return b; }\n"
				"}\n"
				"Pair operator+<T>(T x, T y) where T : IPair\n"
				"{\n"
				"	return Pair(x.getA() + y.getA(), x.getB() + y.getB());\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Pair p = Pair(1, 2) + Pair(3, 4);\n"
				"	print(p.a);\n"
				"	print(p.b);\n"
				"}\n",
				Output,
					&Error),
			"free generic operator+")
				|| !Expect(Output == "4\n6\n", "generic + output"))
	{
		std::cerr << Error << '\n';
		std::cerr << "got: [" << Output << "]\n";
		return EXIT_FAILURE;
	}

	if (!Expect(
			!Run(
				"class Vec {\n"
				"    public:\n"
				"	int x;\n"
				"	Vec(int n) { x = n; }\n"
				"}\n"
				"void main()\n"
				"{\n"
				"	Vec v = Vec(1);\n"
				"	print(v == 1);\n"
				"}\n",
				Output,
					&Error),
			"class == primitive must not soft-fall to builtin Eq"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	struct FReferenceCase
	{
		const char* Name;
		const char* Source;
		const char* Expected;
	};
	const FReferenceCase ReferenceCases[] = {
		{"ordinary operator-prefix names retain normal function rules", R"vs(
void operator1(int& n, int delta = 4) { n += delta; }
class F { public: static void operator_helper(int& n) { n++; } };
void main() { int n = 2; F::operator_helper(ref n); operator1(ref n); print(n); }
)vs", "7\n"},
		{"index references and const overloads", R"vs(
class Box { public: int n = 3;
    int& operator[](int index) { return n; }
    const int& operator[](int index) const { return n; }
};
void main() { Box b = Box(); b[0] = 5; b[0] += 2; print(b[0]++); print(++b[0]);
    int& x = b[0]; x = 11; const Box c = b; print(c[0]); print(b.n); }
)vs", "7\n9\n11\n11\n"},
		{"call references, explicit ref, conversion temporaries", R"vs(
class Fn { public: void operator()(int& target, const double& delta) { target += cast<int>(delta); } };
void main() { Fn f = Fn(); int n = 1; f(n, 2); f(ref n, 3.0); print(n); }
)vs", "6\n"},
		{"mixed references and by-value structs retain aliases", R"vs(
struct Value { int n = 4; };
class Fn { public: void operator()(int& target, Value copy, const Value& source) {
    target += copy.n + source.n; copy.n = 90;
} };
void main() { Fn f = Fn(); int n = 1; Value v = Value(); f(n, v, v); print(n); print(v.n); }
)vs", "9\n4\n"},
		{"const reference temporary and overload choice", R"vs(
class Fn { public:
    int operator()(int& n, int unused = 0) { n += 1; return 1; }
    int operator()(const int& n) { return 2; }
};
void main() { Fn f = Fn(); int n = 4; const int c = 5; print(f(n)); print(n); print(f(c)); print(f(6)); }
)vs", "1\n5\n2\n2\n"},
		{"call defaults and typed rest arguments", R"vs(
class Defaults { public: int operator()(const int& value = 7) { return value; } };
class Rest { public: int operator()(int& total, int... values) {
    for (int value : values) { total += value; } return total;
} };
void main() { Defaults f = Defaults(); print(f()); Rest r = Rest(); int n = 1;
    print(r(n, 2, 3)); print(r(n)); array<int> xs = array<int>(); xs.push(4); print(r(n, xs)); }
)vs", "7\n6\n6\n10\n"},
		{"class postfix int tag and prefix reference", R"vs(
class Counter { public: int n = 2;
    Counter& operator++() { n += 1; return this; }
    Counter operator++(int tag) { Counter old = Counter(); old.n = n; n += 1; return old; }
};
void main() { Counter c = Counter(); print((c++).n); print(c.n); print((++c).n); (++c).n = 9; print(c.n); }
)vs", "2\n3\n4\n9\n"},
		{"free postfix references", R"vs(
struct Counter { int n = 2; };
Counter operator++(Counter& c, int tag) { Counter old = c; c.n += 1; return old; }
Counter& operator--(Counter& c) { c.n -= 1; return c; }
void main() { Counter c = Counter(); print((c++).n); print(c.n); --c; print(c.n); }
)vs", "2\n3\n2\n"},
		{"compound operator reference result and chaining", R"vs(
class Box { public: int n = 1; Box& operator+=(const Box& rhs) { n += rhs.n; return this; } };
void main() { Box a = Box(); Box b = Box(); (a += b) += b; print(a.n); }
)vs", "3\n"},
		{"binary and unary reference-return assignment", R"vs(
class Box { public: int n = 1; int& operator-() { return n; } };
int& operator+(Box& box, const int& unused) { return box.n; }
void main() { Box b = Box(); (b + 0) = 7; (-b) += 2; print(b.n); int& alias = b + 0; alias++; print(b.n); }
)vs", "9\n10\n"},
		{"nested custom and builtin indexing", R"vs(
class Grid { public: array<int> values = array<int>();
    array<int>& operator[](int index) { return values; }
};
void main() { Grid grid = Grid(); grid.values.push(3); grid[0][0] += 4;
    int& n = grid[0][0]; n++; print(grid.values[0]); }
)vs", "8\n"},
		{"index assignment evaluates operands once", R"vs(
int order = 0;
class Box { public: int n = 1; int& operator[](const int& i) { return n; } };
Box box = Box();
Box receiver() { order = order * 10 + 1; return box; }
int index() { order = order * 10 + 2; return 0; }
int value() { order = order * 10 + 3; return 4; }
void main() { receiver()[index()] += value(); print(order); print(box.n); }
)vs", "123\n5\n"},
		{"generic free operator references", R"vs(
class Box<T> { public: T n; Box(T x) { n = x; } };
int operator+<T>(const Box<T>& a, const Box<T>& b) { return a.n + b.n; }
void main() { Box<int> a = Box<int>(2); print(a + Box<int>(5)); }
)vs", "7\n"},
		{"const receiver call overload", R"vs(
class Fn { public: int operator()() { return 1; } int operator()() const { return 2; } };
void main() { Fn f = Fn(); const Fn c = Fn(); print(f()); print(c()); }
)vs", "1\n2\n"},

		{"member assignment and chained reference results", R"vs(
class Box { public: int n = 0;
    Box& operator=(const int& value) { n = value; return this; }
};
void main() { Box a = Box(); Box b = Box(); a = 3; b = (a = 7).n; (a = 9).n++; print(a.n); print(b.n); }
)vs", "10\n7\n"},
		{"free const reference preference", R"vs(
class Box { public: int n = 3; };
int operator+(Box& a, const int& b) { a.n += b; return 1; }
int operator+(const Box& a, const int& b) { return 2; }
void main() { Box a = Box(); const Box b = Box(); print(a + 1); print(b + 1); print(Box() + 1); print(a.n); }
)vs", "1\n2\n2\n4\n"},
		{"const reference defaults on ordinary functions", R"vs(
int read(const int& value = 4) { return value; }
void main() { print(read()); }
)vs", "4\n"},
		{"postfix decrement tag", R"vs(
struct Counter { public: int n = 3;
    Counter operator--(int tag) { Counter old = this; n--; return old; }
};
void main() { Counter a = Counter(); print((a--).n); print(a.n); }
)vs", "3\n2\n"},

		{"compound fallback binds member references", R"vs(
struct Box { public: int n; Box(int x) { n = x; } Box operator+(const Box& b) const { return Box(n + b.n); } };
void main() { Box a = Box(3); Box b = Box(4); a += b; print(a.n); }
)vs", "7\n"},
		{"compound fallback binds free references and evaluates index once", R"vs(
int calls = 0;
struct Box { public: int n; Box(int x) { n = x; } };
Box operator+(const Box& a, const Box& b) { return Box(a.n + b.n); }
int index() { calls++; return 0; }
void main() { array<Box> a = {Box(3)}; Box b = Box(4); a[index()] += b; print(a[0].n); print(calls); }
)vs", "7\n1\n"},
		{"const assignment overload", R"vs(
class Box { public: mutable int n = 0; void operator=(int x) const { n = x; } };
void main() { const Box a = Box(); a = 7; print(a.n); }
)vs", "7\n"},
		{"free index references and call defaults", R"vs(
struct Box { public: int n = 3; };
int& operator[](Box& b, const int& i) { return b.n; }
void operator()(Box& b, const int& step = 2) { b.n += step; }
void main() { Box b = Box(); b[0]++; b(); b(3); print(b.n); }
)vs", "9\n"},
		{"reference-return assignment copies struct values", R"vs(
struct Value { public: int n = 3; };
class Box { public: Value value = Value(); Value& operator[](int i) { return value; } };
void main() { Box b = Box(); Value v = Value(); b[0] = v; v.n = 9; print(b.value.n);
    Value& alias = b[0]; alias = v; v.n = 12; print(b.value.n); }
)vs", "3\n9\n"},
		{"struct prefix reference can initialize user type aliases", R"vs(
struct F { public: int n = 1; F& operator++() { n++; return this; } };
void main() { F f = F(); (++f).n++; print(f.n); F& a = ++f; a.n++; print(f.n); }
)vs", "3\n5\n"},
		{"postfix tag accepts canonical int aliases", R"vs(
using Tag = int; using AnotherTag = Tag;
struct F { public: int n = 1; F operator++(AnotherTag tag) { F old = this; n++; return old; } };
F operator--(F& value, Tag tag) { F old = value; value.n--; return old; }
void main() { F f = F(); print((f++).n); print(f.n); print((f--).n); print(f.n); }
)vs", "1\n2\n2\n1\n"},
		{"reference preference survives equivalent value bindings", R"vs(
class F { public:
    int operator()(int& a, int b) { return 1; }
    int operator()(const int& a, const int& b) { return 2; }
};
class Box {};
int operator()(Box& box, int& a, int b) { return 3; }
int operator()(const Box& box, const int& a, const int& b) { return 4; }
void main() { F f = F(); int a = 3; int b = 4; const int c = 5;
    print(f(a, b)); print(f(a, 5)); print(f(c, b)); Box box = Box(); print(box(a, b)); }
)vs", "1\n1\n2\n3\n"},
		{"inherited generic operator signatures use the declaring type", R"vs(
class Base<T> { public: T value; Base(T x) { value = x; }
    T& operator[](const int& i) { return value; }
    const T& operator[](const int& i) const { return value; }
    T& operator()(T& delta) { value += delta; return value; }
    T& operator+(const T& amount) { value += amount; return value; }
    T& operator-() { return value; }
    T& operator=(const T& next) { value = next; return value; }
    T& operator+=(const T& amount) { value += amount; return value; }
};
class Child : public Base<int> { public: Child() : Base(3) {} };
void main() { Child c = Child(); c[0] = 7; print(c.value); int delta = 2; c(delta)++;
    print(c.value); (c + 1) = 12; (-c)++; print(c.value); c = 17; c += 3;
    const Child view = c; print(view[0]); }
)vs", "7\n10\n13\n20\n"},
		{"virtual index reference preserves derived object identity", R"vs(
class Base { public: int value = 2; virtual int& operator[](int i) { return value; } };
class Child : public Base { public: int other = 3; int& operator[](int i) override { return other; } };
void main() { Child c = Child(); Base b = c; b[0] = 7; print(c.other); print(c.value); }
)vs", "7\n2\n"},
		{"static and namespace fields bind operator reference arguments", R"vs(
namespace State { int value = 1; }
int evaluations = 0;
class Storage { public: static int value = 2; };
Storage receiver() { evaluations++; return Storage(); }
class F { public: void operator()(int& value) { value++; } };
void main() { F f = F(); f(State::value); f(Storage::value); f(receiver().value);
    int& alias = Storage::value; alias++; print(State::value); print(Storage::value); print(evaluations); }
)vs", "2\n5\n1\n"},
		{"qualified container elements bind the original storage", R"vs(
namespace State { int[] values = int[](); map<string,array<int>> nested = { "a": {4} }; }
int evaluations = 0;
class Storage { public: static int[] values = int[](); };
Storage receiver() { evaluations++; return Storage(); }
class F { public: void operator()(int& value) { value++; } };
void main() { State::values.push(1); Storage::values.push(2); F f = F();
    f(State::values[0]); f(Storage::values[0]); f(receiver().values[0]);
    f(State::nested["a"][0]); State::values[0] += 3; Storage::values[0] = 6;
    print(Storage::values[0]++);
    print(State::values[0]); print(Storage::values[0]); print(State::nested["a"][0]); print(evaluations); }
)vs", "6\n5\n7\n5\n1\n"},
		{"inherited generic static index references retain per-instantiation storage", R"vs(
class Base<T> { public:
    static T value = 3;
    static T& operator[](int i) { return Base<T>::value; }
};
class Child<U> : public Base<U> {};
void main() {
    Child<int> a = Child<int>(); Child<long> b = Child<long>();
    a[0] += 4; b[0] = 9i64; print(a[0]); print(b[0]);
}
)vs", "7\n9\n"},
		{"static call and index operators", R"vs(
class F { public: static int value = 1;
    static void operator()(int& x) { x++; }
    static int& operator[](const int& i) { return F::value; }
};
void main() { const F f = F(); int x = 2; f(x); f[0] = 7; print(x); print(F::value); }
)vs", "3\n7\n"},
		{"const references accept temporary struct fields", R"vs(
struct Value { public: int n = 3; };
Value make() { return Value(); }
class F { public: int operator()(const int& n) { return n; } };
void main() { F f = F(); print(f(make().n)); }
)vs", "3\n"},
		{"receiver and reference argument preferences agree", R"vs(
class F { public:
    int operator()(int& n) { return 1; }
    int operator()(const int& n) const { return 2; }
};
void main() { F f = F(); const F c = F(); int n = 3; const int k = 4;
    print(f(n)); print(f(k)); print(c(n)); print(f(5)); }
)vs", "1\n2\n2\n2\n"},
		{"receiver const preference also applies to packed rest arguments", R"vs(
class F { public:
    int operator()(int... n) { return 1; }
    int operator()(int... n) const { return 2; }
};
void main() { F f = F(); const F c = F(); array<int> n = {1, 2};
    print(f()); print(f(1)); print(f(1, 2)); print(f(n)); print(c(1, 2)); }
)vs", "1\n1\n1\n1\n2\n"},
		{"interface default operators share reference overload ordering", R"vs(
interface I {
    int operator()(int& n) { return 1; }
    int operator()(const int& n) const { return 2; }
};
class F : I {};
void main() { F f = F(); const F c = F(); int n = 3;
    print(f(n)); print(c(n)); I i = f; print(i(n)); }
)vs", "1\n2\n1\n"},
		{"generic interface operator results substitute field reference types", R"vs(
interface I<T> { T n = 3;
    T& operator[](const int& i) { return this.n; }
    const T& operator[](const int& i) const { return this.n; }
};
class F<T> : I<T> {};
void main() { F<int> f = F<int>(); f[0] += 4; const F<int> c = f;
    print(c[0]); I<int> i = f; i[0]++; print(f[0]); }
)vs", "7\n8\n"},
		{"inherited generic interface operators retain their declaring specialization", R"vs(
interface Base<T> { T n = 3;
    T& operator[](const int& i) { return this.n; }
    const T& operator[](const int& i) const { return this.n; }
    T operator()(const T& n) { return n; }
};
interface Child<U> : Base<U> {};
class F<V> : Child<V> {};
void main() { F<int> f = F<int>(); Child<int> i = f; i[0] += 4;
    const Child<int> c = i; print(c[0]); print(f[0]); print(i(9)); }
)vs", "7\n7\n9\n"},
		{"inherited interface operators use each parents concrete arguments", R"vs(
interface Base<T> { T& operator[](T& n) { return n; } };
interface Child<U> : Base<int> { U operator()(const U& n) { return n; } };
class F : Child<string> {};
void main() { F f = F(); Child<string> i = f; int n = 3;
    i[n] += 4; print(n); print(i("ok")); }
)vs", "7\nok\n"},
		{"inherited interface field defaults use their declaring type parameters", R"vs(
class Value<T> { public: T n = 9; };
interface Base<T> { Value<T> value = Value<T>();
    Value<T>& operator[](int i) { return this.value; }
};
interface Child<U> : Base<U> {};
class F<V> : Child<V> {};
void main() { F<long> f = F<long>(); Child<long> i = f;
    i[0].n += 1i64; print(f[0].n); }
)vs", "10\n"},
		{"interface default initializer temporaries preserve constructor parameters", R"vs(
interface Storage<T> { T values[2] = {3,4}; };
class F<T> : Storage<T> { public: T tag; F(T input) { tag=input; } };
void main() { F<int> value=F<int>(7); print(value.values[0]+value.values[1]+value.tag); }
)vs", "14\n"},
		{"const interface fields select readonly argument bindings", R"vs(
interface Base<T> { const T n = 3;
    const T& operator[](int i) const { return this.n; }
};
interface I<U> : Base<U> {};
class F<V> : I<V> {};
class Call { public:
    int operator()(int& n) { n++; return 1; }
    int operator()(const int& n) { return 2; }
};
void main() { F<int> f = F<int>(); I<int> i = f; Call c = Call();
    print(c(f.n)); print(c(i.n)); print(c(f[0])); print(f.n); }
)vs", "2\n2\n2\n3\n"},
		{"by-value operator arguments are copied before later argument side effects", R"vs(
struct V { public: int n = 3; };
V v = V();
int mutate() { v.n = 9; return 1; }
int operator+(V a, int n) { return a.n; }
int operator()(V a, int n) { return a.n; }
class F { public: int operator()(V a, int n) { return a.n; } };
void main() { F f = F(); print(v + mutate()); v.n = 3;
    print(v(mutate())); v.n = 3; print(f(v, mutate())); print(v.n); }
)vs", "3\n3\n3\n9\n"},
		{"comparison mirrors capture by-value operands in evaluation order", R"vs(
struct V { public: int n = 3; };
V v = V();
class F { public: bool operator<(V a) { return a.n == 3; } };
class G {};
bool operator<(G g, V a) { return a.n == 3; }
F right_member() { v.n = 9; return F(); }
G right_free() { v.n = 9; return G(); }
void main() { print(v > right_member()); v.n = 3;
    print(v > right_free()); print(v.n); }
)vs", "true\ntrue\n9\n"},
		{"compound fallback and rest calls capture by-value operands before later effects", R"vs(
struct V { public: int n = 3; };
V v = V();
int mutate() { v.n = 9; return 1; }
V operator+(V a, int n) { a.n += n; return a; }
class F { public: int operator()(V a, int... n) { return a.n; } };
void main() { v += mutate(); print(v.n); v.n = 3;
    F f = F(); print(f(v, mutate())); print(v.n); }
)vs", "4\n3\n9\n"},
		{"const reference operator operands retain storage aliases", R"vs(
struct V { public: int n = 3; };
V v = V();
int mutate() { v.n = 9; return 1; }
int operator+(const V& a, int n) { return a.n; }
void main() { print(v + mutate()); print(v.n); }
)vs", "9\n9\n"},
	};
	for (const auto& Test : ReferenceCases)
	{
		if (!Run(Test.Source, Output, &Error) || Output != Test.Expected)
		{
			std::cerr << "FAIL: " << Test.Name << ": " << Error << " output=[" << Output << "]\n";
			return EXIT_FAILURE;
		}
	}
	const FReferenceCase RejectedReferenceCases[] = {
		{"mutable temporary", "class F { public: void operator()(int& x) {} }; void main() { F f = F(); f(1); }", nullptr},
		{"mutable const binding", "class F { public: void operator()(int& x) {} }; void main() { F f = F(); const int x = 1; f(x); }", nullptr},
		{"mutable numeric conversion", "class F { public: void operator()(double& x) {} }; void main() { F f = F(); int x = 1; f(x); }", nullptr},
		{"mutable reference to temporary struct field", "struct Value { public: int n = 3; }; Value make() { return Value(); } class F { public: void operator()(int& n) {} }; void main() { F f = F(); f(make().n); }", nullptr},
		{"explicit mutable reference to temporary struct field", "struct Value { public: int n = 3; }; Value make() { return Value(); } class F { public: void operator()(int& n) {} }; void main() { F f = F(); f(ref make().n); }", nullptr},
		{"static arithmetic operator", "class F { public: static int operator+(int n) { return n; } }; void main() {}", nullptr},
		{"static assignment operator", "class F { public: static void operator=(int n) {} }; void main() {}", nullptr},
		{"static unary operator", "struct F { static int operator-() { return 1; } }; void main() {}", nullptr},
		{"mutable reference to const static field", "class Storage { public: static const int n = 1; }; class F { public: void operator()(int& n) {} }; void main() { F f = F(); f(Storage::n); }", nullptr},
		{"mutable reference to const namespace global", "namespace State { const int n = 1; } class F { public: void operator()(int& n) {} }; void main() { F f = F(); f(State::n); }", nullptr},
		{"mutable reference to const qualified container element", "namespace State { const array<int> values = {1}; } class F { public: void operator()(int& n) {} }; void main() { F f = F(); f(State::values[0]); }", nullptr},
		{"mutable reference to const static container element", "class Storage { public: static const array<int> values = {1}; }; class F { public: void operator()(int& n) {} }; void main() { F f = F(); f(Storage::values[0]); }", nullptr},
		{"reference to private static container element", "class Storage { static array<int> values = {1}; }; class F { public: void operator()(int& n) {} }; void main() { F f = F(); f(Storage::values[0]); }", nullptr},
		{"write through const index result", "class F { public: int n = 1; const int& operator[](int x) { return n; } }; void main() { F f = F(); f[0] = 3; }", nullptr},
		{"increment const index result", "class F { public: int n = 1; const int& operator[](int x) { return n; } }; void main() { F f = F(); f[0]++; }", nullptr},
		{"nonconst call on const receiver", "class F { public: void operator()() {} }; void main() { const F f = F(); f(); }", nullptr},
		{"nonconst index on const receiver", "class F { public: int operator[](int x) { return x; } }; void main() { const F f = F(); print(f[0]); }", nullptr},
		{"nonconst binary on const receiver", "class F { public: int operator+(int x) { return x; } }; void main() { const F f = F(); print(f + 1); }", nullptr},
		{"private operator", "class F { int operator()(int x) { return x; } }; void main() { F f = F(); print(f(1)); }", nullptr},
		{"value-result operator assignment", "class F { public: int operator+(int x) { return x; } }; void main() { F f = F(); (f + 1) = 2; }", nullptr},
		{"postfix tag reference", "class F { public: F operator++(int& tag) { return F(); } }; void main() {}", nullptr},
		{"postfix tag nonint", "class F { public: F operator++(double tag) { return F(); } }; void main() {}", nullptr},
		{"postfix tag nonint alias", "using Tag = double; class F { public: F operator++(Tag tag) { return F(); } }; void main() {}", nullptr},
		{"noncall operator default", "class F { public: int operator+(int x = 1) { return x; } }; void main() {}", nullptr},
		{"mutable ref default", "class F { public: void operator()(int& x = 1) {} }; void main() {}", nullptr},
		{"const ref parameter modification", "class F { public: void operator()(const int& x) { x = 2; } }; void main() {}", nullptr},
		{"const receiver returns mutable index alias", "class F { public: int n = 1; int& operator[](int i) const { return n; } }; void main() {}", nullptr},
		{"explicit ref to byvalue operator parameter", "class F { public: void operator()(int x) {} }; void main() { F f = F(); int x = 1; f(ref x); }", nullptr},

		{"ambiguous value vs const reference", "class F { public: int operator()(int x) { return 1; } int operator()(const int& x) { return 2; } }; void main() { F f = F(); int x = 1; print(f(x)); }", nullptr},
		{"ambiguous value vs mutable reference", "class F { public: int operator()(int x) { return 1; } int operator()(int& x) { return 2; } }; void main() { F f = F(); int x = 1; print(f(x)); }", nullptr},
		{"crossed reference qualifications remain ambiguous", "class F { public: int operator()(int& x, const int& y) { return 1; } int operator()(const int& x, int& y) { return 2; } }; void main() { F f = F(); int x = 1; int y = 2; print(f(x, y)); }", nullptr},
		{"crossed receiver and reference qualifications remain ambiguous", "class F { public: int operator()(int& x) const { return 1; } int operator()(const int& x) { return 2; } }; void main() { F f = F(); int x = 1; print(f(x)); }", nullptr},
		{"crossed receiver and numeric conversions remain ambiguous", "class F { public: int operator()(const int& x) const { return 1; } int operator()(const long& x) { return 2; } }; void main() { F f = F(); int x = 1; print(f(x)); }", nullptr},
		{"crossed free reference and numeric conversions remain ambiguous", "class F {}; int operator+(F& f, const long& n) { return 1; } int operator+(const F& f, const int& n) { return 2; } void main() { F f = F(); int n = 3; print(f + n); }", nullptr},
		{"crossed interface receiver and reference conversions remain ambiguous", "interface I { int operator()(int& n) const { return 1; } int operator()(const int& n) { return 2; } }; class F : I {}; void main() { F f = F(); int n = 3; print(f(n)); }", nullptr},
		{"crossed inherited generic interface references remain ambiguous", "interface Base<T> { int operator()(T& n) const { return 1; } }; interface Child<U> : Base<U> { int operator()(const U& n) { return 2; } }; class F : Child<int> {}; void main() { F f = F(); Child<int> i = f; int n = 3; print(i(n)); }", nullptr},
		{"assignment to inherited const interface field", "interface I { const int n = 3; }; class F : I {}; void main() { F f = F(); f.n = 4; }", nullptr},
		{"assignment to const field through interface", "interface I { const int n = 3; }; class F : I {}; void main() { I f = F(); f.n = 4; }", nullptr},
		{"const generic interface field cannot escape as writable reference", "interface I<T> { const T n = 3; T& operator[](int i) { return this.n; } }; class F : I<int> {}; void main() { F f = F(); f[0] = 4; }", nullptr},
		{"inherited const interface field cannot bind writable operator parameter", "interface Base<T> { const T n = 3; }; interface I<U> : Base<U> {}; class F : I<int> {}; class Call { public: void operator()(int& n) { n++; } }; void main() { F f = F(); Call c = Call(); c(f.n); }", nullptr},
		{"const interface container elements cannot bind writable operator parameter", "interface I { const array<int> n = {3}; }; class F : I {}; class Call { public: void operator()(int& n) { n++; } }; void main() { F f = F(); Call c = Call(); c(f.n[0]); }", nullptr},
		{"void compound result cannot be used", "class F { public: void operator+=(int x) {} }; void main() { F f = F(); int x = (f += 1); }", nullptr},
		{"free assignment operator", "class F {}; F operator=(F& a, const F& b) { return a; } void main() {}", nullptr},
		{"deleted assignment operator", "class F { public: F& operator=(const int& b) = delete; }; void main() { F f = F(); f = 1; }", nullptr},
	};
	for (const auto& Test : RejectedReferenceCases)
	{
		if (Run(Test.Source, Output, &Error, true))
		{
			std::cerr << "FAIL: accepted " << Test.Name << '\n';
			return EXIT_FAILURE;
		}
	}

	std::cout << "VeroScriptOperatorOverloadTests passed\n";
	return EXIT_SUCCESS;
}
