// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.devicesmoke;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import com.vanstysanic.veroscript.android.VeroScriptEngine;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import org.json.JSONArray;
import org.json.JSONObject;

public final class DeviceSmoke extends Instrumentation {
	private interface Check { void run() throws Exception; }
	private final JSONArray cases = new JSONArray();
	private int failures;
	private static native String nativeIdentity();
	private static native long nativePageSize();
	private static native String nativeJitProof(byte[] path);
	private static native VeroScriptEngine.AotPrograms nativeEmptyCatalog();
	private Context context;

	@Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
	private static void require(boolean ok, String message) {
		if (!ok) throw new AssertionError(message);
	}
	private void check(String name, Check check) {
		Bundle status = new Bundle();
		status.putString("class", getClass().getName());
		status.putString("test", name);
		status.putInt("current", cases.length() + 1);
		sendStatus(1, status);
		JSONObject result = new JSONObject();
		long started = System.nanoTime();
		try {
			result.put("name", name);
			check.run();
			result.put("passed", true);
			sendStatus(0, status);
		}
		catch (Throwable error)
		{
			failures++;
			String stack = Log.getStackTraceString(error);
			try { result.put("passed", false); result.put("error", stack); }
			catch (Exception ignored)
			{
			}
			status.putString("stack", stack);
			sendStatus(-2, status);
		}
		try { result.put("seconds", (System.nanoTime() - started) / 1e9); }
		catch (Exception ignored)
		{
		}
		cases.put(result);
	}
	private VeroScriptEngine engine() throws Exception {
		return new VeroScriptEngine(context.getAssets(), "scripts", 0, 5000, 256);
	}
	private VeroScriptEngine selected(VeroScriptEngine.ExecutionBackend backend, String vm, String jit) throws Exception {
		return new VeroScriptEngine(context.getAssets(), "scripts", null, 0, 5000, 256,
				16L * 1024 * 1024, 32L * 1024 * 1024, backend, vm, jit, null, "");
	}
	private static void output(VeroScriptEngine.Result result, String expected) {
		require(result.success && result.output.equals(expected),
				result.error + result.diagnostics + " output=[" + result.output + "]");
	}
	private static void failure(VeroScriptEngine.Result result, String expected) {
		String detail = result.error + result.diagnostics;
		require(!result.success && detail.contains(expected), "Expected " + expected + ": " + detail);
	}
	private static boolean containsCache(File root) {
		File[] files = root.listFiles();
		if (files == null) return false;
		for (File file : files) {
			if (file.isDirectory() ? containsCache(file) : file.getName().endsWith(".vsbc")) return true;
		}
		return false;
	}
	private void catalogs(String asset, File target) throws Exception {
		String[] children = context.getAssets().list(asset);
		if (children != null && children.length > 0) {
			require(target.isDirectory() || target.mkdirs(), "Cannot create catalog directory");
			for (String child : children) catalogs(asset + "/" + child, new File(target, child));
		}
		else
		{
			try (InputStream source = context.getAssets().open(asset); FileOutputStream out = new FileOutputStream(target)) {
				byte[] buffer = new byte[8192];
				int count;
				while ((count = source.read(buffer)) != -1) out.write(buffer, 0, count);
			}
		}
	}
	private void cancellation(boolean close) throws Exception {
		VeroScriptEngine engine = new VeroScriptEngine(context.getAssets(), "scripts", 0, 30000, 256);
		ExecutorService workers = Executors.newFixedThreadPool(2);
		try {
			Future<VeroScriptEngine.Result> active = workers.submit(() -> engine.execute(
					"void main(){while(true){}}", "/veroscript/scripts/cancel.vs"));
			long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
			while (!engine.isExecuting() && !active.isDone() && System.nanoTime() < deadline) Thread.sleep(1);
			require(engine.isExecuting(), "Worker did not become active");
			Future<?> closing = close ? workers.submit(() -> engine.close()) : null;
			while (!active.isDone() && System.nanoTime() < deadline) {
				try { engine.requestCancel(); }
				catch (IllegalStateException closed)
				{
					break;
				}
				Thread.sleep(1);
			}
			require(!active.get(2, TimeUnit.SECONDS).success, "Cancellation ignored");
			if (closing != null) closing.get(2, TimeUnit.SECONDS);
			else
				output(engine.executeAsset("scripts/main.vs"), "移动 📦\n42\na\0b\n");
		}
		finally
		{
			workers.shutdownNow();
			engine.close();
		}
	}

	@Override public void onStart() {
		super.onStart();
		context = getTargetContext();
		JSONObject report = new JSONObject();
		Bundle finish = new Bundle();
		try {
			System.loadLibrary("VeroScriptDeviceSmoke");
			report.put("sdk", Build.VERSION.SDK_INT);
			report.put("hardware", Build.HARDWARE);
			report.put("fingerprint", Build.FINGERPRINT);
			report.put("abis", new JSONArray(Build.SUPPORTED_ABIS));
			report.put("pageSize", nativePageSize());
			report.put("identity", nativeIdentity());
			report.put("withJit", BuildConfig.WITH_JIT);
			check("nativePrefabIdentity", () -> require(nativeIdentity().startsWith("android-"), nativeIdentity()));
			check("assetsImportsUtf8Nul", () -> { try (VeroScriptEngine e = engine()) {
				output(e.executeAsset("scripts/main.vs"), "移动 📦\n42\na\0b\n");
			}});
			check("unicodeAssetName", () -> { try (VeroScriptEngine e = engine()) {
				output(e.executeAsset("scripts/移动 📦.vs"), "资源 📦\n");
			}});
			check("float32Rounding", () -> { try (VeroScriptEngine e = engine()) {
				output(e.execute("void main(){assert(1.000000059604644775390625f==1.0f);assert(1.000000059604644775390626f>1.0f);print(1.5);}",
						"/veroscript/scripts/float.vs"), "1.5\n");
			}});
			check("unsignedMemory", () -> { try (VeroScriptEngine e = engine()) {
				output(e.execute("void main(){auto b=new std::memory::buffer(8);std::memory::write<uint64>(b,0,18446744073709551615u64);assert(std::memory::read<uint64>(b,0)==18446744073709551615u64);print(42);}",
						"/veroscript/scripts/memory.vs"), "42\n");
			}});
			check("memoryBudget", () -> { try (VeroScriptEngine e = new VeroScriptEngine(context.getAssets(), "scripts", null, 100000, 5000, 256, 4, 8)) {
				failure(e.execute("void main(){auto b=new std::memory::buffer(5);}", "/veroscript/scripts/limit.vs"), "diag.memory_limit");
			}});
			check("instructionBudget", () -> { try (VeroScriptEngine e = new VeroScriptEngine(context.getAssets(), "scripts", 10000, 5000, 256)) {
				failure(e.execute("void main(){while(true){}}", "/veroscript/scripts/limit.vs"), "diag.runtime_execution_instruction_limit_exceeded");
			}});
			check("timeoutBudget", () -> { try (VeroScriptEngine e = new VeroScriptEngine(context.getAssets(), "scripts", 0, 50, 256)) {
				failure(e.execute("void main(){while(true){}}", "/veroscript/scripts/timeout.vs"), "diag.execution_time_limit_exceeded");
			}});
			check("inputCapability", () -> { try (VeroScriptEngine e = engine()) {
				VeroScriptEngine.Result r = e.execute("import \"io.vs\";void main(){std::io::read_line();}", "/veroscript/scripts/input.vs");
				require(!r.success && r.error.contains("console input capability"), r.error + r.diagnostics);
			}});
			check("compileErrorRecovery", () -> { try (VeroScriptEngine e = engine()) {
				StringBuilder dimensions = new StringBuilder();
				StringBuilder valueDimensions = new StringBuilder();
				StringBuilder indices = new StringBuilder();
				for (int i = 0; i < 64; ++i) {
					dimensions.append("[1]");
					if (i < 16) { valueDimensions.append("[1]"); indices.append("[0]"); }
				}
				output(e.execute("struct Boundary{int boundary" + dimensions + ";int value" + valueDimensions + ";};" +
						"class Indexed{public:int value=42;Indexed operator[](int i){return this;}};void main(){int boundary" + dimensions + ";int local" + valueDimensions +
						";Boundary item=Boundary();local" + indices + "=21;item.value" + indices +
						"=21;Indexed cursor=Indexed();assert(cursor" + indices + ".value==42);print(local" + indices + "+item.value" + indices + ");}",
						"/veroscript/scripts/native-array-boundary.vs"), "42\n");
				for (int depth : new int[]{200, 512}) {
					StringBuilder source = new StringBuilder("void main(){print(");
					for (int i = 0; i < depth; ++i) source.append('(');
					source.append("42");
					for (int i = 0; i < depth; ++i) source.append(')');
					source.append(");}");
					VeroScriptEngine.Result parsed = e.execute(source.toString(), "/veroscript/scripts/nested.vs");
					if (depth == 200) output(parsed, "42\n");
					else
						require(!parsed.success && !parsed.diagnostics.isEmpty(),
							"Missing nesting limit diagnostic: " + parsed.error + parsed.diagnostics);
				}
				require(!e.execute("void main( {", "/veroscript/scripts/bad.vs").success, "Invalid source accepted");
				output(e.executeAsset("scripts/main.vs"), "移动 📦\n42\na\0b\n");
			}});
			check("nativeExceptionRecovery", () -> { try (VeroScriptEngine e = engine()) {
				boolean rejected = false;
				try { e.execute("void main(){}", "relative/📦.vs"); }
				catch (IllegalStateException expected)
				{
					rejected = true;
				}
				require(rejected, "Relative label accepted");
				output(e.executeAsset("scripts/main.vs"), "移动 📦\n42\na\0b\n");
			}});
			check("cache", () -> {
				File cache = new File(context.getCacheDir(), "vero-cache-📦-" + System.nanoTime());
				try (VeroScriptEngine e = new VeroScriptEngine(context.getAssets(), "scripts", cache.getAbsolutePath(), 0, 5000, 256)) {
					output(e.executeAsset("scripts/main.vs"), "移动 📦\n42\na\0b\n");
					require(containsCache(cache), "Cache path ignored");
					output(e.executeAsset("scripts/main.vs"), "移动 📦\n42\na\0b\n");
				}
			});
			check("mixedVmFallback", () -> { try (VeroScriptEngine e = selected(VeroScriptEngine.ExecutionBackend.MIXED, "", "")) {
				output(e.execute("void main(){print(19);}", "/veroscript/scripts/mixed.vs"), "19\n");
			}});
			check("invalidPackage", () -> { try (VeroScriptEngine e = engine()) {
				require(!e.executePackage(new byte[]{1,2,3}, "/veroscript/scripts/bad.vsapp").success, "Invalid package accepted");
			}});
			check("missingExplicitVm", () -> {
				boolean rejected = false;
				try (VeroScriptEngine e = selected(VeroScriptEngine.ExecutionBackend.VM, new File(context.getFilesDir(), "missing-vm.so").getAbsolutePath(), "")) {
					rejected = !e.execute("void main(){}", "/veroscript/scripts/missing.vs").success;
				}
				catch (IllegalStateException expected)
				{
					rejected = true;
				}
				require(rejected, "Explicit VM path ignored");
			});
			check("missingExplicitJit", () -> {
				boolean rejected = false;
				try (VeroScriptEngine e = selected(VeroScriptEngine.ExecutionBackend.JIT, "", new File(context.getFilesDir(), "missing-jit.so").getAbsolutePath())) {
					rejected = !e.execute("void main(){}", "/veroscript/scripts/missing.vs").success;
				}
				catch (IllegalStateException expected)
				{
					rejected = true;
				}
				require(rejected, "Explicit JIT path ignored");
			});
			check("assetRootValidation", () -> {
				boolean rejected = false;
				try (VeroScriptEngine ignored = new VeroScriptEngine(context.getAssets(), "../scripts")) { }
				catch (IllegalArgumentException expected)
				{
					rejected = true;
				}
				require(rejected, "Invalid asset root accepted");
			});
			check("cancellationAndReuse", () -> cancellation(false));
			check("closeDuringCancellation", () -> cancellation(true));
			check("closedHandle", () -> {
				VeroScriptEngine e = engine(); e.close(); e.close();
				boolean rejected = false;
				try { e.requestCancel(); }
				catch (IllegalStateException expected)
				{
					rejected = true;
				}
				require(rejected, "Closed handle used");
			});
			check("aotCatalogJniLifetime", () -> {
				VeroScriptEngine.AotPrograms programs = nativeEmptyCatalog();
				require(programs != null, "JNI AOT catalog creation failed");
				try {
					boolean rejected = false;
					try (VeroScriptEngine e = new VeroScriptEngine(context.getAssets(), "scripts", null, 0, 5000, 256,
							16L * 1024 * 1024, 32L * 1024 * 1024, VeroScriptEngine.ExecutionBackend.MIXED, "", programs, "absent")) {
						rejected = !e.execute("void main(){}", "/veroscript/scripts/aot.vs").success;
					}
					catch (IllegalStateException expected)
					{
						rejected = true;
					}
					require(rejected, "Unknown AOT program ID accepted");
				}
				finally
				{
					programs.close();
					programs.close();
				}
				boolean rejected = false;
				try (VeroScriptEngine ignored = new VeroScriptEngine(context.getAssets(), "scripts", null, 0, 5000, 256,
						16L * 1024 * 1024, 32L * 1024 * 1024, VeroScriptEngine.ExecutionBackend.MIXED, "", programs, "absent")) { }
				catch (IllegalStateException expected)
				{
					rejected = true;
				}
				require(rejected, "Closed AOT catalog accepted");
			});
			check("boundedOutput", () -> { try (VeroScriptEngine e = engine()) {
				VeroScriptEngine.Result r = e.execute("void main(){for(int i=0;i<20000;i++){print(\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\");}}",
						"/veroscript/scripts/output.vs");
				require(r.success && r.outputTruncated && r.output.getBytes(StandardCharsets.UTF_8).length == 1024 * 1024,
						"Output budget was not enforced: " + r.error + r.diagnostics);
			}});
			check("concurrentExecutionRejected", () -> {
				VeroScriptEngine e = new VeroScriptEngine(context.getAssets(), "scripts", 0, 30000, 256);
				ExecutorService worker = Executors.newSingleThreadExecutor();
				try {
					Future<VeroScriptEngine.Result> active = worker.submit(() -> e.execute("void main(){while(true){}}", "/veroscript/scripts/active.vs"));
					long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
					while (!e.isExecuting() && !active.isDone() && System.nanoTime() < deadline) Thread.sleep(1);
					require(e.isExecuting(), "Worker was not active");
					boolean rejected = false;
					try { rejected = !e.execute("void main(){}", "/veroscript/scripts/concurrent.vs").success; }
					catch (IllegalStateException expected)
					{
						rejected = true;
					}
					require(rejected, "Concurrent execution accepted");
					while (!active.isDone() && System.nanoTime() < deadline) { e.requestCancel(); Thread.sleep(1); }
					require(!active.get(2, TimeUnit.SECONDS).success, "Worker ignored cancellation");
				}
				finally
				{
					worker.shutdownNow();
					e.close();
				}
			});
			if (BuildConfig.WITH_JIT) {
				check("selectedJit", () -> {
					File backend = new File(context.getApplicationInfo().nativeLibraryDir, "libVeroScriptJitAsmJit.so");
					require(backend.isFile(), "JIT library was not extracted");
					report.put("jitProof", new JSONObject(nativeJitProof(backend.getAbsolutePath().getBytes(StandardCharsets.UTF_8))));
					try (VeroScriptEngine e = selected(VeroScriptEngine.ExecutionBackend.JIT, "", backend.getAbsolutePath())) {
						output(e.execute("int sum(int n){int s=0;for(int i=0;i<n;i++){s+=i;}return s;}void main(){print(sum(100));}",
								"/veroscript/scripts/jit.vs"), "4950\n");
					}
				});
			}
			check("localization", () -> {
				File directory = new File(context.getCacheDir(), "catalogs-" + System.nanoTime());
				catalogs("veroscript/localization", directory);
				require(VeroScriptEngine.loadCatalogDirectory(directory.getAbsolutePath()), "Catalog load failed");
				try {
					require(VeroScriptEngine.setLanguage("fr-FR"), "Language selection failed");
					require(!VeroScriptEngine.setLanguage("unsupported-ZZ"), "Invalid language accepted");
					boolean rejected = false;
					try (VeroScriptEngine ignored = new VeroScriptEngine(context.getAssets(), "scripts", -1, 100, 256)) { }
					catch (IllegalArgumentException expected)
					{
						rejected = expected.getMessage().equals("Limites d’exécution invalides");
					}
					require(rejected, "Java validation did not use UTF-8 catalogs");
					try (VeroScriptEngine ignored = new VeroScriptEngine(context.getAssets(), "scripts", "relative-cache", 0, 5000, 256)) {
						throw new AssertionError("Relative cache accepted");
					}
					catch (IllegalStateException expected)
					{
						require(expected.getMessage().equals("Les chemins ressources/cache doivent être absolus UTF-8 non vides sans NUL"),
							"Native exception was not translated: " + expected.getMessage());
					}
				}
				finally
				{
					require(VeroScriptEngine.setLanguage("en-US"), "Language reset failed");
				}
			});
		}
		catch (Throwable fatal)
		{
			failures++;
			try { report.put("fatal", Log.getStackTraceString(fatal)); }
			catch (Exception ignored)
			{
			}
		}
		try {
			report.put("cases", cases); report.put("failures", failures);
			byte[] bytes = report.toString(2).getBytes(StandardCharsets.UTF_8);
			try (FileOutputStream out = context.openFileOutput("device-smoke.json", Context.MODE_PRIVATE)) { out.write(bytes); }
			finish.putString("report", new String(bytes, StandardCharsets.UTF_8));
		}
		catch (Exception error)
		{
			failures++;
			finish.putString("error", Log.getStackTraceString(error));
		}
		finish.putString("stream", "\nTests run: " + cases.length() + ", Failures: " + failures + "\n");
		finish(failures == 0 ? Activity.RESULT_OK : Activity.RESULT_CANCELED, finish);
	}
}
