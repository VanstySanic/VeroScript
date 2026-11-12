// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

internal static class CoreTypeContractTests
{
	internal static void Run()
	{
		string path = Path.Combine(Path.GetTempPath(), "veroscript-type-contracts-" + Guid.NewGuid().ToString("N") + ".vs");
		try
		{
			File.WriteAllText(path, Source);
			string? backend = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND");
			foreach (VeroScriptExecutionBackend policy in Enum.GetValues<VeroScriptExecutionBackend>())
			{
				if (policy == VeroScriptExecutionBackend.Jit && string.IsNullOrEmpty(backend))
				{
					continue;
				}
				using var sourceSession = CreateSession(policy, backend);
				Require(sourceSession.LoadSource(Source, path).Success, sourceSession, "load alias contracts");
				Require(sourceSession.Run().Success, sourceSession, "execute alias contracts: " + policy);
				var (bytes, built) = sourceSession.BuildPackageFile(path, new()
				{
					PackageName = "csharp-type-contracts",
					StripDebugInfo = true
				});
				Require(built.Success && bytes.Length != 0, sourceSession, "build alias package");
				using var package = VeroScriptPackage.Read(bytes);
				using var packageSession = CreateSession(policy, backend);
				Require(packageSession.LoadPackage(package).Success, packageSession, "load alias package");
				Require(packageSession.Run().Success, packageSession, "execute serialized alias contracts: " + policy);
				CheckImplicitMacroImports(policy, backend);
			}
			CheckRejectedTypes();
			CheckTypeResolutionDepth();
		}
		finally
		{
			File.Delete(path);
		}
	}

	private static void CheckImplicitMacroImports(VeroScriptExecutionBackend policy, string? backend)
	{
		using var session = CreateSession(policy, backend);
		session.ApplyConfiguration(new()
		{
			ExecutionBackend = policy,
			ImplicitImportPaths = ["macro-sdk/global.vs"],
			SourceOverlays = [
				new("macro-sdk/global.vs", "#define PRIVATE_VALUE 999\n#define_export SDK_VALUE 42\n"),
				new("macro-sdk/dep.vs", "export int answer(){return SDK_VALUE;}\n")]
		});
		Require(session.LoadSource("#ifdef PRIVATE_VALUE\n#error private macro leaked\n#endif\n"
			+ "import \"dep.vs\"; int main(){return answer();}", "macro-sdk/main.vs").Success,
			session, "load implicit public macros: " + policy);
		var result = session.Run();
		Require(result.Success && result.ExitCode == 42, session, "execute implicit public macros: " + policy);
	}

	private static VeroScriptSession CreateSession(VeroScriptExecutionBackend policy, string? backend)
	{
		var session = new VeroScriptSession();
		try
		{
			if (!string.IsNullOrEmpty(backend))
			{
				session.SetJitBackend(backend);
			}
			session.ApplyConfiguration(new() { ExecutionBackend = policy });
			var signature = new VeroScriptNativeType(VeroScriptNativeTypeKind.Function, "fn(int64)->int64");
			session.RegisterFunction("host_echo", signature, [new(signature)],
				(context, result) => result.Set(context.GetArgument(0)));
			session.RegisterFunction("host_echo_ref", signature, [new(signature, VeroScriptParameterFlags.Ref)],
				(context, result) => result.Set(context.GetMutableArgument(0)));
			var array = new VeroScriptNativeType(VeroScriptNativeTypeKind.Array, null, VeroScriptNativeType.Int64);
			session.RegisterFunction("host_first", VeroScriptNativeType.Int64, [new(array)],
				(context, result) =>
				{
					using VeroScriptValue value = context.GetArgument(0).GetSequenceElement(0);
					result.Set(value.GetInt64());
				});
			return session;
		}
		catch
		{
			session.Dispose();
			throw;
		}
	}

	private static void CheckTypeResolutionDepth()
	{
		using var session = new VeroScriptSession();
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
		session.HotReloadEnabled = true;
		foreach (bool structures in new[] { false, true })
		{
			var source = new System.Text.StringBuilder();
			for (int index = 0; index <= 400; ++index)
			{
				source.Append(structures ? "struct " : "class ").Append("Node_").Append(index).Append("{public:");
				if (index < 400)
				{
					source.Append("Node_").Append(index + 1).Append(" next=Node_").Append(index + 1).Append("();");
				}
				source.Append("};\n");
			}
			source.Append("Node_0 root=Node_0();");
			Require(!session.LoadSource(source.ToString(), "deep-type-dependencies.vs").Success,
				session, "deep type dependencies fail without terminating the managed host");
			Require(session.GetDiagnostics().Any(item => item.Message.Contains("nesting limit exceeded", StringComparison.Ordinal)
				&& item.Line != 0), session, "type depth diagnostic retains its source location");
			Require(session.LoadSource("class Link{public:Link next;int value=7;};"
				+ "class Base{public:int value=35;};class Derived:public Base{};struct Box<T>{T value;};"
				+ "int main(){Link link=Link();Derived item=Derived();Box<int> box=Box<int>();"
				+ "box.value=item.value;return box.value+link.value;}", "after-type-depth.vs").Success,
				session, "same session accepts inheritance, generics and legal self references after a type depth failure");
			var result = session.Run();
			Require(result.Success && result.ExitCode == 42, session, "same session executes normally after a type depth failure");
		}
	}

	private static void CheckRejectedTypes()
	{
		using var session = new VeroScriptSession();
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
		foreach (string source in new[]
		{
			"struct Bad<T>{Bad<T> next;};int main(){Bad<int> value=Bad<int>();return 0;}",
			"class Bad<T>:public Bad<T>{};int main(){Bad<int> value=Bad<int>();return 0;}",
			"class Bad<T>{public:Bad<array<T>> next;};int main(){Bad<int> value=Bad<int>();return 0;}"
		})
		{
			Require(!session.LoadSource(source).Success, session, "recursive values, inheritance and unbounded specialization remain rejected");
			Require(session.LoadSource("class Link<T>{public:T value;Link<T> next;};"
				+ "int main(){Link<int> first=Link<int>();Link<int> second=Link<int>();second.value=42;"
				+ "first.next=second;return first.next.value;}").Success,
				session, "same session accepts a legal generic reference after rejecting recursive types");
			var recovered = session.Run();
			Require(recovered.Success && recovered.ExitCode == 42, session, "generic reference executes after a failed instantiation");
		}
		foreach (string target in new[] { "fn(ref uint64)->void", "fn(ref int)->void", "fn(int64)->void" })
		{
			Require(!session.LoadSource("void main(){fn(ref long)->void original=[](long& n)->void{};"
				+ target + " invalid=original;}").Success, session,
				"signedness, width and reference direction remain distinct");
		}
		Require(!session.LoadSource("void main(){fn(const ref long)->long original=[](const long& n)->long{return n;};"
			+ "fn(ref int64)->int64 invalid=original;}").Success, session, "const-reference qualifiers remain distinct");
		const string mismatch = """
            struct Box { array<long> values=array<long>(); };
            void main(){Box box=Box();field_info<Box> field(type<Box>().qualified_name,0);
                array<uint64> invalid=field.get<array<uint64>>(box);}
            """;
		Require(session.LoadSource(mismatch).Success && !session.Run().Success,
			session, "reflection rejects a container with different signedness");
	}

	private static void Require(bool condition, VeroScriptSession session, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("Core types: " + message + " / " + session.GetLastError()
				+ " / " + string.Join("; ", session.GetDiagnostics().Select(item => item.Message)));
		}
	}

	private const string Source = """
        class Link<T>{public:T value;Link<T> next;};
        struct Box {
            array<long> values=array<long>();
            fn(long)->long callback;
            array<long>& Ref(){return values;}
            const array<long>& ConstRef() const{return values;}
            void Push(array<long>& target){target.push(9i64);}
            fn(long)->long ReadCallback(){return callback;}
            long Invoke(fn(long)->long input){return input(3i64);}
        };
        void main(){
            Link<int> first=Link<int>();Link<int> second=Link<int>();
            assert(first.next==nullptr);second.value=42;first.next=second;
            assert(first.next.value==42);first.next=nullptr;
            fn(long)->long original=[](long n)->long{return n+1i64;};
            fn(int64)->int64 echoed=host_echo(original); assert(echoed(6i64)==7i64);
            fn(int64)->int64 echoed_ref=host_echo_ref(ref original); assert(echoed_ref(7i64)==8i64);
            fn(ref long)->void bump=[](long& n)->void{n+=1i64;};
            fn(ref int64)->void canonical=bump; int64 number=7; canonical(ref number); assert(number==8);
            fn(ref unsigned long)->void bump_unsigned=[](unsigned long& n)->void{n+=1u64;};
            fn(ref uint64)->void canonical_unsigned=bump_unsigned;
            uint64 unsigned_number=7u64; canonical_unsigned(ref unsigned_number); assert(unsigned_number==8u64);
            array<long> source=array<long>();source.push(7i64);assert(host_first(source)==7i64);
            Box box=Box();box.values=source;box.callback=original;
            type<Box> info=type<Box>();
            field_info<Box> values(info.qualified_name,0);field_info<Box> callback(info.qualified_name,1);
            array<int64> snapshot=values.get<array<int64>>(box);assert(snapshot[0]==7i64);
            array<int64> replacement=array<int64>();replacement.push(8i64);
            values.set<array<int64>>(box,replacement);
            fn(int64)->int64 read=callback.get<fn(int64)->int64>(box);assert(read(1i64)==2i64);
            callback.set<fn(int64)->int64>(box,read);
            int calls=0;
            for(int i=0;i<info.method_count();++i){method_info<Box> method(info.qualified_name,i);
                if(method.name()=="Ref"){array<int64>& alias=method.invoke<array<int64>&>(box);alias.push(10i64);calls+=1;}
                if(method.name()=="ConstRef"){const array<int64>& alias=method.invoke<const array<int64>&>(box);
                    assert(alias[0]==8i64);calls+=1;}
                if(method.name()=="Push"){method.invoke<void>(box,&replacement);calls+=1;}
                if(method.name()=="ReadCallback"){fn(int64)->int64 alias=method.invoke<fn(int64)->int64>(box);
                    assert(alias(2i64)==3i64);calls+=1;}
                if(method.name()=="Invoke"){assert(method.invoke<int64>(box,read)==4i64);calls+=1;}
            }
            assert(calls==5 && box.values[0]==8i64 && box.values[1]==10i64 && replacement[2]==9i64);
        }
        """;
}
