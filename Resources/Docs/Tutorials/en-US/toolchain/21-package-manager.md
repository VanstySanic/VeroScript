# 21: Install a local extension package

Use a desktop Runtime with package management enabled. Work in an empty lesson folder. This exercise stays offline and does not require a registry.

## 1. Create the files

Save the package manifest as library/veroscript-package.json, its module as library/scripts/math.vs, the project manifest as app/veroscript-project.json and the entry as app/main.vs. Keep the directory relationship shown by the archive dependency.

### library/veroscript-package.json

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

### library/scripts/math.vs

```VeroScript
export int answer()
{
	return 42;
}
```

### app/veroscript-project.json

```json
{
	"format": "veroscript-project",
	"formatVersion": "0.1.1",
	"name": "demo-app",
	"version": "0.1.1",
	"dependencies": {
		"demo-math": {
			"version": "0.1.1",
			"archive": "../demo-math-0.1.1.vspkg"
		}
	}
}
```

### app/main.vs

```VeroScript
import "demo/math.vs";
int main()
{
	print(answer());
	return 0;
}
```

## 2. Pack, install and run

Run these commands from the lesson folder. The final script prints 42; install/list/verify print their own results before it. The archive suffix identifies a ZIP installation distribution, not a .vslib.

```sh
veroscript pack ./library --output ./demo-math-0.1.1.vspkg
veroscript install --path ./app --offline
veroscript list --path ./app --json
veroscript verify --path ./app
veroscript --backend=vm --path ./app ./app/main.vs
```

## 3. Inspect and change

pack never overwrites an existing output. Move the old lesson archive to another filename before packing again at its original path.

Change answer() to 43 and pack again. The already installed immutable snapshot still returns 42 until you run install again. --locked detects a changed archive instead of silently accepting new bytes. Restore or regenerate the intended lock when changing sources.

## 4. Continue

A successful installation does not import modules automatically: main.vs explicitly imports demo/math.vs. Compile-only dependencies do not belong in runtime deployment. Do not delete receipt/lock files or edit installed content to update a package.

[Reference](../../../../../VeroScriptPackageManager/Resources/Docs/User/en-US/index.md)

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](21-package-manager.md) · [fr-FR](../../fr-FR/toolchain/21-package-manager.md) · [ja-JP](../../ja-JP/toolchain/21-package-manager.md) · [ko-KR](../../ko-KR/toolchain/21-package-manager.md) · [zh-CN](../../zh-CN/toolchain/21-package-manager.md) · [zh-TW](../../zh-TW/toolchain/21-package-manager.md)
