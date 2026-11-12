// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.android;

import android.content.res.AssetManager;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.locks.ReentrantReadWriteLock;

/** Execute on an application worker thread; cancellation may come from the UI thread. */
public final class VeroScriptEngine implements AutoCloseable {
	static {
		System.loadLibrary("VeroScriptInterop");
		System.loadLibrary("VeroScriptAndroid");
	}

	public enum ExecutionBackend { VM, MIXED, JIT }

	/** Created by application JNI bootstrap code from generated native programs. */
	public static final class AotPrograms implements AutoCloseable {
		private final ReentrantReadWriteLock lifetime = new ReentrantReadWriteLock();
		private long nativeHandle;
		private AotPrograms(long handle) { nativeHandle = handle; }
		@Override public void close() {
			lifetime.writeLock().lock();
			try {
				if (nativeHandle != 0) { nativeReleaseAotPrograms(nativeHandle); nativeHandle = 0; }
			}
			finally
			{
				lifetime.writeLock().unlock();
			}
		}
	}

	public static final class Result {
		public final boolean success;
		public final int exitCode;
		public final boolean outputTruncated;
		public final String output;
		public final String diagnostics;
		public final String error;

		private Result(boolean success, int exitCode, boolean truncated, byte[] output, byte[] diagnostics, byte[] error) {
			this.success = success;
			this.exitCode = exitCode;
			this.outputTruncated = truncated;
			this.output = new String(output, StandardCharsets.UTF_8);
			this.diagnostics = new String(diagnostics, StandardCharsets.UTF_8);
			this.error = new String(error, StandardCharsets.UTF_8);
		}
	}

	private final ReentrantReadWriteLock lifetime = new ReentrantReadWriteLock();
	private final Object handleLock = new Object();
	private final Map<String, byte[]> resources;
	private long nativeHandle;

	/**
	 * The AAR supplies veroscript/stdlib. scriptAssetRoot identifies the app's
	 * bundled script tree (for example "scripts"). No assets are written to disk.
	 * This reads assets synchronously, so construct on a worker thread as well.
	 */
	public VeroScriptEngine(AssetManager assets, String scriptAssetRoot) throws IOException {
		this(assets, scriptAssetRoot, 1_000_000L, 100L, 256L);
	}

	public VeroScriptEngine(AssetManager assets, String scriptAssetRoot,
							long maxInstructions, long timeoutMilliseconds, long maxCallDepth) throws IOException {
		this(assets, scriptAssetRoot, null, maxInstructions, timeoutMilliseconds, maxCallDepth);
	}

	/** cacheDirectory is an absolute app-owned writable directory; null disables the cache. */
	public VeroScriptEngine(AssetManager assets, String scriptAssetRoot, String cacheDirectory,
							long maxInstructions, long timeoutMilliseconds, long maxCallDepth) throws IOException {
		this(assets, scriptAssetRoot, cacheDirectory, maxInstructions, timeoutMilliseconds, maxCallDepth,
			 16L * 1024 * 1024, 32L * 1024 * 1024);
	}

	/** Byte budgets are unsigned 64-bit bit patterns; zero means unlimited. */
	public VeroScriptEngine(AssetManager assets, String scriptAssetRoot, String cacheDirectory,
							long maxInstructions, long timeoutMilliseconds, long maxCallDepth,
							long maxBufferBytes, long maxRetainedBytes) throws IOException {
		this(assets, scriptAssetRoot, cacheDirectory, maxInstructions, timeoutMilliseconds, maxCallDepth,
			 maxBufferBytes, maxRetainedBytes, ExecutionBackend.VM, "", null, "");
	}

	/** JIT paths are explicit extracted native library files/directories. AOT IDs belong to programs. */
	public VeroScriptEngine(AssetManager assets, String scriptAssetRoot, String cacheDirectory,
							long maxInstructions, long timeoutMilliseconds, long maxCallDepth,
							long maxBufferBytes, long maxRetainedBytes, ExecutionBackend backend,
							String jitBackendPath, AotPrograms programs, String aotProgramId) throws IOException {
		this(assets, scriptAssetRoot, cacheDirectory, maxInstructions, timeoutMilliseconds, maxCallDepth,
			 maxBufferBytes, maxRetainedBytes, backend, "", jitBackendPath, programs, aotProgramId);
	}

	/** Empty vmBackendPath keeps the assembled VM; nonempty VM/JIT paths are absolute native files/directories. */
	public VeroScriptEngine(AssetManager assets, String scriptAssetRoot, String cacheDirectory,
							long maxInstructions, long timeoutMilliseconds, long maxCallDepth,
							long maxBufferBytes, long maxRetainedBytes, ExecutionBackend backend,
							String vmBackendPath, String jitBackendPath, AotPrograms programs, String aotProgramId) throws IOException {
		if (backend == null) throw new IllegalArgumentException(message("Execution backend is required"));
		if (maxInstructions < 0 || timeoutMilliseconds < 0 || maxCallDepth <= 0) {
			throw new IllegalArgumentException(message("Invalid execution limits"));
		}
		validateAssetRoot(scriptAssetRoot);
		Map<String, byte[]> loaded = new LinkedHashMap<>();
		long[] bytes = {0};
		readAssets(assets, "veroscript/stdlib", loaded, bytes);
		readAssets(assets, scriptAssetRoot, loaded, bytes);
		resources = loaded;
		byte[][] paths = new byte[loaded.size()][];
		byte[][] sources = new byte[loaded.size()][];
		int index = 0;
		for (Map.Entry<String, byte[]> item : loaded.entrySet()) {
			paths[index] = item.getKey().getBytes(StandardCharsets.UTF_8);
			sources[index++] = item.getValue();
		}
		if (programs != null) programs.lifetime.readLock().lock();
		try {
			if (programs != null && programs.nativeHandle == 0) throw new IllegalStateException(message("AOT catalog is closed"));
			nativeHandle = nativeCreate(paths, sources,
				(cacheDirectory == null ? "" : cacheDirectory).getBytes(StandardCharsets.UTF_8),
				maxInstructions, timeoutMilliseconds, maxCallDepth, maxBufferBytes, maxRetainedBytes,
				backend.ordinal(), (vmBackendPath == null ? "" : vmBackendPath).getBytes(StandardCharsets.UTF_8),
				(jitBackendPath == null ? "" : jitBackendPath).getBytes(StandardCharsets.UTF_8),
				programs == null ? 0 : programs.nativeHandle,
				(aotProgramId == null ? "" : aotProgramId).getBytes(StandardCharsets.UTF_8));
		}
		finally
		{
			if (programs != null) programs.lifetime.readLock().unlock();
		}
		if (nativeHandle == 0) {
			throw new IllegalStateException(message("Cannot create VeroScript Android runner"));
		}
	}

	public Result executeAsset(String path) {
		validateAssetRoot(path);
		String label = resourceLabel(path);
		lifetime.readLock().lock();
		try {
			requireOpen();
			byte[] source = resources.get(label);
			if (source == null) {
				throw new IllegalArgumentException(message("Unknown script resource: " + path));
			}
			return nativeExecute(nativeHandle, source, label.getBytes(StandardCharsets.UTF_8));
		}
		finally
		{
			lifetime.readLock().unlock();
		}
	}

	public Result execute(String source, String absoluteResourceLabel) {
		return executeBytes(source.getBytes(StandardCharsets.UTF_8), absoluteResourceLabel);
	}

	private Result executeBytes(byte[] source, String label) {
		lifetime.readLock().lock();
		try {
			requireOpen();
			return nativeExecute(nativeHandle, source, label.getBytes(StandardCharsets.UTF_8));
		}
		finally
		{
			lifetime.readLock().unlock();
		}
	}

	/** Execute exact generated .vsapp bytes; no source recompilation occurs. */
	public Result executePackage(byte[] program, String absoluteResourceLabel) {
		lifetime.readLock().lock();
		try {
			requireOpen();
			return nativeExecutePackage(nativeHandle, program, absoluteResourceLabel.getBytes(StandardCharsets.UTF_8));
		}
		finally
		{
			lifetime.readLock().unlock();
		}
	}

	/** Cooperative; it does not interrupt a blocking host/native callback. */
	public void requestCancel() {
		// Keep cancellation available even while close() is waiting for a run.
		synchronized (handleLock) {
			requireOpen();
			nativeCancel(nativeHandle);
		}
	}

	/** A status snapshot only; concurrent execute calls are still checked natively. */
	public boolean isExecuting() {
		synchronized (handleLock) {
			requireOpen();
			return nativeIsExecuting(nativeHandle);
		}
	}

	/** Waits for an active call and releases assets; request cancellation first when shutting down. */
	@Override
	public void close() {
		lifetime.writeLock().lock();
		try {
			synchronized (handleLock) {
				if (nativeHandle != 0) {
					nativeDestroy(nativeHandle);
					nativeHandle = 0;
				}
			}
			resources.clear();
		}
		finally
		{
			lifetime.writeLock().unlock();
		}
	}

	private void requireOpen() {
		if (nativeHandle == 0) {
			throw new IllegalStateException(message("VeroScript runner is closed"));
		}
	}

	private static void validateAssetRoot(String path) {
		if (path == null || path.isEmpty() || path.startsWith("/") || path.endsWith("/") || path.contains("//")
				|| path.getBytes(StandardCharsets.UTF_8).length > 4000 || path.indexOf('\0') >= 0
				|| path.indexOf('\\') >= 0) {
			throw new IllegalArgumentException(message("Expected a relative asset path without dot components"));
		}
		for (String component : path.split("/")) {
			if (component.equals(".") || component.equals("..")) {
				throw new IllegalArgumentException(message("Asset paths cannot contain dot components"));
			}
		}
	}

	private static String resourceLabel(String path) {
		return path.startsWith("veroscript/") ? "/" + path : "/veroscript/" + path;
	}

	private static void readAssets(AssetManager assets, String path, Map<String, byte[]> loaded, long[] total) throws IOException {
		if (path.getBytes(StandardCharsets.UTF_8).length > 4096 || path.split("/").length > 128) {
			throw new IOException(message("Script asset path exceeds the length or nesting budget"));
		}
		String[] children = assets.list(path);
		if (children != null && children.length > 0) {
			for (String child : children) {
				readAssets(assets, path + "/" + child, loaded, total);
			}
			return;
		}
		if (!path.endsWith(".vs")) {
			return;
		}
		try (InputStream input = assets.open(path); ByteArrayOutputStream output = new ByteArrayOutputStream()) {
			byte[] block = new byte[8192];
			for (int count; (count = input.read(block)) != -1;) {
				if (total[0] + count > 16L * 1024 * 1024) {
					throw new IOException(message("Script assets exceed the 16 MiB input budget"));
				}
				total[0] += count;
				output.write(block, 0, count);
			}
			if (loaded.size() >= 4096) {
				throw new IOException(message("Too many script assets"));
			}
			loaded.put(resourceLabel(path), output.toByteArray());
		}
	}

	/** Process-wide; catalogs must be unpacked to a real directory by the application. */
	public static boolean loadCatalogDirectory(String directory) {
		return nativeLoadCatalogs(directory.getBytes(StandardCharsets.UTF_8));
	}

	/** Selects a loaded language; failure leaves the previous selection unchanged. */
	public static boolean setLanguage(String tag) {
		return nativeSetLanguage(tag.getBytes(StandardCharsets.UTF_8));
	}

	private static String message(String english) {
		return new String(nativeTranslate(english.getBytes(StandardCharsets.UTF_8)), StandardCharsets.UTF_8);
	}

	private static native byte[] nativeTranslate(byte[] english);
	private static native boolean nativeSetLanguage(byte[] tag);
	private static native boolean nativeLoadCatalogs(byte[] directory);

	// Standard UTF-8 byte arrays avoid JNI's modified UTF-8 string encoding.
	private static native long nativeCreate(byte[][] paths, byte[][] sources, byte[] cacheDirectory,
										   long instructions, long timeout, long depth, long maxBufferBytes, long maxRetainedBytes,
										   int backend, byte[] vmPath, byte[] jitPath, long aotCatalog, byte[] aotProgramId);
	private static native Result nativeExecute(long handle, byte[] source, byte[] label);
	private static native Result nativeExecutePackage(long handle, byte[] program, byte[] label);
	private static native void nativeReleaseAotPrograms(long handle);
	private static native void nativeCancel(long handle);
	private static native boolean nativeIsExecuting(long handle);
	private static native void nativeDestroy(long handle);
}
