// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Share the current-core fixtures between Node and real-browser publication gates.
export const currentCoreFixtures = [
	{
		name: "deferred bodies resolve imported namespace aliases",
		source: `import "alias-library.vs";
using namespace Shared;
int main() { Count value = add(20, 22); Label label = "direct"; assert(label == "direct"); return value; }`,
		files: [{
			path: "alias-library.vs",
			source: `namespace Shared { export using Count = int; export typedef Label = string; export Count add(Count a, Count b) { return a + b; } }`,
		}],
	},
	{
		name: "deferred namespace alias lookup follows exported imports",
		source: `import "alias-facade.vs";
using namespace Shared;
int main() { Count value = 42; Label label = "facade"; assert(label == "facade"); return value; }`,
		files: [
			{ path: "alias-facade.vs", source: 'export import "alias-library.vs";' },
			{ path: "alias-library.vs", source: "namespace Shared { export using Count = int; export typedef Label = string; }" },
		],
	},
	{
		name: "scalar reference captures preserve shared cells and aliases",
		source: `int main()
{
    int value = 0;
    fn()->int first = [&value]() -> int { value += 2; return value; };
    fn()->int second = [&value]() -> int { value += 3; return value; };
    assert(first() == 2 && second() == 5 && first() == 7 && value == 7);
    int a = 1;
    int b = 2;
    fn()->int both = [&a, &b]() -> int { a += b; b += a; return a + b; };
    assert(both() == 8 && a == 3 && b == 5);
    return 42;
}`,
		files: [],
	},
	{
		name: "native output arguments write through initialized reference captures",
		source: `import "numeric.vs";
int main()
{
    std::bigint target = std::bigint(0);
    auto parse = [&result = target]() -> bool { return std::bigint::try_parse("42", ref result); };
    assert(parse() && target.to_string() == "42");
    return 42;
}`,
		files: [],
	},
	{
		name: "generic static fields are instance method receivers",
		source: `class Item { public: int Read() { return 7; } };
class Cache<T> { public: static Item value = Item(); };
int main() { return Cache<int>::value.Read() + 35; }`,
		files: [],
	},
	{
		name: "element references retain their original container after reassignment",
		source: `int main()
{
    array<int> current = {10};
    array<int> original = current;
    int& first = current[0];
    array<int> replacement = {20};
    current = replacement;
    first = 42;
    assert(original[0] == 42 && current[0] == 20);
    map<string, int> entries = map<string, int>();
    entries["a"] = 10;
    map<string, int> old_entries = entries;
    int& entry = entries["a"];
    entries = map<string, int>();
    entries["a"] = 20;
    entry = 42;
    assert(old_entries["a"] == 42 && entries["a"] == 20);
    int fixed[1] = {7};
    int& borrowed = fixed[0];
    int copy[1] = fixed;
    fixed[0] = 9;
    fixed[0] += 2;
    assert(borrowed == 11 && copy[0] == 7);
    return 42;
}`,
		files: [],
	},
	{
		name: "fixed array returns copy and release their own elements once",
		source: `int copies = 0;
int destroyed = 0;
struct Item
{
    int value = 7;
    Item() = default;
    Item(const Item& other) { value = other.value; ++copies; }
    ~Item() { ++destroyed; }
};
using Values = Item[2];
class Holder
{
public:
    Item values[2];
    Values Read() { return values; }
};
Holder holder = Holder();
int read_copy()
{
    Values result = holder.Read();
    result[0].value = 9;
    return holder.values[0].value;
}
int main()
{
    copies = 0;
    destroyed = 0;
    assert(read_copy() == 7);
    assert(copies == 2 && destroyed == 2);
    return 42;
}`,
		files: [],
	},
	{
		name: "initialized captures and shared mutable environments",
		source: `function()->int make_counter()
{
    return [count = 0]() mutable -> int { return ++count; };
}
function()->int make_reference()
{
    int local = 39;
    return [&target = local, step = 1]() -> int { target += step; return target; };
}
namespace CaptureSource
{
    int value() { return 42; }
    template<typename T> T identity(T value) { return value; }
    enum Entry { Nested = 7 };
    int Nested() { return 0; }
    namespace Nested { int value() { return 42; } }
}
int initialized = 0;
int initialize_capture() { return ++initialized; }
int invoke_capture(fn()->int callback) { return callback(); }
int main()
{
    assert(initialized == 0);
    auto first = make_counter();
    auto alias = first;
    auto other = make_counter();
    assert(first() == 1 && alias() == 2 && other() == 1 && first() == 3);
    auto reference = make_reference();
    assert(reference() == 40 && reference() == 41);
    auto qualified = [callback = CaptureSource::value]() -> int { return callback(); };
    auto generic = [callback = CaptureSource::identity<int>](int value) -> int { return callback(value); };
    auto nested = [callback = CaptureSource::Nested::value]() -> int { return callback(); };
    assert(qualified() == 42 && generic(42) == 42 && nested() == 42);
    fn(double)->int typed = [callback = CaptureSource::identity<int>](double ignored) -> int { return callback(42); };
    assert(typed(1.5) == 42);
    assert(invoke_capture([callback = CaptureSource::identity<int>]() -> int { return callback(42); }) == 42);
    auto unused = [left = initialize_capture(), right = initialize_capture()]() -> int { return 0; };
    assert(initialized == 2 && unused() == 0 && initialized == 2);
    int value = 40;
    int& binding = value;
    auto snapshot = [binding]() -> int { return binding; };
    value = 99;
    assert(snapshot() == 40);
    return 42;
}`,
		files: [],
	},
	{
		name: "local statics retry initialization and release fresh program roots",
		source: `int attempts = 0;
int initialize_retry()
{
    ++attempts;
    if (attempts == 1) throw 7;
    return 40;
}
int retry()
{
    for (int index = 0; index < 2; ++index)
    {
        try
        {
            static int value = initialize_retry();
            return ++value;
        }
        catch (int error) { assert(error == 7); }
    }
    return -1;
}
function()->int make_static_counter(int seed)
{
    return [seed]() -> int { static int value = seed; return ++value; };
}
class StaticLifetime
{
public:
    StaticLifetime(int value) { id = value; }
    ~StaticLifetime() { print(id); }
    int id;
};
void initialize_objects()
{
    static StaticLifetime first = StaticLifetime(1);
    static StaticLifetime second = StaticLifetime(2);
}
int main()
{
    assert(attempts == 0);
    assert(retry() == 41 && retry() == 42 && attempts == 2);
    auto first = make_static_counter(20);
    auto second = make_static_counter(30);
    assert(first() == 21 && second() == 22 && first() == 23);
    initialize_objects();
    initialize_objects();
    return 42;
}`,
		files: [],
		output: "2\n1\n",
	},
	{
		name: "exact numeric values and fresh compiled globals",
		source: `import "rational.vs";
std::bigint total = std::bigint("18446744073709551616");
int main()
{
    assert(total.to_string() == "18446744073709551616");
    assert(std::bigint(18446744073709551615u64).to_uint64() == 18446744073709551615u64);
    auto divided = std::numeric::divmod(total, std::bigint(3));
    assert(divided.first * std::bigint(3) + divided.second == total);
    assert(std::bigint(-3) >> 1 == std::bigint(-2));
    assert(~std::bigint(-3) == std::bigint(2));
    std::bigint preserved = std::bigint(19);
    assert(!std::bigint::try_parse("invalid", ref preserved));
    assert(preserved == std::bigint(19));
    array<std::bigint> values = {total};
    values[0] += std::bigint(42);
    assert(total.to_string() == "18446744073709551616");
    std::rational fraction = std::rational(1, 3) + std::rational(1, 6);
    assert(fraction.to_string() == "1/2" && fraction.to_decimal(4) == "0.5000");
    total += std::bigint(42);
    assert(values[0] == total);
    print(total.to_string());
    return 42;
}`,
		files: [],
		output: "18446744073709551658\n",
	},
	{
		name: "nested generic and inherited types with local scope isolation",
		source: `class Base { public: struct Inherited { int value = 7; }; };
class Outer<T> : public Base
{
public:
    struct Item<U> { T first; U second; };
    enum class State { Ready = 3 };
};
int main()
{
    Outer<int>::Item<string> item = Outer<int>::Item<string>();
    item.first = 28;
    item.second = "scoped";
    Outer<int>::Inherited inherited = Outer<int>::Inherited();
    Outer<int>::State state = Outer<int>::State::Ready;
    assert(state == Outer<int>::State::Ready);
    using Value = int;
    struct Local { Value value = 7; };
    Local local = Local();
    {
        struct Local { int value = 99; };
        Local shadow = Local();
        assert(shadow.value == 99);
    }
    assert(local.value == 7);
    print(item.second);
    return item.first + inherited.value + local.value;
}`,
		files: [],
		output: "scoped\n",
	},
	{
		name: "managed byte reads preserve alias and snapshot ownership",
		source: `import "memory.vs";
int main()
{
    std::memory::buffer bytes = new std::memory::buffer(3);
    bytes.set(0i64, 7u);
    bytes.set(1i64, 35u);
    auto view = bytes.slice(1i64, 2i64);
    auto readView = bytes.readonly_slice(0i64, 2i64);
    auto snapshot = std::memory::to_array(bytes);
    view.set(0i64, 34u);
    assert(snapshot[1] == 35u && readView.get(1i64) == 34u);
    assert(bytes.get(2i64) == 0u);
    return cast<int>(bytes.get(0i64)) + cast<int>(view.get(0i64)) + 1;
}`,
		files: [],
	},
	{
		name: "loop back-edges and protected break and continue preserve finally transfers",
		source: `int main()
{
    int doubled = 3;
    doubled += doubled;
    assert(doubled == 6);
    int total = 0;
    for (int i = 0; i < 256; ++i) { total += i; }
    assert(total == 32640);
    int trace = 0;
    int selected = 0;
    for (int i = 0; i < 5; ++i)
    {
        try
        {
            if (i == 1) { continue; }
            if (i == 3) { break; }
            selected += i;
        }
        finally { trace = trace * 10 + i + 1; }
    }
    assert(selected == 2 && trace == 1234);
    return 42;
}`,
		files: [],
	},
	{
		name: "scalar calls preserve captures, receiver writes and standard value operations",
		source: `import "memory.vs";
fn(int)->int make(int delta)
{
    int scale = 2;
    return [delta, scale](int value)->int { return (value * scale + delta) % 97; };
}
class State
{
public:
    int value = 1;
    int step(int amount) { value = value + amount; value = value * 2; return value; }
};
int main()
{
    array<fn(int)->int> calls = {make(2)};
    int total = 1;
    for (int i = 0; i < 4; ++i) { total = calls[0](total); }
    assert(total == 46);
    State state = State();
    assert(state.step(2) == 6);
    assert(state.step(3) == 18);
    std::memory::buffer bytes = std::memory::buffer(2);
    bytes.set(0, 7);
    bytes.set(1, 13);
    total = 0;
    for (int i = 0; i < 3; ++i) { total += cast<int>(bytes.get(i % 2)); }
    assert(total == 27 && bytes.size() == 2i64);
    std::bigint value = std::bigint(7);
    for (int i = 0; i < 3; ++i) { value = value * std::bigint(17); }
    assert(value.to_string() == "34391");
    return 42;
}`,
		files: [],
	},
	{
		name: "virtual calls follow constructor and destructor lifetime phases",
		source: `import "attribute.vs";
class Base
{
public:
    Base() { probe(1, "base constructor"); }
    virtual int read() { return 1; }
    void probe(int expected, string phase)
    {
        assert(read() == expected && helper(this) == expected);
        fn()->int closure = [=]()->int { return helper(this); };
        assert(closure() == expected);
        type<Base> info = type<Base>();
        bool found = false;
        for (int i = 0; i < info.method_count(); ++i)
        {
            method_info<Base> method(info.qualified_name, i);
            if (method.name() == "read") { assert(method.invoke<int>(this) == expected); found = true; }
        }
        assert(found);
        string name = "read";
        auto bound = std::bind_method<fn()->int>(this, name);
        assert(bound() == expected);
        print(phase);
    }
    ~Base() { probe(1, "base destructor"); }
};
class Derived : public Base
{
public:
    string text = "ready";
    Derived() { probe(2, "derived constructor"); }
    int read() override { assert(text == "ready"); return 2; }
    ~Derived() { probe(2, "derived destructor"); }
};
int helper(Base value) { return value.read(); }
void work() { Derived value = Derived(); value.probe(2, "ready"); }
int main() { work(); print("done"); return 42; }`,
		files: [],
		output: "base constructor\nderived constructor\nready\nderived destructor\nbase destructor\ndone\n",
	},
	{
		name: "exit during exception cleanup preserves successful process state",
		source: `class Trace
{
public:
    ~Trace() { print("cleanup"); exit(42); }
};
void fail() { Trace value = Trace(); throw 9; }
int main()
{
    try { fail(); } catch (int error) { print("unreachable handler"); }
    print("unreachable continuation");
    return 0;
}`,
		files: [],
		output: "cleanup\n",
		exitRequested: true,
	},
	{
		name: "lowercase pair snapshots and writable map entries",
		source: `int main()
{
    map<string, int> values = map<string, int>();
    values["answer"] = 40;
    array<pair<string, int>> snapshots = values.entries();
    for (std::pair<string, int>& entry : values)
    {
        entry.second += 2;
    }
    assert(snapshots[0].first == "answer" && snapshots[0].second == 40);
    pair<string, int> snapshot = values.entries()[0];
    snapshot.second = 0;
    return values["answer"];
}`,
		files: [],
	},
	{
		name: "attributes and strong and weak method callables",
		source: `import "core.vs";
class Model
{
public:
    int value = 20;
    int read() const { return value; }
    int add(int amount) { return value + amount; }
};
int main()
{
    Model model = Model();
    auto strong = std::make_attribute_by_name<int>(model, "read");
    fn(int)->int add = std::bind_method<fn(int)->int>(model, "add");
    std::delegate<int> reader = std::delegate<int>();
    reader.bind(std::bind_method<fn()->int>(model, "read"));
    model.value = 39;
    model = nullptr;
    assert(strong() == 39 && reader.execute() == 39);
    int answer = add(3);
    Model weak_model = Model();
    auto weak_value = std::make_weak_attribute_by_name<int>(weak<Model>(weak_model), "read");
    assert(weak_value() == 20);
    weak_model = nullptr;
    assert(weak_value.is_bound() && !weak_value.try_get().has_value());
    strong.reset();
    reader.unbind();
    return answer;
}`,
		files: [],
	},
	{
		name: "application i18n typed arguments and independent snapshots",
		source: `import "core.vs";
int main()
{
    std::i18n::arguments args = std::i18n::arguments();
    args.set("count", std::i18n::argument(18446744073709551615u64));
    args.set("name", std::i18n::argument("Vero"));
    std::i18n::context app = std::i18n::context();
    app.set_language("en-US");
    assert(app.format("{name}: {count}", args) == "Vero: 18446744073709551615");
    assert(args.get("count").as_uint64() == 18446744073709551615u64);
    std::i18n::snapshot previous = app.snapshot();
    std::i18n::context independent = app.clone();
    app.set_language("zh-CN");
    assert(previous.version() != app.version());
    assert(previous.configuration().language == "en-US");
    assert(independent.configuration().language == "en-US");
    std::attribute<std::i18n::text> label = std::i18n::text("Literal {braces}");
    assert(app.resolve(label.get()) == "Literal {braces}");
    print(app.format("{name}: {count}", args));
    return 42;
}`,
		files: [],
		output: "Vero: 18446744073709551615\n",
	},
	{
		name: "script compile extension annotation name policies from virtual source files",
		source: `import compiler "builder.vs";
@bUiLdEr @Strict(Name=42) @Unknown(Flag)
class Card { public: int value; };
int main()
{
    Card card = Card::Create().Value(42).Build();
    type<Card> info = type<Card>();
    assert(info.has_annotation("BUILDER") && info.has_annotation("@builder"));
    assert(len(info.find_annotations("@BUILDER")) == 1);
    assert(len(info.find_annotations("BUILDER")) == 0);
    annotation_info builder = info.annotation(0);
    assert(builder.name() == "@bUiLdEr" && builder.raw_text() == "@bUiLdEr");
    assert(builder.matches_name("@Builder") && !builder.matches_name("Builder"));
    assert(builder.definition().name() == "@Builder" && !builder.definition().case_sensitive());
    assert(info.has_annotation("Strict") && !info.has_annotation("strict"));
    assert(info.annotation(1).definition().case_sensitive());
    assert(len(info.annotation(1).find_arguments("Name")) == 1);
    assert(len(info.annotation(1).find_arguments("name")) == 0);
    assert(info.has_annotation("Unknown") && !info.has_annotation("unknown"));
    assert(!info.annotation(2).definition().is_valid());
    assert(info.annotation(2).matches_name("@Unknown") && !info.annotation(2).matches_name("@unknown"));
    annotation_info invalid = annotation_info();
    assert(!invalid.matches_name(""));
    return card.value;
}`,
		files: [{
			path: "builder.vs",
			source: `import "compiler.vs";
using callback = fn(std::compiler::context, std::compiler::output)->void;
callback make_builder()
{
    return [](std::compiler::context ctx, std::compiler::output out)->void {
        assert(ctx.annotation().name() == "@bUiLdEr");
        assert(ctx.annotation().matches_name("@BUILDER"));
        assert(!ctx.annotation().matches_name("BUILDER"));
        assert(!ctx.annotation().definition().case_sensitive());
        if (ctx.phase() == std::compiler::phase::types)
        {
            out.add_declaration(ctx.top_scope(),
                "class CardBuilder { public: Card value=Card();" +
                "CardBuilder Value(int input){value.value=input;return this;}" +
                "Card Build(){return value;}};");
        }
        else
        {
            out.add_declaration(ctx.member_scope(),
                "public: static CardBuilder Create(){return CardBuilder();}");
        }
    };
}
export void register_extension(std::compiler::registration reg)
{
    std::compiler::annotation_definition annotation = std::compiler::annotation_definition();
    assert(annotation.case_sensitive);
    annotation.name = "@Builder";
    annotation.case_sensitive = false;
    annotation.targets = array<std::compiler::target>{std::compiler::target::class_type};
    reg.register_annotation(annotation);
    assert(!reg.find_annotation("@BUILDER").case_sensitive());
    std::compiler::annotation_definition strict = std::compiler::annotation_definition();
    strict.name = "@Strict";
    strict.targets = annotation.targets;
    reg.register_annotation(strict);
    assert(reg.find_annotation("@Strict").case_sensitive());
    assert(!reg.find_annotation("@strict").is_valid());
    std::compiler::subscription subscription = std::compiler::subscription();
    subscription.event = std::compiler::event::annotation;
    subscription.annotation_name = "@Builder";
    subscription.targets = annotation.targets;
    std::compiler::processor_definition definition = std::compiler::processor_definition();
    definition.id = "web.card_builder";
    definition.version = "0.1.1";
    definition.phases = array<std::compiler::phase>{std::compiler::phase::types,std::compiler::phase::members};
    definition.operations = array<std::compiler::operation>{std::compiler::operation::add_declaration};
    definition.subscriptions = array<std::compiler::subscription>{subscription};
    reg.register_processor(definition, make_builder);
}`,
		}],
	},
	{
		name: "captured console output without terminal capabilities",
		source: `int main()
{
    std::console::write("before");
    std::console::error("diagnostic");
    std::console::flush();
    assert(!std::console::is_interactive());
    assert(!std::console::is_output_terminal());
    assert(!std::console::supports_color());
    assert(!std::console::supports_cursor());
    assert(!std::console::supports_clear());
    assert(std::console::width() == 0 && std::console::height() == 0);
    assert(!std::console::set_color("bright_red", "default"));
    assert(!std::console::reset_style());
    assert(!std::console::clear());
    assert(!std::console::clear_line("all"));
    assert(!std::console::move_cursor(0, 0));
    assert(!std::console::set_cursor_visible(false));
    print("-after");
    return 42;
}`,
		files: [],
		output: "before-after\n",
		// The embedding host delivers diagnostics as complete lines.
		errorOutput: "diagnostic\n",
	},
	{
		name: "structured annotations and cross-file declaration preparation",
		source: `import "declarations.vs";
import "definitions.vs";
int main()
{
    Box<int> value(3);
    return value.value + value.extra() + Contract::Echo<int>(27) + Transform();
}`,
		files: [
			{
				path: "declarations.vs",
				source: `@Unknown(flag, text="", meta=(enabled, count=-4))
export struct Box<T>
{
    T value;
    Box(T input) { value = input; }
    int extra() const;
};
namespace Contract { template<T> export T Echo(T value); }
export int Transform(int value = 2);`,
			},
			{
				path: "definitions.vs",
				source: `import "declarations.vs";
int helper() { return 5; }
int Box<T>::extra() const { return helper(); }
template<T> T Contract::Echo(T input) { return input; }
int ::Transform(int input) { return input + helper(); }`,
			},
		],
	},
	{
		name: "lowercase standard delegates",
		source: `import "delegate.vs";
int observed = 0;
int increment(int value) { return value + 1; }
void observe(int a, int b, int c) { observed += a + b + c; }
int main()
{
    std::delegate<int, int> single = std::delegate<int, int>();
    single.bind(increment);
    assert(single.is_bound() && single.execute(41) == 42);
    single.unbind();
    assert(!single.is_bound() && !single.execute_if_bound(1));
    std::multicast_delegate<int, int, int> many = std::multicast_delegate<int, int, int>();
    many.add(observe);
    many.broadcast(10, 20, 12);
    assert(many.count() == 1i64);
    many.remove(observe);
    assert(many.count() == 0i64);
    many.add(observe);
    many.clear();
    assert(many.count() == 0i64);
    return observed;
}`,
		files: [],
	},
	{
		name: "reflection container aliases and reference ownership",
		source: `struct Value
{
    array<long> values = array<long>();
    array<long>& Ref() { return values; }
    void Push(array<long>& target) { target.push(9i64); }
};
int main()
{
    Value value = Value();
    value.values.push(7i64);
    type<Value> info = type<Value>();
    field_info<Value> field(info.qualified_name, 0);
    array<int64> snapshot = field.get<array<int64>>(value);
    assert(snapshot[0] == 7i64);
    array<int64> replacement = array<int64>();
    replacement.push(8i64);
    field.set<array<int64>>(value, replacement);
    for (int i = 0; i < info.method_count(); ++i)
    {
        method_info<Value> method(info.qualified_name, i);
        if (method.name() == "Ref")
        {
            array<int64>& alias = method.invoke<array<int64>&>(value);
            alias.push(10i64);
        }
        if (method.name() == "Push") method.invoke<void>(value, &replacement);
    }
    assert(value.values[0] == 8i64 && value.values[1] == 10i64 && replacement[2] == 9i64);
    return 42;
}`,
		files: [],
	},
];

export const incompatibleReflectionSource = `struct Value { array<long> values = array<long>(); };
void main()
{
    Value value = Value();
    field_info<Value> field(type<Value>().qualified_name, 0);
    array<uint64> incompatible = field.get<array<uint64>>(value);
}`;

export const incompleteDeclarationSource = "int Missing(int value); void main() {}";

export const compileExtensionFailureFixtures = [
	{
		name: "compiler extension execution budget",
		source: 'import compiler "limited.vs"; int main() { print("unreachable"); return 42; }',
		files: [{
			path: "limited.vs",
			// Finite work still terminates if the execution policy regresses. Its
			// observable result prevents the compiler from discarding an empty loop.
			source: `import "compiler.vs";
export void register_extension(std::compiler::registration reg)
{
    int sum = 0;
    for (int index = 0; index < 2000000; ++index)
    {
        sum += index % 2;
    }
    assert(sum == 1000000);
}`,
		}],
		diagnosticMessagePattern: "instruction (?:limit|budget)|deadline",
	},
	{
		name: "compiler extension registration failure",
		source: 'import compiler "broken.vs"; int main() { print("unreachable"); return 42; }',
		files: [{
			path: "broken.vs",
			source: `import "compiler.vs";
export void register_extension(std::compiler::registration reg)
{
    assert(false);
}`,
		}],
		diagnosticMessagePattern: "assertion failed",
	},
	{
		name: "overlapping annotation name policies are rejected",
		source: 'import compiler "overlap.vs"; int main() { print("unreachable"); return 42; }',
		files: [{
			path: "overlap.vs",
			source: `import "compiler.vs";
export void register_extension(std::compiler::registration reg)
{
    std::compiler::annotation_definition annotation = std::compiler::annotation_definition();
    annotation.name = "@Tag";
    annotation.case_sensitive = false;
    reg.register_annotation(annotation);
    std::compiler::annotation_definition collision = std::compiler::annotation_definition();
    collision.name = "@tag";
    reg.register_annotation(collision);
}`,
		}],
		diagnosticMessagePattern: "conflicting annotation descriptions",
	},
	{
		name: "ordinary import cannot acquire compiler authority",
		source: `import "compiler.vs";
int main()
{
    std::compiler::registration reg = std::compiler::registration();
    return 42;
}`,
		files: [],
	},
];

// Valid argument types with invalid values must fail before capability fallback.
export const consoleFailureFixtures = [
	{ operation: "set_color", call: 'std::console::set_color("invalid", "default");' },
	{ operation: "clear_line", call: 'std::console::clear_line("invalid");' },
	{ operation: "move_cursor", call: "std::console::move_cursor(-1, 0);" },
	{ operation: "move_cursor", call: "std::console::move_cursor(0, 32767);" },
].map(({ operation, call }) => ({
	operation,
	source: `int main() { std::console::write("before"); ${call} print("unreachable"); return 42; }`,
}));

export const invalidConsoleSources = [
	"std::console::set_color(1, 2);",
	"std::console::clear_line(true);",
	"std::console::set_cursor_visible(1);",
	"std::console::move_cursor(1.5, 2);",
	"std::console::width(1);",
].map(call => `void main() { ${call} }`);

export const interactiveConsoleSource = `import "io.vs";
int main()
{
    assert(!std::console::is_interactive() && !std::console::is_output_terminal());
    assert(!std::console::supports_color() && !std::console::supports_cursor());
    std::console::write("prompt: ");
    std::console::flush();
    string line = std::io::read_line();
    assert(!std::console::reset_style() && !std::console::set_cursor_visible(true));
    std::console::write(line);
    return 42;
}`;

// Native VM reentry from bound callables must survive Asyncify unwind/rewind.
export const interactiveBoundMethodSource = `import "io.vs";
import "callable.vs";
class Reader
{
public:
    string read(int depth)
    {
        if (depth == 0) return std::io::read_line();
        fn(int)->string bound = std::bind_method<fn(int)->string>(this, "read");
        return bound(depth - 1);
    }
};
int main(string[] args)
{
    Reader reader = Reader();
    int depth = len(args) == 0 ? 32 : 128;
    print(reader.read(depth));
    print(reader.read(depth));
    return 42;
}`;

export const interactiveStateFixtures = [
	{
		name: "bound-method reentry",
		source: interactiveBoundMethodSource,
		// The Core bounds native reentry on targets without a native-stack query.
		overflowArgs: ["deep"],
		lines: ["first", "second"],
		output: "first\nsecond\n",
	},
	{
		name: "suspended static initialization and mutable captures",
		source: `import "io.vs";
function()->string make_reader()
{
    return [calls = 0]() mutable -> string
    {
        ++calls;
        static string prefix = std::io::read_line();
        string line = std::io::read_line();
        assert((calls == 1 && line == "first") || (calls == 2 && line == "second"));
        return prefix + ":" + line;
    };
}
int main()
{
    auto reader = make_reader();
    auto alias = reader;
    print(reader());
    print(alias());
    return 42;
}`,
		lines: ["prefix", "first", "second"],
		output: "prefix:first\nprefix:second\n",
	},
];
