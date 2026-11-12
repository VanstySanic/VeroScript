// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript.Interop;

internal static unsafe class Utf16
{
	internal static void Validate(string value, string parameterName)
	{
		ArgumentNullException.ThrowIfNull(value, parameterName);
		if (!IsValid(value.AsSpan()))
		{
			throw new ArgumentException(
						"The string contains invalid UTF-16 data.", parameterName);
		}
	}

	internal static string Copy(char* data, ulong size)
	{
		if (size > int.MaxValue)
		{
			throw new OverflowException("The native UTF-16 value is too large.");
		}

		if (size == 0) return string.Empty;
		if (data is null)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"The native UTF-16 value returned a null pointer for non-empty data.");
		}

		var characters = new ReadOnlySpan<char>(data, (int)size);
		if (!IsValid(characters))
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"The native string contains invalid UTF-16 data.");
		}

		return new string(characters);
	}

	private static bool IsValid(ReadOnlySpan<char> characters)
	{
		for (int index = 0; index < characters.Length; ++index)
		{
			char character = characters[index];
			if (char.IsHighSurrogate(character))
			{
				if (++index >= characters.Length || !char.IsLowSurrogate(characters[index]))
				{
					return false;
				}
			}
			else if (char.IsLowSurrogate(character))
			{
				return false;
			}
		}
		return true;
	}
}
