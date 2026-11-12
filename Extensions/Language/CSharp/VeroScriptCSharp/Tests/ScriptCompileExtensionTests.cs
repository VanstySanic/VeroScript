// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

internal static class ScriptCompileExtensionTests
{
	internal static void Run()
	{
		string directory = Path.Combine(Path.GetTempPath(), "veroscript-script-extension-" + Guid.NewGuid());
		Directory.CreateDirectory(directory);
		try
		{
			string path = Path.Combine(directory, "extension.vs");
			File.WriteAllText(path, """
                import "compiler.vs";
                using callback=fn(std::compiler::context,std::compiler::output)->void;
                callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
                    out.add_declaration(ctx.top_scope(),"int generated(){return 17;}");};}
                export void register_extension(std::compiler::registration reg){
                    std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
                    std::compiler::processor_definition d=std::compiler::processor_definition();d.id="managed.generated";d.version="0.1.1";
                    d.phases=array<std::compiler::phase>{std::compiler::phase::members};
                    d.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};
                    d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);}
                """);
			using var session = new VeroScriptSession();
			string standardLibrary = Environment.GetEnvironmentVariable("VEROSCRIPT_STDLIB_DIR") ?? throw new InvalidOperationException("Standard-library test assets are required.");
			session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm, StandardLibrarySearchPaths = [standardLibrary] });
			session.SetScriptCompileExtensions(new() { Roots = [new(path)] });
			var loaded = session.LoadSource("int main(){return generated();}", Path.Combine(directory, "main.vs"));
			if (!loaded.Success)
			{
				throw new InvalidOperationException(string.Join("\n", session.GetDiagnostics().Select(d => d.Message)));
			}

			var result = session.Run();
			if (!result.Success || result.ExitCode != 17)
			{
				throw new InvalidOperationException("Managed execution: " + result.ExitCode);
			}

			session.SetScriptCompileExtensions(new() { AllowSourceImports = false });
			if (session.LoadSource("import compiler \"extension.vs\";int main(){return 0;}", Path.Combine(directory, "main.vs")).Success)
			{
				throw new InvalidOperationException("Disabled source compilation imports were accepted.");
			}

			session.SetScriptCompileExtensions(null);
		}
		finally
		{
			File.Delete(Path.Combine(directory, "extension.vs"));
			Directory.Delete(directory);
		}
	}
}
