// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

/** End of a backslash-continued line, excluding its final newline. */
export function continuedLineEnd(source: string, start: number): number {
	const line = /(?:\\(?:\r\n|\r|\n)|[^\r\n])*/y;
	line.lastIndex = start;
	return start + line.exec(source)![0].length;
}
