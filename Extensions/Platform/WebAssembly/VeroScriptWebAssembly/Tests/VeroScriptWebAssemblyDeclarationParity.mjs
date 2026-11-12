// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { spawnSync } from "node:child_process";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const moduleRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const temporaryDirectory = await mkdtemp(join(tmpdir(), "veroscript-webassembly-types-"));

try {
	const compilerPath = resolve(moduleRoot, "node_modules/typescript/bin/tsc");
	const result = spawnSync(
		process.execPath,
		[
			compilerPath,
			"-p",
			"Tests/tsconfig.bindings.json",
			"--noEmit",
			"false",
			"--declaration",
			"true",
			"--emitDeclarationOnly",
			"true",
			"--rootDir",
			"Bindings",
			"--outDir",
			temporaryDirectory,
		],
		{ cwd: moduleRoot, encoding: "utf8" },
	);
	if (result.status !== 0) {
		throw new Error(
			`unable to generate WebAssembly SDK declarations:\n${result.stdout}${result.stderr}`,
		);
	}

	const normalize = (text) => text.replaceAll("\r\n", "\n");
	const declarationPath = resolve(moduleRoot, "Bindings/veroscript-webassembly.d.ts");
	const generated = "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n" + normalize(
		await readFile(resolve(temporaryDirectory, "veroscript-webassembly.d.ts"), "utf8"),
	).replace(/^(?: {4})+/gm, indentation => "\t".repeat(indentation.length / 4));
	if (process.argv.includes("--write")) {
		await writeFile(declarationPath, generated.replaceAll("\n", "\r\n"), "utf8");
	}
	else
	{
		const maintained = normalize(await readFile(declarationPath, "utf8"));
		if (maintained !== generated) {
			throw new Error(
				"Bindings/veroscript-webassembly.d.ts is stale; run npm run generate:declarations",
			);
		}
	}
}
finally
{
	await rm(temporaryDirectory, { force: true, recursive: true });
}

console.log("VeroScript WebAssembly declaration parity check passed");
