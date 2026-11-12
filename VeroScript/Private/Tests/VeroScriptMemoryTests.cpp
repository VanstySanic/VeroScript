// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptAst.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptCompile.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"
#include "VeroScriptMemory.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptBytecodeLinker.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptRuntime.h"
#include <limits>
#include <cmath>
#include <iostream>
#include <sstream>
#include <thread>

using namespace VeroScript;
namespace
{
	bool Expect(bool Ok, const char* Label)
	{
		if (!Ok)
		{
			std::cerr << "FAIL: " << Label << '\n';
		}
		return Ok;
	}
	bool Run(const std::string& Source)
	{
		VeroScriptLexer Lexer(Source, "memory.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "memory.vs");
		auto Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			std::cerr << Parser.GetError().Location.Line << ":" << Parser.GetError().Message << '\n';
			return false;
		}
		VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Module)
		{
			std::cerr << Compiler.GetError().Message << '\n';
			return false;
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!VeroScriptBytecodeIO::SerializeBytecode(*Module, 42, Bytes, Error))
		{
			std::cerr << Error;
			return false;
		}
		Module = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 42, Error);
		if (!Module)
		{
			std::cerr << Error;
			return false;
		}
		std::ostringstream Out, Err;
		std::istringstream In;
		VeroScriptVm Vm(Out, In, Err);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(*Module, ERunMode::Script))
		{
			std::cerr << Vm.GetError().Location.Line << ":" << Vm.GetError().Message << '\n';
			return false;
		}
		return true;
	}
	bool Reject(const std::string& Source)
	{
		VeroScriptLexer L(Source, "reject.vs");
		VeroScriptParser P(L.Tokenize(), "reject.vs");
		auto A = P.ParseProgram();
		if (!A || P.HasError())
		{
			return true;
		}
		VeroScriptCompiler C;
		return !C.Compile(*A);
	}
} // namespace
int main()
{
	bool Ok = true;
	{
		FHostBindingRegistry Bindings;
		FHostTypeId TypeId;
		std::string Error;
		Ok &= Expect(Bindings.RegisterType("MemoryHostClass", EHostTypeKind::Class, TypeId, Error),
		    "worker identity host type registration");
		auto Handle = std::make_shared<FHostObject>();
		Handle->TypeName = "MemoryHostClass";
		Handle->TypeId = TypeId.Value;
		FValue HostValue = Handle;
		std::uint64_t ExpectedId = 0;
		Ok &= Expect(static_cast<bool>(Memory::ObjectId(HostValue, ExpectedId, &Bindings)),
		    "owner host identity query");
		FNativeFunction Probe;
		Probe.Name = "host_identity_probe";
		Probe.ReturnType = ENativeType::UInt64;
		Probe.UserData = &HostValue;
		Probe.Callback = [](const FNativeCallContext& Call, FValue& Out, std::string& OutError)
		{
			std::uint64_t Id = 0;
			const auto Result = Memory::ObjectId(*static_cast<const FValue*>(Call.UserData), Id,
			    Call.MemoryServices->MemoryHostBindings());
			if (!Result)
			{
				OutError = Result.MessageId;
				return false;
			}
			Out = Id;
			return true;
		};
		std::vector<FNativeFunction> Natives{std::move(Probe)};
		auto Catalog = Memory::MergeBindingCatalog(&Bindings, &Natives);
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.Bindings = Catalog.get();
		auto Compiled = VeroScriptCompile::CompileSource(R"VS(
import "concurrent.vs";
export void worker(channel<uint64> result) { result.send(host_identity_probe()); }
export void nested_worker(channel<uint64> result) {
    int child = std::concurrent::spawn(worker, result); std::concurrent::join_task(child);
}
int main() {
    uint64 owner = host_identity_probe();
    assert(owner != 0u64);
    channel<uint64> result = channel<uint64>();
    int named = std::concurrent::spawn("worker", result); std::concurrent::join_task(named);
    assert(result.recv() == owner);
    int callable = std::concurrent::spawn(worker, result); std::concurrent::join_task(callable);
    assert(result.recv() == owner);
    auto closure = [result]() -> void => result.send(host_identity_probe());
    int lambda = std::concurrent::spawn(closure); std::concurrent::join_task(lambda);
    assert(result.recv() == owner);
    int nested = std::concurrent::spawn("nested_worker", result); std::concurrent::join_task(nested);
    assert(result.recv() == owner);
    return 0;
}
)VS",
		    "worker-memory.vs", Options);
		auto Module = std::move(Compiled.Module);
		Ok &= Expect(Module != nullptr, "worker identity source compiles");
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed})
		{
			std::ostringstream Out, Err;
			std::istringstream In;
			VeroScriptVm Vm(Out, In, Err);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			Vm.SetMemoryHostBindings(&Bindings);
			Ok &= Expect(Module && Vm.Execute(*Module, ERunMode::Script, {}, &Natives),
			    "named, callable, lambda and nested workers preserve host identity metadata");
			if (!Module || Vm.HasError())
			{
				std::cerr << (Module ? Vm.GetError().Message : Compiled.Error.Message) << '\n';
			}
		}
	}
	{
		VeroScriptLexer Lexer("int main(){return 0;}", "native-limit.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "native-limit.vs");
		auto Program = Parser.ParseProgram();
		VeroScriptCompiler Compiler;
		std::vector<FNativeFunction> Natives((std::size_t{1} << 16) + 1);
		Ok &= Expect(Program && !Compiler.Compile(*Program, &Natives) &&
		        Compiler.GetError().MessageId == "diag.stdlib_native_limit",
		    "native table capacity has a distinct diagnostic");
		Natives.pop_back();
		Ok &= Expect(Program && !Compiler.Compile(*Program, &Natives) &&
		        Compiler.GetError().MessageId == "diag.stdlib_native_limit",
		    "native table capacity includes standard memory registrations");
		Natives.clear();
		Ok &= Expect(Program && Compiler.Compile(*Program) && !Compiler.HasError(),
		    "compiler recovers after rejecting native table capacity");
		FNativeFunction Reserved;
		Reserved.Name = Memory::GetBindingCatalog().GetNativeFunctions().front().Name;
		Natives.push_back(std::move(Reserved));
		Ok &= Expect(Program && !Compiler.Compile(*Program, &Natives) &&
		        Compiler.GetError().MessageId == "diag.stdlib_reserved_binding",
		    "native name collisions retain the reserved name diagnostic");
	}
	Ok &= Expect(Run(R"VS(
int main() {
    std::memory::buffer b = new std::memory::buffer(16);
    assert(b.size() == 16 && b.capacity() == 16);
    std::memory::write<uint32>(b, 1, 0x12345678u);
    assert(std::memory::read<uint32>(b, 1) == 0x12345678u);
    assert(b.get(1) == 120u);
    std::memory::write<int64>(b, 0, -9223372036854775807l);
    assert(std::memory::read<int64>(b, 0) == -9223372036854775807l);
    auto v = b.slice(0, 8);
    auto r = b.readonly_slice(0, 8);
    assert(std::memory::equal(v, 0, r, 0, 8));
    auto snapshot = std::memory::to_array(b);
    auto independent = std::memory::from_array(snapshot);
    assert(std::memory::object_id(b) != std::memory::object_id(independent));
    auto same = b;
    assert(std::memory::object_id(b) == std::memory::object_id(same));
    std::memory::fill(b, 0, 16, 1);
    assert(independent.get(1) != 1u);
    int x = 1; ref int y = x;
    assert(std::memory::address_of(&x) == std::memory::address_of(&y));
    assert(std::memory::data_address(b) != 0u64);
    return 0;
}
)VS"),
	    "script memory surface");
	Ok &= Expect(Run(R"VS(
std::memory::buffer global = new std::memory::buffer();
class Holder { public: std::memory::buffer bytes; std::memory::view empty; };
int main() {
    std::memory::buffer b = new std::memory::buffer();
    std::memory::view v = std::memory::view();
    std::memory::readonly_view r = std::memory::readonly_view();
    assert(b.size()==0 && global.size()==0 && v.size()==0 && r.size()==0);
    Holder h = new Holder(); assert(h.bytes.size()==0 && h.empty.size()==0);
    b.resize(12); b.set(0, 7); auto old=b.slice(0, 3); auto same=old;
    assert(old==same); b.reserve(12); assert(old.size()==3);
    b.resize(2); b.resize(12); assert(b.get(2)==0u && b.get(0)==7u);
    assert(old==same);
    std::memory::copy(b,2,b,0,8); std::memory::copy(b,0,b,2,8);
    assert(b.get(0)==7u);
    std::memory::write<uint64>(b,1,18446744073709551615u64,std::memory::byte_order::big);
    assert(std::memory::read<uint64>(b,1,std::memory::byte_order::big)==18446744073709551615u64);
    int[] items = {1,2}; int calls=0;
    uint64 a=std::memory::address_of(&items[calls++]); assert(calls==1);
    assert(a==std::memory::address_of(&items[0]));
    return 0;
}
)VS"),
	    "defaults, overlap, expired descriptor equality and once-only index");
	Ok &= Expect(Run(R"VS(
int global = 9;
int& identity(int& value) { return value; }
interface Readable { virtual int get() = 0; };
class Box : Readable { public: int value = 7; int get() { return value; } };
int main() {
    Box box = new Box(); Readable readable = box;
    assert(std::memory::object_id(box) == std::memory::object_id(readable));
    ref int member = box.value;
    assert(std::memory::address_of(&member) == std::memory::address_of(&box.value));
    assert(std::memory::address_of(&identity(&global)) == std::memory::address_of(&global));
    const int& stable = 17;
    assert(std::memory::address_of(&stable) != 0u64);
    int local = 3;
    auto captured = [&local]() -> uint64 => std::memory::address_of(&local);
    assert(captured() == std::memory::address_of(&local));
    auto sameFunction = captured;
    assert(std::memory::object_id(captured) == std::memory::object_id(sameFunction));
    auto bytes = new std::memory::buffer(4); auto key = bytes.slice(0, 2);
    map<std::memory::view,int> entries = {}; entries[key] = 7;
    auto alias = key; assert(entries[alias] == 7);
    bytes.resize(8); assert(key == alias && entries[alias] == 7);
    return 0;
}
)VS"),
	    "reference targets, interface/function identity and stable view map keys");

	Ok &= Expect(
	    Reject("int main(){int x=1;std::memory::address_of(x);return 0;}"), "explicit address reference");
	Ok &= Expect(Reject("int f(){return 1;} int main(){std::memory::address_of(&f());return 0;}"),
	    "no query-created temporary");
	Ok &= Expect(Reject("struct S { int n; }; int main(){S s;std::memory::object_id(s);return 0;}"),
	    "struct has no object identity");
	Ok &= Expect(Reject("int main(){std::memory::readonly_view r;std::memory::fill(r,0,0,1);return 0;}"),
	    "readonly compile check");
	Ok &= Expect(Reject("class Bad : std::memory::buffer {}; int main(){return 0;}"),
	    "core region inheritance rejection");
	{
		FMemoryContext C({64, 96});
		FValue B, V, Other, Empty;
		Ok &= Expect(static_cast<bool>(Memory::CreateBuffer(C, 32, B)), "budget buffer");
		Ok &= Expect(Memory::Resize(C, B, 65).Status == EMemoryStatus::LimitExceeded, "failed resize");
		std::int64_t N = 0;
		Memory::Size(C, B, N);
		Ok &= Expect(N == 32 && C.GetStats().OwnedBytes == 32, "failed resize preserves state");
		Ok &= Expect(static_cast<bool>(Memory::Slice(C, B, 32, 0, false, V)), "owned empty view");
		Ok &= Expect(static_cast<bool>(Memory::CreateEmptyView(false, Empty)) &&
		        static_cast<bool>(Memory::Fill(C, Empty, 0, 0, 1)),
		    "ownerless empty operations");
		Ok &= Expect(Memory::Copy(C, B, std::numeric_limits<std::int64_t>::max(), B, 0, 1).Status ==
		        EMemoryStatus::InvalidArgument,
		    "overflow range");
		Ok &= Expect(static_cast<bool>(Memory::Resize(C, B, 33)), "structure changes expire empty views");
		Ok &= Expect(Memory::Size(C, V, N).Status == EMemoryStatus::Expired, "expired zero-size view");
		FMemoryContext Foreign;
		Ok &= Expect(
		    Memory::Size(Foreign, B, N).Status == EMemoryStatus::InvalidArgument, "cross domain rejected");
		EMemoryStatus ThreadStatus = EMemoryStatus::Ok;
		std::thread T(
		    [&]
		{
			ThreadStatus = Memory::Size(C, B, N).Status;
		});
		T.join();
		Ok &= Expect(ThreadStatus == EMemoryStatus::WrongThread, "owner thread enforced");
		FValue Forged = std::make_shared<FHostObject>();
		auto H = std::get<FHostObjectHandle>(Forged);
		H->TypeName = "std.memory.buffer";
		H->TypeId = 0xfffffff0u;
		H->Payload = reinterpret_cast<void*>(1);
		Ok &= Expect(Memory::Size(C, Forged, N).Status == EMemoryStatus::TypeMismatch,
		    "forged payload not dereferenced");
		std::uint64_t Id = 0;
		Ok &= Expect(Memory::ObjectId(Forged, Id).Status == EMemoryStatus::TypeMismatch,
		    "forged core identity rejected");
		const auto Real = std::get<FHostObjectHandle>(B);
		auto Lookalike = std::make_shared<FHostObject>(*Real);
		FValue Aliased = FHostObjectHandle(Real, Lookalike.get());
		Ok &= Expect(Memory::Size(C, Aliased, N).Status == EMemoryStatus::TypeMismatch,
		    "region ownership cannot authorize a different aliased object");
		FValue Unowned = FHostObjectHandle(Real.get(), [](FHostObject*)
		{
		});
		Ok &= Expect(Memory::Size(C, Unowned, N).Status == EMemoryStatus::TypeMismatch,
		    "region address cannot authorize an unrelated control block");
		const auto Retained = C.GetStats().OwnedBytes;
		FValue Temporary;
		Ok &= Expect(static_cast<bool>(Memory::CreateBuffer(C, 8, Temporary)), "temporary region");
		std::weak_ptr<FHostObject> Weak = std::get<FHostObjectHandle>(Temporary);
		Temporary = std::int32_t{0};
		Ok &= Expect(Weak.expired() && C.GetStats().OwnedBytes == Retained,
		    "weak region control block does not retain its allocation");
		int Released = 0;
		std::uint8_t External[8]{};
		auto Owner = std::make_shared<int>(1);
		Ok &= Expect(Memory::WrapExternalRegion(
		                 C, External, 65, false, Owner,
		                 [](void* P)
		{
			++*static_cast<int*>(P);
		},
		                 &Released, Other)
		                 .Status == EMemoryStatus::LimitExceeded &&
		        Released == 0,
		    "registration failure does not adopt");
		Ok &= Expect(static_cast<bool>(Memory::WrapExternalRegion(
		                 C, External, 8, false, Owner,
		                 [](void* P)
		{
			++*static_cast<int*>(P);
		},
		                 &Released, Other)),
		    "external registration");
		FMemoryLease L;
		Ok &= Expect(static_cast<bool>(Memory::AcquireLease(C, Other, false, L)), "external read lease");
		Ok &= Expect(
		    Memory::InvalidateExternalRegion(C, Other).Status == EMemoryStatus::Busy, "invalidation busy");
		Other = std::int32_t{0};
		C.Close();
		Ok &= Expect(Released == 0, "lease retains owner after domain closes");
		Ok &= Expect(
		    static_cast<bool>(Memory::ReleaseLease(L.Token)) && Released == 1, "release after context close");
	}
	{
		FMemoryContext C;
		FValue B, Value;
		Memory::CreateBuffer(C, 16, B);
		const auto RejectWork = FMemoryWork{[](void*, std::uint64_t)
		{
			return false;
		},
		    nullptr};
		Ok &= Expect(
		    Memory::Write(C, B, 0, "uint64", EMemoryByteOrder::Little, FValue{std::uint64_t{99}}, RejectWork)
		            .Status == EMemoryStatus::Interrupted,
		    "scalar precharge");
		Memory::Read(C, B, 0, "uint64", EMemoryByteOrder::Little, Value);
		Ok &= Expect(std::get<std::uint64_t>(Value) == 0, "scalar interrupted atomically");
		Memory::Write(
		    C, B, 1, "int64", EMemoryByteOrder::Big, FValue{std::numeric_limits<std::int64_t>::min()});
		Memory::Read(C, B, 1, "int64", EMemoryByteOrder::Big, Value);
		Ok &= Expect(std::get<std::int64_t>(Value) == std::numeric_limits<std::int64_t>::min(),
		    "signed minimum portable decoding");
		Memory::Write(C, B, 0, "double", EMemoryByteOrder::Little, FValue{-0.0});
		Memory::Read(C, B, 0, "double", EMemoryByteOrder::Little, Value);
		Ok &= Expect(std::signbit(std::get<double>(Value)), "negative zero retained");
		Memory::Write(
		    C, B, 0, "float", EMemoryByteOrder::Big, FValue{std::numeric_limits<float>::infinity()});
		Memory::Read(C, B, 0, "float", EMemoryByteOrder::Big, Value);
		Ok &= Expect(std::isinf(std::get<float>(Value)), "IEEE infinity");
	}
	{
		const auto& Core = Memory::GetBindingCatalog();
		auto Merged = Memory::MergeBindingCatalog(&Core, nullptr);
		Ok &= Expect(Merged->GetTypes().size() == 4, "trusted standard metadata merge is idempotent");
		VeroScriptLexer L(
		    "int main(){auto b=new std::memory::buffer(4);b.set(1,8);assert(b.get(1)==8u);return 0;}",
		    "package-memory.vs");
		VeroScriptParser P(L.Tokenize(), "package-memory.vs");
		auto Program = P.ParseProgram();
		VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		std::vector<FNativeFunction> Natives(1);
		Natives[0].Name = "unrelated_host_function";
		Natives[0].Callback = [](const FNativeCallContext&, FValue&, std::string&)
		{
			return false;
		};
		VeroScriptLexer DL(
		    "export std::memory::buffer dependency=new std::memory::buffer(3);", "memory-library.vs");
		VeroScriptParser DP(DL.Tokenize(), "memory-library.vs");
		auto DependencyProgram = DP.ParseProgram();
		VeroScriptCompiler DC;
		auto Dependency = DC.Compile(*DependencyProgram, &Natives);
		std::unique_ptr<FBytecodeModule> Linked;
		std::string LinkError;
		Ok &= Expect(
		    Module && Dependency && LinkBytecodeModules(*Module, {Dependency.get()}, Linked, LinkError),
		    "memory package linking");
		if (!Linked)
		{
			std::cerr << "link: " << LinkError << "; dependency: " << DC.GetError().Message << '\n';
		}
		std::vector<std::uint8_t> Bytes;
		if (Linked && VeroScriptBytecodeIO::SerializeBytecode(*Linked, 99, Bytes, LinkError))
		{
			Module = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 99, LinkError);
		}
		else
		{
			Ok &= Expect(false, "linked memory bytecode serialization");
		}
		if (Module)
		{
			for (auto& F : Module->Functions)
			{
				F.DebugLines.clear();
				F.LocalNames.clear();
			}
		}
		std::ostringstream O, E;
		std::istringstream I;
		VeroScriptVm Vm(O, I, E);
		VeroScript::Test::Configure(Vm);
		Ok &= Expect(Module && Vm.Execute(*Module, ERunMode::Script, {}, &Natives),
		    "memory bytecode independent of host native index");
		if (Dependency)
		{
			Program->PrecompiledPackageModules.push_back(
			    std::shared_ptr<const FBytecodeModule>(std::move(Dependency)));
			VeroScriptCompiler SeededCompiler;
			auto Seeded = SeededCompiler.Compile(*Program, &Natives);
			Bytes.clear();
			Ok &= Expect(Seeded && VeroScriptBytecodeIO::SerializeBytecode(*Seeded, 100, Bytes, LinkError),
			    "package-seeded memory compilation has unique standard enums");
			if (!LinkError.empty())
			{
				std::cerr << LinkError << '\n';
			}
			Ok &= Expect(Seeded && Vm.Execute(*Seeded, ERunMode::Script, {}, &Natives),
			    "package-seeded memory execution");
		}
	}
	{
		FMemoryContext C({256000, 300000});
		for (const bool Packed : {false, true})
		{
			for (unsigned Action = 0; Action < 3; ++Action)
			{
				auto Source = std::make_shared<FDynamicArray>();
				Source->TypeName = "array<uint8>";
				Source->ElementTypeName = "uint8";
				if (Packed)
				{
					Source->ScalarElements = std::vector<std::uint8_t>{7, 9};
				}
				else
				{
					Source->Elements = {std::uint8_t{7}, std::uint8_t{9}};
				}
				struct FChange
				{
					FDynamicArrayHandle& Source;
					unsigned Action;
					bool Packed;
				} Change{Source, Action, Packed};
				FMemoryWork Work{[](void* Data, std::uint64_t)
				{
					auto& Change = *static_cast<FChange*>(Data);
					if (Change.Action == 0)
					{
						if (Change.Packed)
						{
							std::get<std::vector<std::uint8_t>>(Change.Source->ScalarElements).clear();
						}
						else
						{
							Change.Source->Elements.clear();
						}
					}
					else if (Change.Action == 1)
					{
						Change.Source->ScalarElements = std::vector<std::int32_t>{7, 9};
					}
					else
					{
						Change.Source.reset();
					}
					return true;
				}, &Change};
				FValue Output = std::int32_t{71};
				const auto Result = Memory::FromArray(C, Source, Output, Work);
				if (Action < 2)
				{
					const auto Expected = Action == 0 ? EMemoryStatus::InvalidArgument : EMemoryStatus::TypeMismatch;
					Ok &= Expect(Result.Status == Expected && std::get_if<std::int32_t>(&Output) && std::get<std::int32_t>(Output) == 71,
					    "array byte conversion rechecks storage after work callbacks and leaves failure output unchanged");
				}
				else
				{
					FValue Byte;
					Ok &= Expect(Result && !Source && Memory::Read(C, Output, 0, "uint8", EMemoryByteOrder::Little, Byte) && std::get<std::uint8_t>(Byte) == 7,
					    "array byte conversion retains the source owner across work callbacks");
				}
			}
		}
	}
	{
		FMemoryContext C({256000, 300000});
		FValue B;
		auto Source = std::make_shared<FDynamicArray>();
		Source->ElementTypeName = "uint8";
		Source->ScalarElements = std::vector<std::uint8_t>{7};
		B = std::int32_t{71};
		const FMemoryWork Close{[](void* Data, std::uint64_t)
		{
			static_cast<FMemoryContext*>(Data)->Close();
			return true;
		}, &C};
		Ok &= Expect(!Memory::FromArray(C, Source, B, Close) && std::get_if<std::int32_t>(&B) && std::get<std::int32_t>(B) == 71,
		    "array byte conversion rejects context closure during work checks");
	}
	{
		FMemoryContext C({256000, 300000});
		FValue B;
		Memory::CreateBuffer(C, 131072, B);
		unsigned Checks = 0;
		FMemoryWork Work{[](void* P, std::uint64_t)
		{
			return ++*static_cast<unsigned*>(P) == 1;
		},
		    &Checks};
		Ok &= Expect(Memory::Fill(C, B, 0, 131072, 9, Work).Status == EMemoryStatus::Interrupted,
		    "bulk cancellation at chunk boundary");
		FValue N;
		Memory::Read(C, B, 65535, "uint8", EMemoryByteOrder::Little, N);
		Ok &= Expect(std::get<std::uint8_t>(N) == 9, "completed chunk retained");
		Memory::Read(C, B, 65536, "uint8", EMemoryByteOrder::Little, N);
		Ok &= Expect(std::get<std::uint8_t>(N) == 0, "unexecuted chunk unchanged");
		FMemoryLease Read;
		Memory::AcquireLease(C, B, false, Read);
		FValue Other;
		Memory::CreateBuffer(C, 1, Other);
		Ok &= Expect(Memory::Copy(C, Other, 0, B, 0, 1).Status == EMemoryStatus::Ok,
		    "read lease permits concurrent readers");
		Ok &= Expect(Memory::Copy(C, B, 0, Other, 0, 1).Status == EMemoryStatus::Busy,
		    "failed second guard releases first");
		Ok &= Expect(static_cast<bool>(Memory::Fill(C, Other, 0, 1, 4)), "guard cleanup after conflict");
		Memory::ReleaseLease(Read.Token);
		bool WorkerOk = false;
		std::thread Worker(
		    [&]
		{
			auto W = C.CreateWorker();
			FValue WB;
			WorkerOk = Memory::CreateBuffer(*W, 200000, WB).Status == EMemoryStatus::LimitExceeded;
		});
		Worker.join();
		Ok &= Expect(WorkerOk, "worker shares retained-byte budget");
	}
	{
		std::vector<FNativeFunction> Natives;
		for (unsigned Flag : {1u, 3u, 0u})
		{
			FNativeFunction N;
			N.Name = Flag == 1 ? "mutable_address" : Flag == 3 ? "const_address"
			                                                   : "value_address";
			N.ReturnType = ENativeType::Bool;
			N.ParamTypes = {ENativeType::Int, ENativeType::UInt64};
			N.ParamFlags = {static_cast<std::uint8_t>(Flag), 0};
			N.UserData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(Flag));
			N.Callback = [](const FNativeCallContext& Call, FValue& Out, std::string& OutError)
			{
				if (!Call.MemoryServices)
				{
					OutError = "native callback omitted memory services";
					return false;
				}
				std::uint64_t Address = 0;
				const auto R = Call.MemoryServices->MemoryArgumentAddress(0, Address);
				Out = Call.UserData ? R && Address == std::get<std::uint64_t>(Call.Args[1])
				                    : R.Status == EMemoryStatus::Unsupported;
				if (std::get<bool>(Out) && Call.UserData)
				{
					std::vector<FValue> Arguments{std::int32_t{32}};
					FValue Returned;
					std::string Error;
					std::uint64_t AfterReentry = 0;
					Out = Call.ScriptInvoker->InvokeFunction("nested", Arguments, Returned, Error) && std::get<std::int32_t>(Returned) == 9 && Call.MemoryServices->MemoryArgumentAddress(0, AfterReentry) && AfterReentry == Address;
				}
				if (!std::get<bool>(Out))
				{
					std::cerr << "native flag " << reinterpret_cast<std::uintptr_t>(Call.UserData)
					          << " status " << static_cast<unsigned>(R.Status) << " actual " << Address
					          << " expected " << std::get<std::uint64_t>(Call.Args[1]) << "\n";
				}
				return true;
			};
			Natives.push_back(std::move(N));
		}
		VeroScriptLexer L("export int nested(int depth){if(depth>0)return nested(depth-1);int q=9;"
		                  "assert(value_address(q,0u64));return q;}"
		                  "int main(){int x=4;auto a=std::memory::address_of(&x);assert(mutable_address(ref "
		                  "x,a));assert(const_address(ref x,a));assert(value_address(x,a));return 0;}",
		    "native-address.vs");
		VeroScriptParser P(L.Tokenize(), "native-address.vs");
		auto A = P.ParseProgram();
		VeroScriptCompiler Compiler;
		auto M = Compiler.Compile(*A, &Natives);
		std::ostringstream O, E;
		std::istringstream I;
		VeroScriptVm Vm(O, I, E);
		VeroScript::Test::Configure(Vm);
		Ok &= Expect(M && Vm.Execute(*M, ERunMode::Script, {}, &Natives),
		    "native original mutable/const references survive nested reentry; by-value rejection");
		if (Vm.HasError())
		{
			std::cerr << Vm.GetError().Message << '\n';
		}
		FScriptCapabilities Permissions;
		Permissions.bAllowMemoryAddress = false;
		Vm.SetCapabilities(Permissions);
		Ok &= Expect(M && !Vm.Execute(*M, ERunMode::Script, {}, &Natives) &&
		        Vm.GetError().MessageId == "diag.memory_permission",
		    "address permission enforcement");
	}
	Ok &= Expect(Reject("namespace std::memory { class buffer {}; } int main(){return 0;}"),
	    "script cannot replace standard region type");
	Ok &= Expect(Reject("namespace std::memory { using view=int; } int main(){return 0;}"),
	    "script cannot alias over standard region type");
	FMemoryContext Context({16, 32});
	FValue Buffer, View, ReadOnly;
	Ok &= Expect(static_cast<bool>(Memory::CreateBuffer(Context, 8, Buffer)), "allocate");
	Ok &= Expect(static_cast<bool>(Memory::Slice(Context, Buffer, 0, 8, false, View)), "view");
	Ok &= Expect(static_cast<bool>(Memory::Slice(Context, Buffer, 0, 8, true, ReadOnly)), "readonly");
	Ok &= Expect(Memory::Fill(Context, ReadOnly, 0, 0, 1).Status == EMemoryStatus::ReadOnly,
	    "zero write checks readonly");
	FMemoryLease Lease;
	Ok &= Expect(static_cast<bool>(Memory::AcquireLease(Context, Buffer, true, Lease)), "lease");
	Ok &= Expect(
	    Memory::Resize(Context, Buffer, 9).Status == EMemoryStatus::Busy, "structural lease exclusion");
	Ok &= Expect(Memory::Fill(Context, View, 0, 1, 1).Status == EMemoryStatus::Busy, "byte lease exclusion");
	Lease.WritableData[0] = 42;
	std::thread Release(
	    [&]
	{
		Ok &= Expect(static_cast<bool>(Memory::ReleaseLease(Lease.Token)), "cross thread release");
	});
	Release.join();
	Ok &= Expect(Memory::ReleaseLease(Lease.Token).Status == EMemoryStatus::Expired, "duplicate token");
	Ok &= Expect(static_cast<bool>(Memory::Resize(Context, Buffer, 16)), "grow");
	std::int64_t Size = 0;
	Ok &= Expect(Memory::Size(Context, View, Size).Status == EMemoryStatus::Expired, "generation expiration");
	Ok &= Expect(Context.GetStats().OwnedBytes == 16 && Context.GetStats().PeakOwnedBytes == 24,
	    "peak grow accounting");
	Ok &= Expect(Memory::CreateBuffer(Context, 17, View).Status == EMemoryStatus::LimitExceeded,
	    "single allocation limit");
	Context.Close();
	Ok &=
	    Expect(Memory::Size(Context, Buffer, Size).Status == EMemoryStatus::Expired, "closed access domain");
	return Ok ? 0 : 1;
}
