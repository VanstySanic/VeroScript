// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript;

/// <summary>Terminal operations delivered synchronously to an embedding host.</summary>
public enum VeroScriptConsoleOperation : uint
{
	Clear = 0,
	ClearLine = 1,
	SetColor = 2,
	ResetStyle = 3,
	MoveCursor = 4,
	SetCursorVisible = 5
}

/// <summary>Output capabilities and visible dimensions in columns and rows; nonpositive dimensions mean unknown.</summary>
public sealed record VeroScriptConsoleInfo(
	bool OutputTerminal = false, bool Color = false, bool Cursor = false,
	bool Clear = false, int Width = 0, int Height = 0);
