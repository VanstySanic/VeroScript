// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package android.content.res;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;

/** Test-only filesystem fixture; never packaged into the Android AAR. */
public final class AssetManager {
	private final Path root;
	public AssetManager(Path root) { this.root = root; }
	public String[] list(String path) throws IOException {
		Path directory = root.resolve(path);
		if (!Files.isDirectory(directory)) { return new String[0]; }
		try (var children = Files.list(directory)) {
			return children.map(child -> child.getFileName().toString()).toArray(String[]::new);
		}
	}
	public InputStream open(String path) throws IOException { return Files.newInputStream(root.resolve(path)); }
}
