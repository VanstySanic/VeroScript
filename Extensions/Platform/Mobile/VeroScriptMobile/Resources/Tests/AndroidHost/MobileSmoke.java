// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import android.content.res.AssetManager;
import com.vanstysanic.veroscript.android.VeroScriptEngine;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public final class MobileSmoke {
	private static void require(boolean condition, String message) {
		if (!condition) { throw new AssertionError(message); }
	}
	public static void main(String[] args) throws Exception {
		Path root = Files.createTempDirectory("vero-jni-📦");
		try {
			Path stdlib = Path.of(args[0]);
			try (var files = Files.walk(stdlib)) {
				for (Path source : files.toList()) {
					Path destination = root.resolve("veroscript/stdlib").resolve(stdlib.relativize(source));
					if (Files.isDirectory(source)) { Files.createDirectories(destination); }
					else
					{
						Files.copy(source, destination);
					}
				}
			}
			Files.createDirectories(root.resolve("scripts"));
			Files.writeString(root.resolve("scripts/dep.vs"), "export int value(){return 42;}");
			Files.writeString(root.resolve("scripts/loop.vs"), "void main(){while(true){}}");
			Files.writeString(root.resolve("scripts/main.vs"),
				"import \"dep.vs\"; void main(){print(\"移动 📦\");print(value());print(\"a\\0b\");}");
			Path cache = root.resolve("cache");
			try (var missingVm = new VeroScriptEngine(new AssetManager(root), "scripts", null,
					100000, 5000, 256, 16L * 1024 * 1024, 32L * 1024 * 1024,
					VeroScriptEngine.ExecutionBackend.VM, root.resolve("missing-vm").toString(), "", null, "")) {
				boolean rejected = false;
				try { rejected = !missingVm.execute("void main(){print(1);}", "/veroscript/scripts/missing-vm.vs").success; }
				catch (IllegalStateException expected)
				{
					rejected = true;
				}
				require(rejected, "JNI ignored the explicit VM path");
			}
			if (args.length > 2) {
				try (var explicitVm = new VeroScriptEngine(new AssetManager(root), "scripts", null,
						100000, 5000, 256, 16L * 1024 * 1024, 32L * 1024 * 1024,
						VeroScriptEngine.ExecutionBackend.VM, args[2], "", null, "")) {
					require(explicitVm.executeAsset("scripts/main.vs").success, "JNI could not use the selected VM");
				}
			}
			try (var limited = new VeroScriptEngine(new AssetManager(root), "scripts", null, 100000, 5000, 256, 4, 8)) {
				require(!limited.execute("void main(){auto b=new std::memory::buffer(5);}",
					"/veroscript/scripts/limit.vs").success, "JNI memory budget was ignored");
			}
			try (var unsigned = new VeroScriptEngine(new AssetManager(root), "scripts", null, 100000, 5000, 256, -1L, -1L)) {
				var bytes = unsigned.execute("void main(){auto b=new std::memory::buffer(8);std::memory::write<uint64>(b,0,42u64);assert(std::memory::read<uint64>(b,0)==42u64);}",
					"/veroscript/scripts/memory.vs");
				require(bytes.success, bytes.error + bytes.diagnostics);
			}
			try (var engine = new VeroScriptEngine(new AssetManager(root), "scripts", cache.toString(), 0, 5000, 256)) {
				var result = engine.executeAsset("scripts/main.vs");
				require(result.success && result.output.equals("移动 📦\n42\na\0b\n"), result.diagnostics + result.error + result.output);
				try (var files = Files.walk(cache)) {
					require(files.anyMatch(path -> path.toString().endsWith(".vsbc")), "JNI cache path was ignored");
				}
				result = engine.execute("import \"io.vs\"; void main(){std::io::read_line();}", "/veroscript/scripts/input.vs");
				require(!result.success && result.error.contains("console input capability"),
					"Input policy failed: " + result.error + result.diagnostics);
				try {
					engine.execute("void main(){}", "relative/📦.vs");
					throw new AssertionError("Relative entry label accepted");
				}
				catch (IllegalStateException expected)
				{
					/* Native exception translation. */
				}
				require(engine.executeAsset("scripts/main.vs").success, "Exception poisoned the next JNI call");
				var worker = Executors.newSingleThreadExecutor();
				try {
					var started = new CountDownLatch(1);
					var future = worker.submit(() -> {
						started.countDown();
						return engine.execute("void main(){while(true){}}", "/veroscript/scripts/cancel.vs");
					});
					require(started.await(5, TimeUnit.SECONDS), "Worker did not start");
					long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
					while (!future.isDone() && System.nanoTime() < deadline) {
						engine.requestCancel();
						Thread.sleep(5);
					}
					result = future.get(1, TimeUnit.SECONDS);
					require(!result.success, "Cancellation failed");
					require(engine.executeAsset("scripts/main.vs").success, "Cancelled JNI runner was not reusable");
				}
				finally
				{
					worker.shutdownNow();
				}
			}
			// The expanded constructor still keeps Mixed without a selected backend on VM.
			try (var mixed = new VeroScriptEngine(new AssetManager(root), "scripts", null, 100000, 5000, 256,
					16 * 1024 * 1024, 32 * 1024 * 1024, VeroScriptEngine.ExecutionBackend.MIXED, null, null, null)) {
				require(mixed.execute("void main(){print(19);}", "/veroscript/scripts/mixed.vs").output.equals("19\n"), "Mixed fallback failed");
				require(!mixed.executePackage(new byte[]{1,2,3}, "/veroscript/scripts/invalid.vsapp").success, "Invalid package was accepted");
			}
			var closed = new VeroScriptEngine(new AssetManager(root), "scripts");
			closed.close();
			closed.close();
			try {
				closed.requestCancel();
				throw new AssertionError("Closed native handle was used");
			}
			catch (IllegalStateException expected)
			{
				/* Closed handles are rejected. */
			}
			try {
				closed.executeAsset("scripts/main.vs");
				throw new AssertionError("Closed resource execution was permitted");
			}
			catch (IllegalStateException expected)
			{
				/* Close also releases cached assets. */
			}

			var closing = new VeroScriptEngine(new AssetManager(root), "scripts", 0, 5000, 256);
			var threads = Executors.newFixedThreadPool(2);
			try {
				var run = threads.submit(() -> closing.executeAsset("scripts/loop.vs"));
				long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
				while (!closing.isExecuting() && System.nanoTime() < deadline) { Thread.sleep(1); }
				require(closing.isExecuting(), "JNI worker did not become active");
				var close = threads.submit(() -> closing.close());
				while (!run.isDone() && System.nanoTime() < deadline) {
					try { closing.requestCancel(); }
					catch (IllegalStateException alreadyClosed)
					{
						break;
					}
					Thread.sleep(1);
				}
				require(!run.get(1, TimeUnit.SECONDS).success, "Closing execution ignored cancellation");
				close.get(1, TimeUnit.SECONDS);
			}
			finally
			{
				threads.shutdownNow();
				closing.close();
			}
			require(VeroScriptEngine.loadCatalogDirectory(args[1]), "Explicit JNI catalog loading failed");
			require(VeroScriptEngine.setLanguage("fr-FR"), "JNI language selection failed");
			require(!VeroScriptEngine.setLanguage("unsupported-ZZ"), "Unknown JNI locale accepted");
			try {
				new VeroScriptEngine(new AssetManager(root), "scripts", -1, 100, 256);
				throw new AssertionError("Invalid Java execution limits accepted");
			}
			catch (IllegalArgumentException expected)
			{
				require(expected.getMessage().equals("Limites d’exécution invalides"),
					"Java validation did not use the selected catalog: " + expected.getMessage());
			}
			try {
				new VeroScriptEngine(new AssetManager(root), "scripts", "relative-cache", 1000, 100, 256);
				throw new AssertionError("Relative cache directory accepted");
			}
			catch (IllegalStateException expected)
			{
				require(!expected.getMessage().contains("paths must be"), "Native JNI exception was not translated");
			}
			require(VeroScriptEngine.setLanguage("en-US"), "JNI locale reset failed");
		}
		finally
		{
			try (var paths = Files.walk(root)) {
				for (Path path : paths.sorted(Comparator.reverseOrder()).toList()) { Files.delete(path); }
			}
		}
	}
}
