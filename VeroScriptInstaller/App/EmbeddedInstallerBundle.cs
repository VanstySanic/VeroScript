// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Reflection;
using System.Security.Cryptography;
using VeroScriptInstaller.Core;

namespace VeroScriptInstaller;

internal sealed class EmbeddedInstallerBundle : IDisposable
{
	internal const string ResourceName = "VeroScriptInstaller.EmbeddedBundle.zip";
	private const string BundleHashKey = "VeroScriptInstaller.BundleSha256";
	private const string BundlePartCountKey = "VeroScriptInstaller.BundlePartCount";

	private EmbeddedInstallerBundle(string root, string manifestPath)
	{
		Root = root;
		ManifestPath = manifestPath;
	}

	internal string Root { get; }
	internal string ManifestPath { get; }

	internal static bool IsAvailable => Assembly.GetExecutingAssembly().GetManifestResourceInfo(ResourceName) is not null
		|| ExpectedContentHash is not null;

	private static string? ExpectedContentHash => Assembly.GetExecutingAssembly()
		.GetCustomAttributes<AssemblyMetadataAttribute>().SingleOrDefault(attribute => attribute.Key == BundleHashKey)?.Value;

	internal static Stream OpenBundledFile(string directory, string expectedHash, int partCount = 1)
	{
		if (partCount < 1 || partCount > 32)
		{
			throw new InvalidDataException("The installer bundle content part count is invalid.");
		}
		byte[] expected;
		try
		{
			expected = Convert.FromHexString(expectedHash);
		}
		catch (FormatException error)
		{
			throw new InvalidDataException("The installer bundle content hash is invalid.", error);
		}
		if (expected.Length != SHA256.HashSizeInBytes)
		{
			throw new InvalidDataException("The installer bundle content hash is invalid.");
		}
		var parts = new List<FileStream>();
		Stream? stream = null;
		try
		{
			for (var index = 0; index < partCount; ++index)
			{
				var path = Path.Combine(Path.GetFullPath(directory), ResourceName + "." + index.ToString("D4", System.Globalization.CultureInfo.InvariantCulture));
				var info = new FileInfo(path);
				if (info.LinkTarget is not null || (info.Exists && (info.Attributes & FileAttributes.ReparsePoint) != 0))
				{
					throw new InvalidDataException("The installer bundle content must not be a link.");
				}
				var part = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 1024, FileOptions.SequentialScan);
				parts.Add(part);
				if (part.Length < 1 || part.Length > 1024L * 1024 * 1024)
				{
					throw new InvalidDataException("The installer bundle content part size is invalid.");
				}
			}
			stream = new BundlePartsStream(parts);
			if (!CryptographicOperations.FixedTimeEquals(SHA256.HashData(stream), expected))
			{
				throw new InvalidDataException("The installer bundle content does not match this installer.");
			}
			stream.Position = 0;
			return stream;
		}
		catch
		{
			foreach (var part in parts) part.Dispose();
			throw;
		}
	}

	internal static EmbeddedInstallerBundle Open()
	{
		// Keep legacy resource-backed installers readable. New single-file builds extract
		// their content next to the managed assembly; bind it to the installer by hash.
		var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(ResourceName)
			?? (ExpectedContentHash is { } hash
				? OpenBundledFile(AppContext.BaseDirectory, hash, ContentPartCount())
				: throw new InvalidDataException("This installer does not contain an embedded VeroScript bundle."));
		// Linux /tmp can be a small RAM-backed mount. The single-file cache already
		// holds the verified payload on disk, so expand beside it on that platform.
		var parent = OperatingSystem.IsLinux() && ExpectedContentHash is not null
			? Path.Combine(AppContext.BaseDirectory, "VeroScriptInstaller.Unpack")
			: Path.Combine(Path.GetTempPath(), "VeroScriptInstaller");
		var root = Path.Combine(parent, $"bundle-{Environment.ProcessId}-{Guid.NewGuid():N}");
		try
		{
			using (stream)
			{
				var manifest = InstallerBundleArchive.ExtractAndVerifyAsync(stream, root)
					.GetAwaiter().GetResult();
				return new EmbeddedInstallerBundle(root, manifest);
			}
		}
		catch
		{
			TryDelete(root);
			throw;
		}
	}

	public void Dispose() => TryDelete(Root);

	private static int ContentPartCount()
	{
		var text = Assembly.GetExecutingAssembly().GetCustomAttributes<AssemblyMetadataAttribute>()
			.SingleOrDefault(attribute => attribute.Key == BundlePartCountKey)?.Value;
		return int.TryParse(text, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out var count)
			? count : throw new InvalidDataException("The installer bundle content part count is missing.");
	}

	/// <summary>Seek across bounded files without buffering the ZIP in memory or making another disk copy.</summary>
	private sealed class BundlePartsStream : Stream
	{
		private readonly List<FileStream> parts;
		private readonly long[] offsets;
		private long position;
		private bool disposed;

		internal BundlePartsStream(List<FileStream> parts)
		{
			this.parts = parts;
			offsets = new long[parts.Count + 1];
			for (var index = 0; index < parts.Count; ++index)
			{
				offsets[index + 1] = checked(offsets[index] + parts[index].Length);
			}
		}

		public override bool CanRead => !disposed;
		public override bool CanSeek => !disposed;
		public override bool CanWrite => false;
		public override long Length { get { ObjectDisposedException.ThrowIf(disposed, this); return offsets[^1]; } }
		public override long Position { get => position; set => Seek(value, SeekOrigin.Begin); }
		public override void Flush() { ObjectDisposedException.ThrowIf(disposed, this); }
		public override int Read(byte[] buffer, int offset, int count) => Read(buffer.AsSpan(offset, count));
		public override int Read(Span<byte> buffer)
		{
			ObjectDisposedException.ThrowIf(disposed, this);
			var total = 0;
			while (!buffer.IsEmpty && position < Length)
			{
				var index = Array.BinarySearch(offsets, position);
				if (index < 0)
				{
					index = ~index - 1;
				}
				var part = parts[index];
				var partPosition = position - offsets[index];
				if (part.Position != partPosition)
				{
					part.Position = partPosition;
				}
				var read = part.Read(buffer[..(int)Math.Min(buffer.Length, offsets[index + 1] - position)]);
				if (read == 0)
				{
					throw new EndOfStreamException("The installer bundle content was truncated.");
				}
				position += read;
				total += read;
				buffer = buffer[read..];
			}
			return total;
		}
		public override long Seek(long offset, SeekOrigin origin)
		{
			ObjectDisposedException.ThrowIf(disposed, this);
			var next = origin switch
			{
				SeekOrigin.Begin => offset,
				SeekOrigin.Current => checked(position + offset),
				SeekOrigin.End => checked(Length + offset),
				_ => throw new ArgumentOutOfRangeException(nameof(origin))
			};
			if (next < 0)
			{
				throw new IOException("Cannot seek before the installer bundle content.");
			}
			return position = next;
		}
		public override void SetLength(long value) => throw new NotSupportedException();
		public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
		protected override void Dispose(bool disposing)
		{
			if (disposing && !disposed)
			{
				foreach (var part in parts)
				{
					part.Dispose();
				}
				disposed = true;
			}
			base.Dispose(disposing);
		}
	}

	private static void TryDelete(string path)
	{
		try
		{
			if (Directory.Exists(path))
			{
				Directory.Delete(path, recursive: true);
			}
			var parent = Path.GetDirectoryName(path);
			if (parent is not null && Directory.Exists(parent)
				&& !Directory.EnumerateFileSystemEntries(parent).Any())
			{
				Directory.Delete(parent);
			}
		}
		catch
		{
			// A unique temporary bundle cannot shadow another run. Cleanup is best effort
			// because virus scanners may briefly retain extracted native binaries on Windows.
		}
	}
}
