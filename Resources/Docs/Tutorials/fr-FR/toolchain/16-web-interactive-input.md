# 16 : Entrée interactive dans le SDK Web

Prérequis : utilisez le wrapper TypeScript et la paire normale `veroscript-engine.mjs`/`.wasm` du même SDK, dans un projet à modules ES acceptant top-level await. Compilez wrapper et leçon ensemble, déployez leur JavaScript et les fichiers moteur avec des URL relatives valides, puis servez l’application navigateur en HTTP. L’exemple importe `veroscript-webassembly.js` émis ; adaptez ce chemin. Voir le [déploiement WebAssembly](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/fr-FR/index.md).

Utilisez `runSourceInteractive` pour les scripts qui appellent `read_line`, une entrée typée ou par jetons, ou `pause`. Le frontend fournit une ligne logique de manière asynchrone pendant que la VM conserve l'état complet du script.

```typescript
import { createVeroScriptRuntime } from "./veroscript-webassembly.js";

const runtime = await createVeroScriptRuntime();
try {
	const lines = ["Ada", "3"];
	const result = await runtime.runSourceInteractive(
		`import "io.vs";
int main()
{
	print(std::io::read_line());
	return std::io::read_int();
}`,
		{ async readLine() { return lines.shift() ?? null; } },
	);
	console.log(result.output, result.exitCode);
}
finally
{
	runtime.dispose();
}
```

Résultat attendu : `result.success` vaut `true`, `result.output` vaut `Ada\n` et `result.exitCode` vaut `3`. Le fournisseur donne les lignes `Ada` puis `3` ; le code de sortie 3 n’est pas une panne du moteur. Cette entrée fixe permet une première exécution reproductible avant de connecter le terminal.

Renvoyez `null` pour signaler la fin du fichier. N'appelez pas une autre opération du runtime et ne le détruisez pas tant qu'une entrée est en attente. Placez le runtime dans un Worker et terminez ce Worker pour arrêter un script sans limite. Exercice : remplacez le tableau par la Promise de votre interface de terminal.

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/toolchain/16-web-interactive-input.md) · [fr-FR](16-web-interactive-input.md) · [ja-JP](../../ja-JP/toolchain/16-web-interactive-input.md) · [ko-KR](../../ko-KR/toolchain/16-web-interactive-input.md) · [zh-CN](../../zh-CN/toolchain/16-web-interactive-input.md) · [zh-TW](../../zh-TW/toolchain/16-web-interactive-input.md)
